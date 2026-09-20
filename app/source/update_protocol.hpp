#pragma once
#include "update_model.hpp"
#include "../vendor/picojson/picojson.h"
#include "../vendor/monocypher/monocypher-ed25519.h"
#include "../../native-installer/source/installer.hpp"
#include <set>
#include <cmath>
namespace UFlowUpdate {
struct Release { uint64_t sequence=0,size=0;std::string version,notes,sha,url; };
inline std::string Decode64(const std::string &s){
    const std::string chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Setup::Need(s.size()%4==0&&s.size()<=32768,"Invalid update encoding");
    std::string out;unsigned value=0,bits=0;bool padding=false;unsigned pads=0;
    for(char c:s){if(c=='='){padding=true;++pads;continue;}auto n=chars.find(c);
        Setup::Need(!padding&&n!=std::string::npos,"Invalid update encoding");
        value=(value<<6)|unsigned(n);bits+=6;if(bits>=8){bits-=8;out+=char(value>>bits);}}
    Setup::Need(pads<=2&&(!bits||(value&((1u<<bits)-1))==0),"Invalid update encoding");return out;
}
inline picojson::object Object(const std::string &s){
    unsigned depth=0;bool quoted=false,escape=false;
    for(char c:s){if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;else if(c=='{'||c=='[')Setup::Need(++depth<=12,"Update JSON is too deeply nested");
        else if(c=='}'||c==']'){Setup::Need(depth>0,"Invalid update JSON");--depth;}}
    picojson::value v;const auto error=picojson::parse(v,s);
    Setup::Need(error.empty()&&v.is<picojson::object>(),"Invalid update manifest");return v.get<picojson::object>();
}
inline std::string Text(const picojson::object &o,const char *key,size_t max){
    auto it=o.find(key);Setup::Need(it!=o.end()&&it->second.is<std::string>(),"Missing update field");
    auto s=it->second.get<std::string>();Setup::Need(s.size()<=max&&s.find('\0')==s.npos,"Update text too long");return s;
}
inline uint64_t Number(const picojson::object &o,const char *key,uint64_t max){
    auto it=o.find(key);Setup::Need(it!=o.end()&&it->second.is<double>(),"Missing update number");
    double n=it->second.get<double>();Setup::Need(std::isfinite(n)&&n>=1&&n<=double(max)&&std::floor(n)==n,"Invalid update number");return uint64_t(n);
}
inline Release Parse(const std::string &envelope,const uint8_t *key,const std::string &channel){
    Setup::Need(envelope.size()<=32768,"Update manifest too large");auto outer=Object(envelope);
    auto body=Decode64(Text(outer,"manifest",24000)),sig=Decode64(Text(outer,"signature",88));
    Setup::Need(sig.size()==64&&crypto_ed25519_check(reinterpret_cast<const uint8_t*>(sig.data()),key,
        reinterpret_cast<const uint8_t*>(body.data()),body.size())==0,"Update signature is invalid");
    auto o=Object(body);Setup::Need(Number(o,"schema",1)==1&&Text(o,"base_url",128)==BaseURL&&Text(o,"channel",16)==channel,"Wrong update channel or server");
    Release r;r.sequence=Number(o,"sequence",9007199254740991ULL);r.version=Text(o,"release",64);r.notes=Text(o,"changes",2048);
    Setup::Need(!r.version.empty()&&r.version.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._-")==r.version.npos,"Invalid release name");
    auto it=o.find("components");Setup::Need(it!=o.end()&&it->second.is<picojson::array>(),"Missing update components");
    // Protocol 1 deliberately updates the app only. System modules need a
    // separate pre-Aroma recovery mechanism before they can be updated safely.
    const auto &files=it->second.get<picojson::array>();Setup::Need(files.size()==1&&files[0].is<picojson::object>(),"This update needs a newer installer");
    const auto &f=files[0].get<picojson::object>();
    Setup::Need(Text(f,"component",32)=="uflow"&&Text(f,"destination",100)=="wiiu/apps/uFlow/uFlow.wuhb","Unsupported update destination");
    r.size=Number(f,"size",256*1024*1024);r.sha=Text(f,"sha256",64);
    Setup::Need(r.sha.size()==64&&r.sha.find_first_not_of("0123456789abcdef")==r.sha.npos,"Invalid update hash");
    r.url=Text(f,"url",180);Setup::Need(r.url=="releases/"+r.version+"/files/"+r.sha,"Invalid update URL");return r;
}
}
