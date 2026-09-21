#include "RawExitTrace.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
using BOOL=int;
enum ProcUIStatus {Foreground, Background, ReleaseForeground, Exiting};
static ProcUIStatus nextStatus=Foreground;
static int messages=0,shutdowns=0,exits=0,lastBlock=-1,lastCode=-1;
static std::vector<std::string> reports;
void *OSGetCurrentThread(){return nullptr;}
void OSReport(const char *format,...){
    char buffer[256];va_list args;va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);reports.emplace_back(buffer);
}
ProcUIStatus real_Raw_TraceProcUIProcessMessages(BOOL block){++messages;lastBlock=block;return nextStatus;}
void real_Raw_TraceProcUIShutdown(){++shutdowns;}
void real_Raw_TraceExit(int code){++exits;lastCode=code;}
#define DECL_FUNCTION(result,name,...) result name(__VA_ARGS__)
// PRODUCTION_EXIT_TRACE
int main(){
    RawExitTraceBegin(false);
    assert(Raw_TraceProcUIProcessMessages(1)==Foreground&&messages==1&&lastBlock==1);
    Raw_TraceProcUIShutdown();Raw_TraceExit(17);RawExitTracePoint("disabled");
    assert(shutdowns==1&&exits==1&&lastCode==17&&reports.empty());
    RawExitTraceBegin(true);
    for(auto status:{Foreground,Background,ReleaseForeground,Exiting}){
        nextStatus=status;auto before=reports.size();
        assert(Raw_TraceProcUIProcessMessages(0)==status&&lastBlock==0);
        assert(reports.size()==before+1);
        assert(Raw_TraceProcUIProcessMessages(1)==status&&lastBlock==1);
        assert(reports.size()==before+1);
    }
    assert(messages==9);
    Raw_TraceProcUIShutdown();Raw_TraceExit(42);
    assert(shutdowns==2&&exits==2&&lastCode==42&&reports.size()==8);
    assert(reports[4].find("procui-shutdown-enter")!=std::string::npos);
    assert(reports[5].find("procui-shutdown-return")!=std::string::npos);
    assert(reports[6].find("coreinit-exit-enter")!=std::string::npos);
    assert(reports[7].find("coreinit-exit-return")!=std::string::npos);
    RawExitTracePoint("module-end-return");RawExitTracePoint("all-module-end-callbacks-done");
    assert(reports.size()==10); // stays latched through late shutdown
    RawExitTraceBegin(true);Raw_TraceProcUIProcessMessages(0);
    assert(reports.size()==11); // same status, new application
    RawExitTraceBegin(false);Raw_TraceProcUIProcessMessages(0);RawExitTracePoint("disabled");
    assert(reports.size()==11);
    puts("PASS: production raw exit hooks preserve calls, arguments and statuses; raw-only bounded lifecycle tracing, reset on next app.");
}
