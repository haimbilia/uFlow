// Explicit host-side repair of ONE published NES/SNES/N64 package, using the same
// staging/publishing path as the app. Mount the card as fs:/vol/external01.
// No source ROM, base, save, or previous generation is modified/deleted.
#include "../app/source/vc_inject.cpp"
#include "../tests/rpx_layout_check.hpp"
#include <iostream>
int main(int argc,char **argv){try{
    if(argc!=3||std::string(argv[1])!="--publish"){
        std::cerr<<"Usage: rebuild-vc-package --publish fs:/vol/external01/path/game.nes.uflow\n";return 2;
    }
    GameEntry source,previous;
    VCInject::Require(VCPackage::Load(argv[2],source,previous),"EXISTING PACKAGE INVALID - NO CHANGES");
    const auto system=VCSystems::File(source.absolutePath);
    VCInject::Require(system=="nes"||system=="snes"||system=="n64","ONLY NES/SNES/N64 REPAIR IS SUPPORTED");
    VCInject::Require(UFlowRom::File(source.absolutePath),"ORIGINAL ROM REQUIRED FOR THIS HOST REBUILD");
    VCInject::Require(VCInject::Seal(previous.absolutePath,{})==VCFormat::Read(previous.absolutePath+"/uflow.sha256"),"EXISTING PACKAGE MANIFEST MISMATCH");
    const auto sourceHash=VCInject::HashFile(source.absolutePath,{});
    auto settings=VCInject::Load(system+"-vc");settings.system=system;settings.reuse=false;
    auto target=source;auto error=VCInject::Prepare(target,settings);
    VCInject::Require(error.empty(),error);
    VCInject::Require(target.saveRelative==previous.saveRelative,"SAVE IDENTITY CHANGED - NOT PUBLISHING");
    if(system!="n64")CheckRpxLayout(VCFormat::Read(VCPackage::SD+target.rpxRelative));
    else{
        const auto plan=VCInject::Inspect(settings);
        VCInject::Require(VCFormat::Read(target.absolutePath+"/"+plan.target)==VCFormat::Cartridge(source.absolutePath,system),"N64 ROM READBACK FAILED");
        VCInject::Require(VCFormat::Read(target.absolutePath+"/"+N64Profile::Marker).rfind(N64Profile::Version,0)==0,"N64 PROFILE MARKER MISSING");
    }
    VCInject::Require(VCInject::HashFile(source.absolutePath,{})==sourceHash,"SOURCE CHANGED - NOT PUBLISHING");
    error=VCInject::SavePrepared(source,target);VCInject::Require(error.empty(),error);
    GameEntry reread,active;
    VCInject::Require(VCPackage::Load(argv[2],reread,active)&&active.absolutePath==target.absolutePath&&active.saveRelative==previous.saveRelative,"PUBLISHED RECORD READBACK FAILED");
    VCInject::Require(VCInject::Seal(active.absolutePath,{})==VCFormat::Read(active.absolutePath+"/uflow.sha256"),"PUBLISHED CONTENT READBACK FAILED");
    std::cout<<"Published: "<<active.absolutePath<<"\nROM and save identity unchanged. "<<(system=="n64"?"ROM/profile":"RPX layout")<<" and manifest readback PASS.\n";
    return 0;
}catch(const VCInject::Failure &e){std::cerr<<e.text<<'\n';return 1;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
