#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

inline uint64_t RawAssetFingerprint(const std::string &text) {
    uint64_t h=14695981039346656037ULL;
    for(unsigned char c:text){h^=c;h*=1099511628211ULL;}
    return h;
}
inline std::string RawAssetHex(uint64_t value){char b[17];snprintf(b,sizeof(b),"%016llx",(unsigned long long)value);return b;}
inline std::string RawAssetKey(const std::string &path) {
    std::string out;
    for(size_t i=0;i<path.size();) {
        auto end=path.find('/',i);if(end==std::string::npos)end=path.size();
        auto part=path.substr(i,end-i);i=end+1;
        if(part.empty()||part==".")continue;
        if(part=="..") {if(out.empty())return {};auto p=out.rfind('/');out=p==std::string::npos?"":out.substr(0,p);continue;}
        for(char &c:part)if(c>='A'&&c<='Z')c+='a'-'A';
        if(!out.empty())out+='/';
        out+=part;
    }
    return out;
}
class RawAssetCache {
    struct Entry {std::string key;unsigned slot;};
    std::vector<Entry> entries;
    std::string root;
public:
    void Clear(){std::vector<Entry>().swap(entries);root.clear();}
    size_t Size() const{return entries.size();}
    bool Load(const std::string &absolute,const std::string &relative,const std::string &identity) {
        Clear();std::ifstream f(absolute+"/manifest.ready",std::ios::binary);std::string line;
        if(!std::getline(f,line)||line!="UFLOW-ASSET-CACHE-1"||!std::getline(f,line)||line!=identity||!std::getline(f,line))return false;
        if(line.empty()||line.size()>6)return false;
        unsigned count=0;for(char c:line){if(c<'0'||c>'9')return false;count=count*10+c-'0';}
        if(count>262144)return false;
        std::vector<Entry> parsed;parsed.reserve(count);
        std::vector<bool> seen(262144,false);
        for(unsigned i=0;i<count;++i){
            if(!std::getline(f,line))return false;
            auto tab=line.find('\t');if(tab==std::string::npos||tab==0||tab>768||line.size()!=tab+7)return false;
            const auto key=line.substr(0,tab);if(key!=RawAssetKey(key)||key[0]=='/'||key.find(':')!=std::string::npos)return false;
            unsigned slot=0;for(char c:line.substr(tab+1)){unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(digit>15)return false;slot=(slot<<4)|digit;}
            if(slot>=262144||seen[slot]||(!parsed.empty()&&parsed.back().key>=key))return false;
            seen[slot]=true;parsed.push_back({key,slot});
        }
        if(!std::getline(f,line)||line!="END"||std::getline(f,line)||!f.eof())return false;
        if(relative.empty()||relative[0]=='/'||relative.find(':')!=std::string::npos)return false;
        entries.swap(parsed);root=relative;return true;
    }
    bool Resolve(char *out,size_t capacity,const char *path,const char *mode,bool active) const {
        if(!active||entries.empty()||!out||!path||!mode||(strcmp(mode,"r")&&strcmp(mode,"rb"))||strncmp(path,"/vol/content/",13))return false;
        const auto key=RawAssetKey(path+13);
        const auto it=std::lower_bound(entries.begin(),entries.end(),key,[](const Entry &e,const std::string &k){return e.key<k;});
        if(it==entries.end()||it->key!=key)return false;
        const auto n=it->slot;
        const int size=snprintf(out,capacity,"/vol/external01/%s/data/%02x/%02x/%02x.bin",root.c_str(),n>>12,(n>>6)&63,n&63);
        return size>=0&&static_cast<size_t>(size)<capacity;
    }
};
