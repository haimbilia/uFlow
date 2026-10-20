#pragma once
#include <cstdint>
using IOSError=int;using IOSAsyncCallbackFn=void(*)(IOSError,void*);
struct IOSVec{void *vaddr;uint32_t len;void *paddr;};
int IOS_IoctlvAsync(int,uint32_t,uint32_t,uint32_t,IOSVec*,IOSAsyncCallbackFn,void*);
