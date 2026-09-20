#include "launchers.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
void Put(const std::string &p,const std::string &s){fs::create_directories(fs::path(p).parent_path());std::ofstream(p,std::ios::binary)<<s;}
int main(int argc,char **argv) {
    assert(argc==2);const std::string root="fs:/vol/external01";assert(!fs::exists(root));
    std::ifstream input(argv[1]);std::string manifest((std::istreambuf_iterator<char>(input)),{});
    LauncherModule m;assert(ParseLauncherModule(manifest,m));assert(m.id=="noods");
    const auto valid=m;
    assert(!ParseLauncherModule("UFLOW-MODULE-2\n",m));
    assert(!ParseLauncherModule(manifest+"protocol=rom-request-v1\n",m));
    assert(!ParseLauncherModule(manifest+"unknown=value\n",m));
    auto malicious=manifest;auto at=malicious.find("wiiu/apps/noods/uflow-launch.req");malicious.replace(at,30,"wiiu/apps/../../bad.req");assert(!ParseLauncherModule(malicious,m));
    Put(root+"/wiiu/apps/uFlow/launchers/noods.ufmod",manifest);
    auto modules=DiscoverLauncherModules();assert(modules.size()==1&&modules[0].error.empty());
    auto launchers=LoadLaunchers();assert(launchers.size()==14);assert(launchers[3].moduleId=="noods");
    assert(ModuleReadiness(valid)=="EMULATOR NOT INSTALLED");
    Put(root+"/"+valid.executable,"fixture");assert(!ModuleReadiness(valid).empty());
    Put(root+"/"+valid.dependency,"UFLOW-ROM-1\n");assert(ModuleReadiness(valid).empty());
    assert(LauncherReadiness(launchers[3]).find("DIRECT ROM LAUNCH")==0);
    Put(root+"/nds/Game with spaces.NDS","ROM fixture");
    const auto request=root+"/"+valid.request;
    assert(UFlowRom::RelativeROM(root+"/nds/Game with spaces.NDS")=="nds/Game with spaces.NDS");
    assert(UFlowRom::RelativeROM("fs:/vol/storage_mlc01/game.nds").empty());
    for(const char *p:{"../bad.nds","nds/../bad.nds","/nds/game.nds","nds//game.nds","nds/game.nds\n"})assert(!UFlowRom::RelativePath(p));
    assert(UFlowRom::Write(request,"nds/Game with spaces.NDS"));
    std::string rom;assert(UFlowRom::Consume(request,root,rom));assert(rom==root+"/nds/Game with spaces.NDS");
    assert(!fs::exists(request));assert(!UFlowRom::Consume(request,root,rom));
    const auto now=std::time(nullptr);
    Put(request,UFlowRom::Serialize("nds/Game with spaces.NDS",now-121));assert(!UFlowRom::Consume(request,root,rom));assert(!fs::exists(request));
    Put(request,UFlowRom::Serialize("nds/Game with spaces.NDS",now+1000));assert(!UFlowRom::Consume(request,root,rom));
    assert(UFlowRom::Write(request,"nds/missing.nds"));assert(!UFlowRom::Consume(request,root,rom));
    Put(request,std::string(5000,'x'));assert(!UFlowRom::Consume(request,root,rom));
    assert(!UFlowRom::Parse(UFlowRom::Serialize("nds/a.nds",now)+"extra\n",now,rom));
    Put(root+"/wiiu/apps/uFlow/launchers/duplicate.ufmod",manifest);
    modules=DiscoverLauncherModules();assert(modules.size()==2);for(const auto &entry:modules)assert(entry.error=="DUPLICATE MODULE ID");
    std::cout<<"PASS: module discovery/version/dependencies, duplicate rejection, ROM request roundtrip, uppercase/spaces, expiry, replay and missing-file protection\n";
}
