#pragma once
#include <cstdint>
using s32 = int32_t;
using u64 = uint64_t;
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
void DCFlushRange(void *, uint32_t);
