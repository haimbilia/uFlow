// The runner inserts the actual production free-space hooks.
#include "RawNativePath.h"
#include "RawNativePathStorage.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

using FSStatus = int;
using FSErrorFlag = int;
constexpr int FS_STATUS_OK = 0;
constexpr int FS_STATUS_FATAL_ERROR = -1024;
struct FSClient {};
struct FSCmdBlock {};
struct FSAsyncData { void *callback; void *queue; void *context; };
struct {
    bool active = true;
    const char *savePath = "wiiu/raw-saves/0005000010146a00";
} gLooseTitleInfo;
std::atomic<uint32_t> sRawSpaceSamples{0}, sRawFlushSamples{0};
void OSReport(const char *, ...) {}
#define DECL_FUNCTION(ret, name, ...) ret name(__VA_ARGS__)

FSClient client;
FSCmdBlock block;
uint64_t output, available;
FSAsyncData async{&client, &block, &output};
std::string seenPath;
FSStatus status;
int calls;
bool failPathAllocation = false;
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    if (failPathAllocation) return nullptr;
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}

FSStatus Capture(FSClient *c, FSCmdBlock *b, const char *path,
                 uint64_t *size, FSErrorFlag mask, FSAsyncData *a) {
    assert(c == &client && b == &block && size == &output && mask == 0x52);
    seenPath = path ? path : "<null>";
    ++calls;
    if (a) {
        assert(a == &async && a->callback == &client && a->queue == &block && a->context == &output);
        // Submission must neither fabricate a completion nor touch output.
    } else if (status == FS_STATUS_OK) *size = available;
    return status;
}
FSStatus real_Raw_FSGetFreeSpaceSize(FSClient *c, FSCmdBlock *b, const char *p,
                                    uint64_t *s, FSErrorFlag mask) {
    return Capture(c, b, p, s, mask, nullptr);
}
FSStatus real_Raw_FSGetFreeSpaceSizeAsync(FSClient *c, FSCmdBlock *b, const char *p,
                                         uint64_t *s, FSErrorFlag mask, FSAsyncData *a) {
    assert(a == &async);
    return Capture(c, b, p, s, mask, a);
}
FSStatus real_Raw_FSFlushQuota(FSClient *c, FSCmdBlock *b, const char *p, FSErrorFlag mask) {
    const auto saved = output;
    auto result = Capture(c, b, p, &output, mask, nullptr);
    output = saved;
    return result;
}
FSStatus real_Raw_FSFlushQuotaAsync(FSClient *c, FSCmdBlock *b, const char *p,
                                  FSErrorFlag mask, FSAsyncData *a) {
    assert(a == &async);
    return Capture(c, b, p, &output, mask, a);
}

// PRODUCTION_HOOKS

void Check(const char *path, const char *expected) {
    constexpr uint64_t sentinel = 0x12345678ABCDEF00ULL;
    for (bool asynchronous : {false, true}) {
        for (int result : {0, -6, -12, -1024}) {
            for (uint64_t bytes : {0ULL, 512ULL, 128ULL * 1024 * 1024 * 1024}) {
                output = sentinel; available = bytes; status = result; calls = 0;
                auto got = asynchronous
                    ? Raw_FSGetFreeSpaceSizeAsync(&client, &block, path, &output, 0x52, &async)
                    : Raw_FSGetFreeSpaceSize(&client, &block, path, &output, 0x52);
                assert(got == result && calls == 1 && seenPath == expected);
                assert(output == (!asynchronous && result == 0 ? bytes : sentinel));
                if (asynchronous && result == 0) {
                    // The underlying API owns completion, including its 64-bit result.
                    output = available;
                    assert(output == bytes);
                }
                const auto before = output;
                calls = 0;
                got = asynchronous ? Raw_FSFlushQuotaAsync(&client, &block, path, 0x52, &async)
                                   : Raw_FSFlushQuota(&client, &block, path, 0x52);
                assert(got == result && calls == 1 && seenPath == expected && output == before);
            }
        }
    }
}
int main() {
    failPathAllocation = true;
    calls = 0; output = 0x1234;
    assert(Raw_FSGetFreeSpaceSize(&client, &block, "/vol/save", &output, 0x52) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSGetFreeSpaceSizeAsync(&client, &block, "/vol/save", &output, 0x52, &async) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSFlushQuota(&client, &block, "/vol/save", 0x52) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSFlushQuotaAsync(&client, &block, "/vol/save", 0x52, &async) == FS_STATUS_FATAL_ERROR);
    assert(calls == 0 && output == 0x1234);
    gLooseTitleInfo.active = false;
    Check("/vol/save", "/vol/save");
    gLooseTitleInfo.active = true;
    failPathAllocation = false;
    Check("/vol/save", "/vol/external01/wiiu/raw-saves/0005000010146a00");
    Check("/vol/save/", "/vol/external01/wiiu/raw-saves/0005000010146a00/");
    Check("/vol/save/80000007", "/vol/external01/wiiu/raw-saves/0005000010146a00/80000007");
    Check("/vol/save/common/", "/vol/external01/wiiu/raw-saves/0005000010146a00/common/");
    Check("/vol/save-other", "/vol/save-other");
    Check("/vol/content", "/vol/content");
    Check("/vol/external01", "/vol/external01");
    Check(nullptr, "<null>");
    gLooseTitleInfo.active = false;
    Check("/vol/save/80000007", "/vol/save/80000007");
    gLooseTitleInfo.active = true;
    gLooseTitleInfo.savePath = "";
    Check("/vol/save", "/vol/save");
    puts("PASS: production save-space/flush hooks; allocation failure, SD/account routing, zero/64-bit capacity, errors, async ownership, inactive passthrough");
}
