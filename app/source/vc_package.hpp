#pragma once
#include "library.hpp"
#include "raw_launch.hpp"
#include "vc_systems.hpp"
#include "saved_file.hpp"
#include "../../launcher-modules/sdk/rom_request.hpp"
#include <dirent.h>
#include <iomanip>
#include <sstream>

// A package retains the ROM's logical identity even when that file is absent.
// The small active record points at one immutable generation inside the package.
namespace VCPackage {
inline const std::string SD="fs:/vol/external01/";
inline std::string Path(const GameEntry &rom){return rom.absolutePath+".uflow";}
inline bool Directory(const std::string &path){struct stat s{};return lstat(path.c_str(),&s)==0&&S_ISDIR(s.st_mode);}
inline bool IsPackageName(const std::string &path){return path.size()>6&&path.substr(path.size()-6)==".uflow";}
inline std::string Read(const std::string &path){
    auto in=SavedFile::Open(path,std::ios::binary);std::string bytes(16385,'\0');
    in.read(bytes.data(),bytes.size());bytes.resize(in.gcount());
    return bytes.size()>16384?std::string():bytes;
}
inline bool SafeText(const std::string &s){return s.size()<4096&&s.find_first_of("\r\n\0",0,3)==std::string::npos;}
inline bool TargetValid(GameEntry &target,const std::string &system){
    const auto relative=UFlowRom::RelativeROM(target.absolutePath);
    if(!UFlowRom::RelativePath(relative)||!UFlowRom::RelativePath(target.rpxRelative)||
       target.rpxRelative.rfind(relative+"/code/",0)!=0||
       !UFlowRom::RelativePath(target.saveRelative)||target.saveRelative.rfind("wiiu/vc-saves/"+system+"/",0)!=0)return false;
    target.platform=Platform::WiiU;target.installed=false;target.installedTitleId=0;
    target.codeRelative=relative+"/code";target.contentRelative=relative+"/content";
    const auto marker=Read(target.absolutePath+"/uflow.complete");
    return marker.size()==64&&marker.find_first_not_of("0123456789abcdef")==std::string::npos&&ValidateRawGame(target).empty();
}
inline bool Load(const std::string &folder,GameEntry &source,GameEntry &target){
    if(!IsPackageName(folder)||!Directory(folder))return false;
    std::istringstream in(Read(folder+"/game.cfg"));std::string version,filename,generation,rpx;
    std::getline(in,version);if(version!="UFLOW-ROM-PACKAGE-1")return false;
    GameEntry rom,prepared;rom.platform=Platform::Files;
    if(!(in>>std::quoted(filename)>>std::quoted(rom.name)>>std::quoted(rom.folder)>>std::quoted(rom.publisher)>>std::quoted(rom.titleId)
         >>std::quoted(generation)>>std::quoted(rpx)>>std::quoted(prepared.saveRelative)>>std::quoted(prepared.titleId)))return false;
    for(const auto *s:{&filename,&rom.name,&rom.folder,&rom.publisher,&rom.titleId,&generation,&rpx,&prepared.saveRelative,&prepared.titleId})if(!SafeText(*s))return false;
    if(filename.find_first_of("/\\:")!=std::string::npos||!UFlowRom::RelativePath(filename)||
       generation.rfind("game-",0)!=0||generation.find_first_of("/\\:")!=std::string::npos||!UFlowRom::RelativePath(generation)||
       !UFlowRom::RelativePath(rpx)||rpx.rfind("code/",0)!=0)return false;
    rom.absolutePath=folder.substr(0,folder.find_last_of('/')+1)+filename;
    const auto system=VCSystems::File(rom.absolutePath);
    if(system.empty()||Path(rom)!=folder||UFlowRom::RelativeROM(rom.absolutePath).empty())return false;
    if(rom.name.empty())rom.name=filename.substr(0,filename.find_last_of('.'));
    if(rom.folder.empty())rom.folder=filename;
    prepared.absolutePath=folder+"/"+generation;
    if(!Directory(prepared.absolutePath))return false;
    prepared.rpxRelative=UFlowRom::RelativeROM(prepared.absolutePath)+"/"+rpx;
    if(!TargetValid(prepared,system))return false;
    prepared.name=rom.name;prepared.publisher=rom.publisher;
    source=std::move(rom);target=std::move(prepared);return true;
}
inline std::string Record(const GameEntry &rom,const GameEntry &prepared){
    const auto folder=Path(rom),prefix=folder+"/";
    if(prepared.absolutePath.rfind(prefix,0)!=0)return {};
    const auto generation=prepared.absolutePath.substr(prefix.size());
    const auto relative=UFlowRom::RelativeROM(prepared.absolutePath)+"/";
    if(prepared.rpxRelative.rfind(relative,0)!=0)return {};
    std::ostringstream out;out<<"UFLOW-ROM-PACKAGE-1\n";
    for(const auto &s:{rom.absolutePath.substr(rom.absolutePath.find_last_of('/')+1),rom.name,rom.folder,rom.publisher,rom.titleId,
                      generation,prepared.rpxRelative.substr(relative.size()),prepared.saveRelative,prepared.titleId}){
        if(!SafeText(s))return {};
        out<<std::quoted(s)<<'\n';
    }
    const auto body=out.str();return body.size()<=16384?body:std::string();
}
// Read-only compatibility for existing cache-based records; no original ROM needed.
inline bool LoadLegacy(const std::string &record,GameEntry &source,GameEntry &target){
    std::istringstream in(record);std::string version;GameEntry rom,prepared;
    std::getline(in,version);std::getline(in,rom.absolutePath);
    std::getline(in,prepared.absolutePath);std::getline(in,prepared.rpxRelative);
    std::getline(in,prepared.saveRelative);std::getline(in,prepared.titleId);
    const auto system=VCSystems::File(rom.absolutePath);
    if(system.empty()||UFlowRom::RelativeROM(rom.absolutePath).empty()||
       version!=(system=="ds"?"UFLOW-SAVED-DS-1":"UFLOW-SAVED-VC-1")||
       prepared.absolutePath.rfind(SD+"wiiu/apps/uFlow/vc-cache/"+system+"/",0)!=0||!TargetValid(prepared,system))return false;
    rom.platform=Platform::Files;rom.folder=rom.absolutePath.substr(rom.absolutePath.find_last_of('/')+1);
    rom.name=rom.folder.substr(0,rom.folder.find_last_of('.'));
    source=std::move(rom);target=std::move(prepared);return true;
}
inline std::vector<GameEntry> LegacySources(){
    std::vector<GameEntry> result;
    for(const auto &system:VCSystems::all){
        const auto path=SD+"wiiu/apps/uFlow/injections/"+system.id+"/";
        DIR *dir=opendir(path.c_str());if(!dir)continue;
        std::vector<std::string> names;while(auto *e=readdir(dir)){
            const std::string n=e->d_name;
            if(n.size()==68&&n.substr(64)==".cfg")names.push_back(n);
            else if(n.size()==72&&n.substr(64)==".cfg.bak")names.push_back(n.substr(0,68));
        }closedir(dir);
        for(const auto &name:names){GameEntry source,target;if(LoadLegacy(Read(path+name),source,target))result.push_back(std::move(source));}
    }
    return result;
}
}
