#pragma once

#include "library.hpp"
#include "launcher_modules.hpp"
#include "saved_file.hpp"
#include "vc_inject.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

enum class LauncherKind { Native, WiiFlow, USBLoaderGX, VirtualConsole, Emulator, DSInjection };
struct LauncherDefinition {
    std::string id, name;
    LauncherKind kind = LauncherKind::Emulator;
    std::string system, extensions, executable;
    bool enabled = true;
    std::string moduleId;
    // RetroArch launchers: the .wuhb supplying RetroArch's assets. Like the
    // executable (the core), it is the user's choice; the module only fills in
    // defaults when nothing valid is saved.
    std::string contentBundle;
    // The user set LAUNCH MODULE to NONE: never attach a module automatically.
    // Stored as "-" in the module field of launchers.cfg.
    bool moduleOptOut = false;
};
// A generic emulator entry that pointed straight at the RetroArch bundle for
// the same file types this module now launches natively. Name, enabled state
// and library associations are the user's and stay untouched.
inline std::string NormalizedExtensions(const std::string &list) {
    std::set<std::string> parts;std::istringstream stream(list);std::string item;
    while(std::getline(stream,item,',')) {
        item.erase(std::remove_if(item.begin(),item.end(),[](unsigned char c){return std::isspace(c);}),item.end());
        std::transform(item.begin(),item.end(),item.begin(),[](unsigned char c){return std::tolower(c);});
        if(!item.empty()&&item.front()!='.')item="."+item;
        if(!item.empty())parts.insert(item);
    }
    std::string out;for(const auto &p:parts){if(!out.empty())out+=",";out+=p;}return out;
}
inline bool LegacyRetroArchEntry(const LauncherDefinition &existing,const LauncherModule &module) {
    if(existing.id!=module.id||!module.error.empty())return false;
    if(existing.kind!=LauncherKind::Emulator)return false;
    auto executable=existing.executable;
    for(const char *prefix:{"fs:/vol/external01/","sd:/","/vol/external01/"})
        if(executable.rfind(prefix,0)==0){executable.erase(0,std::char_traits<char>::length(prefix));break;}
    if(executable!=module.contentBundle&&executable!=module.executable)return false;
    return NormalizedExtensions(existing.extensions)==NormalizedExtensions(module.extensions);
}
inline bool SdRelativeWithSuffix(const std::string &path,const char *suffix) {
    const std::string tail(suffix);
    if(!UFlowRom::RelativePath(path)||path.size()<=tail.size())return false;
    auto end=path.substr(path.size()-tail.size());
    std::transform(end.begin(),end.end(),end.begin(),[](unsigned char c){return std::tolower(c);});
    return end==tail;
}
inline bool RetroArchCorePath(const std::string &path){return SdRelativeWithSuffix(path,".rpx");}
inline bool RetroArchBundlePath(const std::string &path){return SdRelativeWithSuffix(path,".wuhb");}
inline const std::vector<LauncherModule> &InstalledLauncherModules() {static const auto modules=DiscoverLauncherModules();return modules;}
inline const LauncherModule *FindLauncherModule(const std::string &id) {for(const auto &m:InstalledLauncherModules())if(m.id==id)return &m;return nullptr;}
inline std::vector<LauncherDefinition> MergeLauncherModules(std::vector<LauncherDefinition> list) {
    list.erase(std::remove_if(list.begin(),list.end(),[](const auto &l){return l.kind==LauncherKind::VirtualConsole;}),list.end());
    for(const auto &m:InstalledLauncherModules()) {
        if(m.id=="native"||m.id=="wiiflow"||m.id=="usbloadergx"||m.id=="nintendont"||m.id.find("vc_")==0)continue;
        auto it=std::find_if(list.begin(),list.end(),[&](const auto &l){return l.id==m.id;});
        const auto kind=!VCSystems::Protocol(m.protocol).empty()?LauncherKind::DSInjection:LauncherKind::Emulator;
        if(it==list.end()){list.push_back({m.id,m.name,kind,m.system,m.extensions,m.executable,true,m.id});list.back().contentBundle=m.contentBundle;}
        else if(it->kind==LauncherKind::Emulator&&!it->moduleOptOut&&m.protocol==kRetroArchProtocol&&(it->moduleId==m.id||LegacyRetroArchEntry(*it,m))) {
            // Saved core/bundle paths and extensions win, so files can live
            // anywhere on the SD; an entry from before the module pointed at the
            // bundle, not a core.
            const bool legacy=it->moduleId.empty();
            it->moduleId=m.id;
            if(it->extensions.empty())it->extensions=m.extensions;
            if(legacy||!RetroArchCorePath(it->executable))it->executable=m.executable;
            if(!RetroArchBundlePath(it->contentBundle))it->contentBundle=m.contentBundle;
        }
        else if(it->kind==LauncherKind::Emulator&&!it->moduleOptOut&&
                (it->moduleId==m.id||(it->id=="noods"&&it->moduleId.empty()))) {
            // rom-request-v1 hands the ROM to that emulator's own build, so its
            // executable follows the module; the file types stay the user's.
            it->moduleId=m.id;it->executable=m.executable;
            if(it->extensions.empty())it->extensions=m.extensions;
        }
        else if(it->kind==LauncherKind::DSInjection&&m.protocol=="vc-nes-v1"&&it->extensions==".nes")it->extensions=m.extensions;
    }return list;
}
inline const char *LauncherKindName(LauncherKind kind) {
    switch(kind) {
        case LauncherKind::Native: return "WII U / RAW";
        case LauncherKind::WiiFlow: return "WIIFLOW";
        case LauncherKind::USBLoaderGX: return "USB LOADER GX";
        case LauncherKind::VirtualConsole: return "VC / PREPARED INJECTION";
        case LauncherKind::Emulator: return "EMULATOR / HOMEBREW";
        case LauncherKind::DSInjection: return "ON-DEMAND VC INJECTION"; // kind 5 retained for saved DS configs
    }
    return "UNKNOWN";
}
inline std::vector<LauncherDefinition> DefaultLaunchers() {
    std::vector<LauncherDefinition> out = {
        {"native","Wii U / Raw",LauncherKind::Native,"Wii U",".rpx","",true},
        {"wiiflow","WiiFlow (Wii / GameCube)",LauncherKind::WiiFlow,"Wii / GameCube",".wbfs,.iso","",true},
        {"usbloadergx","USB Loader GX (Wii / GameCube)",LauncherKind::USBLoaderGX,"Wii / GameCube",".wbfs,.iso","",true},
        {"noods","NooDS",LauncherKind::Emulator,"Nintendo DS",".nds","wiiu/apps/noods/noods.wuhb",true,"noods"}
    };
    out.push_back({"nintendont","Nintendont via WiiFlow",LauncherKind::WiiFlow,"GameCube",".iso","",true});
    return out;
}
inline constexpr const char *LauncherConfigPath = "fs:/vol/external01/wiiu/apps/uFlow/launchers.cfg";
inline bool LauncherFieldValid(const std::string &text) {
    return text.size()<=512 && text.find_first_of("\t\r\n") == std::string::npos && text.find('\0')==std::string::npos;
}
// Executables are SD-relative, never shell commands or paths into system NAND.
inline std::string LauncherSDPath(std::string path) {
    for(const char *prefix : {"fs:/vol/external01/","sd:/","/vol/external01/"})
        if(path.rfind(prefix,0)==0) {path.erase(0,std::char_traits<char>::length(prefix));break;}
    if(path.empty() || path.front()=='/' || path.find(':')!=std::string::npos || path.find('\\')!=std::string::npos || !LauncherFieldValid(path)) return {};
    std::istringstream stream(path); std::string part;
    while(std::getline(stream,part,'/')) if(part.empty()||part=="."||part=="..") return {};
    auto ext=path.substr(path.find_last_of('.')==std::string::npos?path.size():path.find_last_of('.'));
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    return ext==".rpx"||ext==".wuhb"?path:std::string();
}
inline bool LauncherExecutableExists(const std::string &relative) {
    const auto path=LauncherSDPath(relative); struct stat info{};
    return !path.empty() && stat(("fs:/vol/external01/"+path).c_str(),&info)==0 && S_ISREG(info.st_mode);
}
inline std::vector<LauncherDefinition> LoadLaunchers() {
    auto input=SavedFile::Open(LauncherConfigPath);
    if(!input.is_open()) return MergeLauncherModules(DefaultLaunchers());
    std::vector<LauncherDefinition> result; std::set<std::string> ids; std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r') line.pop_back();
        std::vector<std::string> p; std::size_t start=0,end;
        while((end=line.find('\t',start))!=std::string::npos){p.push_back(line.substr(start,end-start));start=end+1;}
        p.push_back(line.substr(start)); if(p.size()<7||p.size()>9)continue;
        if(p[0].empty()||p[1].find_first_not_of(' ')==std::string::npos||!ids.insert(p[0]).second)continue;
        if(p[2].size()!=1||p[2][0]<'0'||p[2][0]>'5'||(p[6]!="0"&&p[6]!="1"))continue;
        if(!std::all_of(p.begin(),p.end(),LauncherFieldValid))continue;
        result.push_back({p[0],p[1],static_cast<LauncherKind>(p[2][0]-'0'),p[3],p[4],p[5],p[6]=="1"});
        result.back().moduleId=p.size()>=8?p[7]:p[0]=="noods"?"noods":"";
        if(p.size()==9)result.back().contentBundle=p[8];
        if(result.back().moduleId=="-"){result.back().moduleId.clear();result.back().moduleOptOut=true;}
    }
    return MergeLauncherModules(result);
}
inline bool SaveLaunchers(const std::vector<LauncherDefinition> &list) {
    std::set<std::string> ids;
    for(const auto &l:list) {
        if(l.id.empty()||l.name.find_first_not_of(' ')==std::string::npos||!ids.insert(l.id).second)return false;
        for(const auto *field:{&l.id,&l.name,&l.system,&l.extensions,&l.executable,&l.moduleId,&l.contentBundle})if(!LauncherFieldValid(*field))return false;
        if(!l.contentBundle.empty()&&!RetroArchBundlePath(l.contentBundle))return false;
        if(static_cast<int>(l.kind)<0||static_cast<int>(l.kind)>5)return false;
        if(l.kind==LauncherKind::Emulator&&!l.executable.empty()&&LauncherSDPath(l.executable).empty())return false;
    }
    const std::string temporary=std::string(LauncherConfigPath)+".tmp";
    std::ofstream output(temporary,std::ios::trunc); if(!output)return false;
    for(const auto &l:list) {
        output<<l.id<<'\t'<<l.name<<'\t'<<static_cast<int>(l.kind)<<'\t'<<l.system<<'\t'<<l.extensions<<'\t'<<l.executable<<'\t'<<l.enabled<<'\t'<<(l.moduleOptOut?std::string("-"):l.moduleId);
        if(!l.contentBundle.empty())output<<'\t'<<l.contentBundle;
        output<<'\n';
    }
    output.flush(); bool ok=static_cast<bool>(output);output.close();ok=ok&&!output.fail();
    return ok&&SavedFile::Commit(temporary,LauncherConfigPath);
}
// What a RetroArch launcher still needs, checked on its own paths; empty when ready.
inline std::string RetroArchLauncherProblem(const LauncherDefinition &l) {
    const std::string root="fs:/vol/external01/";
    if(!RetroArchCorePath(l.executable)||!UFlowRom::File(root+l.executable))return "CORE NOT FOUND - CHOOSE ITS .RPX";
    if(!RetroArchBundlePath(l.contentBundle)||!UFlowRom::File(root+l.contentBundle))return "RETROARCH NOT FOUND - CHOOSE ITS .WUHB";
    return {};
}
// Modules the user may pick for this launcher. RetroArch's protocol works with
// any core the user chooses; a rom-request module only with its own emulator.
inline bool CompatibleLauncherModule(const LauncherModule &m,const LauncherDefinition &l) {
    if(!m.error.empty()||l.kind!=LauncherKind::Emulator)return false;
    return m.protocol==kRetroArchProtocol||(m.protocol=="rom-request-v1"&&m.id==l.id);
}
inline bool IsRetroArchLauncher(const LauncherDefinition &l) {
    const auto *m=l.moduleId.empty()?nullptr:FindLauncherModule(l.moduleId);
    return m&&m->protocol==kRetroArchProtocol;
}
inline std::string LauncherReadiness(const LauncherDefinition &l) {
    if(!l.enabled)return "DISABLED";
    if(l.kind==LauncherKind::DSInjection)return "EXPERIMENTAL - CONFIGURE VC BASE";
    if(const auto *m=FindLauncherModule(l.moduleId);m&&!m->error.empty())return m->error;
    if(IsRetroArchLauncher(l)) {
        const auto problem=RetroArchLauncherProblem(l);
        return problem.empty()?"READY - ONE-PRESS LAUNCH":problem;
    }
    if(!l.moduleId.empty()) {const auto *m=FindLauncherModule(l.moduleId);if(!m)return "LAUNCH MODULE NOT INSTALLED";const auto error=ModuleReadiness(*m);return error.empty()?"DIRECT ROM LAUNCH / v"+m->version:error;}
    if(l.kind==LauncherKind::Emulator)return LauncherExecutableExists(l.executable)?"APP FOUND / ROM BROWSER":"SELECT INSTALLED RPX / WUHB";
    if(l.kind==LauncherKind::VirtualConsole)return "PREPARED INJECTION REQUIRED";
    return "BUILT-IN ADAPTER";
}
inline bool LauncherAccepts(const LauncherDefinition &l,Platform platform) {
    if(l.kind==LauncherKind::DSInjection)return platform==Platform::Files;
    if(l.kind==LauncherKind::VirtualConsole)return platform==Platform::WiiU||platform==Platform::Files;
    if(l.kind==LauncherKind::Native)return platform==Platform::WiiU;
    if(l.kind==LauncherKind::Emulator)return platform==Platform::Files;
    if(l.id=="nintendont")return platform==Platform::GameCube;
    return platform==Platform::Wii||platform==Platform::GameCube;
}
inline bool LauncherMatchesFile(const LauncherDefinition &l,const std::string &path) {
    if(l.extensions.empty())return true;
    const auto dot=path.find_last_of('.');if(dot==std::string::npos)return false;
    auto extension=path.substr(dot);
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return std::tolower(c);});
    const auto extensions=l.kind==LauncherKind::DSInjection&&l.extensions==".nes"?std::string(VCSystems::Find("nes")->extensions):l.extensions;
    std::istringstream stream(extensions);std::string match;
    while(std::getline(stream,match,',')) {
        match.erase(std::remove_if(match.begin(),match.end(),[](unsigned char c){return std::isspace(c);}),match.end());
        std::transform(match.begin(),match.end(),match.begin(),[](unsigned char c){return std::tolower(c);});
        if(!match.empty()&&match.front()!='.')match="."+match;
        if(match==extension)return true;
    }
    return false;
}
// Mapping only selects an already discovered injection. It never edits installed
// content, injects a ROM, writes tickets or substitutes a shared VC base.
inline std::string ResolveVCInjection(GameEntry &game,const std::vector<GameEntry> &games) {
    if(game.platform==Platform::WiiU)return {};
    auto input=SavedFile::Open(game.absolutePath+".uflow-title"); std::string id;
    std::getline(input,id); if(!id.empty()&&id.back()=='\r')id.pop_back();
    if(id.size()!=16||!std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isxdigit(c);}))return "MAP THIS ROM TO A PREPARED INJECTION (.uflow-title)";
    std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return std::tolower(c);});
    if(id.rfind("00050000",0)!=0)return "MAPPING MUST TARGET A WII U GAME TITLE";
    for(const auto &candidate:games) {
        auto target=candidate.titleId;
        std::transform(target.begin(),target.end(),target.begin(),[](unsigned char c){return std::tolower(c);});
        if(candidate.platform==Platform::WiiU&&target==id){game=candidate;return {};}
    }
    return "INJECTION NOT IN LIBRARY - INSTALL / ADD PATH AND SCAN";
}
inline bool SaveVCMapping(const GameEntry &rom,const GameEntry &injection) {
    if(rom.platform!=Platform::Files||injection.platform!=Platform::WiiU)return false;
    auto id=injection.titleId;
    if(id.size()!=16||id.rfind("00050000",0)!=0||!std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isxdigit(c);}))return false;
    struct stat info{};if(stat(rom.absolutePath.c_str(),&info)!=0||!S_ISREG(info.st_mode))return false;
    const auto target=rom.absolutePath+".uflow-title",temp=target+".tmp";
    std::ofstream output(temp,std::ios::trunc);if(!output)return false;
    output<<id<<'\n';output.flush();bool ok=static_cast<bool>(output);output.close();ok=ok&&!output.fail();
    return ok&&SavedFile::Commit(temp,target);
}
