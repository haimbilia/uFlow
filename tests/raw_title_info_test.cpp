#include "LoadiineTitleInfo.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    KernelPrepareTitleInfo info;
    std::memset(&info, 0xA5, sizeof(info));
    RawTitleProfile profile{};
    profile.appType = 0x80000000;
    profile.groupId = 0xDEADBEEF;
    profile.titleId = 0x000500001012F000ULL;
    profile.maxSize = 0x40000000;
    profile.defaultStack0Size = 0x10000;
    std::strcpy(profile.argstr, "project.rpx -test hello");
    ApplyLoadiineTitleInfo(info, profile);
    assert(info.appType == profile.appType && info.appType != profile.groupId);
    assert(info.titleId == profile.titleId);
    assert(info.maxSize == profile.maxSize);
    assert(info.defaultStack0Size == profile.defaultStack0Size);
    assert(std::memcmp(info.argstr, "project.rpx\0-test\0hello\0", 23) == 0);
    for (size_t i = 0; i < 64; ++i) assert(info.unknown2[i] == 0xA5A5A5A5);
    for (size_t i = 0; i < 59; ++i) assert(info.unknown4[i] == 0xA5A5A5A5);
    assert(info.argstr[4095] == 0);
    profile.enabled = 1;
    profile.sdkVersion = 21213;
    profile.maxCodeSize = 0x04F00000;
    volatile uint32_t armed = 1;
    for (int pass = 0; pass < 2; ++pass) {
        KernelPrepareTitleInfo wrapper{};
        std::strcpy(wrapper.argstr, "safe.rpx");
        wrapper.sdkVersion = 20804;
        wrapper.codegenSize = 0x02000000;
        assert(ApplyArmedRawTitleInfo(wrapper, profile, armed));
        assert(wrapper.sdkVersion == 21213 && wrapper.maxCodeSize == 0x04F00000);
        assert(wrapper.codegenSize == 0 && wrapper.titleId == profile.titleId);
    }
    assert(armed == 3);
    std::strcpy(info.argstr, "men.rpx");
    assert(!ApplyArmedRawTitleInfo(info, profile, armed) && armed == 3);
    std::strcpy(info.argstr, "safe.rpx.bak");
    assert(!ApplyArmedRawTitleInfo(info, profile, armed));
    std::strcpy(info.argstr, "safe.rpx");
    armed = 0;
    assert(!ApplyArmedRawTitleInfo(info, profile, armed));
    armed = 1;
    profile.enabled = 0;
    assert(!ApplyArmedRawTitleInfo(info, profile, armed));
    std::cout << "PASS: Loadiine kernel ABI, app type, argument splitting, opaque fields preserved\n";
}
