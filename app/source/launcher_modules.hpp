#pragma once
#include "vc_systems.hpp"
#include "../../launcher-modules/sdk/rom_request.hpp"
#include <dirent.h>
#include <map>
#include <vector>
struct LauncherModule {
    std::string id,name,version,system,extensions,executable,protocol,request,dependency,capability,error;
};
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
    for(const auto &f:fields)if(f.first!="id"&&f.first!="name"&&f.first!="version"&&f.first!="system"&&f.first!="extensions"&&f.first!="executable"&&f.first!="protocol"&&f.first!="request"&&f.first!="dependency"&&f.first!="capability"){out.error="UNKNOWN MODULE FIELD";return false;}
    out.id=fields["id"];out.name=fields["name"];out.version=fields["version"];out.system=fields["system"];out.extensions=fields["extensions"];
    out.executable=fields["executable"];out.protocol=fields["protocol"];out.request=fields["request"];out.dependency=fields["dependency"];out.capability=fields["capability"];
    if(out.id.empty()||out.id.size()>64||!std::all_of(out.id.begin(),out.id.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';})||out.name.empty()||out.version.empty()||out.extensions.empty()){out.error="INCOMPLETE MODULE";return false;}
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
        for(auto &prior:result)if(prior.id==m.id){prior.error="DUPLICATE MODULE ID";m.error=prior.error;}
        result.push_back(m);
    }return result;
}
inline std::string ModuleReadiness(const LauncherModule &m) {
    if(!m.error.empty())return m.error;
    if(!VCSystems::Protocol(m.protocol).empty())return {}; // base/profile validation belongs to the injection backend
    const std::string root="fs:/vol/external01/";
    if(!UFlowRom::File(root+m.executable))return "EMULATOR NOT INSTALLED";
    std::ifstream in(root+m.dependency);std::string capability;std::getline(in,capability);
    if(!capability.empty()&&capability.back()=='\r')capability.pop_back();
    if(capability!=m.capability)return "EMULATOR NEEDS ROM HANDOFF UPDATE";
    return {};
}
