#pragma once
#include "cover_texture_cache.hpp"
// Lossless, display-sized disk cache. SDL receives ordinary RGBA surfaces;
// no private renderer ABI, full mip generation, or GPU access on the worker.
namespace CoverDisplayCache {
inline std::string File(const std::string &source,unsigned edge) {
    auto name=CoverTextureCache::File(source);
    return name.empty()?name:name+".rgba"+std::to_string(edge);
}
inline SDL_Surface *Read(const std::string &file,unsigned edge) {
    if(file.empty())return nullptr;
    FILE *f=std::fopen(file.c_str(),"rb");if(!f)return nullptr;
    unsigned char h[24]{};
    if(std::fread(h,1,sizeof h,f)!=sizeof h){std::fclose(f);return nullptr;}
    const auto w=CoverTextureCache::Get(h+4),height=CoverTextureCache::Get(h+8),bytes=CoverTextureCache::Get(h+12),packed=CoverTextureCache::Get(h+16);
    if(CoverTextureCache::Get(h)!=0x31444752 || !w || !height || w>edge || height>edge || edge>2048 ||
       bytes!=w*height*4 || !packed || packed>compressBound(bytes)) {std::fclose(f);return nullptr;}
    std::vector<unsigned char> input(packed);
    bool ok=std::fread(input.data(),1,packed,f)==packed && std::fgetc(f)==EOF;std::fclose(f);
    if(!ok)return nullptr;
    auto *s=SDL_CreateRGBSurfaceWithFormat(0,w,height,32,SDL_PIXELFORMAT_RGBA32);if(!s)return nullptr;
    uLongf actual=bytes;
    ok=s->pitch==int(w*4) && uncompress((Bytef*)s->pixels,&actual,input.data(),packed)==Z_OK && actual==bytes &&
        crc32(0,(Bytef*)s->pixels,bytes)==CoverTextureCache::Get(h+20);
    if(!ok){SDL_FreeSurface(s);return nullptr;}return s;
}
inline void Save(const std::string &file,SDL_Surface *surface) {
    if(file.empty() || !surface || surface->w>2048 || surface->h>2048)return;
    auto *s=surface->format->format==SDL_PIXELFORMAT_RGBA32?surface:SDL_ConvertSurfaceFormat(surface,SDL_PIXELFORMAT_RGBA32,0);
    if(!s)return;
    const unsigned bytes=s->w*s->h*4;
    if(s->pitch!=s->w*4){if(s!=surface)SDL_FreeSurface(s);return;}
    uLongf length=compressBound(bytes);std::vector<unsigned char> blob(24+length);
    if(compress2(blob.data()+24,&length,(Bytef*)s->pixels,bytes,Z_BEST_SPEED)==Z_OK) {
        CoverTextureCache::Put(blob.data(),0x31444752);CoverTextureCache::Put(blob.data()+4,s->w);
        CoverTextureCache::Put(blob.data()+8,s->h);CoverTextureCache::Put(blob.data()+12,bytes);
        CoverTextureCache::Put(blob.data()+16,length);CoverTextureCache::Put(blob.data()+20,crc32(0,(Bytef*)s->pixels,bytes));
        blob.resize(24+length);CoverTextureCache::Save(file,blob);
    }
    if(s!=surface)SDL_FreeSurface(s);
}
}
