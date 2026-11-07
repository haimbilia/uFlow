#include "skin_surface.hpp"
#include <cassert>
#include <iostream>
int main(){
    assert(SDL_Init(SDL_INIT_VIDEO)==0);auto*s=SDL_CreateRGBSurfaceWithFormat(0,128,96,32,SDL_PIXELFORMAT_RGBA32);assert(s);
    auto*r=SDL_CreateSoftwareRenderer(s);assert(r);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
    auto*t=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,16,16);assert(t);
    Uint32 pixels[256];for(auto&p:pixels)p=SDL_MapRGBA(s->format,40,200,80,255);SDL_UpdateTexture(t,nullptr,pixels,64);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
    SkinSurface::Fill(r,{20,20,80,50},16,{255,255,255,255},{255,255,255,255},t);
    SDL_RenderFlush(r);
    auto pixel=[&](int x,int y){SDL_Color c{};auto*p=reinterpret_cast<Uint32*>(static_cast<char*>(s->pixels)+y*s->pitch);SDL_GetRGBA(p[x],s->format,&c.r,&c.g,&c.b,&c.a);return c;};
    assert(pixel(20,20).g==0);assert(pixel(60,40).g>190);assert(pixel(60,20).g>100);
    SkinSurface::Border(r,{20,20,80,50},16,{255,190,75,255},true);SDL_RenderFlush(r);
    assert(pixel(60,18).r>0&&pixel(60,5).r==0);
    SDL_BlendMode mode;SDL_GetRenderDrawBlendMode(r,&mode);assert(mode==SDL_BLENDMODE_BLEND);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)
        pixels[y*16+x]=SDL_MapRGBA(s->format,(x<8&&y<8)||(x>=8&&y>=8)?255:0,x>=8?255:0,y>=8?255:0,255);
    SDL_UpdateTexture(t,nullptr,pixels,64);SDL_SetTextureScaleMode(t,SDL_ScaleModeNearest);
    SkinSurface::Fill(r,{20,20,80,50},0,{255,255,255,255},{255,255,255,255},t,{0,0,1,1},1);SDL_RenderFlush(r);
    assert(pixel(40,30).b>240&&pixel(40,30).r<10);
    SkinSurface::Fill(r,{20,20,80,50},0,{255,255,255,255},{255,255,255,255},t,{0,0,1,1},-1);SDL_RenderFlush(r);
    assert(pixel(40,30).g>240&&pixel(40,30).b<10);
    SDL_DestroyTexture(t);SDL_DestroyRenderer(r);SDL_FreeSurface(s);SDL_Quit();
    std::cout<<"PASS: rounded texture corners, retained center artwork, feathered border and unchanged blend mode\n";
}
