#pragma once
#include <string>
#include <vector>
#include <algorithm>
namespace BrowserPath {
inline std::string Normalize(std::string path){
    if(path.empty()||path=="fs:")return "fs:/";
    const auto colon=path.find(':');const auto root=colon==std::string::npos?size_t(1):colon+2;
    while(path.size()>root&&path.back()=='/')path.pop_back();
    return path;
}
inline std::string Parent(std::string path){
    path=Normalize(path);
    if(path=="fs:/")return path;
    if(path=="usb1:/")return "fs:/";
    const auto slash=path.rfind('/');
    if(path.rfind("usb1:/",0)==0&&slash==5)return "usb1:/";
    return slash==std::string::npos||slash<=3?"fs:/":path.substr(0,slash);
}
inline std::string Child(const std::string &path,const std::string &name){
    const auto base=Normalize(path);return base+(base.back()=='/'?"":"/")+name;
}
inline bool Container(const std::string &path){const auto p=Normalize(path);return p=="fs:/"||p=="fs:/vol";}
// CafeOS namespace parents need not support readdir/stat even when a mounted
// child is accessible. Keep the virtual parent navigable, probe real volumes.
template<class CanOpen> inline void AddNamespaceEntries(const std::string &path,std::vector<std::string> &names,CanOpen canOpen){
    const auto p=Normalize(path);
    const auto add=[&](const std::string &name){if(std::find(names.begin(),names.end(),name)==names.end())names.push_back(name);};
    if(p=="fs:/"){add("vol");if(canOpen("usb1:/"))add("usb1:/");}
    if(p=="fs:/vol")for(const char *name:{"external01","external02","storage_usb01","storage_usb02"})
        if(canOpen(Child(p,name)))add(name);
}
}
