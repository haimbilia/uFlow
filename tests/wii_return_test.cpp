#include "wii_return.hpp"
#include <ogc/ipc.h>
#include <cassert>
#include <cstring>
#include <cstdio>

static int openResult = 7, ioctlResult = 0, calls = 0, closes = 0, flushes = 0;
s32 IOS_Open(const char *path, uint32_t mode) {
    assert(std::strcmp(path, "/dev/es") == 0 && mode == 0);
    return openResult;
}
s32 IOS_Close(s32 handle) { assert(handle == 7); ++closes; return 0; }
void DCFlushRange(void *data, uint32_t size) {
    assert(reinterpret_cast<uintptr_t>(data) % 32 == 0 && size % 32 == 0);
    ++flushes;
}
s32 IOS_Ioctlv(s32 handle, uint32_t cmd, uint32_t in, uint32_t out, ioctlv *vec) {
    assert(handle == 7 && cmd == 0xA1 && in == 1 && out == 0);
    assert(reinterpret_cast<uintptr_t>(vec) % 32 == 0);
    assert(reinterpret_cast<uintptr_t>(vec[0].data) % 32 == 0);
    assert(vec[0].len == 8);
    assert(*static_cast<u64 *>(vec[0].data) == 0x0001000248435641ULL);
    ++calls;
    return ioctlResult;
}
int main() {
    assert(UFlowPatchReturnToWiiU() == 0);
    assert(calls == 1 && closes == 1 && flushes == 2);
    ioctlResult = -1017;
    assert(UFlowPatchReturnToWiiU() == -1017);
    assert(calls == 2 && closes == 2 && flushes == 4);
    openResult = -6;
    assert(UFlowPatchReturnToWiiU() == -6);
    assert(calls == 2 && closes == 2 && flushes == 4);
    puts("PASS: Wii U return target, IPC alignment, and failure handling");
}
