#pragma once
// Streams wiiutdb.xml / wiitdb.xml out of GameTDB's zip without holding the
// whole inflated XML (33 MB for wiitdb) in memory. No network or file access.
#include "gametdb.hpp"
#include "../vendor/picojson/picojson.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <zlib.h>

namespace GameTdb {
inline uint32_t Le(const std::string &b,std::size_t at,int bytes) {
    uint32_t v=0;
    for(int i=bytes-1;i>=0;--i)v=(v<<8)|static_cast<unsigned char>(b[at+i]);
    return v;
}
// Inflates the archive's first entry into `sink`. Returns false for a damaged
// or unsupported archive, or when `sink` returns false (cancelled).
inline bool InflateFirstEntry(const std::string &zip,const std::function<bool(const char*,std::size_t)> &sink) {
    if(zip.size()<22)return false;
    // End of central directory: last 22..65557 bytes.
    std::size_t eocd=std::string::npos;
    for(std::size_t at=zip.size()-22;;--at){
        if(Le(zip,at,4)==0x06054b50){eocd=at;break;}
        if(at==0 || zip.size()-at>65557)break;
    }
    if(eocd==std::string::npos)return false;
    const std::size_t central=Le(zip,eocd+16,4);
    if(central+46>zip.size() || Le(zip,central,4)!=0x02014b50)return false;
    const auto method=Le(zip,central+10,2);
    const std::size_t packed=Le(zip,central+20,4),local=Le(zip,central+42,4);
    if(local+30>zip.size() || Le(zip,local,4)!=0x04034b50)return false;
    const std::size_t data=local+30+Le(zip,local+26,2)+Le(zip,local+28,2);
    if(data>zip.size() || packed>zip.size()-data)return false;
    if(method==0)return sink(zip.data()+data,packed);
    if(method!=8)return false;
    z_stream z{};
    if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)return false;
    z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(zip.data()+data));z.avail_in=static_cast<uInt>(packed);
    char out[64*1024];int status=Z_OK;bool ok=true;
    while(status==Z_OK){
        z.next_out=reinterpret_cast<Bytef*>(out);z.avail_out=sizeof(out);
        status=inflate(&z,Z_NO_FLUSH);
        if(status!=Z_OK && status!=Z_STREAM_END){ok=false;break;}
        const std::size_t produced=sizeof(out)-z.avail_out;
        if(produced && !sink(out,produced)){ok=false;break;}
        if(status==Z_OK && produced==0 && z.avail_in==0){ok=false;break;} // truncated
    }
    inflateEnd(&z);
    return ok;
}
// Parses only the wanted IDs, one complete <game> block at a time.
inline bool ParseZippedDatabase(const std::string &zip,std::set<std::string> wanted,const std::string &locale,
        std::map<std::string,Metadata> &result,const std::function<bool()> &cancelled={}) {
    std::string pending;
    const auto drain=[&](bool final){
        const auto last=final?pending.size():pending.rfind("</game>");
        if(last==std::string::npos || last==0)return;
        const auto cut=final?last:last+7;
        for(auto &[id,meta]:ParseDatabase(pending.substr(0,cut),wanted,locale)){wanted.erase(id);result[id]=std::move(meta);}
        pending.erase(0,cut);
    };
    bool complete=false,cancel=false;
    const bool ok=InflateFirstEntry(zip,[&](const char *data,std::size_t size){
        if(cancelled && cancelled()){cancel=true;return false;}
        pending.append(data,size);
        if(pending.size()>256*1024)drain(false);
        if(wanted.empty()){complete=true;return false;} // everything found: stop inflating
        return pending.size()<16*1024*1024; // a single game block is never this large
    });
    if(cancel)return false;
    if(ok && !wanted.empty())drain(true);
    return ok || complete;
}
// JSON in the shape uFlow's metadata reader already accepts.
inline std::string MetadataJson(const Metadata &m) {
    picojson::object details,root;
    const auto put=[](picojson::object &o,const char *key,const std::string &value){if(!value.empty())o[key]=picojson::value(value);};
    put(details,"description",m.synopsis);put(details,"developer",m.developer);put(details,"publisher",m.publisher);
    put(details,"genres",m.genre);put(details,"region",m.region);put(details,"age_rating",m.rating);
    put(details,"release_date",m.date);put(details,"max_players",m.players);
    put(root,"title",m.title);put(root,"gametdb_id",m.id);
    root["details"]=picojson::value(details);
    return picojson::value(root).serialize(true);
}
}
