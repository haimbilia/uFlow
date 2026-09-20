#pragma once
#include <string>
namespace BrowserPath {
inline std::string Normalize(std::string path){
    if(path.empty()||path=="fs:")return "fs:/";
    while(path.size()>4&&path.back()=='/')path.pop_back();
    return path;
}
inline std::string Parent(std::string path){
    path=Normalize(path);
    if(path=="fs:/")return path;
    const auto slash=path.rfind('/');
    return slash==std::string::npos||slash<=3?"fs:/":path.substr(0,slash);
}
inline std::string Child(const std::string &path,const std::string &name){
    const auto base=Normalize(path);return base+(base.back()=='/'?"":"/")+name;
}
inline bool Container(const std::string &path){const auto p=Normalize(path);return p=="fs:/"||p=="fs:/vol";}
}
