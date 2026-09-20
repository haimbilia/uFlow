#pragma once
#include <gccore.h>
struct ioctlv { void *data; uint32_t len; };
s32 IOS_Open(const char *, uint32_t);
s32 IOS_Close(s32);
s32 IOS_Ioctlv(s32, uint32_t, uint32_t, uint32_t, ioctlv *);
