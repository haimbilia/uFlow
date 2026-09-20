#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include "rom_archive.hpp"
#include "saved_file.hpp"
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
namespace VCSystems {
struct Definition {const char *id,*name,*extensions;};
inline constexpr Definition all[]={
    {"ds","Nintendo DS",".nds,.srl"}, {"nes","NES",".nes,.unf,.unif,.zip"},
    {"snes","SNES",".sfc,.smc"}, {"gba","Game Boy Advance",".gba"},
    {"n64","Nintendo 64",".z64,.v64,.n64"},
    {"pce","TurboGrafx-16 / PC Engine",".pce"}, {"msx","MSX",".rom,.mx1,.mx2"}
};
inline const Definition *Find(const std::string &id){for(const auto &s:all)if(id==s.id)return &s;return nullptr;}
inline std::string Protocol(const std::string &p){for(const auto &s:all)if(p==std::string("vc-")+s.id+"-v1")return s.id;return {};}
inline std::string ZipSystem(const std::string &path){
    // A package remains launchable after deleting its original ZIP. Only infer
    // type here; VCPackage still validates the full record and executable.
    struct stat st{};
    if(stat(path.c_str(),&st)!=0){
        auto file=SavedFile::Open(path+".uflow/game.cfg");std::string bytes(16385,'\0');
        file.read(bytes.data(),bytes.size());bytes.resize(file.gcount());if(bytes.size()>16384)return {};
        std::istringstream f(bytes);std::string version,fields[9];
        std::getline(f,version);if(version!="UFLOW-ROM-PACKAGE-1")return {};
        for(auto &s:fields)if(!(f>>std::quoted(s))||s.size()>4096)return {};
        if(fields[0]!=path.substr(path.find_last_of("/\\")+1)||fields[7].rfind("wiiu/vc-saves/nes/",0)!=0)return {};
        return "nes";
    }
    struct Cached {int64_t size,modified;std::string system;};
    static std::map<std::string,Cached> cache;static std::mutex mutex;
    std::lock_guard lock(mutex);const auto it=cache.find(path);
    if(it!=cache.end()&&it->second.size==st.st_size&&it->second.modified==st.st_mtime)return it->second.system;
    std::string system;try{RomArchive::Inspect(path);system="nes";}catch(const std::exception &){}
    if(cache.size()>=256)cache.clear();
    cache[path]={st.st_size,st.st_mtime,system};return system;
}
inline std::string File(std::string path){const auto original=path;auto p=path.find_last_of('.');if(p==std::string::npos)return {};path=path.substr(p);
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return std::tolower(c);});
    if(path==".zip")return ZipSystem(original);
    for(const auto &s:all){const std::string list=std::string(",")+s.extensions+",";if(list.find(","+path+",")!=std::string::npos)return s.id;}return {};
}
}
