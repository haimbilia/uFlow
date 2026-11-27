#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
int rpx_utils_function_replacements[]={0,1,2};
uint32_t rpx_utils_function_replacements_size=3;
constexpr int FUNCTION_PATCHER_RESULT_SUCCESS=0;
int failure=-1,calls=0,pending=0;
int FunctionPatcher_AddFunctionPatch(int *hook,void *,bool *patched){
    ++calls;*patched=(*hook==2);return *hook==failure?-7:0;
}
void OSReport(const char *,...){++pending;}
[[noreturn]] void OSFatal(const char *message){throw std::runtime_error(message);}
// PRODUCTION_REGISTRATION
int main(){
    RegisterFunctionPatches();assert(calls==3 && pending==2);
    calls=pending=0;failure=1;
    try {RegisterFunctionPatches();assert(false);}catch(const std::runtime_error &e){
        assert(std::string(e.what()).find("hook 1 registration failed (-7)")!=std::string::npos);
    }
    assert(calls==2 && pending==1);
    puts("PASS: actual hook registration accepts deferred RPL hooks and reports real registration failures");
}
