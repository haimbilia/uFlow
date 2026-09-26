#include "installer.hpp"
#include "generated_files.hpp"
#include <filesystem>
#include <cassert>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
using namespace Setup;
void Put(const std::string &p,const std::string &s){fs::create_directories(fs::path(p).parent_path());Write(p,s);}
template<class F>void Fails(F f,const std::string &s){bool failed=false;try{f();}catch(const std::exception &e){failed=std::string(e.what()).find(s)!=std::string::npos;if(!failed)std::cerr<<e.what()<<'\n';}assert(failed);}
void Aroma(const std::string &sd){for(const char *p:{"wiiu/environments/aroma/root.rpx","wiiu/environments/aroma/modules/setup/10_wums_loader.rpx","wiiu/environments/aroma/modules/ContentRedirectionModule.wms","wiiu/environments/aroma/modules/WUHBUtilsModule.wms"})Put(sd+"/"+p,"stock");}
int main(int argc,char **argv){
    for(uint32_t bits:{0u,All,2u,4u}) {
        std::vector<std::string> dirs;
        bool found=false;
        for(const auto &[name,body]:Configs(bits,dirs)) if(name=="uflow.cfg") {
            found=true;assert(body.find("sort_mode=0\n")!=std::string::npos);assert(body.find("preload_builtin_covers=1\n")!=std::string::npos);
            assert(body.find("startup_video=1\n")!=std::string::npos);
        }
        assert(found);
    }
    assert(Hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(Hash(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    char temp[]="/tmp/uflow-native-setup-XXXXXX";const std::string root=mkdtemp(temp),sd=root+"/sd",bundle=root+"/bundle";
    Aroma(sd);fs::create_directories(bundle);std::vector<FileDef> defs;
    auto asset=[&](const std::string &p,uint32_t mask,char mode){Put(bundle+"/payload/"+p,"payload "+p);auto text="payload "+p;defs.push_back({p,Hash(text),text.size(),mask,mode});};
    asset("wiiu/apps/uFlow/uFlow.wuhb",Core,'r');
    const std::string forwarder="wiiu/apps/WiiFlow.wuhb";
    asset(forwarder,6u,'r');
    assert(Allowed(forwarder)&&!Allowed("wiiu/apps/Other.wuhb"));
    asset("wiiu/environments/aroma/modules/RPXLoadingModule.wms",1u|(1u<<4),'r');
    const std::string regionPlugin="wiiu/environments/aroma/plugins/regionfree.wps";
    const std::string regionConfig="wiiu/environments/aroma/plugins/config/region_free_plugin.json";
    asset(regionPlugin,1017u,'r');
    const std::string ftpPlugin="wiiu/environments/aroma/plugins/ftpiiu.wps";
    const std::string ftpConfig="wiiu/environments/aroma/plugins/config/ftpiiu.json";
    asset(ftpPlugin,Core,'r');
    Put(sd+"/"+ftpPlugin,"stock FTP");Put(sd+"/"+ftpConfig,"disabled user FTP settings");
    asset("apps/nintendont/boot.dol",1u<<2,'r');
    const std::string dsCover="wiiu/apps/uFlow/media/ds/fullcovers/Default.png";
    const std::string wiiuCover="wiiu/apps/uFlow/media/wiiu/fullcovers/Default.png";
    const std::string gbaCover="wiiu/apps/uFlow/media/gba/fullcovers/Default.png";
    const std::string n64Cover="wiiu/apps/uFlow/media/n64/fullcovers/Default.png";
    const std::string snesCover="wiiu/apps/uFlow/media/snes/fullcovers/Default.png";
    const std::string nesCover="wiiu/apps/uFlow/media/nes/fullcovers/Default.png";
    const std::string msxCover="wiiu/apps/uFlow/media/msx/fullcovers/Default.png";
    const std::string pceCover="wiiu/apps/uFlow/media/pce/fullcovers/Default.png";
    const std::string wiiCover="wiiu/apps/uFlow/media/wii/fullcovers/Default.png";
    const std::string gcCover="wiiu/apps/uFlow/media/gamecube/fullcovers/Default.png";
    asset(dsCover,(1u<<3)|(1u<<10),'k');asset(wiiCover,1u<<1,'k');asset(gcCover,1u<<2,'k');
    asset(wiiuCover,1u,'k');
    asset(gbaCover,(1u<<6)|(1u<<GbaRetroArch),'k');
    asset(n64Cover,1u<<7,'k');
    asset(snesCover,(1u<<5)|(1u<<SnesRetroArch),'k');
    asset(nesCover,(1u<<4)|(1u<<NesRetroArch),'k');
    asset(msxCover,1u<<9,'k');
    asset(pceCover,1u<<8,'k');
    asset("wiiu/vc-bases/nes/default/content/rom.bin",1u<<4,'b');
    const std::string existing=sd+"/wiiu/environments/aroma/modules/RPXLoadingModule.wms";
    Put(existing,"original");Put(sd+"/games/keep.sav","save");Put(sd+"/wiiu/apps/uFlow/libraries.cfg","custom");
    Put(sd+"/"+regionPlugin,"stock region plugin");Put(sd+"/"+regionConfig,"saved user region choices");
    Put(sd+"/wiiu/apps/uFlow/play_history.cfg","preserve play history");
    auto full=Prepare(sd,bundle,defs,All,{});assert(full.preserved==1);assert(!fs::exists(sd+"/.uflow-setup"));
    auto report=Install(sd,bundle,full,"test",{});assert(File(report));
    assert(HashFile(sd+"/games/keep.sav",{},"")==Hash("save"));
    assert(HashFile(sd+"/wiiu/apps/uFlow/libraries.cfg",{},"")==Hash("custom"));
    assert(HashFile(sd+"/"+regionPlugin,{},"")==Hash("payload "+regionPlugin));
    assert(HashFile(sd+"/"+regionConfig,{},"")==Hash("saved user region choices"));
    assert(HashFile(sd+"/"+ftpPlugin,{},"")==Hash("payload "+ftpPlugin));
    assert(HashFile(sd+"/"+ftpConfig,{},"")==Hash("disabled user FTP settings"));
    assert(!Allowed(ftpConfig));
    assert(!Allowed(regionConfig)&&!Allowed("wiiu/environments/aroma/plugins/unrelated.wps"));
    assert(HashFile(sd+"/.uflow-setup/test/backup/wiiu/environments/aroma/modules/RPXLoadingModule.wms",{},"")==Hash("original"));
    assert(Prepare(sd,bundle,defs,All,{}).bytes==0);
    Put(sd+"/"+dsCover,"user custom sleeve");
    assert(HashFile(sd+"/wiiu/apps/uFlow/play_history.cfg",{},"")==Hash("preserve play history"));
    auto preserved=Prepare(sd,bundle,defs,All,{});assert(preserved.bytes==0&&preserved.preserved==2);
    assert(HashFile(sd+"/"+dsCover,{},"")==Hash("user custom sleeve"));
    auto fresh=root+"/fresh";Aroma(fresh);
    auto minimal=Prepare(fresh,bundle,defs,0,{});assert(minimal.items.size()==5&&minimal.dirs.empty());
    Install(fresh,bundle,minimal,"minimal",{});assert(Size(fresh+"/wiiu/apps/uFlow/libraries.cfg")==0);
    Put(fresh+"/wiiu/apps/uFlow/uflow.cfg","sort_mode=2\npreload_builtin_covers=0\nstartup_video=0\n");
    Install(fresh,bundle,Prepare(fresh,bundle,defs,0,{}),"preload-preserved",{});
    assert(HashFile(fresh+"/wiiu/apps/uFlow/uflow.cfg",{},"")==Hash("sort_mode=2\npreload_builtin_covers=0\nstartup_video=0\n"));
    assert(!File(fresh+"/apps/nintendont/boot.dol"));
    assert(!File(fresh+"/"+regionPlugin));
    assert(!File(fresh+"/"+ftpPlugin));
    for(int bit=0;bit<ComponentCount;++bit){
        const auto selected=Prepare(fresh,bundle,defs,1u<<bit,{});
        const bool hasPlugin=std::any_of(selected.items.begin(),selected.items.end(),[&](const auto &i){return i.file.path==regionPlugin;});
        assert(hasPlugin==bool(1017u&(1u<<bit)));
        auto contains=[&](const std::string &path){return std::any_of(selected.items.begin(),selected.items.end(),[&](const auto &i){return i.file.path==path;});};
        assert(contains(dsCover)==(bit==3||bit==10));assert(contains(wiiCover)==(bit==1));assert(contains(gcCover)==(bit==2));
        assert(contains(wiiuCover)==(bit==0));
        assert(contains(gbaCover)==(bit==6||bit==GbaRetroArch));
        assert(contains(n64Cover)==(bit==7));
        assert(contains(snesCover)==(bit==5||bit==SnesRetroArch));
        assert(contains(nesCover)==(bit==4||bit==NesRetroArch));
        assert(contains(msxCover)==(bit==9));
        assert(contains(pceCover)==(bit==8));
        assert(contains(forwarder)==(bit==1||bit==2));
        assert(!contains(ftpPlugin));
    }
    std::vector<std::string> dirs;auto config=Configs(1u<<10,dirs);assert(config[0].second.find("\tnoods\t")!=std::string::npos&&config[0].second.find("ds-vc")==std::string::npos);
    auto nesConfig=Configs(1u<<4,dirs);assert(nesConfig[0].second.find("\t.nes,.unf,.unif,.zip\t")!=std::string::npos);
    const auto coverConfig=Configs(All,dirs,defs)[2].second;
    for(const auto &id:{"wiiu","ds","wii","gamecube","nes","snes","gba","n64","pce","msx"})assert(coverConfig.find(std::string("fullcovers\tid:")+id+"\t1\t1\tfs:/vol/external01/wiiu/apps/uFlow/media/"+id+"/fullcovers\t1000\t1\t70\tDefault.png\t100\n")!=std::string::npos);
    assert(Configs(1u<<10,dirs,defs)[2].second.find("\tDefault.png\t")!=std::string::npos);
    assert(Configs(All,dirs,{})[2].second.find("\tDefault.png\t")==std::string::npos);
    for(int bit=0;bit<=10;++bit) {
        std::vector<std::string> selectedDirs;
        const auto cfg=Configs(1u<<bit,selectedDirs,defs);
        const auto id=std::string(components[bit==10?3:bit].id);
        const auto size=CaseProfiles::For(id);
        assert(cfg[0].second.find("\t"+std::to_string(size.width)+"\t"+std::to_string(size.height)+"\t"+std::to_string(size.thickness)+"\t1\n")!=std::string::npos);
        for(const auto *part:{"front","back","spine","fullcovers"}) {
            const auto path="wiiu/apps/uFlow/media/"+id+"/"+part;
            assert(std::find(selectedDirs.begin(),selectedDirs.end(),path)!=selectedDirs.end());
            assert(cfg[2].second.find(std::string(part)+"\tid:"+id+"\t")!=std::string::npos);
        }
    }
    for(auto p:{"../escape","x/../a","/abs","x//y","x\\y","x:ads"})assert(!Safe(p));
    fs::create_directory_symlink(bundle,fresh+"/link");Fails([&]{Path(fresh,"link/out");},"Link refused");
    Put(sd+"/wiiu/vc-bases/nes/default/content/rom.bin","different base");Fails([&]{Prepare(sd,bundle,defs,All,{});},"Different existing VC base");
    auto duplicate=defs;duplicate.push_back(defs[0]);Fails([&]{Prepare(fresh,bundle,duplicate,0,{});},"duplicate");
    auto cancelled=root+"/cancelled";Aroma(cancelled);auto cp=Prepare(cancelled,bundle,defs,0,{});
    Fails([&]{Install(cancelled,bundle,cp,"cancel",[](auto&,auto&,auto,auto,bool){return false;});},"Stopped");
    assert(!File(cancelled+"/wiiu/apps/uFlow/uFlow.wuhb"));
    auto changed=root+"/changed";Aroma(changed);auto pp=Prepare(changed,bundle,defs,0,{});
    Put(changed+"/wiiu/apps/uFlow/uFlow.wuhb","user concurrent write");
    Fails([&]{Install(changed,bundle,pp,"changed",{});},"Destination changed");
    assert(HashFile(changed+"/wiiu/apps/uFlow/uFlow.wuhb",{},"")==Hash("user concurrent write"));
    Put(bundle+"/payload/wiiu/apps/uFlow/uFlow.wuhb","corrupt");Fails([&]{Prepare(cancelled,bundle,defs,0,{});},"Incomplete");
    {   // PlayStation (bit 13): RetroArch files ship only with this component, are
        // kept when the user already has them, and the generated type never
        // replaces an existing library configuration.
        assert(Allowed("wiiu/apps/retroarch.wuhb")&&Allowed("retroarch/cores/pcsx_rearmed_libretro.rpx")&&
               Allowed("retroarch/cores/info/pcsx_rearmed_libretro.info"));
        assert(!Allowed("retroarch/retroarch.cfg")&&!Allowed("retroarch/cores/system/scph5501.bin")&&
               !Allowed("retroarch/cores/gambatte_libretro.rpx")&&!Allowed("retroarch/saves/crash.srm"));
        assert(ComponentCount==17&&std::string(components[PlayStation].id)=="ps1");
        assert(std::string(components[11].id)=="startup"&&std::string(components[12].id)=="vwii-tools");
        // Own bundle: earlier fixture steps deliberately damage the shared one.
        const std::string psBundle=root+"/ps1-bundle";fs::create_directories(psBundle);
        std::vector<FileDef> ps1Defs;
        auto psAsset=[&](const std::string &path,uint32_t mask,char mode){
            Put(psBundle+"/payload/"+path,"payload "+path);auto text="payload "+path;
            ps1Defs.push_back({path,Hash(text),text.size(),mask,mode});
        };
        psAsset("wiiu/apps/retroarch.wuhb",1u<<PlayStation,'k');
        psAsset("retroarch/cores/pcsx_rearmed_libretro.rpx",1u<<PlayStation,'k');
        psAsset("retroarch/cores/info/pcsx_rearmed_libretro.info",1u<<PlayStation,'k');
        psAsset("wiiu/apps/uFlow/launchers/retroarch-ps1.ufmod",1u<<PlayStation,'r');
        const auto has=[](const Plan &plan,const std::string &path){
            return std::any_of(plan.items.begin(),plan.items.end(),[&](const auto &i){return i.file.path==path;});
        };
        // PS1 alone.
        const std::string psOnly=root+"/ps1-only";Aroma(psOnly);
        auto plan=Prepare(psOnly,psBundle,ps1Defs,1u<<PlayStation,{});
        assert(has(plan,"wiiu/apps/retroarch.wuhb")&&has(plan,"retroarch/cores/pcsx_rearmed_libretro.rpx"));
        assert(has(plan,"wiiu/apps/uFlow/launchers/retroarch-ps1.ufmod"));
        // Not selected: nothing of RetroArch's is touched.
        auto without=Prepare(psOnly,psBundle,ps1Defs,1u<<4,{});
        assert(!has(without,"wiiu/apps/retroarch.wuhb")&&!has(without,"retroarch/cores/pcsx_rearmed_libretro.rpx"));
        std::vector<std::string> psDirs;auto cfg=Configs(1u<<PlayStation,psDirs);
        assert(cfg[0].second=="ps1\tPlayStation\tfiles\twiiflow\t.cue,.chd,.pbp\t1\t13\tps1\t142\t125\t10\t1\n");
        assert(cfg[1].second=="files\t1\t3\tfs:/vol/external01/wiiu/roms/ps1\twiiflow\tps1\tps1\n");
        assert(std::find(psDirs.begin(),psDirs.end(),"wiiu/roms/ps1")!=psDirs.end());
        // PS1 together with existing systems.
        auto both=Configs((1u<<PlayStation)|(1u<<3)|(1u<<4),psDirs);
        assert(both[0].second.find("\nps1\tPlayStation\t")!=std::string::npos);
        assert(both[0].second.rfind("ds\tNintendo DS\t",0)==0&&both[0].second.find("\nnes\tNES\t")!=std::string::npos);
        Install(psOnly,psBundle,plan,"ps1-first",{});
        assert(HashFile(psOnly+"/wiiu/apps/retroarch.wuhb",{},"")==Hash("payload wiiu/apps/retroarch.wuhb"));
        assert(Prepare(psOnly,psBundle,ps1Defs,1u<<PlayStation,{}).bytes==0); // reinstall changes nothing
        // A different RetroArch already on the card and the user's own
        // libraries (their custom PS1 type) both survive a PS1 install.
        const std::string mine=root+"/ps1-existing";Aroma(mine);
        const std::string theirs="someone else's RetroArch";
        const std::string library="custom_1\tPS1\tfiles\twiiflow\t.cue\t1\t10\tps1\t142\t125\t10\t1\n";
        Put(mine+"/wiiu/apps/retroarch.wuhb",theirs);
        Put(mine+"/wiiu/apps/uFlow/libraries.cfg",library);
        auto keep=Prepare(mine,psBundle,ps1Defs,1u<<PlayStation,{});
        assert(keep.preserved>=2);
        Install(mine,psBundle,keep,"ps1-keep",{});
        assert(HashFile(mine+"/wiiu/apps/retroarch.wuhb",{},"")==Hash(theirs));
        assert(HashFile(mine+"/wiiu/apps/uFlow/libraries.cfg",{},"")==Hash(library));
        std::cout<<"PASS: PlayStation component - exact RetroArch paths only, PS1-only and combined configs, reinstall, existing RetroArch and libraries kept\n";
    }
    {   // SNES, NES and GBA through RetroArch (bits 14-16): exact core paths, the
        // shared frontend with any RetroArch system, and RetroArch as the type's
        // default launcher when chosen, with or without the VC injector.
        struct System {int bit,vcBit;const char *id,*core,*module,*line,*source;};
        const System systems[]={
            {SnesRetroArch,5,"snes-retroarch","snes9x","retroarch-snes",
             "snes\tSNES\tfiles\twiiflow\t.sfc,.smc\t1\t5\tsnes-retroarch\t178\t125\t30\t1\n",
             "files\t1\t3\tfs:/vol/external01/wiiu/roms/snes\twiiflow\tsnes\tsnes-retroarch\n"},
            {NesRetroArch,4,"nes-retroarch","nestopia","retroarch-nes",
             "nes\tNES\tfiles\twiiflow\t.nes,.unf,.unif,.zip\t1\t4\tnes-retroarch\t130\t178\t25\t1\n",
             "files\t1\t3\tfs:/vol/external01/wiiu/roms/nes\twiiflow\tnes\tnes-retroarch\n"},
            {GbaRetroArch,6,"gba-retroarch","mgba","retroarch-gba",
             "gba\tGBA\tfiles\twiiflow\t.gba\t1\t6\tgba-retroarch\t127\t127\t25\t1\n",
             "files\t1\t3\tfs:/vol/external01/wiiu/roms/gba\twiiflow\tgba\tgba-retroarch\n"}};
        const uint32_t retroArchBits=(1u<<PlayStation)|(1u<<SnesRetroArch)|(1u<<NesRetroArch)|(1u<<GbaRetroArch);
        const std::string raBundle=root+"/ra-bundle";fs::create_directories(raBundle);
        std::vector<FileDef> raDefs;
        auto raAsset=[&](const std::string &path,uint32_t mask,char mode){
            Put(raBundle+"/payload/"+path,"payload "+path);auto text="payload "+path;
            raDefs.push_back({path,Hash(text),text.size(),mask,mode});
        };
        raAsset("wiiu/apps/retroarch.wuhb",retroArchBits,'k');
        raAsset("retroarch/cores/pcsx_rearmed_libretro.rpx",1u<<PlayStation,'k');
        for(const auto &system:systems) {
            const std::string core=std::string("retroarch/cores/")+system.core+"_libretro.rpx";
            const std::string info=std::string("retroarch/cores/info/")+system.core+"_libretro.info";
            assert(Allowed(core)&&Allowed(info));
            raAsset(core,1u<<system.bit,'k');raAsset(info,1u<<system.bit,'k');
            raAsset(std::string("wiiu/apps/uFlow/launchers/")+system.module+".ufmod",1u<<system.bit,'r');
            assert(std::string(components[system.bit].id)==system.id);
        }
        const auto has=[](const Plan &plan,const std::string &path){
            return std::any_of(plan.items.begin(),plan.items.end(),[&](const auto &i){return i.file.path==path;});
        };
        for(const auto &system:systems) {
            std::vector<std::string> raDirs;
            auto cfg=Configs(1u<<system.bit,raDirs);
            assert(cfg[0].second==system.line&&cfg[1].second==system.source);
            // Chosen together with the VC injector, RetroArch is still the default.
            auto both=Configs((1u<<system.bit)|(1u<<system.vcBit),raDirs);
            assert(both[0].second==system.line&&both[1].second==system.source);
            // The VC injector alone is unchanged.
            auto vc=Configs(1u<<system.vcBit,raDirs);
            assert(vc[0].second.find(std::string("\t")+components[system.vcBit].id+"-vc\t")!=std::string::npos);
            // Only this system's core and module, plus the shared frontend.
            const std::string card=root+"/ra-"+system.id;Aroma(card);
            auto plan=Prepare(card,raBundle,raDefs,1u<<system.bit,{});
            assert(has(plan,"wiiu/apps/retroarch.wuhb")&&has(plan,std::string("retroarch/cores/")+system.core+"_libretro.rpx"));
            assert(has(plan,std::string("wiiu/apps/uFlow/launchers/")+system.module+".ufmod"));
            assert(!has(plan,"retroarch/cores/pcsx_rearmed_libretro.rpx"));
            for(const auto &other:systems)if(&other!=&system)
                assert(!has(plan,std::string("retroarch/cores/")+other.core+"_libretro.rpx"));
            Install(card,raBundle,plan,"ra-first",{});
            assert(Prepare(card,raBundle,raDefs,1u<<system.bit,{}).bytes==0);
        }
        // Every RetroArch system at once shares one frontend entry.
        auto all=Prepare(root+"/ra-snes-retroarch",raBundle,raDefs,retroArchBits,{});
        assert(std::count_if(all.items.begin(),all.items.end(),[](const auto &i){return i.file.path=="wiiu/apps/retroarch.wuhb";})==1);
        std::cout<<"PASS: SNES/NES/GBA RetroArch components - exact cores, shared frontend, RetroArch default launcher with or without VC, reinstall unchanged\n";
    }
    std::cout<<"PASS: SHA-256, selection, DS/NooDS routing, backups, preserved settings/saves, idempotence, path safety, base conflicts, cancellation and races.\n";
    if(argc>=2){
        const uint32_t bits=argc==3?std::stoul(argv[2]):All;
        const auto actual=root+"/actual";Aroma(actual);
        Put(actual+"/wiiu/environments/aroma/modules/PluginBackend.wms","stock plugin backend");
        Put(actual+"/"+regionPlugin,"stock Aroma region plugin");
        Put(actual+"/"+regionConfig,"preserve these region choices");
        Put(actual+"/"+ftpPlugin,"stock FTP");Put(actual+"/"+ftpConfig,"preserve FTP choices");
        auto files=BundledFiles();auto ready=Prepare(actual,argv[1],files,bits,{});
        for(const char *name:{"app_booter.bin","ext_loader.bin","ext_booter.bin"}) {
            const auto path=std::string("apps/wiiflow/bins/")+name;
            assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==path&&f.mask==6u&&f.mode=='r';}));
        }
        for(const auto &p:{wiiuCover,dsCover,wiiCover,gcCover,nesCover,snesCover,gbaCover,n64Cover,pceCover,msxCover})assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==p&&f.mode=='k';}));
        for(const char *path:{"wiiu/environments/aroma/modules/ContentRedirectionModule.wms","wiiu/environments/aroma/modules/PluginBackend.wms","wiiu/environments/aroma/modules/RPXLoadingModule.wms","wiiu/environments/aroma/modules/uFlowRawKernel.wms","wiiu/environments/aroma/modules/setup/00_mocha.rpx"})
            assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==path&&f.mask==Core&&f.mode=='r';}));
        assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==regionPlugin&&f.mask==1017u&&f.mode=='r';}));
        std::cout<<"Real bundle: "<<ready.items.size()<<" files, "<<ready.bytes<<" bytes\n"<<std::flush;
        auto result=Install(actual,argv[1],ready,"real-bundle",{});
        const std::string contentModule="wiiu/environments/aroma/modules/ContentRedirectionModule.wms";
        assert(HashFile(actual+"/"+contentModule,{},"")==HashFile(std::string(argv[1])+"/payload/"+contentModule,{},""));
        for(const char *name:{"app_booter.bin","ext_loader.bin","ext_booter.bin"}) {
            const auto path=std::string("apps/wiiflow/bins/")+name;
            assert(File(actual+"/"+path)==bool(bits&6u));
            if(bits&6u) assert(HashFile(actual+"/"+path,{},"")==HashFile(std::string(argv[1])+"/payload/"+path,{},""));
        }
        assert(!File(actual+"/wiiu/apps/compat_installer.wuhb"));
        assert(!File(actual+"/apps/d2x-cios-installer-vwii/boot.dol"));
        assert(!File(actual+"/apps/wiiflow_channel_installer/boot.dol"));
        // Region Free ships only with Wii U and VC systems (mask 1017).
        if(bits&1017u)assert(HashFile(actual+"/"+regionPlugin,{},"")==HashFile(std::string(argv[1])+"/payload/"+regionPlugin,{},""));
        assert(HashFile(actual+"/"+regionConfig,{},"")==Hash("preserve these region choices"));
        assert(HashFile(actual+"/"+ftpPlugin,{},"")==HashFile(std::string(argv[1])+"/payload/"+ftpPlugin,{},""));
        assert(HashFile(actual+"/"+ftpConfig,{},"")==Hash("preserve FTP choices"));
        assert(Prepare(actual,argv[1],files,bits,{}).bytes==0);
        std::cout<<"PASS: selected actual native payload files installed and verified; rerun unchanged (mask "<<bits<<"). "<<result<<'\n';
    }
}
