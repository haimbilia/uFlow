// tools/test-raw-file-opens.ps1 inserts the ACTUAL four production hooks below.
#include "RawNativePath.h"
#include "RawNativePathStorage.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

using FSStatus = int;
using FSFileHandle = int;
using FSMode = int;
using FSOpenFileFlags = int;
using FSErrorFlag = int;
constexpr int FS_STATUS_OK = 0;
constexpr int FS_STATUS_FATAL_ERROR = -1024;
struct FSClient {};
struct FSCmdBlock {};
struct FSAsyncData { void *callback; void *ioMsgQueue; void *param; };
struct {
    bool active = true;
    const char *contentPath = "wiiu/raw-games/Yoshi/content";
    const char *codePath = "wiiu/raw-games/Yoshi/code";
    const char *savePath = "wiiu/raw-saves/0005000010184d00";
} gLooseTitleInfo;
std::atomic<uint32_t> sRawSaveOpenSamples{0}, sRawNativeOpenSamples{0};
void OSReport(const char *, ...) {}
#define DECL_FUNCTION(ret, name, ...) ret name(__VA_ARGS__)

FSClient client;
FSCmdBlock block;
FSFileHandle handle = 0x1234;
FSAsyncData async{&client, &block, &handle};
std::string seenPath, seenMode;
FSStatus result = 0;
int calls = 0;
bool failPathAllocation = false;
bool nestedOpen = false;
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    if (failPathAllocation) return nullptr;
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
FSStatus Raw_FSOpenFileAsync(FSClient *, FSCmdBlock *, const char *, const char *,
                            FSFileHandle *, FSErrorFlag, FSAsyncData *);

FSStatus Capture(FSClient *c, FSCmdBlock *b, const char *path, const char *mode,
                 FSFileHandle *h, FSErrorFlag mask, FSAsyncData *a) {
    assert(c == &client && b == &block && h == &handle && mask == 0x52);
    assert(a == nullptr || a == &async);
    if (a) assert(a->callback == &client && a->ioMsgQueue == &block && a->param == &handle);
    seenPath = path;
    seenMode = mode;
    ++calls;
    // Simulate submission only: async completion owns the output handle.
    return result;
}
FSStatus real_Raw_FSOpenFile(FSClient *c, FSCmdBlock *b, const char *p, const char *m,
                             FSFileHandle *h, FSErrorFlag e) {
    if (nestedOpen) {
        const std::string outerPath(p);
        Raw_FSOpenFileAsync(c, b, "/vol/save/common/nested", m, h, e, &async);
        assert(outerPath == p);
        assert(seenPath.ends_with("/common/nested"));
    }
    return Capture(c, b, p, m, h, e, nullptr);
}
FSStatus real_Raw_FSOpenFileAsync(FSClient *c, FSCmdBlock *b, const char *p, const char *m,
                                  FSFileHandle *h, FSErrorFlag e, FSAsyncData *a) {
    assert(a == &async);
    return Capture(c, b, p, m, h, e, a);
}
FSStatus real_Raw_FSOpenFileEx(FSClient *c, FSCmdBlock *b, const char *p, const char *m,
                               FSMode cm, FSOpenFileFlags f, uint32_t pre, FSFileHandle *h, FSErrorFlag e) {
    assert(cm == 0x660 && f == 0x21 && pre == 24576);
    return Capture(c, b, p, m, h, e, nullptr);
}
FSStatus real_Raw_FSOpenFileExAsync(FSClient *c, FSCmdBlock *b, const char *p, const char *m,
                                    FSMode cm, FSOpenFileFlags f, uint32_t pre, FSFileHandle *h,
                                    FSErrorFlag e, FSAsyncData *a) {
    assert(cm == 0x660 && f == 0x21 && pre == 24576 && a == &async);
    return Capture(c, b, p, m, h, e, a);
}

// PRODUCTION_HOOKS

void Check(const char *path, const char *mode, const char *expected) {
    for (int entry = 0; entry != 4; ++entry) {
        for (int status : {0, -6, -12, -1024}) {
            result = status;
            calls = 0;
            FSStatus got;
            switch (entry) {
            case 0: got = Raw_FSOpenFile(&client, &block, path, mode, &handle, 0x52); break;
            case 1: got = Raw_FSOpenFileAsync(&client, &block, path, mode, &handle, 0x52, &async); break;
            case 2: got = Raw_FSOpenFileEx(&client, &block, path, mode, 0x660, 0x21, 24576, &handle, 0x52); break;
            default: got = Raw_FSOpenFileExAsync(&client, &block, path, mode, 0x660, 0x21, 24576, &handle, 0x52, &async); break;
            }
            assert(got == status && calls == 1 && seenPath == expected && seenMode == mode);
            assert(handle == 0x1234); // Hook must not manufacture a handle/completion.
        }
    }
}
int main() {
    static_assert(sizeof(RawNativePathStorage) <= 2 * sizeof(void *));
    // Two simultaneously live buffers remain independent across nested calls.
    RawNativePathStorage first(true), second(true);
    assert(first.get() && second.get() && first.get() != second.get());
    std::memset(first.get(), 'a', RawNativePathStorage::Capacity);
    std::memset(second.get(), 'b', RawNativePathStorage::Capacity);
    assert(first.get()[RawNativePathStorage::Capacity - 1] == 'a');
    result = 0;
    nestedOpen = true;
    assert(Raw_FSOpenFile(&client, &block, "/vol/save/common/outer", "r", &handle, 0x52) == 0);
    assert(seenPath.ends_with("/common/outer"));
    nestedOpen = false;
    failPathAllocation = true;
    calls = 0;
    assert(Raw_FSOpenFile(&client, &block, "/vol/save/x", "w", &handle, 0x52) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSOpenFileAsync(&client, &block, "/vol/save/x", "w", &handle, 0x52, &async) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSOpenFileEx(&client, &block, "/vol/save/x", "w", 0x660, 0x21, 24576, &handle, 0x52) == FS_STATUS_FATAL_ERROR);
    assert(Raw_FSOpenFileExAsync(&client, &block, "/vol/save/x", "w", 0x660, 0x21, 24576, &handle, 0x52, &async) == FS_STATUS_FATAL_ERROR);
    assert(calls == 0 && handle == 0x1234);
    gLooseTitleInfo.active = false;
    Check("/vol/save/x", "r", "/vol/save/x");
    gLooseTitleInfo.active = true;
    failPathAllocation = false;
    for (auto mode : {"r", "rb", "r+", "w", "wb", "w+", "a", "a+"}) {
        Check("/vol/save/common/savedata.dat", mode,
              "/vol/external01/wiiu/raw-saves/0005000010184d00/common/savedata.dat");
        Check("/vol/save/80000007/file", mode,
              "/vol/external01/wiiu/raw-saves/0005000010184d00/80000007/file");
    }
    Check("/vol/content/movie", "rb", "/vol/external01/wiiu/raw-games/Yoshi/content/movie");
    Check("/vol/content/file", "w", "/vol/content/file");
    Check("/vol/save-other/file", "w", "/vol/save-other/file");
    Check("/vol/external01/native", "w", "/vol/external01/native");
    gLooseTitleInfo.active = false;
    Check("/vol/save/common/savedata.dat", "w", "/vol/save/common/savedata.dat");
    Check("/vol/content/movie", "r", "/vol/content/movie");
    puts("PASS: all four production file-open hooks; paths, arguments, errors, async data, inactive passthrough");
}
