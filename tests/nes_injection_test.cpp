#define UFLOW_VC_TEST_IO
#include "../app/source/vc_inject.cpp"
#include "vc_launch.hpp"
#include "rpx_layout_check.hpp"
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
void Check(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    const std::string sd="fs:/vol/external01/",base=sd+"bases/nes";
    fs::create_directories(base);for(const char *part:{"code","content","meta"})fs::copy(fs::path("/base")/part,fs::path(base)/part,fs::copy_options::recursive);
    auto seal=VCInject::Seal(base,{});VCInject::Settings settings;settings.system="nes";settings.base="bases/nes";
    Check(VCInject::Save("nes-vc",settings),"save test settings");LauncherModule module;
    Check(ParseLauncherModule(VCFormat::Read("/project/launcher-modules/nes-vc.ufmod"),module),"NES manifest");
    fs::create_directories(sd+"roms");
    for(const char *name:{"Adventures of Lolo (USA).nes","Adventures of Lolo 2 (USA).nes","Adventures of Lolo 3 (USA).zip"}){
        const auto path=sd+"roms/"+name;fs::copy_file(fs::path("/roms")/name,path);
        const auto original=VCFormat::Read(path),normalized=VCFormat::Cartridge(path,"nes");
        GameEntry source;source.platform=Platform::Files;source.absolutePath=path;source.name=name;auto target=source;
        const auto error=ConfigureVCLaunch(target,&module,"nes-vc");Check(error.empty(),error);
        Check(VCFormat::Read(path)==original,"source unchanged during injection");
        const auto plan=VCInject::Inspect(settings);const auto packed=VCFormat::Read(target.absolutePath+"/"+plan.target);CheckRpxLayout(packed);
        VCFormat::Rpx r(packed);auto slot=VCFormat::RomSlot(r,"nes");
        Check(r.sections[slot.section].data.substr(slot.offset,normalized.size())==normalized,"injected normalized bytes");
        // Delete ONLY the disposable test copy, never /roms.
        fs::remove(path);const auto reads=VCInject::verificationReadBytes;auto again=source;
        Check(ConfigureVCLaunch(again,&module,"nes-vc").empty()&&again.absolutePath==target.absolutePath,"saved launch without source");
        Check(VCInject::verificationReadBytes==reads,"no content hash on saved launch");
        std::cout<<"PASS full NES injection + source deletion + quick relaunch: "<<name<<"\n";
    }
    Check(VCInject::Seal(base,{})==seal,"base unchanged");return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}catch(const VCInject::Failure &e){std::cerr<<"FAIL: "<<e.text<<"\n";return 1;}}
