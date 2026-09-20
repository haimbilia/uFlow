#include "return_to_uflow.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace fs=std::filesystem;
void Put(const char *path,const char *text){std::ofstream f(path,std::ios::binary);f<<text;assert(bool(f));}
int main(){
    using namespace UFlowReturn;
    char temporary[]="/tmp/uflow-return-test-XXXXXX";
    assert(mkdtemp(temporary));fs::current_path(temporary);
    fs::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    assert(Clear()&&!Consume()&&!Arm());
    Put(Target,"test app");assert(Arm());
    MenuGate gate;gate.Begin(false);assert(!gate.Poll(true));
    assert(fs::exists(Request)); // Closing game / HOME overlay cannot consume it.
    gate.Begin(true);assert(!gate.Poll(false));assert(gate.Poll(true));
    assert(Consume());assert(!gate.Poll(true)&&!Consume());
    // Launch failure after consumption, or manually exiting uFlow, reaches Menu.
    gate.Begin(true);assert(gate.Poll(true)&&!Consume());
    assert(Arm()&&Clear()&&!Consume());
    assert(Arm());MenuGate afterVWii;afterVWii.Begin(true);
    assert(afterVWii.Poll(true)&&Consume());
    assert(Arm());fs::remove(Target);assert(!Consume()&&!fs::exists(Request));
    Put(Target,"test app");
    for(const char *invalid:{"","UFLOW-RETURN-1","UFLOW-RETURN-1\nextra","other"}){
        Put(Request,invalid);assert(!Consume()&&!fs::exists(Request));
    }
    fs::create_directory(Request);Put("fs:/vol/external01/wiiu/apps/uFlow/return-to-uflow.req/keep","x");
    assert(!Clear()&&!Arm()&&!Consume()); // Cannot remove request -> fail closed.
    puts("PASS: one-shot game return, Menu foreground only, vWii handoff, normal uFlow exit, launch failure, missing target, malformed request and I/O failure.");
}
