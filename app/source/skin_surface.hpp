#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <algorithm>
#include <cmath>

// Small, fixed meshes for opt-in scene surfaces; no render targets or allocations.
namespace SkinSurface {
constexpr int Steps=6,Count=4*(Steps+1);
inline SDL_FPoint Point(SDL_FRect r,float radius,int i){
    radius=std::clamp(radius,0.f,std::min(r.w,r.h)*.5f);
    const int corner=i/(Steps+1);
    static const auto arcs=[](){std::array<SDL_FPoint,Count> result{};for(int n=0;n<Count;++n){
        const float angle=(-90.f+(n/(Steps+1))*90.f+(n%(Steps+1))*90.f/Steps)*.01745329252f;
        result[n]={std::cos(angle),std::sin(angle)};}return result;}();
    return {r.x+(corner<2?r.w-radius:radius)+radius*arcs[i].x,
            r.y+(corner==0||corner==3?radius:r.h-radius)+radius*arcs[i].y};
}
inline SDL_FRect Expand(SDL_FRect r,float v){return {r.x-v,r.y-v,r.w+2*v,r.h+2*v};}
inline SDL_Color Mix(SDL_Color a,SDL_Color b,float t){t=std::clamp(t,0.f,1.f);return {Uint8(a.r+(b.r-a.r)*t),Uint8(a.g+(b.g-a.g)*t),Uint8(a.b+(b.b-a.b)*t),Uint8(a.a+(b.a-a.a)*t)};}
inline void Fill(SDL_Renderer*r,SDL_FRect box,float radius,SDL_Color top,SDL_Color bottom,SDL_Texture*texture=nullptr,SDL_FRect uv={0,0,1,1},int rotation=0){
    std::array<SDL_Vertex,1+Count*2> v{};std::array<int,Count*9> indices{};
    auto vertex=[&](SDL_FPoint p,bool edge){float x=(p.x-box.x)/box.w,y=(p.y-box.y)/box.h;auto c=Mix(top,bottom,y);if(edge)c.a=0;
        x=std::clamp(x,0.f,1.f);y=std::clamp(y,0.f,1.f);
        if(rotation){const float old=x;x=rotation>0?y:1-y;y=rotation>0?1-old:old;}
        return SDL_Vertex{p,c,{uv.x+uv.w*x,uv.y+uv.h*y}};};
    v[0]=vertex({box.x+box.w/2,box.y+box.h/2},false);
    for(int i=0;i<Count;++i){v[1+i]=vertex(Point(box,radius,i),false);v[1+Count+i]=vertex(Point(Expand(box,1),radius+1,i),true);
        const int a=1+i,b=1+(i+1)%Count,c=a+Count,d=b+Count;int k=i*9;
        for(int n:{0,a,b,a,c,b,b,c,d})indices[k++]=n;}
    SDL_RenderGeometry(r,texture,v.data(),int(v.size()),indices.data(),int(indices.size()));
}
inline void Border(SDL_Renderer*r,SDL_FRect box,float radius,SDL_Color color,bool glow){
    std::array<SDL_Vertex,Count*5> v{};std::array<int,Count*24> indices{};
    const float offsets[]={glow?14.f:1.5f,glow?5.f:0.f,glow?1.f:-1.5f,-.6f,-1.8f};
    const int alpha[]={0,glow?20:150,glow?180:0,180,0};const int bands=glow?5:3;
    for(int band=0;band<bands;++band)for(int i=0;i<Count;++i){auto c=color;c.a=Uint8(int(c.a)*alpha[band]/255);v[band*Count+i]={Point(Expand(box,offsets[band]),std::max(0.f,radius+offsets[band]),i),c,{0,0}};}
    int k=0;for(int band=0;band<bands-1;++band)for(int i=0;i<Count;++i){int a=band*Count+i,b=band*Count+(i+1)%Count,c=a+Count,d=b+Count;for(int n:{a,b,c,b,d,c})indices[k++]=n;}
    SDL_RenderGeometry(r,nullptr,v.data(),bands*Count,indices.data(),k);
}
}
