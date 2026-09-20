#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <zlib.h>

// ZIPs are read in memory, never extracted onto the SD. Central-directory
// inspection is separate from decompression so library scans stay bounded.
namespace RomArchive {
inline void Need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
inline std::string Ext(std::string p){auto d=p.find_last_of('.');if(d==p.npos)return {};p=p.substr(d);
    std::transform(p.begin(),p.end(),p.begin(),[](unsigned char c){return std::tolower(c);});return p;}
inline bool NesName(const std::string &p){const auto e=Ext(p);return e==".nes"||e==".unf"||e==".unif";}
inline uint32_t U(const std::string &b,size_t p,int n=4){Need(p<=b.size()&&size_t(n)<=b.size()-p,"TRUNCATED ZIP DIRECTORY");
    uint32_t v=0;for(int i=0;i<n;++i)v|=uint32_t(uint8_t(b[p+i]))<<(i*8);return v;}
inline std::string ReadAt(std::ifstream &f,uint64_t off,size_t n){std::string b(n,'\0');f.seekg(off);if(n)f.read(b.data(),n);Need(bool(f),"ZIP READ FAILED");return b;}
struct Entry {std::string name;uint32_t compressed=0,size=0,crc=0,offset=0,directory=0;uint16_t method=0,flags=0;};
inline Entry Inspect(std::ifstream &f){
    f.seekg(0,std::ios::end);const auto end=f.tellg();Need(end>=22&&uint64_t(end)<=64*1024*1024,"ZIP EXCEEDS 64 MB / INVALID SIZE");
    const auto total=uint64_t(end),tailSize=std::min<uint64_t>(total,65557);
    const auto tail=ReadAt(f,total-tailSize,tailSize);size_t pos=tail.size()-22;
    for(;;){if(U(tail,pos)==0x06054b50&&pos+22+U(tail,pos+20,2)==tail.size())break;
        Need(pos>0,"ZIP END RECORD MISSING");--pos;}
    const auto count=U(tail,pos+10,2),cdSize=U(tail,pos+12),cd=U(tail,pos+16);
    Need(U(tail,pos+4,2)==0&&U(tail,pos+6,2)==0&&U(tail,pos+8,2)==count&&count>0&&count<=4096,
         "MULTI-DISK / ZIP64 / TOO MANY ZIP ENTRIES");
    Need(cdSize<=2*1024*1024&&uint64_t(cd)+cdSize==total-tailSize+pos,"INVALID ZIP DIRECTORY RANGE");
    const auto directory=ReadAt(f,cd,cdSize);size_t p=0;unsigned matches=0;Entry chosen;
    for(unsigned i=0;i<count;++i){
        Need(p<=directory.size()&&directory.size()-p>=46&&U(directory,p)==0x02014b50,"INVALID ZIP ENTRY");
        const auto n=U(directory,p+28,2),extra=U(directory,p+30,2),comment=U(directory,p+32,2);
        Need(n>0&&n<=1024&&uint64_t(p)+46+n+extra+comment<=directory.size(),"INVALID ZIP ENTRY NAME");
        const auto name=directory.substr(p+46,n);
        if(NesName(name)&&name.rfind("__MACOSX/",0)!=0){
            Need(++matches==1,"ZIP CONTAINS MULTIPLE NES ROMS - EXTRACT OR SPLIT THE ARCHIVE");
            Need(name.find('\0')==name.npos&&name.front()!='/'&&name.find(':')==name.npos&&name.find('\\')==name.npos,
                 "UNSAFE ZIP MEMBER NAME");
            size_t start=0;while(start<name.size()){auto slash=name.find('/',start);auto part=name.substr(start,slash==name.npos?name.npos:slash-start);
                Need(part!=".."&&part!="."&&!part.empty(),"UNSAFE ZIP MEMBER PATH");if(slash==name.npos)break;start=slash+1;}
            chosen={name,U(directory,p+20),U(directory,p+24),U(directory,p+16),U(directory,p+42),cd,
                    uint16_t(U(directory,p+10,2)),uint16_t(U(directory,p+8,2))};
            Need(!(chosen.flags&0x41)&&U(directory,p+34,2)==0,"ENCRYPTED / MULTI-DISK ZIP NOT SUPPORTED");
            Need(chosen.method==0||chosen.method==8,"ZIP MUST USE STORED OR DEFLATE COMPRESSION");
            Need(chosen.size>0&&chosen.size<=32*1024*1024&&chosen.compressed<=64*1024*1024&&
                 uint64_t(chosen.offset)+30<=cd,"INVALID ZIP ROM SIZE / OFFSET");
        }
        p+=46+n+extra+comment;
    }
    Need(p==directory.size()&&matches==1,"ZIP MUST CONTAIN ONE NES / UNIF ROM");return chosen;
}
inline Entry Inspect(const std::string &path){std::ifstream f(path,std::ios::binary);Need(bool(f),"ZIP MISSING / UNREADABLE");return Inspect(f);}
inline std::string Extract(const std::string &path){
    std::ifstream f(path,std::ios::binary);Need(bool(f),"ZIP MISSING / UNREADABLE");const auto e=Inspect(f);
    const auto h=ReadAt(f,e.offset,30);Need(U(h,0)==0x04034b50&&U(h,6,2)==e.flags&&U(h,8,2)==e.method,"ZIP LOCAL HEADER MISMATCH");
    const auto n=U(h,26,2),extra=U(h,28,2);const uint64_t data=uint64_t(e.offset)+30+n+extra;
    Need(n==e.name.size()&&data+e.compressed<=e.directory,"INVALID ZIP LOCAL RANGE");
    Need(ReadAt(f,e.offset+30,n)==e.name,"ZIP LOCAL NAME MISMATCH");
    if(!(e.flags&8))Need(U(h,14)==e.crc&&U(h,18)==e.compressed&&U(h,22)==e.size,"ZIP LOCAL SIZE / CRC MISMATCH");
    auto packed=ReadAt(f,data,e.compressed);std::string out;
    if(e.method==0){Need(e.compressed==e.size,"INVALID STORED ZIP LENGTH");out=std::move(packed);}
    else {
        out.resize(e.size);z_stream z{};z.next_in=reinterpret_cast<Bytef*>(packed.data());z.avail_in=packed.size();
        z.next_out=reinterpret_cast<Bytef*>(out.data());z.avail_out=out.size();
        Need(inflateInit2(&z,-MAX_WBITS)==Z_OK,"ZIP DECOMPRESSOR INIT FAILED");const int status=inflate(&z,Z_FINISH);
        const bool valid=status==Z_STREAM_END&&z.total_in==e.compressed&&z.total_out==e.size;inflateEnd(&z);
        Need(valid,"CORRUPT / TRUNCATED ZIP DEFLATE STREAM");
    }
    Need(crc32(0,reinterpret_cast<const Bytef*>(out.data()),out.size())==e.crc,"ZIP ROM CRC MISMATCH");return out;
}
}
