#pragma once
#include "nes_database.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cstdint>
extern "C" {
#include <libavutil/sha.h>
#include <libavutil/mem.h>
}

namespace NesRom {
inline void Require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
inline std::string Header(const std::string &payload) {
    auto *ctx = av_sha_alloc();
    Require(ctx != nullptr, "NOT ENOUGH MEMORY FOR NES IDENTIFICATION");
    unsigned char digest[20];
    av_sha_init(ctx, 160); av_sha_update(ctx, reinterpret_cast<const unsigned char *>(payload.data()), payload.size());
    av_sha_final(ctx, digest); av_free(ctx);
    const auto *end = database + sizeof(database)/sizeof(*database);
    const auto *it = std::lower_bound(database, end, digest,
        [](const DbEntry &a, const unsigned char *b) { return std::memcmp(a.sha,b,20)<0; });
    if (it == end || std::memcmp(it->sha,digest,20)) return {};
    const auto size = size_t(it->header[4])*16384 + size_t(it->header[5])*8192;
    return size == payload.size() ? std::string(reinterpret_cast<const char *>(it->header),16) : std::string();
}
inline uint32_t LE(const std::string &b, size_t p) {
    Require(p<=b.size() && 4<=b.size()-p,"TRUNCATED UNIF CHUNK");
    uint32_t v=0; for (unsigned i=0;i<4;++i) v |= uint32_t(uint8_t(b[p+i]))<<(8*i); return v;
}
inline std::string Unif(const std::string &b) {
    Require(b.size()>=32,"TRUNCATED UNIF HEADER");
    std::string prg[16],chr[16]; bool seenPrg[16]{},seenChr[16]{};
    size_t p=32; unsigned chunks=0;
    while(p<b.size()) {
        Require(++chunks<=1024 && b.size()-p>=8,"INVALID UNIF CHUNK TABLE");
        const auto tag=b.substr(p,4); const auto n=LE(b,p+4);p+=8;
        Require(n<=b.size()-p,"TRUNCATED UNIF CHUNK DATA");
        if(tag.substr(0,3)=="PRG" || tag.substr(0,3)=="CHR") {
            const auto digit=std::string("0123456789ABCDEF").find(tag[3]);
            Require(digit!=std::string::npos,"INVALID UNIF ROM BANK");
            auto &seen=tag[0]=='P'?seenPrg[digit]:seenChr[digit];
            Require(!seen && n>0,"DUPLICATE / EMPTY UNIF ROM BANK"); seen=true;
            (tag[0]=='P'?prg[digit]:chr[digit])=b.substr(p,n);
        }
        p+=n;
    }
    std::string payload; for(auto &bank:prg)payload+=bank;for(auto &bank:chr)payload+=bank;
    auto h=Header(payload);Require(!h.empty(),"UNIF BOARD NOT IN NES VC DATABASE - USE A COMPATIBLE EMULATOR");
    return h+payload;
}
inline std::string Normalize(std::string b) {
    Require(!b.empty() && b.size()<=32*1024*1024,"INVALID NES ROM SIZE");
    if(b.compare(0,4,"UNIF")==0)return Unif(b);
    if(b.size()<16 || b.compare(0,4,"NES\x1a",4)!=0) {
        const auto h=Header(b);
        Require(!h.empty(),"UNKNOWN HEADERLESS NES ROM - CORRECT INES HEADER REQUIRED");
        return h+b;
    }
    const bool nes2=(uint8_t(b[7])&12)==8;
    // Legacy dumper signatures must not become high mapper bits (DiskDude=64).
    if(!nes2 && b.compare(7,9,"DiskDude!")==0)std::fill(b.begin()+7,b.begin()+16,0);
    Require(!(uint8_t(b[6])&4),"NES TRAINER REQUIRES EMULATOR SUPPORT - CANNOT DISCARD IT");
    Require(!(uint8_t(b[7])&3),"VS / PLAYCHOICE NES ROM NEEDS A COMPATIBLE EMULATOR");
    size_t prg=uint8_t(b[4])*16384,chr=uint8_t(b[5])*8192;
    if(nes2) {
        const auto size=[](unsigned low,unsigned high,size_t unit) -> size_t {
            if(high!=15)return (low+(high<<8))*unit;
            const unsigned exponent=low>>2;
            Require(exponent<25,"NES 2.0 ROM SIZE EXCEEDS VC LIMIT");
            return (size_t(1)<<exponent)*((low&3)*2+1);
        };
        prg=size(uint8_t(b[4]),uint8_t(b[9])&15,16384);
        chr=size(uint8_t(b[5]),uint8_t(b[9])>>4,8192);
        const unsigned mapper=(uint8_t(b[6])>>4)|(uint8_t(b[7])&240)|((uint8_t(b[8])&15)<<8);
        const unsigned sub=uint8_t(b[8])>>4;
        Require(mapper<256 && (sub==0 || (mapper==1 && sub==5 && prg==32768)),"NES 2.0 MAPPER / SUBMAPPER NOT REPRESENTABLE BY VC");
        Require(prg<=255*16384 && chr<=255*8192 && prg%16384==0 && chr%8192==0,"NES 2.0 ROM LAYOUT NOT REPRESENTABLE BY VC");
        const auto ram=[](unsigned shift){return shift?size_t(64)<<shift:0;};
        Require(ram(uint8_t(b[10])&15)+ram(uint8_t(b[10])>>4)<=8192 &&
                (b[11]==0 || uint8_t(b[11])==7) && !(uint8_t(b[14])&3) && (uint8_t(b[12])&3)!=3,
                "NES 2.0 EXTRA RAM / ROM / TIMING REQUIRES A COMPATIBLE EMULATOR");
        Require(b.size()==16+prg+chr,"INVALID NES 2.0 ROM LENGTH");
        b[4]=char(prg/16384);b[5]=char(chr/8192);b[7]=char(mapper&240);
        b[8]=b[10]?1:0; b[9]=(uint8_t(b[12])&3)==1?1:0;
    }
    Require(prg>0 && prg+chr<=b.size()-16,"TRUNCATED NES PRG / CHR DATA");
    // iNES bounds the cartridge with its PRG/CHR counts. Old 127/128-byte
    // annotations and overdump tails are not part of that cartridge.
    b.resize(16+prg+chr);
    std::fill(b.begin()+10,b.begin()+16,0);
    if(!nes2) {
        // Exact hash only; never infer a mapper from a filename or file length.
        const auto h=Header(b.substr(16));
        if(!h.empty())b.replace(0,16,h);
        else { b[7]=char(uint8_t(b[7])&240);b[9]=char(uint8_t(b[9])&1); }
    }
    return b;
}
}
