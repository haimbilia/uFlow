#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> calls;
struct { struct {bool isRPXReplaced=true;} rpxReplacementInfo; } gReplacementInfo;
void RawExitTracePoint(const char *point){calls.emplace_back(point);}
void RL_UnmountCurrentRunningBundle(){calls.emplace_back("unmount-bundle");}
void RPXLoadingCleanUp(){assert(!gReplacementInfo.rpxReplacementInfo.isRPXReplaced);calls.emplace_back("close-readers");}
void ContentRedirection_RemoveDevice(const char *name,int *result){assert(std::string(name)=="ufusb:");*result=0;calls.emplace_back("remove-device");}
namespace USBStorage {void Unmount(){calls.emplace_back("unmount-usb");}}
void deinitLogging(){calls.emplace_back("deinit-logger");}
void OSReport(const char *,...){}
#define WUMS_APPLICATION_ENDS() void EndApplication()
// PRODUCTION_END_CALLBACK
static int opens=0;
FILE *TestOpen(const char *,const char *){++opens;return nullptr;}
#define fopen TestOpen
void ProfileLog(bool enabled){
    struct {uint64_t titleId=0x00050000101cb500ULL;} profile;
    int result=0;uint32_t response[1]={0};
    // PRODUCTION_PROFILE_LOG
}
int main(){
    EndApplication();
    assert((calls==std::vector<std::string>{"module-end-enter","unmount-bundle","bundle-layers-released",
        "close-readers","file-readers-released","remove-device","usb-redirection-device-removed",
        "unmount-usb","usb-unmounted","deinit-logger","module-end-return"}));
    ProfileLog(false);assert(opens==0);
    ProfileLog(true);assert(opens==1);
    ProfileLog(false);assert(opens==1);
    puts("PASS: actual shutdown callback preserves full cleanup order without SD log writes; disabled profile never opens a log.");
}
