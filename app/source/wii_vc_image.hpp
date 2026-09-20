#pragma once
// Streaming NFS -> single-disc WBFS. Format references (not vendored code):
// Dolphin DiscIO/NFSBlob.cpp, FIX94/nfs2iso2nfs and WiiFlow libwbfs.
// No common keys, title keys, firmware or game data are embedded in this code.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>
extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace WiiVC::Image {
using Byte=unsigned char;
using Key=std::array<Byte,16>;
using Progress=std::function<bool(const std::string &,unsigned long long,unsigned long long)>;
using KeyProvider=std::function<bool(unsigned,Key &)>;
constexpr uint64_t Sector=0x8000,Chunk=0xfa00000,Block=0x200000,Split=0x80000000;
constexpr unsigned MapCount=(143432*2)/64;
inline void Need(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
inline uint32_t U32(const Byte *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline void Put32(Byte *p,uint32_t n){for(int i=3;i>=0;--i){p[i]=Byte(n);n>>=8;}}
inline void Wipe(void *p,size_t n){auto *b=static_cast<volatile Byte*>(p);while(n--)*b++=0;}
inline void Tick(const Progress &cb,const char *phase,uint64_t n,uint64_t size){
    Need(!cb||cb(phase,n,size),"PREPARATION CANCELLED - ORIGINAL FILES UNCHANGED");
}
inline uint64_t Size(const std::string &path){struct stat s{};Need(stat(path.c_str(),&s)==0&&S_ISREG(s.st_mode)&&s.st_size>=0,"MISSING OR UNREADABLE FILE: "+path);return uint64_t(s.st_size);}
struct File {
    FILE *f=nullptr;
    explicit File(const std::string &p,const char *mode="rb"):f(std::fopen(p.c_str(),mode)){Need(f!=nullptr,"CANNOT OPEN: "+p);}
    ~File(){if(f)std::fclose(f);}
    File(const File&)=delete;File&operator=(const File&)=delete;
    void Read(uint64_t off,void *p,size_t n){Need(off<0x80000000ULL&&std::fseek(f,static_cast<long>(off),SEEK_SET)==0&&std::fread(p,1,n,f)==n,"SHORT GAME IMAGE READ");}
    void Write(const void *p,size_t n){Need(std::fwrite(p,1,n,f)==n,"CANNOT WRITE PREPARED GAME - CHECK SD SPACE");}
    void Close(){FILE *old=f;f=nullptr;const bool flushed=std::fflush(old)==0;const bool closed=std::fclose(old)==0;Need(flushed&&closed,"CANNOT FLUSH PREPARED GAME");}
};
struct AES {
    AVAES *ctx=av_aes_alloc();
    AES(){Need(ctx!=nullptr,"NOT ENOUGH MEMORY FOR GAME PREPARATION");}
    ~AES(){if(ctx){Wipe(ctx,av_aes_size);av_free(ctx);}}
    AES(const AES&)=delete;AES&operator=(const AES&)=delete;
    void Init(const Key &key,bool decrypt){Need(av_aes_init(ctx,key.data(),128,decrypt)==0,"AES INITIALIZATION FAILED");}
    void Crypt(Byte *data,size_t size,Key iv,bool decrypt){Need(size%16==0,"INVALID AES BLOCK SIZE");av_aes_crypt(ctx,data,data,static_cast<int>(size/16),iv.data(),decrypt);}
};
struct SHA {
    AVSHA *ctx=av_sha_alloc();
    explicit SHA(int bits=256){Need(ctx&&av_sha_init(ctx,bits)==0,"HASH INITIALIZATION FAILED");}
    ~SHA(){av_free(ctx);}
    void Add(const void *p,size_t n){av_sha_update(ctx,static_cast<const Byte*>(p),n);}
    std::string Hex(){Byte out[32];av_sha_final(ctx,out);std::string s;for(Byte b:out){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
};
struct Range {uint32_t start,count;uint64_t packed;};
class Source {
    std::vector<std::unique_ptr<File>> files;
    AES aes;
    uint32_t cached=~0u;
    std::array<Byte,Sector> data{};
public:
    std::vector<Range> ranges;
    uint64_t sectors=0;
    std::string identity;
    explicit Source(const std::string &root){
        std::array<Byte,512> header{};File first(root+"/content/hif_000000.nfs");first.Read(0,header.data(),header.size());
        Need(!std::memcmp(header.data(),"EGGS",4)&&!std::memcmp(header.data()+508,"SGGE",4),"INVALID NFS HEADER");
        const auto count=U32(header.data()+16);Need(count>0&&count<=61,"INVALID NFS EXTENT COUNT");
        SHA hash;hash.Add(header.data(),header.size());
        uint64_t end=0;
        for(unsigned i=0;i<count;++i){
            const uint32_t start=U32(header.data()+20+i*8),length=U32(header.data()+24+i*8);
            Need(length&&start>=end&&uint64_t(start)+length<=uint64_t(MapCount)*64,"INVALID OR OVERLAPPING NFS EXTENTS");
            ranges.push_back({start,length,sectors});sectors+=length;end=uint64_t(start)+length;
        }
        const uint64_t packed=512+sectors*Sector,parts=(packed+Chunk-1)/Chunk;
        for(uint64_t i=0;i<parts;++i){
            char leaf[40];std::snprintf(leaf,sizeof(leaf),"/content/hif_%06u.nfs",unsigned(i));
            const auto path=root+leaf;const auto bytes=Size(path);
            Need(i+1<parts?bytes==Chunk:bytes>=packed-i*Chunk&&bytes<=Chunk,"TRUNCATED NFS CHUNK");
            struct stat st{};Need(stat(path.c_str(),&st)==0,"CANNOT INSPECT NFS CHUNK");
            const auto description=std::to_string(bytes)+":"+std::to_string(st.st_mtime)+";";hash.Add(description.data(),description.size());
            files.push_back(std::make_unique<File>(path));
        }
        Key key{};const auto path=root+"/code/htk.bin";Need(Size(path)==16,"INVALID OR MISSING CODE/HTK.BIN");
        File f(path);f.Read(0,key.data(),key.size());aes.Init(key,true);hash.Add(key.data(),key.size());Wipe(key.data(),key.size());identity=hash.Hex();
    }
    bool Mapped(uint64_t lba)const{for(const auto &r:ranges)if(lba>=r.start&&lba<uint64_t(r.start)+r.count)return true;return false;}
    bool Covered(uint64_t start,uint64_t count)const{
        uint64_t end=start+count;
        for(const auto &r:ranges){if(start>=r.start&&start<uint64_t(r.start)+r.count)start=std::min(end,uint64_t(r.start)+r.count);if(start==end)return true;}
        return false;
    }
    void Read(uint64_t off,void *out,size_t length){
        auto *dest=static_cast<Byte*>(out);
        while(length){
            uint32_t lba=static_cast<uint32_t>(off/Sector);const size_t within=off%Sector,n=std::min<size_t>(length,Sector-within);
            if(cached!=lba){
                data.fill(0);
                for(const auto &r:ranges)if(lba>=r.start&&uint64_t(lba)<uint64_t(r.start)+r.count){
                    uint64_t pos=512+(r.packed+lba-r.start)*Sector;size_t done=0;
                    while(done<Sector){const auto part=pos/Chunk,at=pos%Chunk,take=std::min<uint64_t>(Sector-done,Chunk-at);Need(part<files.size(),"MISSING NFS CHUNK");files[part]->Read(at,data.data()+done,take);done+=take;pos+=take;}
                    Key iv{};Put32(iv.data()+12,lba);aes.Crypt(data.data(),data.size(),iv,true);break;
                }
                cached=lba;
            }
            std::memcpy(dest,data.data()+within,n);dest+=n;off+=n;length-=n;
        }
    }
};
inline std::string PartPath(const std::string &base,unsigned index){return base+(index?".wbf"+std::to_string(index):".wbfs");}
struct Result {std::string id;uint64_t bytes=0;std::string hash;};

// Output is deliberately restricted to a fresh staging directory owned by the caller.
inline Result Convert(Source &source,const std::string &base,const Progress &progress){
    Tick(progress,"READING WII DISC",0,0);
    std::array<Byte,Sector> sector{};source.Read(0,sector.data(),sector.size());
    Result result;result.id.assign(reinterpret_cast<char*>(sector.data()),6);
    Need(result.id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos&&U32(sector.data()+24)==0x5d1c9ea3,"NFS KEY / WII DISC HEADER INVALID");
    Need(sector[0x60]==0,"HASHLESS WII DISC FORMAT IS NOT SUPPORTED");
    std::array<Byte,256> discHeader{};std::copy_n(sector.begin(),256,discHeader.begin());discHeader[0x61]=0;
    std::array<Byte,32> table{};source.Read(0x40000,table.data(),table.size());
    uint64_t partition=0;unsigned partitions=0;
    for(unsigned g=0;g<4;++g){
        auto count=U32(table.data()+g*8);const uint64_t at=uint64_t(U32(table.data()+g*8+4))*4;
        Need(count<=32&&(!count||(at>=0x40020&&at+count*8<=0x50000)),"INVALID WII PARTITION TABLE");
        for(unsigned p=0;p<count;++p){Byte entry[8];source.Read(at+p*8,entry,8);Need(U32(entry+4)==0,"ONLY GAME-PARTITION NFS IMAGES ARE SUPPORTED");partition=uint64_t(U32(entry))*4;++partitions;}
    }
    Need(partitions==1&&partition%Sector==0,"EXPECTED ONE WII GAME PARTITION");
    // A Wii partition starts with a 0x2a4-byte ticket followed by the
    // partition header up to the 0x20000-byte data boundary.  The size at
    // header+0x18 is the encrypted game-data length in 32-byte words.
    std::array<Byte,0x2a4> ticket{};source.Read(partition,ticket.data(),ticket.size());
    std::array<Byte,0x1fd5c> partitionHeader{};source.Read(partition+ticket.size(),partitionHeader.data(),partitionHeader.size());
    const uint64_t dataStart=partition+0x20000ULL,dataBytes=uint64_t(U32(partitionHeader.data()+0x18))*4;
    Need(dataStart%Sector==0&&dataBytes>0&&dataBytes%Sector==0&&dataStart+dataBytes<=uint64_t(MapCount)*Block,"INVALID WII PARTITION SIZE");
    // NFS is a sparse image.  HAI stores only allocated extents and leaves
    // unallocated sectors implicit; Source::Read() supplies zeroes for those
    // holes.  Requiring every sector of the disc partition to be present
    // rejects valid retail dumps (including Jungle Beat) whose partition has
    // large zero ranges at the end.
    Need(source.Covered(partition/Sector,1),"NFS IS MISSING GAME PARTITION HEADER");
    std::set<unsigned> blocks;
    for(const auto &r:source.ranges)for(unsigned b=r.start/64;b<=(uint64_t(r.start)+r.count-1)/64;++b)blocks.insert(b);
    result.bytes=(blocks.size()+1)*Block;
    std::vector<Byte> buffer(Block,0);
    std::memcpy(buffer.data(),"WBFS",4);Put32(buffer.data()+4,static_cast<uint32_t>(result.bytes/512));buffer[8]=9;buffer[9]=21;buffer[12]=1;
    std::copy(discHeader.begin(),discHeader.end(),buffer.begin()+512);
    unsigned physical=1;
    for(unsigned block:blocks){buffer[768+block*2]=Byte(physical>>8);buffer[769+block*2]=Byte(physical);++physical;}
    std::unique_ptr<File> out;uint64_t written=0;SHA digest;unsigned openPart=~0u;
    auto write=[&](const Byte *p,size_t n){
        while(n){const auto part=unsigned(written/Split);if(part!=openPart){if(out)out->Close();out=std::make_unique<File>(PartPath(base,part),"wb");openPart=part;}
            const auto take=std::min<uint64_t>(n,Split-written%Split);out->Write(p,take);digest.Add(p,take);written+=take;p+=take;n-=take;}
    };
    write(buffer.data(),buffer.size());
    for(unsigned block:blocks){
        for(unsigned i=0;i<64;++i){
            const uint64_t off=uint64_t(block)*Block+i*Sector;auto *p=buffer.data()+i*Sector;source.Read(off,p,Sector);
            if(off==0)p[0x61]=0;
        }
        write(buffer.data(),buffer.size());Tick(progress,"PREPARING WII GAME (ONE TIME)",written,result.bytes);
    }
    out->Close();out.reset();result.hash=digest.Hex();
    // Only the initial conversion is read back; subsequent launches use metadata.
    SHA verify;uint64_t checked=0;
    for(unsigned i=0;checked<result.bytes;++i){File f(PartPath(base,i));const auto n=std::min<uint64_t>(Split,result.bytes-checked);
        for(uint64_t at=0;at<n;){const auto take=std::min<uint64_t>(buffer.size(),n-at);f.Read(at,buffer.data(),take);verify.Add(buffer.data(),take);at+=take;checked+=take;Tick(progress,"CHECKING PREPARED COPY (ONE TIME)",checked,result.bytes);}}
    Need(verify.Hex()==result.hash,"PREPARED GAME READBACK FAILED - CHECK SD CARD");return result;
}
}
