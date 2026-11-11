#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <cmath>

// SDL's 2D geometry API discards clip W. Keep it beside each vertex and
// subdivide in the original surface coordinates, preserving UVs through spins.
namespace PerspectiveGeometry {
struct Vertex { SDL_Vertex v; float w; };
inline Vertex Mid(const Vertex &a,const Vertex &b) {
    const float sum=a.w+b.w;
    auto channel=[](Uint8 x,Uint8 y){return Uint8((unsigned(x)+y)/2);};
    return {{{(a.v.position.x*a.w+b.v.position.x*b.w)/sum,
               (a.v.position.y*a.w+b.v.position.y*b.w)/sum},
             {channel(a.v.color.r,b.v.color.r),channel(a.v.color.g,b.v.color.g),
              channel(a.v.color.b,b.v.color.b),channel(a.v.color.a,b.v.color.a)},
             {(a.v.tex_coord.x+b.v.tex_coord.x)*.5f,(a.v.tex_coord.y+b.v.tex_coord.y)*.5f}},sum*.5f};
}
inline float Error(const Vertex &a,const Vertex &b) {
    const float factor=(a.w-b.w)/(2.f*(a.w+b.w));
    const float dx=(a.v.position.x-b.v.position.x)*factor,dy=(a.v.position.y-b.v.position.y)*factor;
    return dx*dx+dy*dy;
}
template<class Emit>
inline void Triangle(Vertex a,Vertex b,Vertex c,Emit &emit,int depth=0) {
    const float ab=Error(a,b),bc=Error(b,c),ca=Error(c,a);
    if(depth==10 || (ab<=.04f && bc<=.04f && ca<=.04f)){emit(a.v,b.v,c.v);return;}
    if(ab>=bc && ab>=ca){auto m=Mid(a,b);Triangle(a,m,c,emit,depth+1);Triangle(m,b,c,emit,depth+1);}
    else if(bc>=ca){auto m=Mid(b,c);Triangle(a,b,m,emit,depth+1);Triangle(a,m,c,emit,depth+1);}
    else {auto m=Mid(c,a);Triangle(a,b,m,emit,depth+1);Triangle(m,b,c,emit,depth+1);}
}
inline int Draw(SDL_Renderer *r,SDL_Texture *texture,const SDL_Vertex *v,const float *w,int count,const int *indices=nullptr,int indexCount=0) {
    if(!texture)return SDL_RenderGeometry(r,texture,v,count,indices,indexCount);
    // Render-thread scratch, fixed capacity: no per-frame allocation or huge stack.
    static std::array<SDL_Vertex,3072> buffer;
    int used=0,result=0;
    const auto flush=[&](){if(used){if(SDL_RenderGeometry(r,texture,buffer.data(),used,nullptr,0)<0)result=-1;used=0;}};
    auto emit=[&](const SDL_Vertex &a,const SDL_Vertex &b,const SDL_Vertex &c){
        if(used+3>int(buffer.size()))flush();buffer[used++]=a;buffer[used++]=b;buffer[used++]=c;
    };
    const int n=indices?indexCount:count;
    for(int i=0;i+2<n;i+=3){const int a=indices?indices[i]:i,b=indices?indices[i+1]:i+1,c=indices?indices[i+2]:i+2;
        if(w[a]>.001f&&w[b]>.001f&&w[c]>.001f)Triangle({v[a],w[a]},{v[b],w[b]},{v[c],w[c]},emit);
        else emit(v[a],v[b],v[c]);
    }
    flush();return result;
}
}
