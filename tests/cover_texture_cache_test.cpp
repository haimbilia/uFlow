#include "cover_display_cache.hpp"
#include "cover_texture_cache.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
int main(){
    using namespace CoverTextureCache;
    auto root=fs::temp_directory_path()/"uflow-cover-texture-test";
    fs::create_directories(root);fs::current_path(root);
    fs::create_directories("fs:/vol/external01/wiiu/apps/uFlow/cache");
    std::ofstream("source.png")<<"identity";
    auto *s=SDL_CreateRGBSurfaceWithFormat(0,1025,513,32,SDL_PIXELFORMAT_RGBA32);assert(s);
    for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){
        auto *p=(unsigned char*)s->pixels+y*s->pitch+x*4;
        p[0]=x*255/(s->w-1);p[1]=y*255/(s->h-1);p[2]=127;p[3]=(x<8?0:255);
    }
    const auto file=File("source.png");auto *built=Build(s,file,2048);assert(Packed(built));
    assert(Width(built)==1025 && Height(built)==513 && Levels(built)==11);
    assert(ValidHeader((unsigned char*)built->pixels,Get((unsigned char*)built->pixels+16)));
    assert(fs::exists(file) && !fs::exists(file+".tmp"));
    auto *loaded=Read(file,512);assert(loaded && Width(loaded)==512 && Height(loaded)==256);
    assert(Get((unsigned char*)loaded->pixels+16)<1025u*513u*4/4);
    auto *expanded=Expand(built);assert(expanded);
    double error=0;unsigned count=0;
    for(int y=0;y<s->h;++y)for(int x=8;x<s->w;++x)for(int c=0;c<3;++c){
        int d=((unsigned char*)s->pixels)[y*s->pitch+x*4+c]-((unsigned char*)expanded->pixels)[y*expanded->pitch+x*4+c];error+=d*d;++count;
    }
    assert(error/count<30); // smooth color field remains visually faithful
    assert(((unsigned char*)expanded->pixels)[3]==0);
    SDL_FreeSurface(expanded);SDL_FreeSurface(loaded);SDL_FreeSurface(built);
    // Cached reads reject corruption, truncation, malformed geometry/counts.
    std::ifstream in(file,std::ios::binary);std::vector<unsigned char> original((std::istreambuf_iterator<char>(in)),{});in.close();
    auto write=[&](const auto &b){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write((const char*)b.data(),b.size());};
    for(int mutation=0;mutation<5;++mutation){auto b=original;
        if(mutation==0)b.back()^=1;
        if(mutation==1)b.resize(b.size()-1);
        if(mutation==2)Put(b.data()+4,8192);
        if(mutation==3)Put(b.data()+12,1000);
        if(mutation==4)Put(b.data()+32,0xffffffff);
        write(b);assert(!Read(file,2048));
    }
    write(original);assert(File("source.png")==file);
    std::ofstream("source.png",std::ios::app)<<"changed";assert(File("source.png")!=file);
    // Cache write failure does not discard the usable prepared cover.
    auto *unsaved=Build(s,"missing-parent/cache.uft",768);assert(unsaved && Width(unsaved)==1025);
    SDL_FreeSurface(unsaved);
    const auto display=CoverDisplayCache::File("source.png",2048);
    CoverDisplayCache::Save(display,s);
    auto *roundtrip=CoverDisplayCache::Read(display,2048);
    assert(roundtrip && roundtrip->w==s->w && roundtrip->h==s->h);
    assert(std::memcmp(s->pixels,roundtrip->pixels,s->pitch*s->h)==0);
    SDL_FreeSurface(roundtrip);
    assert(!CoverDisplayCache::Read(display,512));
    std::ifstream displayIn(display,std::ios::binary);
    std::vector<unsigned char> displayBytes((std::istreambuf_iterator<char>(displayIn)),{});
    displayIn.close();
    for(int mutation=0;mutation<5;++mutation) {
        auto b=displayBytes;
        if(mutation==0)b.back()^=1;
        if(mutation==1)b.resize(b.size()-1);
        if(mutation==2)Put(b.data()+4,0xffffffff);
        if(mutation==3)Put(b.data()+16,0xffffffff);
        if(mutation==4)b.push_back(1);
        {std::ofstream out(display,std::ios::binary|std::ios::trunc);out.write((const char*)b.data(),b.size());}
        assert(!CoverDisplayCache::Read(display,2048));
    }
    CoverDisplayCache::Save("missing-parent/display.cache",s);
    SDL_FreeSurface(s);
    std::cout<<"PASS: lossless display-cache round trip, alpha, geometry and corruption bounds\n";
    std::cout<<"PASS: BC3 quality/alpha, odd dimensions, complete mip chain, bounded cached reads, checksums, corruption/truncation rejection, source invalidation and read-only fallback\n";
}
