#pragma once
#include "launchers.hpp"
#include "saved_file.hpp"
#include <map>

using LauncherOverrides = std::map<std::string,std::string>;
inline constexpr const char *LauncherOverridesPath="fs:/vol/external01/wiiu/apps/uFlow/game_launchers.cfg";
// Some console filesystem drivers cannot rename over an existing file.
// Keep the previous version recoverable until the new file has its final name.
inline bool CommitLauncherOverrides(const std::string &temp,const std::string &target,
    int (*renameFile)(const char *,const char *)=std::rename) {
    return SavedFile::Commit(temp,target,renameFile);
}
inline std::string LauncherGameKey(const GameEntry &g) {
    return g.installed ? "title:"+g.titleId : g.absolutePath;
}
inline LauncherOverrides LoadLauncherOverrides() {
    LauncherOverrides out; std::ifstream f(LauncherOverridesPath); std::string line;
    if(!f.is_open()) { f.clear();f.open(std::string(LauncherOverridesPath)+".bak"); }
    while(std::getline(f,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto sep=line.find('\t'); if(sep==std::string::npos)continue;
        const auto key=line.substr(0,sep), id=line.substr(sep+1);
        // Retired mapping-only adapters are not executable launcher choices.
        static const std::set<std::string> retired={"vc_nes","vc_snes","vc_n64","vc_gba","vc_ds","vc_tg16","vc_msx","vc_wii","vc_gc"};
        if(retired.count(id))continue;
        if(!key.empty()&&!id.empty()&&LauncherFieldValid(key)&&LauncherFieldValid(id))out[key]=id;
    }
    return out;
}
inline bool SaveLauncherOverrides(const LauncherOverrides &values) {
    const std::string temp=std::string(LauncherOverridesPath)+".tmp";
    std::ofstream f(temp,std::ios::trunc); if(!f)return false;
    for(const auto &[key,id]:values) {
        if(key.empty()||id.empty()||!LauncherFieldValid(key)||!LauncherFieldValid(id))return false;
        f<<key<<'\t'<<id<<'\n';
    }
    f.flush();bool ok=static_cast<bool>(f);f.close();
    return ok&&!f.fail()&&CommitLauncherOverrides(temp,LauncherOverridesPath);
}
inline std::string EffectiveLauncher(const GameEntry &g,const LauncherOverrides &overrides,
    const std::vector<ContentSource> &sources,const std::vector<LibraryDefinition> &libraries) {
    const auto override=overrides.find(LauncherGameKey(g));
    if(override!=overrides.end())return override->second;
    const ContentSource *best=nullptr;
    if(!g.installed)for(const auto &s:sources) {
        const auto library=s.libraryId.empty()?DefaultLibraryId(s.platform):s.libraryId;
        auto path=s.path;while(!path.empty()&&path.back()=='/')path.pop_back();
        if(!s.enabled||library!=g.libraryId||s.platform!=g.platform||path.empty())continue;
        if(g.absolutePath!=path&&g.absolutePath.rfind(path+"/",0)!=0)continue;
        if(!best||path.size()>best->path.size())best=&s;
    }
    if(best&&!best->launcherId.empty())return best->launcherId;
    for(const auto &l:libraries)if(l.id==g.libraryId&&!l.launcherId.empty())return l.launcherId;
    if(g.platform==Platform::Files)return {};
    if(g.platform==Platform::WiiU)return "native";
    const auto legacy=best?best->launcher:g.launcher;
    return legacy==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
}
