#pragma once
#include "ps1_jewel_data.hpp"
#include <SDL2/SDL.h>
#include <png.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Ps1Jewel {
inline float DiscAngle(unsigned milliseconds){return (milliseconds%3000)*6.28318530718f/3000.f;}
struct Art {SDL_Texture *texture=nullptr;float u0=0,u1=1;};
class Materials {
    SDL_Texture *shell_=nullptr,*disc_=nullptr;
    static SDL_Texture *Load(SDL_Renderer *r,const unsigned char *data,size_t size){
        png_image im{};im.version=PNG_IMAGE_VERSION;
        if(!png_image_begin_read_from_memory(&im,data,size))return nullptr;
        im.format=PNG_FORMAT_RGBA;std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(im));
        if(!png_image_finish_read(&im,nullptr,pixels.data(),0,nullptr)){png_image_free(&im);return nullptr;}
        auto *t=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,im.width,im.height);
        if(t){SDL_UpdateTexture(t,nullptr,pixels.data(),im.width*4);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(t,SDL_ScaleModeLinear);}
        png_image_free(&im);return t;
    }
public:
    ~Materials(){SDL_DestroyTexture(shell_);SDL_DestroyTexture(disc_);}
    SDL_Texture *Shell(SDL_Renderer *r){if(!shell_)shell_=Load(r,ShellPng,sizeof(ShellPng));return shell_;}
    SDL_Texture *Disc(SDL_Renderer *r){if(!disc_)disc_=Load(r,DiscPng,sizeof(DiscPng));return disc_;}
};

// The supplied FBX's 13 quads retain their UVs and material assignments.
// Projection is supplied by the layout, so its shared camera also works here.
template<class Project>
void Draw(SDL_Renderer *r,Project project,Materials &materials,bool media,Art front,Art back,Art spine,
          SDL_Texture *discArt,float angle=0,float opacity=1.f,bool branded=true,SDL_Color material={255,255,255,255},bool tinted=false){
    const auto tint=[&](SDL_Color fallback){return tinted?SDL_Color{material.r,material.g,material.b,fallback.a}:fallback;};
    const Uint8 alpha=Uint8(255*std::clamp(opacity,0.f,1.f));
    SDL_BlendMode old;SDL_GetRenderDrawBlendMode(r,&old);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    const int indices[]={0,1,2,0,2,3};
    const auto quad=[&](const Face &f,SDL_Texture *texture,SDL_Color ink){
        SDL_Vertex v[4];for(int k=0;k<4;++k){const auto &c=f.c[k];v[k]={project(c.p.x,c.p.y,c.p.z).point,ink,{c.u,c.v}};}
        const float cross=(v[1].position.x-v[0].position.x)*(v[2].position.y-v[0].position.y)-(v[1].position.y-v[0].position.y)*(v[2].position.x-v[0].position.x);
        if(cross>=0)return;
        SDL_RenderGeometry(r,texture,v,4,indices,6);
    };
    std::array<int,13> order{};for(int i=0;i<13;++i)order[i]=i;
    const auto depth=[&](int i){float z=0;for(const auto &c:Faces[i].c)z+=project(c.p.x,c.p.y,c.p.z).depth;return z;};
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return depth(a)<depth(b);});
    for(int i:order)if(Faces[i].material==1)quad(Faces[i],nullptr,tint({25,27,29,alpha}));
    if(media){
        auto *texture=discArt?discArt:materials.Disc(r);
        const auto a=project(-.39f,-.48f,-.036f).point,b=project(.48f,-.48f,-.036f).point,c=project(.48f,.49f,-.036f).point;
        if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<0){
            constexpr int segments=64;SDL_Vertex vertices[(segments+1)*2];int triangles[segments*6];
            for(int i=0;i<=segments;++i)for(int inner=0;inner<2;++inner){
                const float theta=i*6.28318530718f/segments,radius=inner?.145f:1.f;
                const float x=std::cos(theta)*radius,y=std::sin(theta)*radius;
                const float u=.5f+.5f*std::cos(theta-angle)*radius,v=.5f-.5f*std::sin(theta-angle)*radius;
                vertices[i*2+inner]={project(.0456f+x*.430f,.0055f+y*.489f,-.036f).point,{248,248,248,alpha},{u,v}};
                if(i<segments){int *t=triangles+i*6;t[0]=i*2;t[1]=i*2+2;t[2]=i*2+1;t[3]=i*2+1;t[4]=i*2+2;t[5]=i*2+3;}
            }
            SDL_RenderGeometry(r,texture,vertices,(segments+1)*2,triangles,segments*6);
        }
    }else{
        const float left=branded?-.36f:-.48f;
        const Face cover{{{{left,-.48f,.48f},front.u0,1},{{.49f,-.48f,.48f},front.u1,1},{{.49f,.48f,.48f},front.u1,0},{{left,.48f,.48f},front.u0,0}},0};
        quad(cover,front.texture,front.texture?SDL_Color{255,255,255,alpha}:tint({48,50,54,alpha}));
    }
    if(back.texture){const Face cover{{{{.48f,-.48f,-.49f},back.u0,1},{{-.48f,-.48f,-.49f},back.u1,1},{{-.48f,.48f,-.49f},back.u1,0},{{.48f,.48f,-.49f},back.u0,0}},0};quad(cover,back.texture,{255,255,255,alpha});}
    for(int i:order)if(Faces[i].material==0){
        if((i==0||i==5)&&spine.texture){
            auto face=Faces[i];
            for(auto &c:face.c){
                const float across=i==0?c.p.z+.5f:.5f-c.p.z;
                c.u=spine.u0+across*(spine.u1-spine.u0);c.v=.5f-c.p.y;
            }
            quad(face,spine.texture,{225,225,225,alpha});
            continue;
        }
        // The transparent front/back windows sit over artwork: keep them neutral.
        const bool window=i==1||i==2;
        if(branded)quad(Faces[i],materials.Shell(r),window?SDL_Color{255,255,255,alpha}:tint({255,255,255,alpha}));
        else quad(Faces[i],nullptr,window?SDL_Color{205,224,234,Uint8(alpha*.10f)}:tint({205,224,234,Uint8(alpha*(tinted?.65f:.10f))}));
    }
    SDL_SetRenderDrawBlendMode(r,old);
}
}
