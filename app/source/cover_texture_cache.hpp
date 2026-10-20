#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef UFLOW_HOST_PREVIEW
#include <atomic>
#endif
#include <sys/stat.h>
#include <zlib.h>

#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include "../vendor/stb/stb_dxt.h"

// Portable little-endian BC3 blocks, including every mip. No GPU calls or
// shared renderer state: construction and reads belong to ArtworkWorker.
namespace CoverTextureCache {
inline constexpr unsigned Header=176, MaxLevels=12, MaxEdge=2048;
inline constexpr std::uint32_t Magic=0x31434655; // UFC1
inline char surfaceTag;
#ifdef UFLOW_HOST_PREVIEW
inline std::atomic<std::size_t> payloadBytesRead{0};
#endif
inline unsigned Get(const unsigned char *p) { return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24; }
inline void Put(unsigned char *p,unsigned v) { for(int i=0;i<4;++i)p[i]=v>>(8*i); }
inline unsigned Bytes(unsigned w,unsigned h) {return ((w+3)/4)*((h+3)/4)*16;}
inline bool Packed(SDL_Surface *s) {return s && s->userdata==&surfaceTag;}
inline unsigned Width(SDL_Surface *s) {return Packed(s)?Get((unsigned char*)s->pixels+4):unsigned(s->w);}
inline unsigned Height(SDL_Surface *s) {return Packed(s)?Get((unsigned char*)s->pixels+8):unsigned(s->h);}
inline unsigned Levels(SDL_Surface *s) {return Get((unsigned char*)s->pixels+12);}
inline std::string File(const std::string &path) {
    struct stat st{};if(stat(path.c_str(),&st) || !S_ISREG(st.st_mode))return {};
    const auto identity=path+"|"+std::to_string(st.st_size)+"|"+std::to_string(st.st_mtime)+"|bc3-mips-v1";
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:identity){hash^=c;hash*=1099511628211ULL;}
    char name[24];std::snprintf(name,sizeof(name),"%016llx",(unsigned long long)hash);
    return std::string("fs:/vol/external01/wiiu/apps/uFlow/cache/cover-textures-v1/")+std::string(name,2)+"/"+name+".uft";
}
inline SDL_Surface *Carrier(const std::vector<unsigned char> &blob) {
    auto *s=SDL_CreateRGBSurfaceWithFormat(0,256,(blob.size()+1023)/1024,32,SDL_PIXELFORMAT_RGBA32);
    if(s){std::memcpy(s->pixels,blob.data(),blob.size());s->userdata=&surfaceTag;}
    return s;
}
inline bool ValidHeader(const unsigned char *b,unsigned length) {
    unsigned w=Get(b+4),h=Get(b+8),n=Get(b+12),offset=Header;
    if(Get(b)!=Magic || !w || !h || w>MaxEdge || h>MaxEdge || !n || n>MaxLevels)return false;
    for(unsigned i=0;i<n;++i){
        const auto *p=b+32+i*12;
        if(Get(p)!=offset || Get(p+4)!=Bytes(w,h))return false;
        offset+=Get(p+4);w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    return offset==length && Get(b+16)==length;
}
inline SDL_Surface *Read(const std::string &file,unsigned edge) {
    if(file.empty())return nullptr;
    FILE *f=std::fopen(file.c_str(),"rb");if(!f)return nullptr;
    std::array<unsigned char,Header> header{};
    if(std::fseek(f,0,SEEK_END)){std::fclose(f);return nullptr;}
    const long length=std::ftell(f);std::rewind(f);
    if(length<long(Header) || length>6*1024*1024 || std::fread(header.data(),1,Header,f)!=Header || !ValidHeader(header.data(),length)){
        std::fclose(f);return nullptr;
    }
    unsigned w=Get(header.data()+4),h=Get(header.data()+8),n=Get(header.data()+12),first=0;
    // Choose a mip at least as sharp as the requested edge; never undershoot
    // by a whole level as the old JPEG reduction path did.
    while(first+1<n && std::max(w,h)/2>=edge){++first;w=std::max(1u,w/2);h=std::max(1u,h/2);}
    const unsigned start=Get(header.data()+32+first*12),size=Header+unsigned(length)-start;
    std::vector<unsigned char> blob(size);
    std::copy(header.begin(),header.end(),blob.begin());
    Put(blob.data()+4,w);Put(blob.data()+8,h);Put(blob.data()+12,n-first);Put(blob.data()+16,size);
    bool ok=std::fseek(f,start,SEEK_SET)==0 && std::fread(blob.data()+Header,1,size-Header,f)==size-Header;
#ifdef UFLOW_HOST_PREVIEW
    if(ok)payloadBytesRead+=size-Header;
#endif
    std::fclose(f);
    for(unsigned i=0;ok && i<n-first;++i){
        const auto *old=header.data()+32+(i+first)*12;auto *p=blob.data()+32+i*12;
        const unsigned offset=Header+Get(old)-start,bytes=Get(old+4);
        Put(p,offset);Put(p+4,bytes);Put(p+8,Get(old+8));
        ok=crc32(0,blob.data()+offset,bytes)==Get(p+8);
    }
    return ok?Carrier(blob):nullptr;
}
inline void Save(const std::string &file,const std::vector<unsigned char> &blob) {
    if(file.empty())return;
    const std::string base="fs:/vol/external01/wiiu/apps/uFlow/cache";
    mkdir(base.c_str(),0777);mkdir((base+"/cover-textures-v1").c_str(),0777);
    mkdir(file.substr(0,file.find_last_of('/')).c_str(),0777);
    const auto temp=file+".tmp";FILE *f=std::fopen(temp.c_str(),"wb");if(!f)return;
    bool ok=std::fwrite(blob.data(),1,blob.size(),f)==blob.size();if(std::fclose(f))ok=false;
    if(ok)std::rename(temp.c_str(),file.c_str());std::remove(temp.c_str());
}
inline std::vector<unsigned char> Encode(SDL_Surface *source) {
    if(!source || source->w<=0 || source->h<=0 || source->w>int(MaxEdge) || source->h>int(MaxEdge))return {};
    auto *rgba=source->format->format==SDL_PIXELFORMAT_RGBA32?source:SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0);if(!rgba)return {};
    unsigned w=rgba->w,h=rgba->h,n=0;
    std::vector<unsigned char> pixels(std::size_t(w)*h*4),blob(Header);
    for(unsigned y=0;y<h;++y)std::memcpy(pixels.data()+y*w*4,(unsigned char*)rgba->pixels+y*rgba->pitch,w*4);
    if(rgba!=source)SDL_FreeSurface(rgba);
    unsigned total=Header;for(unsigned mw=w,mh=h;;mw=std::max(1u,mw/2),mh=std::max(1u,mh/2)){total+=Bytes(mw,mh);if(mw==1 && mh==1)break;}
    blob.reserve(total);
    Put(blob.data(),Magic);Put(blob.data()+4,w);Put(blob.data()+8,h);
    for(;;){
        const unsigned offset=blob.size(),bytes=Bytes(w,h);blob.resize(offset+bytes);
        unsigned char block[64];unsigned cursor=offset;
        for(unsigned y=0;y<h;y+=4)for(unsigned x=0;x<w;x+=4){
            for(unsigned j=0;j<4;++j) {
                const auto *row=pixels.data()+std::min(y+j,h-1)*w*4;
                if(x+4<=w)std::memcpy(block+j*16,row+x*4,16);
                else for(unsigned i=0;i<4;++i)std::memcpy(block+(j*4+i)*4,row+std::min(x+i,w-1)*4,4);
            }
            stb_compress_dxt_block(blob.data()+cursor,block,1,STB_DXT_HIGHQUAL);cursor+=16;
        }
        auto *entry=blob.data()+32+n*12;Put(entry,offset);Put(entry+4,bytes);Put(entry+8,crc32(0,blob.data()+offset,bytes));++n;
        if(w==1 && h==1)break;
        const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);
        std::vector<unsigned char> small(std::size_t(nw)*nh*4);
        for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
            unsigned sums[4]={},alpha=0,count=0;
            // Area averaging, with alpha-weighted RGB to prevent dark fringes.
            for(unsigned sy=y*h/nh;sy<(y+1)*h/nh;++sy)for(unsigned sx=x*w/nw;sx<(x+1)*w/nw;++sx){
                const auto *p=pixels.data()+(sy*w+sx)*4;alpha+=p[3];++count;
                for(int c=0;c<3;++c)sums[c]+=unsigned(p[c])*p[3];
            }
            auto *p=small.data()+(y*nw+x)*4;
            for(int c=0;c<3;++c)p[c]=alpha?(sums[c]+alpha/2)/alpha:0;p[3]=(alpha+count/2)/count;
        }
        pixels=std::move(small);w=nw;h=nh;
    }
    Put(blob.data()+12,n);Put(blob.data()+16,blob.size());return blob;
}
inline SDL_Surface *Build(SDL_Surface *source,const std::string &file,unsigned edge) {
    auto blob=Encode(source);if(blob.empty())return nullptr;Save(file,blob);
    // A read-only/full SD still displays the newly prepared texture.
    unsigned w=Get(blob.data()+4),h=Get(blob.data()+8),n=Get(blob.data()+12),first=0;
    while(first+1<n && std::max(w,h)/2>=edge){++first;w=std::max(1u,w/2);h=std::max(1u,h/2);}
    if(!first)return Carrier(blob);
    const unsigned start=Get(blob.data()+32+first*12);
    std::vector<unsigned char> subset(blob.size()-start+Header);std::copy_n(blob.begin(),Header,subset.begin());
    std::copy(blob.begin()+start,blob.end(),subset.begin()+Header);
    Put(subset.data()+4,w);Put(subset.data()+8,h);Put(subset.data()+12,n-first);Put(subset.data()+16,subset.size());
    for(unsigned i=0;i<n-first;++i){const auto *p=blob.data()+32+(i+first)*12;auto *q=subset.data()+32+i*12;std::copy_n(p,12,q);Put(q,Header+Get(p)-start);}
    return Carrier(subset);
}
// Expand cached mip blocks on ArtworkWorker on every platform. GPU uploads use
// ordinary SDL surfaces; the experimental native BC3 importer is not linked.
inline SDL_Surface *Expand(SDL_Surface *packed) {
    const unsigned w=Width(packed),h=Height(packed);auto *out=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);if(!out)return nullptr;
    const auto *src=(unsigned char*)packed->pixels+Header;
    for(unsigned y=0;y<h;y+=4)for(unsigned x=0;x<w;x+=4,src+=16){
        unsigned a[8]={src[0],src[1]},rgb[4][3]{};
        if(a[0]>a[1])for(int i=1;i<=6;++i)a[i+1]=((7-i)*a[0]+i*a[1])/7;
        else {for(int i=1;i<=4;++i)a[i+1]=((5-i)*a[0]+i*a[1])/5;a[6]=0;a[7]=255;}
        std::uint64_t ai=0;for(int i=0;i<6;++i)ai|=std::uint64_t(src[2+i])<<(8*i);
        for(int i=0;i<2;++i){unsigned c=src[8+2*i]|unsigned(src[9+2*i])<<8;rgb[i][0]=((c>>11)*255+15)/31;rgb[i][1]=(((c>>5)&63)*255+31)/63;rgb[i][2]=((c&31)*255+15)/31;}
        for(int c=0;c<3;++c){rgb[2][c]=(2*rgb[0][c]+rgb[1][c])/3;rgb[3][c]=(rgb[0][c]+2*rgb[1][c])/3;}
        unsigned ci=Get(src+12);
        for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i){unsigned k=j*4+i;if(x+i>=w || y+j>=h)continue;
            auto *p=(unsigned char*)out->pixels+(y+j)*out->pitch+(x+i)*4;
            for(int c=0;c<3;++c)p[c]=rgb[(ci>>(2*k))&3][c];p[3]=a[(ai>>(3*k))&7];
        }
    }
    return out;
}
}
