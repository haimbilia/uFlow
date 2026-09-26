#pragma once
#include "saved_file.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace BuiltinIconCache {
inline bool Directory(const std::string &path) {
    struct stat s{};
    if(!stat(path.c_str(),&s)) return S_ISDIR(s.st_mode);
    const auto slash=path.find_last_of('/');
    if(slash!=std::string::npos && !Directory(path.substr(0,slash))) return false;
    return mkdir(path.c_str(),0777)==0;
}
inline bool Valid(const std::string &path,std::uint64_t size) {
    struct stat s{};
    if(stat(path.c_str(),&s) || !S_ISREG(s.st_mode) || std::uint64_t(s.st_size)!=size || size<18 || size>4*1024*1024) return false;
    std::ifstream f(path,std::ios::binary);unsigned char h[18]{};
    if(!f.read(reinterpret_cast<char*>(h),18) || h[1]!=0 || h[2]!=2 || (h[16]!=24 && h[16]!=32)) return false;
    const unsigned w=h[12]+(h[13]<<8),height=h[14]+(h[15]<<8);
    return w && height && w<=512 && height<=512 && 18u+h[0]+std::uint64_t(w)*height*(h[16]/8)<=size;
}
// Called during discovery only. Never consult the HDD while rendering a cached
// icon. Shard the SD cache so large libraries do not create a single huge FAT
// directory. Identity includes the source path and revision, not just title ID.
inline std::string Store(const std::string &source,const std::string &root) {
    struct stat s{};
    if(stat(source.c_str(),&s) || !S_ISREG(s.st_mode) || s.st_size<18 || s.st_size>4*1024*1024) return {};
    const std::string identity=source+"/"+std::to_string(s.st_size)+"/"+std::to_string(s.st_mtime);
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:identity){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream key;key<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    const auto dir=root+"/cache/icons/"+key.str().substr(0,2);
    const auto destination=dir+"/"+key.str()+".tga";
    if(Valid(destination,s.st_size)) return destination;
    if(!Valid(source,s.st_size) || !Directory(dir)) return {};
    std::ifstream input(source,std::ios::binary);
    std::vector<char> bytes(static_cast<std::size_t>(s.st_size));
    if(!input.read(bytes.data(),bytes.size())) return {};
    const auto temporary=destination+".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(bytes.data(),bytes.size());output.flush();bool ok=bool(output);output.close();
    if(!ok || output.fail() || !Valid(temporary,s.st_size) || !SavedFile::Commit(temporary,destination)) {
        std::remove(temporary.c_str());return {};
    }
    return destination;
}
}
