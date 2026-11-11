#pragma once
#include "perspective_geometry.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

namespace DiscModel {
// A two-sided 120/80 mm optical disc, with a 15 mm hole and 1.2 mm edge.
// The label texture is a cached square; lighting belongs to the silver side.
inline void Draw(SDL_Renderer *renderer,SDL_Rect rect,SDL_Texture *label,bool mini,
                 float yaw,float pitch,float roll,float opacity) {
    constexpr int segments=128;
    constexpr float pi=3.14159265359f;
    const float diameter=mini?80.f:120.f,fit=mini?.82f:1.f;
    const float radius=std::min(rect.w,rect.h)*.5f*fit,hole=15.f/diameter;
    const float depth=1.2f/diameter,camera=4.9f/fit;
    const float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch),sr=std::sin(roll),cr=std::cos(roll);
    const auto project=[&](float x,float y,float z){
        const float xx=x*cy+z*sy,zz=-x*sy+z*cy,yy=y*cp-zz*sp,zp=y*sp+zz*cp;
        const float scale=radius*camera/(camera-zp);
        return SDL_FPoint{rect.x+rect.w*.5f+(xx*cr-yy*sr)*scale,rect.y+rect.h*.5f-(xx*sr+yy*cr)*scale};
    };
    const auto clip=[&](float x,float y,float z){return camera-(y*sp+(-x*sy+z*cy)*cp);};
    const auto color=[&](float r,float angle){
        float red=215,green=227,blue=230;
        if(r<.34f){
            if(r<hole+.025f){red=175;green=188;blue=194;}
            else if(r<hole+.045f){red=232;green=239;blue=240;}
            else if((r>.225f&&r<.235f)||(r>.315f&&r<.323f)){red=155;green=166;blue=171;}
            else {red=25;green=28;blue=32;}
        }else{
            const float a=angle+yaw*.4f+pitch*.65f;
            const float band=std::pow(std::abs(std::cos(a)),18.f);
            const float rainbow=std::pow(std::abs(std::cos(a)),4.f)*(1-band);
            red=222+35*rainbow*std::sin(a*7.f+1.1f)-190*band;
            green=235+25*rainbow*std::sin(a*7.f+3.2f)-194*band;
            blue=236+24*rainbow*std::sin(a*7.f+5.3f)-167*band;
            if(r>.99f){red*=.65f;green*=.65f;blue*=.65f;}
        }
        return SDL_Color{Uint8(std::clamp(red,0.f,255.f)),Uint8(std::clamp(green,0.f,255.f)),Uint8(std::clamp(blue,0.f,255.f)),Uint8(255*opacity)};
    };
    const bool front=cy*cp>=0;
    float cosine[segments+1],sine[segments+1];SDL_Color reflection[segments+1];
    for(int i=0;i<=segments;++i){
        const float a=i*2*pi/segments;cosine[i]=std::cos(a);sine[i]=std::sin(a);
        if(!front)reflection[i]=color(.8f,a);
    }
    SDL_Vertex vertices[(segments+1)*2];float w[(segments+1)*2];int indices[segments*6];
    for(int i=0;i<segments;++i){int *t=indices+i*6;t[0]=i*2;t[1]=i*2+2;t[2]=i*2+1;t[3]=i*2+1;t[4]=i*2+2;t[5]=i*2+3;}
    // Outer edge and the wall of the centre hole remain visible edge-on.
    for(float r:{1.f,hole}){
        for(int i=0;i<=segments;++i)for(int side=0;side<2;++side){
            const float x=cosine[i]*r,y=sine[i]*r;
            const Uint8 shade=Uint8(135+65*(.5f+.5f*(sine[i]*cy+cosine[i]*sy)));
            vertices[i*2+side]={project(x,y,side?depth:-depth),{shade,shade,Uint8(std::min(255,int(shade)+8)),Uint8(255*opacity)},{0,0}};
        }
        SDL_RenderGeometry(renderer,nullptr,vertices,(segments+1)*2,indices,segments*6);
    }
    const float rings[]={hole,hole+.025f,hole+.045f,.225f,.23f,.235f,.31f,.315f,.323f,.34f,.35f,.5f,.7f,.9f,.99f,1.f};
    float previous=hole;
    for(float next:rings){
        if(next<=previous||next>1.f)continue;
        for(int i=0;i<=segments;++i)for(int side=0;side<2;++side){
            const float r=side?next:previous,x=cosine[i]*r,y=sine[i]*r;
            SDL_Color ink=front?SDL_Color{255,255,255,Uint8(255*opacity)}:r>=.34f?reflection[i]:color(r,0);
            if(!front&&r>.99f){ink.r=Uint8(ink.r*.65f);ink.g=Uint8(ink.g*.65f);ink.b=Uint8(ink.b*.65f);}
            w[i*2+side]=clip(x,y,front?depth:-depth);
            vertices[i*2+side]={project(x,y,front?depth:-depth),ink,{.5f+x*.5f*fit,.5f-y*.5f*fit}};
        }
        PerspectiveGeometry::Draw(renderer,front?label:nullptr,vertices,w,(segments+1)*2,indices,segments*6);
        previous=next;
    }
}
}
