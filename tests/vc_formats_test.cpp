#define UFLOW_VC_TEST_IO
#include "../app/source/vc_inject.cpp"
#include "../app/source/launcher_modules.hpp"
#include "../app/source/vc_launch.hpp"
#include "rpx_layout_check.hpp"
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
void Check(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char **argv){try{
    using namespace VCFormat;
    Check(MdfKey("alldata.psb.m").substr(0,8)==Bytes("\x3e\xa2\xcb\x35\xb4\x83\x46\xe9",8),"MDF key vector");
    Check(DecodeMdf(EncodeMdf("roundtrip","ROM.gba"),"ROM.gba")=="roundtrip","MDF roundtrip");
    auto rejects=[](auto fn){try{fn();return false;}catch(...){return true;}};
    Check(rejects([]{Rpx r(Bytes(80,'\0'));}),"reject malformed RPX");
    Check(rejects([]{Package(Bytes(20,'\0'));}),"reject malformed package");
    Check(rejects([]{DecodeMdf("mdf","bad");}),"reject truncated MDF");
    Check(rejects([]{size_t pos=0;PsbArray(Bytes("\x10\xff\xff\xff\xff\x10",6),pos);}),"reject unbounded PSB array");
    for(const auto &system:VCSystems::all){
        LauncherModule module;std::string body="UFLOW-MODULE-1\nid="+std::string(system.id)+"-vc\nname=Test\nversion=1\nsystem="+system.name+"\nextensions="+system.extensions+"\nprotocol=vc-"+system.id+"-v1\n";
        Check(ParseLauncherModule(body,module),"module protocol");Check(!ParseLauncherModule(body+"executable=wiiu/apps/bad.wuhb\n",module),"reject injection executable");
    }
    Check(argc==3,"expected base folder and system");std::string sys=argv[2];
    const std::string sd="fs:/vol/external01/",base=sd+"bases/"+sys;
    fs::create_directories(base);for(const char *part:{"code","content","meta"})fs::copy(fs::path(argv[1])/part,fs::path(base)/part,fs::copy_options::recursive);
    VCInject::Settings s;s.base="bases/"+sys;s.system=sys;
    LauncherModule launchModule;
    Check(ParseLauncherModule(Read("/project/launcher-modules/"+sys+"-vc.ufmod"),launchModule),"real launcher module");
    Check(VCInject::Save(launchModule.id,s),"launcher profile save");
    const auto before=VCInject::Seal(base,{});auto plan=VCInject::Inspect(s);Bytes rom;
    const auto target=base+"/"+plan.target;
    if(sys=="nes"||sys=="snes"){
        CheckRpxLayout(Read(target));
        std::cout<<"PASS original "<<sys<<" RPX loader layout\n";
        Rpx r(Read(target));auto slot=RomSlot(r,sys);rom=r.sections[slot.section].data.substr(slot.offset,slot.length);
        if(sys=="nes")rom[3]=0x1a;
        auto repacked=r.Pack();CheckRpxLayout(repacked);Rpx roundtrip(repacked);Check(roundtrip.sections.size()==r.sections.size(),"RPX count");
        for(size_t i=0;i<r.sections.size();++i)Check(r.sections[i].data==roundtrip.sections[i].data,"RPX section roundtrip");
        auto oversized=rom+Bytes(slot.length,'x');bool rejected=false;try{InjectRpx(repacked,oversized,sys);}catch(...){rejected=true;}Check(rejected,"oversized RPX ROM rejected");
    }else if(sys=="gba"){
        GbaArchive archive(Read(target));auto bin=Read(base+"/content/alldata.bin",128*1024*1024);
        rom=DecodeMdf(bin.substr(archive.offset,archive.length),archive.romName);
    }else if(sys=="n64"){rom=Read(target);if(const char *sample=std::getenv("UFLOW_TEST_N64_ROM"))rom=Read(sample);}
    else {auto pkg=Read(target);auto e=PackageRom(pkg,sys);rom=pkg.substr(e.offset,e.length);}
    const auto ext=sys=="nes"?".nes":sys=="snes"?".sfc":sys=="gba"?".gba":sys=="n64"?".z64":sys=="pce"?".pce":".rom";
    if(sys=="nes")rom=NesRom::Normalize(rom);
    fs::create_directories(sd+"roms");const auto romPath=sd+"roms/test"+ext;Write(romPath,rom);
    Check(Cartridge(romPath,sys)==rom,"cartridge validation");
    if(const char *exportPath=std::getenv("UFLOW_VC_EXPORT")){
        fs::create_directories(std::string(exportPath)+"/"+sys);
        Write(std::string(exportPath)+"/"+sys+"/baseline"+ext,rom);
    }
    GameEntry source;source.platform=Platform::Files;source.absolutePath=romPath;source.name="VC real base test";auto g=source;
    Check(!VCInject::HasSaved(source),"first Play has no injection");
    const auto readsBefore=VCInject::verificationReadBytes;
    auto error=ConfigureVCLaunch(g,&launchModule,launchModule.id);Check(error.empty(),error);
    const auto firstPlayReads=VCInject::verificationReadBytes-readsBefore;
    if(sys=="pce"){
        std::vector<VCInject::File> files;VCInject::Walk(base+"/","",files);uint64_t baseSize=0;for(const auto &file:files)baseSize+=file.size;
        Check(firstPlayReads<baseSize*4,"first injection stays below four full base read passes");
        std::cout<<"PCE I/O: "<<firstPlayReads<<" read bytes vs prior >= "<<baseSize*5<<"; base "<<baseSize<<"\n";
    }
    Check(VCInject::HasSaved(source)&&g.platform==Platform::WiiU,"first Play prepares and records raw launch target");
    Check(VCInject::Seal(base,{})==before,"source base untouched");
    auto injected=Read(g.absolutePath+"/"+plan.target);
    if(sys=="nes"||sys=="snes"){
        CheckRpxLayout(injected);
        Rpx original(Read(target)),out(injected);auto slot=RomSlot(out,sys);
        Check(out.sections[slot.section].data.substr(slot.offset,rom.size())==rom,"RPX injected ROM matches");
        const Section *crc=nullptr;
        for(const auto &s:out.sections)if(s.h[1]==0x80000003)crc=&s;
        Check(crc!=nullptr,"CRC section present");
        for(size_t i=0;i<out.sections.size();++i){const auto &s=out.sections[i],&old=original.sections[i];
            for(int h=0;h<10;++h)if(h!=4&&h!=5)Check(s.h[h]==old.h[h],"RPX section identity/address/links preserved");
            Check(s.data.size()==old.data.size(),"RPX uncompressed memory size preserved");
            if(s.h[1]==8)Check(s.h[5]==old.h[5]&&s.h[4]==0,"NOBITS memory size preserved without file offset");
            const auto expected=&s==crc||s.data.empty()?0:crc32(0,reinterpret_cast<const Bytef*>(s.data.data()),s.data.size());
            Check(Number(crc->data,i*4,4,true)==expected,"RPX stored section CRC matches decompressed bytes");
        }
        for(size_t i=0;i<out.sections.size();++i){if(i==slot.section||out.sections[i].h[1]==0x80000003)continue;Check(out.sections[i].data==original.sections[i].data,"unrelated RPX sections unchanged");}
    }else if(sys=="gba"){
        GbaArchive original(Read(target)),out(injected);auto bin=Read(g.absolutePath+"/content/alldata.bin",128*1024*1024);Check(DecodeMdf(bin.substr(out.offset,out.length),out.romName)==rom,"GBA injected bytes");
        auto originalBin=Read(base+"/content/alldata.bin",128*1024*1024);Check(bin.substr(0,originalBin.size())==originalBin,"original GBA subfiles retained");
        Check(out.names==original.names,"GBA PSB names unchanged");
        for(size_t i=0;i<out.root.children.size();++i){
            if(i!=out.info)Check(PsbWrite(out.root.children[i])==PsbWrite(original.root.children[i]),"GBA unrelated PSB objects unchanged");
            else for(size_t j=0;j<out.root.children[i].children.size();++j)if(j!=out.rom)Check(PsbWrite(out.root.children[i].children[j])==PsbWrite(original.root.children[i].children[j]),"GBA other file records unchanged");
        }
    }else if(sys=="n64"){
        Check(injected==rom,"N64 injected bytes");
        std::vector<std::string> names;for(const auto &file:fs::directory_iterator(base+"/content/config"))names.push_back(file.path().filename().string());
        const auto profile=N64Profile::Select(rom,names,[&](const auto &name){return Read(base+"/content/config/"+name);});
        Check(Read(g.absolutePath+"/content/config/"+fs::path(plan.target).filename().string()+".ini")==profile.text,"N64 uses selected cartridge profile, not donor patches");
        const auto marker=g.absolutePath+"/"+N64Profile::Marker;const auto stamp=Read(marker);
        Check(stamp==std::string(N64Profile::Version)+"\n"+profile.source+"\n","N64 versioned profile stamp");
        fs::remove(marker);Check(!VCInject::HasSaved(source),"legacy N64 injection requires one-time rebuild");Write(marker,stamp);
        std::cout<<"PASS N64 auto profile: "<<profile.source<<"\n";
    }
    else {
        auto e=PackageRom(injected,sys);Check(injected.substr(e.offset,e.length)==rom,"package injected bytes");
        const auto original=Read(target);auto beforeEntries=Package(original),afterEntries=Package(injected);
        Check(beforeEntries.size()==afterEntries.size(),"package entry count preserved");
        for(size_t i=0;i<beforeEntries.size();++i){
            const auto &a=beforeEntries[i],&b=afterEntries[i];Check(a.name==b.name,"package names preserved");
            if(b.offset!=e.offset)Check(original.substr(a.offset,a.length)==injected.substr(b.offset,b.length),"package BIOS/config preserved");
        }
        auto resized=injected;const auto small=rom.substr(0,8192);InjectPackage(resized,small,sys);
        auto smallEntry=PackageRom(resized,sys);Check(smallEntry.length==small.size()&&resized.substr(smallEntry.offset,smallEntry.length)==small,"package resized cartridge");
        auto invalid=injected;Put(invalid,e.field,uint32_t(invalid.size()));
        Check(rejects([&]{PackageRom(invalid,sys);}),"package cartridge range checked");
        if(sys=="msx")Check(e.name=="MEGAROM.ROM"&&beforeEntries.size()>1,"MSX cartridge distinguished from BIOS ROMs");
    }
    Check(VCInject::SavePrepared(source,g).empty()&&VCInject::HasSaved(source),"saved registry");auto saved=source;
    const auto savedLaunchReads=VCInject::verificationReadBytes;
    Check(VCInject::LaunchSaved(saved).empty(),"saved launch readiness check");
    auto playAgain=source;
    Check(ConfigureVCLaunch(playAgain,&launchModule,launchModule.id,[](const std::string &phase,auto,auto){return phase.find("BUILDING ")==std::string::npos&&phase.find("VERIFY")==std::string::npos;}).empty()&&playAgain.absolutePath==g.absolutePath,"subsequent Play uses saved output without verification");
    Check(VCInject::verificationReadBytes==savedLaunchReads,"saved launches never hash/copy content");
    auto invalidModule=launchModule;invalidModule.protocol=sys=="ds"?"vc-pce-v1":"vc-ds-v1";
    auto rejectedGame=source;Check(!ConfigureVCLaunch(rejectedGame,&invalidModule,launchModule.id).empty()&&rejectedGame.absolutePath==source.absolutePath,"wrong system module rejected before saved launch");
    Check(!ConfigureVCLaunch(rejectedGame,nullptr,launchModule.id).empty(),"missing module rejected");
    auto again=source;Check(VCInject::Prepare(again,s).empty()&&again.absolutePath==g.absolutePath,"cache reuse");
    s.reuse=false;again=source;Check(VCInject::Prepare(again,s).empty()&&again.absolutePath!=g.absolutePath&&again.saveRelative==g.saveRelative,"rebuild preserves saves");
    auto changed=rom;changed.back()^=1;Write(romPath,changed);auto changedGame=source;
    Check(VCInject::Prepare(changedGame,s).empty()&&changedGame.saveRelative!=g.saveRelative,"different ROM save namespace");
    Check(VCInject::Seal(base,{})==before,"base preserved after different ROM");Write(romPath,rom);
    auto wrong=s;wrong.system=sys=="nes"?"snes":"nes";auto wrongGame=source;
    Check(!VCInject::Prepare(wrongGame,wrong).empty(),"cross-system injection rejected");
    auto cancelled=source;
    auto cancellation=VCInject::Prepare(cancelled,s,[](const std::string &phase,auto,auto){return phase!="VC CARTRIDGE WRITTEN";});
    Check(cancellation.find("CANCELLED")!=std::string::npos&&cancelled.absolutePath==source.absolutePath,"cancel after transform keeps game unchanged");
    Check(VCInject::Seal(base,{})==before,"base unchanged after cancellation");
    // Missing files in a recorded output must rebuild privately on Play and
    // retain the same save namespace. Cancellation must not publish a target.
    fs::remove(g.absolutePath+"/code/"+fs::path(plan.rpx).filename().string());
    Check(!VCInject::HasSaved(source),"missing injected RPX detected");
    auto missingExecutable=source;
    Check(!VCInject::LaunchSaved(missingExecutable).empty()&&missingExecutable.absolutePath==source.absolutePath,"quick launch rejects missing executable");
    auto autoCancelled=source;
    auto autoError=ConfigureVCLaunch(autoCancelled,&launchModule,launchModule.id,[](const std::string &phase,auto,auto){return phase!="VC CARTRIDGE WRITTEN";});
    Check(autoError.find("CANCELLED")!=std::string::npos&&autoCancelled.absolutePath==source.absolutePath&&!VCInject::HasSaved(source),"automatic injection cancellation keeps source and record unchanged");
    auto repaired=source;
    Check(ConfigureVCLaunch(repaired,&launchModule,launchModule.id).empty()&&VCInject::HasSaved(source)&&repaired.saveRelative==g.saveRelative&&repaired.absolutePath!=g.absolutePath,"Play recreates missing output and preserves saves");
    if(sys=="n64"){
        Write(romPath+".ini","[RomOption]\nRetraceByVsync = 1\n");auto custom=source;
        Check(VCInject::Prepare(custom,s).empty(),"N64 sidecar injection");
        Check(Read(custom.absolutePath+"/content/config/"+fs::path(plan.target).filename().string()+".ini")==Read(romPath+".ini"),"N64 INI uses emulator ROM filename");
    }
    if(sys=="n64")for(int order:{2,4}){auto swap=rom;for(size_t i=0;i<swap.size();i+=order)std::reverse(swap.begin()+i,swap.begin()+i+order);Write(romPath,swap);Check(Cartridge(romPath,sys)==rom,"N64 byteorder normalization");} 
    std::cout<<"PASS real "<<sys<<": "<<rom.size()<<" ROM bytes, base preservation, injected bytes, cache, saved launch, rebuild/save isolation\n";
    return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}catch(const VCInject::Failure &e){std::cerr<<"FAIL: "<<e.text<<"\n";return 1;}}
