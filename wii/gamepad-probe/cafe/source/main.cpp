#include "../../prepare.hpp"
#include <coreinit/dynload.h>
#include <coreinit/thread.h>
#include <nn/cmpt/cmpt.h>
#include <padscore/kpad.h>
#include <whb/proc.h>
#include <whb/sdcard.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <string>
#include <unistd.h>

namespace {
int32_t (*getDrc)(int32_t*);
int32_t (*setDrc)(int32_t);
int32_t (*setWork)(void*, uint32_t);
int32_t (*prepare)(uint32_t, const void*, uint32_t);
int32_t (*launch)();
std::string journalPath, logPath;
constexpr char Recovery[]="UFLOW_GAMEPAD_PROBE_DRC_RESTORE_0\n";
constexpr uint64_t Hbc=0x000100014f484243ULL;

void Log(const char *label, int result) {
    WHBLogPrintf("%s: %d",label,result);
    FILE *f=fopen(logPath.c_str(),"a");
    if(f) { fprintf(f,"%s: %d\n",label,result); fclose(f); }
}
struct Backend {
    bool Read(int &value) {
        int32_t read=-1;
        int rc=getDrc(&read);
        Log("Read DRC result",rc); Log("DRC value",read);
        value=read;
        return rc==0;
    }
    bool Set(int value) {
        int rc=setDrc(value);
        Log("Set DRC result",rc);
        int actual=-1;
        bool verified=Read(actual) && actual==value;
        return rc==0 && verified;
    }
    bool Journal(int original) {
        if(original!=0) return false;
        int fd=open(journalPath.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) return false;
        bool ok=write(fd,Recovery,sizeof(Recovery)-1)==sizeof(Recovery)-1;
        if(fsync(fd)!=0) ok=false;
        if(close(fd)!=0) ok=false;
        // Keep an incomplete marker as a blocker; never change settings unless
        // the restoration record was successfully flushed and closed.
        return ok;
    }
    bool ClearJournal() { return unlink(journalPath.c_str())==0; }
    bool Prepare() {
        int rc=prepare(2,&Hbc,sizeof(Hbc));
        Log("CMPTExPrepareLaunch result",rc);
        return rc==0;
    }
};
bool Recover(Backend &backend) {
    FILE *f=fopen(journalPath.c_str(),"rb");
    if(!f) return errno==ENOENT;
    char data[sizeof(Recovery)+1]={};
    size_t count=fread(data,1,sizeof(data),f);
    bool valid=!ferror(f) && count==sizeof(Recovery)-1 &&
        memcmp(data,Recovery,sizeof(Recovery)-1)==0;
    fclose(f);
    if(!valid) { Log("Invalid recovery marker; stopping",-1); return false; }
    Log("Recovering interrupted preparation",0);
    return backend.Set(0) && backend.ClearJournal();
}
template<class T> bool Resolve(OSDynLoad_Module module,const char *name,T &fn) {
    return OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,name,
        reinterpret_cast<void **>(&fn))==OS_DYNLOAD_OK && fn;
}
bool Run() {
    OSDynLoad_Module module;
    if(OSDynLoad_Acquire("nn_cmpt.rpl",&module)!=OS_DYNLOAD_OK) return false;
    if(!Resolve(module,"CMPTAcctGetDrcCtrlEnabled",getDrc) ||
       !Resolve(module,"CMPTAcctSetDrcCtrlEnabled",setDrc) ||
       !Resolve(module,"CMPTExSetWorkBuffer",setWork) ||
       !Resolve(module,"CMPTExPrepareLaunch",prepare) ||
       !Resolve(module,"CMPTExLaunch",launch)) {
        Log("Missing compatibility API; stopping",-1); return false;
    }
    Backend backend;
    if(!Recover(backend)) return false;
    int screen=CMPTCheckScreenState();
    Log("Screen state",screen);
    if(screen<0) return false;
    constexpr uint32_t size=0x00c00000;
    void *buffer=memalign(64,size);
    if(!buffer) return false;
    int rc=setWork(buffer,size);
    Log("CMPTExSetWorkBuffer result",rc);
    if(rc!=0) { free(buffer); return false; }
    KPADInit();
    auto result=GamePadProbe::PrepareWithDrc(backend);
    Log("Preparation status (0=ready)",static_cast<int>(result));
    if(result!=GamePadProbe::Preparation::Ready) {
        // Work buffer stays alive until process exit: the library retains it.
        return false;
    }
    Log("Settings restored/unchanged; launching prepared HBC",0);
    // Launch uses prepared IOS-side state; whether ordinary vWii honors the
    // DRC flag is deliberately an unanswered hardware-test question.
    rc=launch();
    Log("CMPTExLaunch returned",rc);
    return rc==0;
}
}
int main() {
    WHBProcInit(); WHBLogConsoleInit();
    bool mounted=WHBMountSdCard();
    if(mounted) {
        const char *root=WHBGetSdCardMountPath();
        if(root) {
            std::string directory=std::string(root)+"/wiiu/apps/uFlow/";
            journalPath=directory+"gamepad-probe-drc.restore";
            logPath=directory+"gamepad-launch-probe.log";
            // Require the existing installation directory, never create an SD
            // layout or proceed without a writable diagnostic log.
            FILE *f=fopen(logPath.c_str(),"a");
            if(f) {
                fputs("\nGamePad launch preparation experiment v1\n",f); fclose(f);
                Run();
            }
        }
    }
    WHBLogPrintf("Diagnostic finished or stopped. HOME returns to Wii U Menu.");
    while(WHBProcIsRunning()) {
        WHBLogConsoleDraw(); OSSleepTicks(OSMillisecondsToTicks(100));
    }
    if(mounted) WHBUnmountSdCard();
    WHBLogConsoleFree(); WHBProcShutdown();
    return 0;
}
