#pragma once
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// One-shot handoff, shared by uFlow and the Aroma loader. Stored on SD so it
// also survives the vWii -> Cafe transition. Never a global autoboot setting.
namespace UFlowReturn {
class MenuGate {
    std::atomic<bool> pending{false};
public:
    void Begin(bool isMenu){pending.store(isMenu);}
    bool Poll(bool foreground){return foreground&&pending.exchange(false);}
};
constexpr const char *Request="fs:/vol/external01/wiiu/apps/uFlow/return-to-uflow.req";
constexpr const char *Temporary="fs:/vol/external01/wiiu/apps/uFlow/return-to-uflow.tmp";
constexpr const char *Target="fs:/vol/external01/wiiu/apps/uFlow/uFlow.wuhb";
constexpr const char *RelativeTarget="wiiu/apps/uFlow/uFlow.wuhb";
constexpr char Magic[]="UFLOW-RETURN-1\n";
inline bool Remove(const char *path){return std::remove(path)==0||errno==ENOENT;}
inline bool Clear(){const bool a=Remove(Request),b=Remove(Temporary);return a&&b;}
inline bool Arm(){
    if(!Clear())return false;
    struct stat st{};if(stat(Target,&st)!=0||!S_ISREG(st.st_mode))return false;
    FILE *f=std::fopen(Temporary,"wb");if(!f)return false;
    bool ok=std::fwrite(Magic,1,sizeof(Magic)-1,f)==sizeof(Magic)-1;
    if(std::fclose(f)!=0)ok=false;
    if(!ok||std::rename(Temporary,Request)!=0){Clear();return false;}
    return true;
}
inline bool Consume(){
    FILE *f=std::fopen(Request,"rb");if(!f)return false;
    char text[sizeof(Magic)]{};const auto size=std::fread(text,1,sizeof(text),f);
    const bool valid=!std::ferror(f)&&size==sizeof(Magic)-1&&std::memcmp(text,Magic,size)==0;
    std::fclose(f);
    // Remove BEFORE launching, even for a malformed request. Failed deletion,
    // a missing app or a failed launch must never create a relaunch loop.
    if(!Remove(Request)||!valid)return false;
    struct stat st{};return stat(Target,&st)==0&&S_ISREG(st.st_mode);
}
}
