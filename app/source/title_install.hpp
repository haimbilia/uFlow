#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <filesystem>

namespace TitleInstall {
inline constexpr const char *ModulePath="fs:/vol/external01/wiiu/apps/uFlow/modules/title-installer.ufmod";
inline bool ReadModuleAvailability(){std::ifstream f(ModulePath);std::string line;std::getline(f,line);if(!line.empty()&&line.back()=='\r')line.pop_back();return line=="uflow-title-installer-v1";}
inline bool Available(){
#ifdef __WIIU__
    // Components change while the app is closed. Never open a file from each
    // library rebuild/render: that needlessly enters Aroma's shared heap.
    static const bool available=ReadModuleAvailability();return available;
#else
    return ReadModuleAvailability();
#endif
}
enum class Kind {Game,Update,DLC};
inline const char *Name(Kind k){return k==Kind::Game?"Game":k==Kind::Update?"Update":"DLC";}
struct Package {
    uint64_t titleId=0,bytes=0;uint16_t version=0;Kind kind=Kind::Game;
    std::string path,error; bool selected=true; int installedVersion[2]={-1,-1};
    bool Installed(int target)const{return target>=0&&target<2&&installedVersion[target]>=version;}
};
inline std::string Id(uint64_t id){char s[17];std::snprintf(s,sizeof(s),"%016llx",(unsigned long long)id);return s;}
inline uint64_t BaseId(uint64_t id){return (id>>32)==0x50002?id:(id&0xffffffffULL)|0x0005000000000000ULL;}
inline std::string Size(uint64_t n){char s[48];std::snprintf(s,sizeof(s),n>=1073741824?"%.2f GB":"%.1f MB",double(n)/(n>=1073741824?1073741824:1048576));return s;}
inline uint64_t Big(const unsigned char *p,unsigned n){uint64_t v=0;while(n--)v=(v<<8)|*p++;return v;}
// TMD fields are big endian. Only user titles are installable through this UI.
inline bool ReadPackage(const std::string &folder,Package &p){
    p={};p.path=folder;
    std::ifstream f(folder+"/title.tmd",std::ios::binary);if(!f)return false;
    f.seekg(0,std::ios::end);auto length=f.tellg();if(length<0x204||length>4*1024*1024){p.error="Invalid title metadata";return true;}
    std::vector<unsigned char>b(static_cast<size_t>(length));f.seekg(0);f.read((char*)b.data(),b.size());if(!f){p.error="Cannot read title metadata";return true;}
    const auto sig=Big(b.data(),4);size_t head=sig==0x10004?0x140:sig==0x10003?0x240:sig==0x10005?0x80:0;
    if(!head||b.size()<head+0x9c4){p.error="Unsupported or truncated TMD";return true;}
    p.titleId=Big(b.data()+head+0x4c,8);p.version=Big(b.data()+head+0x9c,2);
    const auto high=p.titleId>>32;
    if(high!=0x50000&&high!=0x50002&&high!=0x5000e&&high!=0x5000c){p.error="Unsupported title type";return true;}
    p.kind=high==0x5000e?Kind::Update:high==0x5000c?Kind::DLC:Kind::Game;
    const auto count=Big(b.data()+head+0x9e,2);size_t records=head+0x9c4;
    if(!count||count>4096||count>(b.size()-records)/48){p.error="Incomplete content table";return true;}
    std::error_code ec;
    for(auto name:{"title.tik","title.cert"})if(!std::filesystem::is_regular_file(folder+"/"+name,ec)){p.error=std::string("Missing ")+name;return true;}
    for(size_t i=0;i<count;++i){auto *r=b.data()+records+i*48;char name[32];std::snprintf(name,sizeof(name),"/%08x.app",unsigned(Big(r,4)));
        const auto size=Big(r+8,8);if(size>UINT64_MAX-p.bytes){p.error="Invalid content size";return true;}p.bytes+=size;
        auto disk=std::filesystem::file_size(folder+name,ec);
        if(ec||disk<size){p.error=std::string("Missing or incomplete ")+(name+1);return true;}
        if(Big(r+6,2)&2){std::snprintf(name,sizeof(name),"/%08x.h3",unsigned(Big(r,4)));if(!std::filesystem::is_regular_file(folder+name,ec)){p.error=std::string("Missing ")+name;return true;}}
    }
    return true;
}
struct Group {uint64_t id=0;std::string name;std::vector<Package> packages;};
inline std::vector<Group> Scan(const std::string &root,int depth){
    std::vector<Group> groups;std::map<uint64_t,size_t> indices;
    std::vector<std::pair<std::string,int>> todo{{root,std::clamp(depth,0,8)}};size_t visited=0;
    while(!todo.empty()&&visited++<16384){auto [path,level]=todo.back();todo.pop_back();Package p;
        if(ReadPackage(path,p)){
            if(!p.titleId)continue;
            auto id=BaseId(p.titleId);auto [it,added]=indices.emplace(id,groups.size());
            if(added)groups.push_back({id,std::filesystem::path(path).filename().string(),{}});
            auto &g=groups[it->second];if(p.kind==Kind::Game)g.name=std::filesystem::path(path).filename().string();
            g.packages.push_back(std::move(p));continue;
        }
        if(!level)continue;
        std::error_code ec;
        for(std::filesystem::directory_iterator it(path,ec),end;!ec&&it!=end;it.increment(ec))
            if(it->is_directory(ec)&&!it->is_symlink(ec))todo.emplace_back(it->path().string(),level-1);
    }
    for(auto &g:groups){std::sort(g.packages.begin(),g.packages.end(),[](const auto&a,const auto&b){return a.kind==b.kind?a.version>b.version:a.kind<b.kind;});
        std::array<bool,3> seen{};for(auto &p:g.packages){p.selected=!seen[int(p.kind)]&&p.error.empty();seen[int(p.kind)]=true;}}
    return groups;
}
struct Snapshot {bool busy=false,complete=false;uint64_t done=0,total=0;std::string message;size_t package=0;};
bool Refresh(std::vector<Package>& packages);
bool FreeSpace(int target,uint64_t &bytes,std::string &error);
// Starts only from the user's Install button. All work and callbacks remain
// alive until MCP acknowledges completion; leaving the details page is locked.
class Job {
public:
    Job();~Job();Job(const Job&)=delete;Job& operator=(const Job&)=delete;
    bool Start(const std::vector<Package>& packages,int target);
    Snapshot Poll()const;
private:struct Impl;Impl *impl_;
};
}
