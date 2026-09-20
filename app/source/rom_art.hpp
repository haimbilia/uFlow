#pragma once
// Read-only, bounded extraction. No emulation and no donor-title artwork.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace RomArt {
using Bytes=std::vector<unsigned char>;
inline uint32_t U32(const Bytes &b,size_t p,bool be=false){
    if(p>b.size()||b.size()-p<4)return 0;
    uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b[p+i])<<(8*(be?3-i:i));return v;
}
struct Reader {
    std::ifstream in;uint64_t size=0;
    explicit Reader(const std::string &path):in(path,std::ios::binary){if(in){in.seekg(0,std::ios::end);auto n=in.tellg();if(n>0)size=uint64_t(n);}}
    Bytes Read(uint64_t off,size_t n){
        if(n>4*1024*1024||off>size||n>size-off)return {};
        Bytes b(n);in.clear();in.seekg(off);in.read(reinterpret_cast<char*>(b.data()),n);return in?b:Bytes{};
    }
};
struct Image {int width=0,height=0;Bytes rgba;};
inline Image DS(Reader &r){
    const auto h=r.Read(0,0x160);if(h.empty())return {};
    // Reject arbitrary .nds files and absent/overlapping banner pointers.
    const auto arm9=U32(h,0x20),arm9Size=U32(h,0x2c),off=U32(h,0x68);
    if(arm9<0x200||!arm9Size||uint64_t(arm9)+arm9Size>r.size||off<0x200)return {};
    const auto b=r.Read(off,0x840);if(b.empty())return {};
    const unsigned version=b[0]|(b[1]<<8);if(version!=1&&version!=2&&version!=3&&version!=0x103)return {};
    unsigned crc=0xffff;
    for(size_t i=0x20;i<0x840;++i){crc^=b[i];for(int k=0;k<8;++k)crc=(crc>>1)^((crc&1)?0xa001:0);}
    if(crc!=unsigned(b[2]|(b[3]<<8)))return {};
    Image out{32,32,Bytes(32*32*4)};bool visible=false;
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        const auto tile=(y/8)*4+x/8;auto v=b[0x20+tile*32+(y%8)*4+(x%8)/2];
        const auto c=(v>>((x%2)*4))&15;const auto p=b[0x220+c*2]|(b[0x221+c*2]<<8);const auto dst=(y*32+x)*4;
        out.rgba[dst]=(p&31)*255/31;out.rgba[dst+1]=((p>>5)&31)*255/31;out.rgba[dst+2]=((p>>10)&31)*255/31;
        out.rgba[dst+3]=c?255:0;visible|=c!=0;
    }
    return visible?out:Image{};
}
inline Image GC(Reader &r){
    const auto h=r.Read(0,0x430);if(h.empty()||U32(h,0x1c,true)!=0xc2339f3d)return {};
    const auto off=U32(h,0x424,true),size=U32(h,0x428,true);
    if(off<0x430||size<12||size>4*1024*1024)return {};
    const auto fst=r.Read(off,size);if(fst.empty()||fst[0]!=1)return {};
    const auto count=U32(fst,8,true);if(!count||count>100000||uint64_t(count)*12>fst.size())return {};
    // opening.bnr lives in the disc root. Skip complete subdirectory ranges.
    for(uint32_t i=1;i<count;){
        const size_t e=i*12;
        if(fst[e]==1){const auto end=U32(fst,e+8,true);if(end<=i||end>count)return {};i=end;continue;}
        if(fst[e]!=0)return {};
        const uint64_t name=uint64_t(count)*12+(U32(fst,e,true)&0xffffff);
        const char target[]="opening.bnr";
        if(name+sizeof(target)<=fst.size()&&std::equal(target,target+sizeof(target),fst.begin()+name)){
            const auto length=U32(fst,e+8,true),pos=U32(fst,e+4,true);
            if((length!=0x1960&&length!=0x1fa0)||uint64_t(pos)+length>r.size)return {};
            const auto b=r.Read(pos,0x1820);if(b.empty()||b[0]!='B'||b[1]!='N'||b[2]!='R'||
                !((b[3]=='1'&&length==0x1960)||(b[3]=='2'&&length==0x1fa0)))return {};
            Image out{96,32,Bytes(96*32*4)};
            for(int y=0;y<32;++y)for(int x=0;x<96;++x){
                const auto p=0x20+(((y/4)*24+x/4)*16+(y%4)*4+x%4)*2;
                const unsigned v=(b[p]<<8)|b[p+1];const auto d=(y*96+x)*4;
                if(v&0x8000){out.rgba[d]=((v>>10)&31)*255/31;out.rgba[d+1]=((v>>5)&31)*255/31;out.rgba[d+2]=(v&31)*255/31;out.rgba[d+3]=255;}
                else {out.rgba[d]=((v>>8)&15)*17;out.rgba[d+1]=((v>>4)&15)*17;out.rgba[d+2]=(v&15)*17;out.rgba[d+3]=((v>>12)&7)*255/7;}
            }
            return out;
        }
        ++i;
    }
    return {};
}
// Fit small extracted art onto a neutral front, preserving its aspect ratio.
inline Bytes Card(const Image &image,bool square){
    if(image.rgba.size()!=size_t(image.width*image.height*4)||image.rgba.empty())return {};
    const int w=256,h=square?256:360;Bytes out(18+w*h*4,0);
    out[2]=2;out[12]=w&255;out[13]=w>>8;out[14]=h&255;out[15]=h>>8;out[16]=32;out[17]=0x28;
    const int scale=std::min(224/image.width,192/image.height),iw=image.width*scale,ih=image.height*scale;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const auto d=18+(y*w+x)*4;const unsigned shade=34+16*(h-y)/h;
        out[d]=shade+12;out[d+1]=shade+5;out[d+2]=shade;out[d+3]=255;
        const int sx=x-(w-iw)/2,sy=y-(h-ih)/2;
        if(sx>=0&&sx<iw&&sy>=0&&sy<ih){const auto p=((sy/scale)*image.width+sx/scale)*4;const auto a=image.rgba[p+3];
            for(int c=0;c<3;++c)out[d+c]=(image.rgba[p+2-c]*a+out[d+c]*(255-a))/255;
        }
    }
    return out;
}
inline std::string Cached(const std::string &rom,bool ds,const std::string &root){
    struct stat st{};if(stat(rom.c_str(),&st)||!S_ISREG(st.st_mode))return {};
    const std::string identity="rom-art-1/"+rom+"/"+std::to_string(st.st_size)+"/"+std::to_string(st.st_mtime)+(ds?"/ds":"/gc");
    uint64_t hash=14695981039346656037ULL;for(unsigned char c:identity){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream key;key<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    const std::string dir=root+"/cache/rom-art",path=dir+"/"+key.str()+".tga";
    const auto expected=18+256*(ds?256:360)*4;
    if(!lstat(path.c_str(),&st)&&S_ISREG(st.st_mode)&&st.st_size==expected){
        Reader cached(path);const auto h=cached.Read(0,18);
        if(h.size()==18&&h[2]==2&&h[12]==0&&h[13]==1&&h[14]==(ds?0:104)&&h[15]==1&&h[16]==32&&h[17]==0x28)return path;
    }
    Reader r(rom);const auto data=Card(ds?DS(r):GC(r),ds);if(data.empty())return {};
    for(const auto &p:{root+"/cache",dir}){
        if(lstat(p.c_str(),&st)==0){if(!S_ISDIR(st.st_mode))return {};}
        else if(mkdir(p.c_str(),0777))return {};
    }
    const auto temp=path+".tmp";if(lstat(temp.c_str(),&st)==0)return {};
    std::ofstream out(temp,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.flush();bool ok=bool(out);out.close();
    if(!ok||out.fail()||std::rename(temp.c_str(),path.c_str())){std::remove(temp.c_str());return {};}
    return path;
}
} // namespace RomArt
