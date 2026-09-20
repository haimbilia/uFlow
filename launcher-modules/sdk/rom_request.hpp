#pragma once
// uFlow ROM handoff protocol 1. C++11, shared by the frontend and emulator.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
namespace UFlowRom {
inline bool RelativePath(const std::string &p) {
    if(p.empty()||p.size()>1024||p.front()=='/'||p.back()=='/'||p.find_first_of(":\\\r\n\t")!=std::string::npos||p.find('\0')!=std::string::npos)return false;
    std::istringstream s(p);std::string part;
    while(std::getline(s,part,'/'))if(part.empty()||part=="."||part=="..")return false;
    return true;
}
inline std::string RelativeROM(std::string p) {
    for(const char *prefix:{"fs:/vol/external01/","sd:/","/vol/external01/"})
        if(p.find(prefix)==0){p.erase(0,std::char_traits<char>::length(prefix));return RelativePath(p)?p:std::string();}
    return {};
}
inline bool File(const std::string &p) {struct stat s{};return stat(p.c_str(),&s)==0&&S_ISREG(s.st_mode);}
inline std::string Serialize(const std::string &rom,std::time_t now) {
    return RelativePath(rom)&&now>0?"UFLOW-ROM-1\n"+std::to_string(static_cast<long long>(now))+"\n"+rom+"\n":std::string();
}
inline bool Parse(const std::string &text,std::time_t now,std::string &rom) {
    rom.clear();if(text.size()>1200)return false;
    std::istringstream s(text);std::string magic,stamp,path,extra;
    if(!std::getline(s,magic)||!std::getline(s,stamp)||!std::getline(s,path)||std::getline(s,extra))return false;
    if(magic!="UFLOW-ROM-1"||stamp.empty()||stamp.size()>12||!std::all_of(stamp.begin(),stamp.end(),[](unsigned char c){return std::isdigit(c);})||!RelativePath(path))return false;
    long long timestamp=0;std::istringstream number(stamp);number>>timestamp;
    if(!number||timestamp<=0||now<timestamp||static_cast<long long>(now)-timestamp>120)return false;
    rom=path;return true;
}
inline bool Clear(const std::string &file) {return std::remove(file.c_str())==0||errno==ENOENT;}
inline bool Write(const std::string &file,const std::string &rom) {
    const auto text=Serialize(rom,std::time(nullptr));if(text.empty())return false;
    const auto tmp=file+".tmp";std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
    if(!out)return false;
    out<<text;out.flush();bool ok=bool(out);out.close();ok=ok&&!out.fail();
    if(!ok){Clear(tmp);return false;}
    if(!Clear(file)||std::rename(tmp.c_str(),file.c_str())!=0){Clear(tmp);return false;}return true;
}
inline bool Consume(const std::string &file,const std::string &root,std::string &rom) {
    rom.clear();std::ifstream in(file,std::ios::binary);if(!in)return false;
    char data[1201];in.read(data,sizeof(data));const auto count=in.gcount();in.close();
    // Consume BEFORE starting the core. A crash or manual relaunch cannot replay it.
    if(!Clear(file)||count>=1201)return false;
    std::string relative;if(!Parse(std::string(data,static_cast<size_t>(count)),std::time(nullptr),relative))return false;
    const auto path=root+"/"+relative;if(!File(path))return false;rom=path;return true;
}
}
