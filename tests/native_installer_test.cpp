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
    assert(Hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(Hash(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    char temp[]="/tmp/uflow-native-setup-XXXXXX";const std::string root=mkdtemp(temp),sd=root+"/sd",bundle=root+"/bundle";
    Aroma(sd);fs::create_directories(bundle);std::vector<FileDef> defs;
    auto asset=[&](const std::string &p,uint32_t mask,char mode){Put(bundle+"/payload/"+p,"payload "+p);auto text="payload "+p;defs.push_back({p,Hash(text),text.size(),mask,mode});};
    asset("wiiu/apps/uFlow/uFlow.wuhb",Core,'r');
    asset("wiiu/environments/aroma/modules/RPXLoadingModule.wms",1u|(1u<<4),'r');
    const std::string regionPlugin="wiiu/environments/aroma/plugins/regionfree.wps";
    const std::string regionConfig="wiiu/environments/aroma/plugins/config/region_free_plugin.json";
    asset(regionPlugin,1017u,'r');
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
    asset(gbaCover,1u<<6,'k');
    asset(n64Cover,1u<<7,'k');
    asset(snesCover,1u<<5,'k');
    asset(nesCover,1u<<4,'k');
    asset(msxCover,1u<<9,'k');
    asset(pceCover,1u<<8,'k');
    asset("wiiu/vc-bases/nes/default/content/rom.bin",1u<<4,'b');
    const std::string existing=sd+"/wiiu/environments/aroma/modules/RPXLoadingModule.wms";
    Put(existing,"original");Put(sd+"/games/keep.sav","save");Put(sd+"/wiiu/apps/uFlow/libraries.cfg","custom");
    Put(sd+"/"+regionPlugin,"stock region plugin");Put(sd+"/"+regionConfig,"saved user region choices");
    auto full=Prepare(sd,bundle,defs,All,{});assert(full.preserved==1);assert(!fs::exists(sd+"/.uflow-setup"));
    auto report=Install(sd,bundle,full,"test",{});assert(File(report));
    assert(HashFile(sd+"/games/keep.sav",{},"")==Hash("save"));
    assert(HashFile(sd+"/wiiu/apps/uFlow/libraries.cfg",{},"")==Hash("custom"));
    assert(HashFile(sd+"/"+regionPlugin,{},"")==Hash("payload "+regionPlugin));
    assert(HashFile(sd+"/"+regionConfig,{},"")==Hash("saved user region choices"));
    assert(!Allowed(regionConfig)&&!Allowed("wiiu/environments/aroma/plugins/unrelated.wps"));
    assert(HashFile(sd+"/.uflow-setup/test/backup/wiiu/environments/aroma/modules/RPXLoadingModule.wms",{},"")==Hash("original"));
    assert(Prepare(sd,bundle,defs,All,{}).bytes==0);
    Put(sd+"/"+dsCover,"user custom sleeve");
    auto preserved=Prepare(sd,bundle,defs,All,{});assert(preserved.bytes==0&&preserved.preserved==2);
    assert(HashFile(sd+"/"+dsCover,{},"")==Hash("user custom sleeve"));
    auto fresh=root+"/fresh";Aroma(fresh);
    auto minimal=Prepare(fresh,bundle,defs,0,{});assert(minimal.items.size()==5&&minimal.dirs.empty());
    Install(fresh,bundle,minimal,"minimal",{});assert(Size(fresh+"/wiiu/apps/uFlow/libraries.cfg")==0);
    assert(!File(fresh+"/apps/nintendont/boot.dol"));
    assert(!File(fresh+"/"+regionPlugin));
    for(int bit=0;bit<13;++bit){
        const auto selected=Prepare(fresh,bundle,defs,1u<<bit,{});
        const bool hasPlugin=std::any_of(selected.items.begin(),selected.items.end(),[&](const auto &i){return i.file.path==regionPlugin;});
        assert(hasPlugin==bool(1017u&(1u<<bit)));
        auto contains=[&](const std::string &path){return std::any_of(selected.items.begin(),selected.items.end(),[&](const auto &i){return i.file.path==path;});};
        assert(contains(dsCover)==(bit==3||bit==10));assert(contains(wiiCover)==(bit==1));assert(contains(gcCover)==(bit==2));
        assert(contains(wiiuCover)==(bit==0));
        assert(contains(gbaCover)==(bit==6));
        assert(contains(n64Cover)==(bit==7));
        assert(contains(snesCover)==(bit==5));
        assert(contains(nesCover)==(bit==4));
        assert(contains(msxCover)==(bit==9));
        assert(contains(pceCover)==(bit==8));
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
    std::cout<<"PASS: SHA-256, selection, DS/NooDS routing, backups, preserved settings/saves, idempotence, path safety, base conflicts, cancellation and races.\n";
    if(argc>=2){
        const uint32_t bits=argc==3?std::stoul(argv[2]):All;
        const auto actual=root+"/actual";Aroma(actual);
        Put(actual+"/"+regionPlugin,"stock Aroma region plugin");
        Put(actual+"/"+regionConfig,"preserve these region choices");
        auto files=BundledFiles();auto ready=Prepare(actual,argv[1],files,bits,{});
        for(const char *name:{"app_booter.bin","ext_loader.bin","ext_booter.bin"}) {
            const auto path=std::string("apps/wiiflow/bins/")+name;
            assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==path&&f.mask==6u&&f.mode=='r';}));
        }
        for(const auto &p:{wiiuCover,dsCover,wiiCover,gcCover,nesCover,snesCover,gbaCover,n64Cover,pceCover,msxCover})assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==p&&f.mode=='k';}));
        for(const char *path:{"wiiu/environments/aroma/modules/RPXLoadingModule.wms","wiiu/environments/aroma/modules/uFlowRawKernel.wms","wiiu/environments/aroma/modules/setup/00_mocha.rpx"})
            assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==path&&f.mask==Core&&f.mode=='r';}));
        assert(std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==regionPlugin&&f.mask==1017u&&f.mode=='r';}));
        std::cout<<"Real bundle: "<<ready.items.size()<<" files, "<<ready.bytes<<" bytes\n"<<std::flush;
        auto result=Install(actual,argv[1],ready,"real-bundle",{});
        for(const char *name:{"app_booter.bin","ext_loader.bin","ext_booter.bin"}) {
            const auto path=std::string("apps/wiiflow/bins/")+name;
            assert(File(actual+"/"+path)==bool(bits&6u));
            if(bits&6u) assert(HashFile(actual+"/"+path,{},"")==HashFile(std::string(argv[1])+"/payload/"+path,{},""));
        }
        assert(!File(actual+"/wiiu/apps/compat_installer.wuhb"));
        assert(!File(actual+"/apps/d2x-cios-installer-vwii/boot.dol"));
        assert(!File(actual+"/apps/wiiflow_channel_installer/boot.dol"));
        assert(HashFile(actual+"/"+regionPlugin,{},"")==HashFile(std::string(argv[1])+"/payload/"+regionPlugin,{},""));
        assert(HashFile(actual+"/"+regionConfig,{},"")==Hash("preserve these region choices"));
        assert(Prepare(actual,argv[1],files,bits,{}).bytes==0);
        std::cout<<"PASS: selected actual native payload files installed and verified; rerun unchanged (mask "<<bits<<"). "<<result<<'\n';
    }
}
