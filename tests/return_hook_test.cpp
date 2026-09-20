// test-return-hook.ps1 injects the actual loader callback and ProcUI hook.
#include "return_to_uflow.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using BOOL=int;
using RPXLoaderStatus=int;
enum ProcUIStatus {PROCUI_STATUS_IN_FOREGROUND,PROCUI_STATUS_IN_BACKGROUND,PROCUI_STATUS_RELEASE_FOREGROUND,PROCUI_STATUS_EXITING};
constexpr int SYSTEM_APP_ID_WII_U_MENU=0;
static uint64_t title=0x0005000012345600;
static uint64_t menu=0x0005001010040100;
uint64_t OSGetTitleID(){return title;}
uint64_t _SYSGetSystemApplicationTitleId(int){return menu;}
static ProcUIStatus nextStatus=PROCUI_STATUS_IN_FOREGROUND;
static int realCalls=0,launches=0,result=0;
ProcUIStatus real_UFlow_ProcUIProcessMessages(BOOL){++realCalls;return nextStatus;}
void OSReport(const char *,...){}
RPXLoaderStatus RL_LaunchHomebrew(const char *path){
    assert(!std::filesystem::exists(UFlowReturn::Request));
    assert(std::strcmp(path,UFlowReturn::RelativeTarget)==0);++launches;return result;
}
#define DECL_FUNCTION(result,name,...) result name(__VA_ARGS__)
// PRODUCTION_RETURN_HOOK
int main(){
    char temporary[]="/tmp/uflow-return-hook-XXXXXX";assert(mkdtemp(temporary));
    std::filesystem::current_path(temporary);
    std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    {std::ofstream f(UFlowReturn::Target);f<<"fixture";}
    assert(UFlowReturn::Arm());UFlowReturnApplicationStarts();
    assert(UFlow_ProcUIProcessMessages(1)==nextStatus&&realCalls==1&&launches==0);
    title=menu;UFlowReturnApplicationStarts();
    for(auto status:{PROCUI_STATUS_IN_BACKGROUND,PROCUI_STATUS_RELEASE_FOREGROUND,PROCUI_STATUS_EXITING}){
        nextStatus=status;assert(UFlow_ProcUIProcessMessages(1)==status&&launches==0);
    }
    nextStatus=PROCUI_STATUS_IN_FOREGROUND;
    UFlow_ProcUIProcessMessages(1);assert(launches==1);
    UFlow_ProcUIProcessMessages(1);assert(launches==1);
    UFlowReturnApplicationStarts();UFlow_ProcUIProcessMessages(1);assert(launches==1);
    assert(UFlowReturn::Arm());result=-1;UFlowReturnApplicationStarts();
    UFlow_ProcUIProcessMessages(1);assert(launches==2&&!UFlowReturn::Consume());
    UFlowReturnApplicationStarts();UFlow_ProcUIProcessMessages(1);assert(launches==2);
    puts("PASS: actual Menu ProcUI hook preserves real statuses, ignores games/background/exit, consumes before launch, returns only once even on launch failure.");
}
