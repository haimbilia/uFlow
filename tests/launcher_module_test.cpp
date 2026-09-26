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
    // The module attaches to the built-in NooDS entry instead of adding one.
    auto launchers=LoadLaunchers();assert(launchers.size()==DefaultLaunchers().size());assert(launchers[3].moduleId=="noods");
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
    // RetroArch native-argument modules: core under retroarch/cores, assets
    // from a .wuhb content provider, and no request/dependency/capability.
    const std::string ra="UFLOW-MODULE-1\nid=ps1\nname=PlayStation (RetroArch)\nversion=1.0.0\n"
                         "system=PlayStation\nextensions=.cue,.chd,.pbp\n"
                         "executable=retroarch/cores/pcsx_rearmed_libretro.rpx\n"
                         "protocol=retroarch-sysargs-v1\ncontent_bundle=wiiu/apps/retroarch.wuhb\n";
    LauncherModule r;
    assert(ParseLauncherModule(ra,r)&&r.error.empty());
    assert(r.id=="ps1"&&r.protocol==kRetroArchProtocol&&r.contentBundle=="wiiu/apps/retroarch.wuhb");
    assert(r.request.empty()&&r.dependency.empty()&&r.capability.empty());
    for(const std::string &bad:{std::string("request=wiiu/apps/x.req"),std::string("capability=UFLOW-ROM-1"),
                                std::string("dependency=wiiu/apps/x.txt")})
        assert(!ParseLauncherModule(ra+bad+"\n",r));
    const auto swap=[&](const std::string &from,const std::string &to){auto copy=ra;copy.replace(copy.find(from),from.size(),to);return copy;};
    for(const std::string &bad:{swap("retroarch/cores/pcsx_rearmed_libretro.rpx","wiiu/apps/other/core.rpx"),   // outside the core root
                                swap("retroarch/cores/pcsx_rearmed_libretro.rpx","retroarch/cores/core.wuhb"), // not an rpx
                                swap("retroarch/cores/pcsx_rearmed_libretro.rpx","retroarch/cores/../../core.rpx"),
                                swap("retroarch/cores/pcsx_rearmed_libretro.rpx","/retroarch/cores/core.rpx"),
                                swap("wiiu/apps/retroarch.wuhb","retroarch/retroarch.wuhb"),                   // provider outside wiiu/apps
                                swap("wiiu/apps/retroarch.wuhb","wiiu/apps/retroarch.rpx"),                    // provider not a bundle
                                swap("wiiu/apps/retroarch.wuhb","wiiu/apps//retroarch.wuhb")})
        assert(!ParseLauncherModule(bad,r));
    {   // content_bundle belongs to this protocol only
        LauncherModule other;
        assert(!ParseLauncherModule(manifest+"content_bundle=wiiu/apps/retroarch.wuhb\n",other));
    }
    assert(ParseLauncherModule(ra,r)); // the refusals above left their error in r
    assert(ModuleReadiness(r)=="RETROARCH CORE NOT INSTALLED");
    Put(root+"/"+r.executable,"core fixture");
    assert(ModuleReadiness(r)=="RETROARCH BUNDLE NOT INSTALLED");
    Put(root+"/"+r.contentBundle,"bundle fixture");
    assert(ModuleReadiness(r).empty()); // ready means no error text
    Put(root+"/wiiu/apps/uFlow/launchers/duplicate.ufmod",manifest);
    modules=DiscoverLauncherModules();assert(modules.size()==2);for(const auto &entry:modules)assert(entry.error=="DUPLICATE MODULE ID");
    std::cout<<"PASS: module discovery/version/dependencies, RetroArch native-argument manifests, duplicate rejection, ROM request roundtrip, uppercase/spaces, expiry, replay and missing-file protection\n";
}
