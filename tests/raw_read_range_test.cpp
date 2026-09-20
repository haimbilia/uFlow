#include "RawReadRange.h"
#include <cassert>
#include <cstdio>
int main() {
    static_assert(RawReadBytes(-6, 1, 4096) == 0);
    static_assert(RawReadBytes(0, 1, 4096) == 0);
    static_assert(RawReadBytes(1, 0, 1) == 0);
    static_assert(RawReadBytes(3, 64, 2) == 0);
    static_assert(RawReadBytes(2, UINT32_MAX, 2) == 0);
    assert(RawReadBytes(4096, 1, 8192) == 4096);
    assert(RawReadBytes(2, 64, 4) == 128);
    assert(RawReadBytes(1, UINT32_MAX, 1) == UINT32_MAX);
    puts("PASS: raw read flush lengths (success, short read, EOF, error, overflow)");
}
