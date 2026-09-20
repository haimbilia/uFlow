#pragma once
// Bounded, endian-independent cartridge/container transformations. Source bases
// are inspected read-only; Apply writes only into the caller's private stage.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>
#include "nes_rom.hpp"
#include "vc_systems.hpp"
extern "C" {
#include <libavutil/md5.h>
}

namespace VCFormat {
using Bytes = std::string;
inline void Need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
inline uint32_t Number(const Bytes &b,size_t p,int n=4,bool be=false){
    Need(n>=0&&n<=4&&p<=b.size()&&size_t(n)<=b.size()-p,"TRUNCATED VC CONTAINER");
    uint32_t v=0;for(int i=0;i<n;++i)v|=uint32_t(uint8_t(b[p+i]))<<(8*(be?n-1-i:i));return v;
}
inline void Put(Bytes &b,size_t p,uint32_t v,int n=4,bool be=false){
    Need(p<=b.size()&&size_t(n)<=b.size()-p,"INVALID VC OUTPUT RANGE");
    for(int i=0;i<n;++i)b[p+i]=char(v>>(8*(be?n-1-i:i)));
}
inline Bytes Read(const std::string &path,size_t limit=64*1024*1024){
    std::ifstream f(path,std::ios::binary|std::ios::ate);Need(bool(f),"VC FILE MISSING / UNREADABLE");
    auto n=f.tellg();Need(n>=0&&uint64_t(n)<=limit,"VC FILE EXCEEDS SAFE SIZE LIMIT");Bytes b(size_t(n),'\0');
    f.seekg(0);if(!b.empty())f.read(&b[0],b.size());Need(bool(f),"VC FILE READ FAILED");return b;
}
inline void Write(const std::string &path,const Bytes &b){
    std::ofstream f(path,std::ios::binary|std::ios::trunc);Need(bool(f),"VC OUTPUT OPEN FAILED");
    f.write(b.data(),b.size());f.flush();Need(bool(f),"VC OUTPUT WRITE FAILED / SD FULL");f.close();Need(!f.fail(),"VC OUTPUT CLOSE FAILED");
}
inline Bytes Inflate(const Bytes &b,size_t p,size_t n,size_t length){
    Need(length>0&&length<=64*1024*1024&&p<=b.size()&&n<=b.size()-p,"INVALID VC COMPRESSED RANGE");
    Bytes out(length,'\0');uLongf size=length;uLong consumed=n;
    Need(uncompress2(reinterpret_cast<Bytef*>(&out[0]),&size,reinterpret_cast<const Bytef*>(b.data()+p),&consumed)==Z_OK&&size==length&&consumed==n,"INVALID VC ZLIB STREAM");return out;
}
inline Bytes Deflate(const Bytes &b){
    uLongf n=compressBound(b.size());Bytes out(n,'\0');
    Need(compress2(reinterpret_cast<Bytef*>(&out[0]),&n,reinterpret_cast<const Bytef*>(b.data()),b.size(),1)==Z_OK,"VC COMPRESSION FAILED");out.resize(n);return out;
}
inline std::string SystemFor(const std::string &path){
    auto dot=path.find_last_of('.');if(dot==std::string::npos)return {};auto ext=path.substr(dot);
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    if(ext==".nds"||ext==".srl")return "ds";
    if(ext==".nes"||ext==".unf"||ext==".unif")return "nes";
    if(ext==".zip")return VCSystems::File(path);
    if(ext==".sfc"||ext==".smc")return "snes";
    if(ext==".gba")return "gba";
    if(ext==".z64"||ext==".v64"||ext==".n64")return "n64";
    if(ext==".pce")return "pce";
    if(ext==".rom"||ext==".mx1"||ext==".mx2")return "msx";
    return {};
}
inline bool System(const std::string &s){return s=="ds"||s=="nes"||s=="snes"||s=="gba"||s=="n64"||s=="pce"||s=="msx";}
inline size_t SnesHeader(const Bytes &b){
    for(size_t p:{size_t(0x7fc0),size_t(0xffc0),size_t(0x40ffc0)})if(p+32<=b.size()){
        const auto complement=Number(b,p+28,2),checksum=Number(b,p+30,2);
        if((complement^checksum)==65535&&uint8_t(b[p+23])>=5&&uint8_t(b[p+23])<=13)return p;
    }throw std::runtime_error("INVALID SNES INTERNAL HEADER / CHECKSUM PAIR");
}
inline Bytes Cartridge(const std::string &path,const std::string &system){
    Need(SystemFor(path)==system,"ROM EXTENSION DOES NOT MATCH VC SYSTEM");
    auto b=system=="nes"&&RomArchive::Ext(path)==".zip"?RomArchive::Extract(path):Read(path,system=="n64"?64*1024*1024:32*1024*1024);
    if(system=="nes"){
        b=NesRom::Normalize(std::move(b));
    }else if(system=="snes"){
        if(b.size()%32768==512)b.erase(0,512);
        Need(!b.empty()&&b.size()%32768==0&&b.size()<=8*1024*1024,"INVALID SNES ROM LENGTH");SnesHeader(b);
    }else if(system=="gba"){
        Need(b.size()>=192&&uint8_t(b[0xb2])==0x96,"INVALID GBA HEADER");
        uint8_t sum=0;for(int i=0xa0;i<=0xbc;++i)sum+=uint8_t(b[i]);
        Need(uint8_t(sum+uint8_t(b[0xbd])+0x19)==0,"INVALID GBA HEADER CHECKSUM");
    }else if(system=="n64"){
        Need(b.size()>=4096&&b.size()%4==0,"INVALID N64 ROM LENGTH");auto magic=Number(b,0,4,true);
        if(magic==0x37804012)for(size_t i=0;i<b.size();i+=2)std::swap(b[i],b[i+1]);
        else if(magic==0x40123780)for(size_t i=0;i<b.size();i+=4){std::swap(b[i],b[i+3]);std::swap(b[i+1],b[i+2]);}
        Need(Number(b,0,4,true)==0x80371240,"INVALID N64 BYTE ORDER / HEADER");
    }else if(system=="pce"){
        if(b.size()%8192==512)b.erase(0,512);
        Need(b.size()>=8192&&b.size()<=0x280000&&b.size()%8192==0,"REQUIRES A PC ENGINE HUCARD ROM (NOT CD)");
    }else if(system=="msx"){
        Need(b.size()>=8192&&b.size()<=4*1024*1024&&b.size()%8192==0,"INVALID MSX CARTRIDGE SIZE");
        Need(b.compare(0,2,"AB")==0||(b.size()>=0x4002&&b.compare(0x4000,2,"AB")==0),"REQUIRES AN MSX CARTRIDGE ROM (NOT DISK)");
    }return b;
}

struct Section {std::array<uint32_t,10> h{};Bytes data;};
struct Rpx {
    Bytes header;std::vector<Section> sections;size_t shoff=0;
    explicit Rpx(const Bytes &b){
        Need(b.size()>=64&&b.compare(0,4,"\x7f" "ELF",4)==0&&b[4]==1&&b[5]==2&&Number(b,18,2,true)==20,"INVALID POWERPC RPX");
        shoff=Number(b,32,4,true);auto count=Number(b,48,2,true);
        Need(count>0&&count<=256&&shoff>=52&&shoff<=4096&&Number(b,46,2,true)==40&&shoff+count*40<=b.size(),"INVALID RPX SECTION TABLE");
        header=b.substr(0,shoff+count*40);uint64_t total=0;
        for(unsigned i=0;i<count;++i){Section s;for(int j=0;j<10;++j)s.h[j]=Number(b,shoff+i*40+j*4,4,true);
            if(s.h[1]!=8&&s.h[5]){
                Need(s.h[4]>=header.size()&&uint64_t(s.h[4])+s.h[5]<=b.size(),"INVALID RPX SECTION RANGE");
                size_t length=(s.h[2]&0x8000000)?Number(b,s.h[4],4,true):s.h[5];total+=length;
                Need(total<=96*1024*1024,"RPX EXCEEDS DECOMPRESSION BUDGET");
                if(s.h[2]&0x8000000){Need(s.h[5]>=4,"INVALID RPX COMPRESSED SECTION");s.data=Inflate(b,s.h[4]+4,s.h[5]-4,length);}
                else s.data=b.substr(s.h[4],s.h[5]);
            }sections.push_back(std::move(s));
        }
    }
    Bytes Pack(){
        int crcIndex=-1;for(size_t i=0;i<sections.size();++i)if(sections[i].h[1]==0x80000003){Need(crcIndex<0,"MULTIPLE RPX CRC TABLES");crcIndex=int(i);}
        Need(crcIndex>=0&&sections[crcIndex].data.size()==sections.size()*4,"RPX CRC TABLE MISSING / INVALID");
        auto &crc=sections[crcIndex].data;
        for(size_t i=0;i<sections.size();++i)Put(crc,i*4,i==size_t(crcIndex)||sections[i].data.empty()?0:crc32(0,reinterpret_cast<const Bytef*>(sections[i].data.data()),sections[i].data.size()),4,true);
        // Section indices are NOT physical file order. Cafe's loader expects
        // CRC / FILEINFO / data / imports+read / text / temporary relocations.
        // In particular FILEINFO must be available in the initial loader read.
        // See devkitPro/wut-tools src/elf2rpl/main.cpp calculateSectionOffsets.
        auto group=[](const Section &s){const auto type=s.h[1],flags=s.h[2];
            if(type==0x80000003)return 0;
            if(type==0x80000004)return 1;
            if(type!=0x80000002&&(flags&7)==3)return 2;
            if((flags&2)&&(type==0x80000001||type==0x80000002||!(flags&5)))return 3;
            if((flags&4)&&type!=0x80000001&&type!=0x80000002)return 4;
            if(!(flags&6)&&type!=0x80000002)return 5;
            return -1;
        };
        std::vector<size_t> order;
        for(size_t i=0;i<sections.size();++i){auto &s=sections[i];
            if(s.h[1]==0||s.h[1]==8||s.data.empty()){s.h[4]=0;continue;}
            Need(group(s)>=0,"UNSUPPORTED RPX SECTION FLAGS");order.push_back(i);
        }
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return group(sections[a])<group(sections[b]);});
        Bytes out=header;
        for(const auto i:order){auto &s=sections[i];
                  out.resize((out.size()+63)&~size_t(63),'\0');s.h[4]=out.size();Bytes encoded=s.data;
                  if(s.h[2]&0x8000000){encoded=Bytes(4,'\0')+Deflate(s.data);Put(encoded,0,s.data.size(),4,true);}
                  s.h[5]=encoded.size();out+=encoded;
        }
        // Keep indices, links, load addresses and NOBITS memory sizes intact.
        for(size_t i=0;i<sections.size();++i){const auto &s=sections[i];
            for(int j=0;j<10;++j)Put(out,shoff+i*40+j*4,s.h[j],4,true);
        }out.resize((out.size()+63)&~size_t(63),'\0');return out;
    }
};
struct Slot {size_t section=0,offset=0,length=0;};
inline Slot RomSlot(const Rpx &r,const std::string &system){
    Slot result;int hits=0;
    for(size_t i=0;i<r.sections.size();++i){const auto &s=r.sections[i];if(s.h[1]!=1||!(s.h[2]&2)||(s.h[2]&4))continue;
        const auto &b=s.data;size_t p=0;
        while((p=b.find("WUP-",p))!=Bytes::npos){size_t q=p++;if(q<36||q+32>b.size())continue;
            const bool nes=b.compare(q+16,3,"NES")==0;
            if((system=="nes")!=nes||(!nes&&b[q+4]!='J'))continue;
            // RPX VC wrappers carry an explicit cartridge span; never scan into
            // executable code or use the entire remainder of a section as space.
            size_t off=q+(nes?16:12),length=0;
            if(nes){length=Number(b,q-16)-0x30; // wrapper end minus ROM offset
                const size_t original=16+size_t(uint8_t(b[q+20]))*16384+size_t(uint8_t(b[q+21]))*8192;
                Need(length>=original&&length<=b.size()-off,"INVALID NES BASE SLOT");length=original;
            }else{
                const size_t start=Number(b,q-36+8),end=Number(b,q-36+12);
                Need(start==0x30&&end>start,"INVALID SNES VC WRAPPER");length=end-start;
            }
            Need(length>0&&length<=8*1024*1024&&off<=b.size()&&length<=b.size()-off,"VC ROM SLOT OUT OF RANGE");
            result={i,off,length};++hits;
        }
    }Need(hits==1,"VC BASE MUST CONTAIN ONE MATCHING ROM SLOT");return result;
}
inline void InjectRpx(Bytes &file,const Bytes &rom,const std::string &system){
    Rpx r(file);auto slot=RomSlot(r,system);Need(rom.size()<=slot.length,"ROM TOO LARGE FOR THIS VC BASE");
    auto &b=r.sections[slot.section].data;b.replace(slot.offset,rom.size(),rom);file=r.Pack();
}

struct PackageEntry {size_t field=0,offset=0,length=0;std::string name;};
inline std::vector<PackageEntry> Package(const Bytes &b){
    Need(b.size()>=12&&Number(b,0)==b.size()-4,"INVALID VC PACKAGE LENGTH");std::vector<PackageEntry> entries;size_t p=4;
    while(p<b.size()){
        size_t field=p;auto length=Number(b,p);p+=4;auto end=b.find('\0',p);
        Need(end!=Bytes::npos&&end-p>0&&end-p<256,"INVALID VC PACKAGE FILENAME");auto name=b.substr(p,end-p);p=end+1;
        Need(length<=b.size()-p&&entries.size()<512,"INVALID VC PACKAGE ENTRY RANGE");entries.push_back({field,p,length,name});p+=length;
    }Need(p==b.size(),"TRUNCATED VC PACKAGE");return entries;
}
inline PackageEntry PackageRom(const Bytes &b,const std::string &system){
    PackageEntry result;int count=0;for(const auto &e:Package(b)){
        auto name=e.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
        if((system=="pce"&&name.size()>4&&name.substr(name.size()-4)==".pce")||
           (system=="msx"&&name=="megarom.rom")){result=e;++count;}
    }Need(count==1,"VC PACKAGE MUST CONTAIN ONE CARTRIDGE ROM");return result;
}
inline void InjectPackage(Bytes &b,const Bytes &rom,const std::string &system){
    auto e=PackageRom(b,system);b.replace(e.offset,e.length,rom);Put(b,e.field,rom.size());Put(b,0,b.size()-4);
}

// M2 MDF uses a filename-derived 80-byte XOR stream (MT19937 init_by_array).
// std::seed_seq is NOT the same seeding algorithm; keep all arithmetic explicit.
inline Bytes MdfKey(std::string name){
    auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});name="MX8wgGEJ2+M47"+name;
    uint8_t hash[16];av_md5_sum(hash,reinterpret_cast<const uint8_t*>(name.data()),name.size());
    uint32_t seed[4];for(int k=0;k<4;++k)seed[k]=uint32_t(hash[k*4])|(uint32_t(hash[k*4+1])<<8)|(uint32_t(hash[k*4+2])<<16)|(uint32_t(hash[k*4+3])<<24);
    std::array<uint32_t,624> mt{};mt[0]=19650218;for(size_t i=1;i<624;++i)mt[i]=1812433253u*(mt[i-1]^(mt[i-1]>>30))+i;
    size_t i=1,j=0;for(int k=0;k<624;++k){mt[i]=(mt[i]^((mt[i-1]^(mt[i-1]>>30))*1664525u))+seed[j]+j;
        if(++i==624){mt[0]=mt[623];i=1;}j=(j+1)%4;}
    for(int k=0;k<623;++k){mt[i]=(mt[i]^((mt[i-1]^(mt[i-1]>>30))*1566083941u))-i;if(++i==624){mt[0]=mt[623];i=1;}}
    mt[0]=0x80000000;
    for(i=0;i<624;++i){uint32_t y=(mt[i]&0x80000000)|(mt[(i+1)%624]&0x7fffffff);mt[i]=mt[(i+397)%624]^(y>>1)^((y&1)?0x9908b0df:0);}
    Bytes key(80,'\0');for(i=0;i<20;++i){uint32_t y=mt[i];y^=y>>11;y^=(y<<7)&0x9d2c5680;y^=(y<<15)&0xefc60000;y^=y>>18;Put(key,i*4,y);}return key;
}
inline void XorMdf(Bytes &b,const std::string &name){auto key=MdfKey(name);for(size_t i=8;i<b.size();++i)b[i]^=key[(i-8)%80];}
inline Bytes DecodeMdf(Bytes b,const std::string &name){Need(b.size()>8&&b.compare(0,4,"mdf\0",4)==0,"INVALID GBA MDF HEADER");XorMdf(b,name);return Inflate(b,8,b.size()-8,Number(b,4));}
inline Bytes EncodeMdf(const Bytes &b,const std::string &name){Bytes out=Bytes("mdf\0",4)+Bytes(4,'\0')+Deflate(b);Put(out,4,b.size());XorMdf(out,name);return out;}

inline std::vector<uint32_t> PsbArray(const Bytes &b,size_t &p){
    int n=Number(b,p++,1)-12;Need(n>=1&&n<=4,"INVALID PSB ARRAY COUNT");auto count=Number(b,p,n);p+=n;
    n=Number(b,p++,1)-12;Need(n>=1&&n<=4&&count<=100000&&p<=b.size()&&uint64_t(count)*n<=b.size()-p,"INVALID PSB ARRAY RANGE");
    std::vector<uint32_t> a;for(uint32_t i=0;i<count;++i){a.push_back(Number(b,p,n));p+=n;}return a;
}
inline std::vector<std::string> PsbNames(const Bytes &b){
    size_t p=Number(b,12);auto offsets=PsbArray(b,p),jumps=PsbArray(b,p),starts=PsbArray(b,p);std::vector<std::string> names;
    for(auto a:starts){Need(a<jumps.size(),"INVALID PSB NAME START");uint32_t x=jumps[a];std::string name;
        while(x){Need(x<jumps.size()&&name.size()<512,"INVALID PSB NAME CHAIN");auto parent=jumps[x];Need(parent<offsets.size()&&x>=offsets[parent]&&x-offsets[parent]<=255,"INVALID PSB NAME CHARACTER");name.insert(name.begin(),char(x-offsets[parent]));x=parent;}
        names.push_back(name);
    }return names;
}
struct PsbNode {uint8_t type=0;Bytes leaf;std::vector<uint32_t> names;std::vector<PsbNode> children;};
inline PsbNode PsbRead(const Bytes &b,size_t p,int depth=0,size_t *budget=nullptr){
    size_t local=100000;if(!budget)budget=&local;Need(depth<8&&*budget>0,"PSB STRUCTURE TOO COMPLEX");--*budget;
    PsbNode n;n.type=Number(b,p++,1);
    if(n.type==32||n.type==33){if(n.type==33)n.names=PsbArray(b,p);auto off=PsbArray(b,p);
        Need(n.type!=33||n.names.size()==off.size(),"INVALID PSB DICTIONARY");
        for(auto o:off){Need(o<b.size()&&p<b.size()-o,"INVALID PSB CHILD OFFSET");n.children.push_back(PsbRead(b,p+o,depth+1,budget));}
    }else{
        int length=n.type<=4?0:n.type<=12?n.type-4:n.type>=21&&n.type<=28?(n.type<=24?n.type-20:n.type-24):n.type==29?0:n.type==30?4:n.type==31?8:-1;
        Need(length>=0&&p<=b.size()&&size_t(length)<=b.size()-p,"UNSUPPORTED PSB VALUE");n.leaf=b.substr(p,length);
    }return n;
}
inline Bytes PsbArrayWrite(const std::vector<uint32_t> &a){Bytes out(10+a.size()*4,'\0');out[0]=16;Put(out,1,a.size());out[5]=16;for(size_t i=0;i<a.size();++i)Put(out,6+i*4,a[i]);out.resize(6+a.size()*4);return out;}
inline Bytes PsbWrite(const PsbNode &n){
    Bytes out(1,char(n.type));if(n.type!=32&&n.type!=33)return out+n.leaf;
    if(n.type==33)out+=PsbArrayWrite(n.names);
    Bytes children;std::vector<uint32_t> offsets;
    for(const auto &c:n.children){offsets.push_back(children.size());children+=PsbWrite(c);}return out+PsbArrayWrite(offsets)+children;
}
inline uint32_t PsbInteger(const PsbNode &n){Need(n.type>=4&&n.type<=8,"INVALID GBA ARCHIVE INTEGER");return Number(n.leaf,0,n.type-4);}
inline void PsbSet(PsbNode &n,uint32_t v){n.type=8;n.leaf=Bytes(4,'\0');Put(n.leaf,0,v);}
struct GbaArchive {
    Bytes psb;std::vector<std::string> names;PsbNode root;size_t info=0,rom=0;std::string romName;uint32_t offset=0,length=0;
    explicit GbaArchive(const Bytes &packed){
        psb=DecodeMdf(packed,"alldata.psb.m");Need(psb.size()>=40&&psb.compare(0,4,"PSB\0",4)==0&&Number(psb,4)==2,"REQUIRES A VERSION 2 GBA PSB BASE");
        names=PsbNames(psb);root=PsbRead(psb,Number(psb,36));Need(root.type==33,"INVALID GBA PSB ROOT");int matches=0;
        for(size_t i=0;i<root.names.size();++i){Need(root.names[i]<names.size(),"INVALID PSB NAME INDEX");if(names[root.names[i]]!="file_info")continue;info=i;auto &files=root.children[i];Need(files.type==33,"INVALID GBA FILE TABLE");
            for(size_t j=0;j<files.names.size();++j){Need(files.names[j]<names.size(),"INVALID GBA FILE NAME");auto name=names[files.names[j]];
                if(name.rfind("system/roms/",0)==0){auto &r=files.children[j];Need(r.type==32&&r.children.size()==2,"INVALID GBA ROM RECORD");rom=j;romName=name;offset=PsbInteger(r.children[0]);length=PsbInteger(r.children[1]);++matches;}
            }
        }Need(matches==1,"GBA BASE MUST CONTAIN ONE ROM RECORD");
    }
    Bytes Update(uint32_t off,uint32_t len){auto &r=root.children[info].children[rom];PsbSet(r.children[0],off);PsbSet(r.children[1],len);
        Put(psb,36,psb.size());psb+=PsbWrite(root);return EncodeMdf(psb,"alldata.psb.m");}
};
} // namespace VCFormat
