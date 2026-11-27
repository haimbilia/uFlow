#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

// Host-testable discovery. All paths here are absolute device paths; the
// launcher translates the selected roots to its SD/USB protocol afterwards.
namespace RawAddons {
inline bool Directory(const std::string &p) { struct stat s{}; return stat(p.c_str(), &s)==0 && S_ISDIR(s.st_mode); }
inline bool File(const std::string &p) { struct stat s{}; return stat(p.c_str(), &s)==0 && S_ISREG(s.st_mode); }
inline std::string Read(const std::string &p) { std::ifstream f(p); return {std::istreambuf_iterator<char>(f), {}}; }
inline uint64_t Hex(const std::string &xml, const char *tag) {
    auto p=xml.find(std::string("<")+tag); if(p==std::string::npos)return 0;
    p=xml.find('>',p); if(p==std::string::npos)return 0;
    return strtoull(xml.c_str()+p+1,nullptr,16);
}
inline std::vector<std::string> Children(const std::string &p) {
    std::vector<std::string> out;
    if(DIR *d=opendir(p.c_str())) { while(auto *e=readdir(d)) {
        if(e->d_name[0]!='.')out.emplace_back(e->d_name);
    } closedir(d); }
    std::sort(out.begin(),out.end()); return out;
}
inline std::string Id(uint64_t id) { char b[17]; snprintf(b,sizeof(b),"%016llx",(unsigned long long)id);return b; }
struct Plan { uint64_t title=0; unsigned version=0; std::string update, dlc; unsigned dlcVersion=0; std::vector<uint16_t> dlcContentIds; };
inline Plan Discover(const std::string &base) {
    Plan plan; plan.title=Hex(Read(base+"/code/app.xml"),"title_id");
    if((plan.title>>32)!=0x00050000)return plan;
    std::vector<std::string> candidates{base+"/update",base+"/updates"};
    for(const auto &name:Children(base+"/updates"))candidates.push_back(base+"/updates/"+name);
    const auto expected=(plan.title & 0xffffffffULL)|0x0005000e00000000ULL;
    for(const auto &p:candidates) {
        auto xml=Read(p+"/code/app.xml"); auto version=Hex(xml,"title_version");
        if(Hex(xml,"title_id")!=expected || !Directory(p+"/content") ||
           !File(p+"/code/cos.xml"))continue;
        // Version comes from XML, never lexicographic folder order (v9/v10).
        if(plan.update.empty() || version>plan.version) {plan.update=p;plan.version=version;}
    }
    const auto dlcId=(plan.title & 0xffffffffULL)|0x0005000c00000000ULL;
    for(const auto &name:std::vector<std::string>{"aoc"+Id(dlcId),"dlc","aoc"}) {
        auto p=base+"/"+name;
        auto xml=Read(p+"/code/app.xml");
        if(xml.empty())xml=Read(p+"/meta/meta.xml");
        if(!xml.empty() && Hex(xml,"title_id")!=dlcId)continue;
        if(xml.empty() && name!="aoc"+Id(dlcId))continue;
        if(Directory(p+"/content")) { plan.dlc=p+"/content";plan.dlcVersion=Hex(xml,"title_version");break; }
        // Loadiine's aoc<id> directory contains the decrypted content itself.
        if(name=="aoc"+Id(dlcId) && Directory(p) && !Children(p).empty()) {plan.dlc=p;plan.dlcVersion=Hex(xml,"title_version");break;}
    }
    if(!plan.dlc.empty()) {
        for(const auto &name:Children(plan.dlc)) {
            if(name.size()!=4 || name.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos ||
               !Directory(plan.dlc+"/"+name))continue;
            plan.dlcContentIds.push_back(static_cast<uint16_t>(strtoul(name.c_str(),nullptr,16)));
        }
    }
    return plan;
}

// Only code is staged, in a disposable loader cache. This gives MCP static
// RPL imports the same update-first/base-fallback view as PPC filesystem reads.
// The source dump and multi-gigabyte content trees are never merged or edited.
inline bool StageCode(const std::string &base, const std::string &update, const std::string &cache) {
    std::map<std::string,std::string> files;
    for(const auto &root:{base,update}) for(const auto &n:Children(root)) {
        if(!File(root+"/"+n))return false; // Wii U code directories are flat.
        files[n]=root+"/"+n;
    }
    if(files.empty())return false;
    for(const auto &[n,src]:files) {
        const auto tmp=cache+"/"+n+".part", dst=cache+"/"+n;
        std::ifstream in(src,std::ios::binary);std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        if(!in || !out)return false;
        if(in.peek()!=std::char_traits<char>::eof())out<<in.rdbuf();
        out.flush();bool ok=!in.bad() && out.good();out.close();
        if(!ok)return false;
        // Cafe FS / FAT rename need not overwrite an existing destination.
        if(File(dst) && remove(dst.c_str())!=0)return false;
        if(rename(tmp.c_str(),dst.c_str())!=0)return false;
    }
    for(const auto &n:Children(cache)) if(!files.count(n)) {
        const auto p=cache+"/"+n;
        if(!File(p) || remove(p.c_str())!=0)return false;
    }
    return true;
}
}
