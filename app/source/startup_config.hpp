#pragma once
#include <cstdio>
#include <cstring>
namespace UFlowStartup {
constexpr const char *Flag = "fs:/vol/external01/wiiu/apps/uFlow/start-on-boot.cfg";
constexpr const char *Target = "fs:/vol/external01/wiiu/apps/uFlow/uFlow.wuhb";
inline bool Enabled() {
    FILE *file=std::fopen(Flag,"rb");
    if(!file) return false;
    char text[16]{};
    const auto size=std::fread(text,1,sizeof(text),file);
    std::fclose(file);
    return size==10 && std::memcmp(text,"enabled=1\n",10)==0;
}
}
