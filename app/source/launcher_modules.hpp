#pragma once
#include "vc_systems.hpp"
#include "../../launcher-modules/sdk/rom_request.hpp"
#include <dirent.h>
#include <map>
#include <vector>
struct LauncherModule {
    std::string id,name,version,system,extensions,executable,protocol,request,dependency,capability,error;
    // retroarch-sysargs-v1 only: the .wuhb whose /vol/content supplies the
    // emulator's assets while a core .rpx runs as the executable.
    std::string contentBundle;
    std::string file; // the .ufmod it came from, shown in the launcher editor
};
// RetroArch's Aroma port receives argv through SYSStandardArgsIn; the core is a
// separately linked executable and its assets come from the RetroArch bundle.
inline constexpr const char *kRetroArchProtocol = "retroarch-sysargs-v1";
inline constexpr const char *kRetroArchCoreRoot = "retroarch/cores/";
inline bool ParseLauncherModule(const std::string &text,LauncherModule &out) {
    out={};if(text.size()>8192){out.error="MODULE TOO LARGE";return false;}
    std::istringstream input(text);std::string line;std::getline(input,line);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="UFLOW-MODULE-1"){out.error="UNSUPPORTED MODULE VERSION";return false;}
    std::map<std::string,std::string> fields;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line.front()=='#')continue;
        auto equal=line.find('=');if(equal==std::string::npos||!fields.emplace(line.substr(0,equal),line.substr(equal+1)).second){out.error="INVALID / DUPLICATE MODULE FIELD";return false;}
        if(line.size()>1100||line.find_first_of("\t\r\n")!=std::string::npos||line.find('\0')!=std::string::npos){out.error="INVALID MODULE FIELD";return false;}
    }
    for(const auto &f:fields)if(f.first!="id"&&f.first!="name"&&f.first!="version"&&f.first!="system"&&f.first!="extensions"&&f.first!="executable"&&f.first!="protocol"&&f.first!="request"&&f.first!="dependency"&&f.first!="capability"&&f.first!="content_bundle"){out.error="UNKNOWN MODULE FIELD";return false;}
    out.id=fields["id"];out.name=fields["name"];out.version=fields["version"];out.system=fields["system"];out.extensions=fields["extensions"];
    out.executable=fields["executable"];out.protocol=fields["protocol"];out.request=fields["request"];out.dependency=fields["dependency"];out.capability=fields["capability"];
    out.contentBundle=fields["content_bundle"];
    if(out.id.empty()||out.id.size()>64||!std::all_of(out.id.begin(),out.id.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';})||out.name.empty()||out.version.empty()||out.extensions.empty()){out.error="INCOMPLETE MODULE";return false;}
    if(out.protocol==kRetroArchProtocol) {
        // Native arguments carry the ROM, so there is no request file, no
        // handoff dependency and no capability marker to check.
        if(!out.request.empty()||!out.dependency.empty()||!out.capability.empty()){out.error="INVALID RETROARCH MODULE FIELDS";return false;}
        for(const auto *p:{&out.executable,&out.contentBundle})
            if(!UFlowRom::RelativePath(*p)){out.error="INVALID MODULE SD PATH";return false;}
        if(out.executable.rfind(kRetroArchCoreRoot,0)!=0||out.executable.size()<5||
           out.executable.substr(out.executable.size()-4)!=".rpx"){out.error="INVALID RETROARCH CORE PATH";return false;}
        if(out.contentBundle.rfind("wiiu/apps/",0)!=0||out.contentBundle.size()<6||
           out.contentBundle.substr(out.contentBundle.size()-5)!=".wuhb"){out.error="INVALID RETROARCH CONTENT BUNDLE";return false;}
        return true;
    }
    if(!out.contentBundle.empty()){out.error="CONTENT BUNDLE NEEDS A RETROARCH MODULE";return false;}
    if(const auto *system=VCSystems::Find(VCSystems::Protocol(out.protocol))) {
        // Upgrade the shipped v1 NES manifest in memory; existing installs do
        // not need a replacement .ufmod just to gain format support.
        if(std::string(system->id)=="nes"&&out.extensions==".nes")out.extensions=system->extensions;
        if(!out.executable.empty()||!out.request.empty()||!out.dependency.empty()||!out.capability.empty()||out.extensions!=system->extensions||out.system!=system->name) {out.error="INVALID VC INJECTION MODULE";return false;}
        return true;
    }
    if(out.protocol!="rom-request-v1"||out.capability!="UFLOW-ROM-1"){out.error="UNSUPPORTED LAUNCH PROTOCOL";return false;}
    for(const auto *p:{&out.executable,&out.request,&out.dependency})if(!UFlowRom::RelativePath(*p)||p->find("wiiu/apps/")!=0){out.error="INVALID MODULE SD PATH";return false;}
    const auto parent=out.executable.substr(0,out.executable.find_last_of('/')+1);
    if(out.request!=parent+"uflow-launch.req"||out.dependency!=parent+"uflow-api.txt"){out.error="INVALID HANDOFF LOCATION";return false;}
    if(out.executable.size()<5||(out.executable.substr(out.executable.size()-5)!=".wuhb"&&out.executable.substr(out.executable.size()-4)!=".rpx")){out.error="INVALID EMULATOR EXECUTABLE";return false;}
    return true;
}
inline std::vector<LauncherModule> DiscoverLauncherModules() {
    const std::string root="fs:/vol/external01/wiiu/apps/uFlow/launchers/";
    std::vector<LauncherModule> result;DIR *dir=opendir(root.c_str());if(!dir)return result;
    std::vector<std::string> files;while(auto *e=readdir(dir)){std::string n=e->d_name;if(n.size()>6&&n.substr(n.size()-6)==".ufmod")files.push_back(n);}closedir(dir);
    std::sort(files.begin(),files.end());
    for(const auto &file:files){std::ifstream in(root+file,std::ios::binary);char buf[8193];in.read(buf,sizeof(buf));LauncherModule m;
        ParseLauncherModule(std::string(buf,static_cast<size_t>(in.gcount())),m);
        if(m.id.empty())m.id=file;
        if(m.name.empty())m.name=file;
        m.file=file;
        for(auto &prior:result)if(prior.id==m.id){prior.error="DUPLICATE MODULE ID";m.error=prior.error;}
        result.push_back(m);
    }return result;
}
inline std::string ModuleReadiness(const LauncherModule &m) {
    if(!m.error.empty())return m.error;
    if(!VCSystems::Protocol(m.protocol).empty())return {}; // base/profile validation belongs to the injection backend
    const std::string root="fs:/vol/external01/";
    if(m.protocol==kRetroArchProtocol) {
        // Files only; whether this build accepts native arguments is proven by
        // the hardware test, not by their presence on the card.
        if(!UFlowRom::File(root+m.executable))return "RETROARCH CORE NOT INSTALLED";
        if(!UFlowRom::File(root+m.contentBundle))return "RETROARCH BUNDLE NOT INSTALLED";
        return {}; // ready; the settings label comes from LauncherReadiness
    }
    if(!UFlowRom::File(root+m.executable))return "EMULATOR NOT INSTALLED";
    std::ifstream in(root+m.dependency);std::string capability;std::getline(in,capability);
    if(!capability.empty()&&capability.back()=='\r')capability.pop_back();
    if(capability!=m.capability)return "EMULATOR NEEDS ROM HANDOFF UPDATE";
    return {};
}
