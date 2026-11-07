#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Analytic, antialiased glass tile. Two small immutable textures, built once;
// the soft bevel and specular corners remain smooth during page animation.
class WiiUMenuMaterial {
    SDL_Texture *tiles_[2]{};
    SDL_Texture *shadow_{};
    SDL_Texture *arrow_{};
    static SDL_Texture *Create(SDL_Renderer *renderer,bool active) {
        constexpr int size=192;
        auto *surface=SDL_CreateRGBSurfaceWithFormat(0,size,size,32,SDL_PIXELFORMAT_RGBA32);
        if(!surface)return nullptr;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            const float qx=std::abs(x+.5f-96)-67,qy=std::abs(y+.5f-96)-67;
            const float d=std::sqrt(std::max(qx,0.f)*std::max(qx,0.f)+std::max(qy,0.f)*std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-28;
            const float coverage=std::clamp(.5f-d,0.f,1.f);
            const float depth=std::max(-d,0.f);
            const auto byte=[](float value){return Uint8(std::clamp(value,0.f,255.f));};
            float r,g,b,a;
            if(active){
#include "wiiu_menu_active_material.inc"
            }else{
#include "wiiu_menu_normal_material.inc"
            }
            auto *row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch);
            row[x]=SDL_MapRGBA(surface->format,byte(r),byte(g),byte(b),byte(a));
        }
        auto *texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_FreeSurface(surface);
        if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);}
        return texture;
    }
public:
    void DrawShadow(SDL_Renderer *renderer,SDL_Rect rect,Uint8 opacity=255){
        if(!shadow_){
            auto *s=SDL_CreateRGBSurfaceWithFormat(0,224,224,32,SDL_PIXELFORMAT_RGBA32);
            if(!s)return;
            for(int y=0;y<224;++y)for(int x=0;x<224;++x){
                const float qx=std::abs(x+.5f-112)-67,qy=std::abs(y+.5f-116)-67;
                const float d=std::sqrt(std::max(qx,0.f)*std::max(qx,0.f)+std::max(qy,0.f)*std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-28;
                const float alpha=48.f*std::exp(-std::pow(std::max(d,0.f)/7.f,2.f));
                auto *row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels)+y*s->pitch);
                row[x]=SDL_MapRGBA(s->format,54,70,63,Uint8(alpha));
            }
            shadow_=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);
            if(shadow_){SDL_SetTextureBlendMode(shadow_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(shadow_,SDL_ScaleModeLinear);}
        }
        const int pad=std::max(1,rect.w/12);
        const SDL_Rect dst{rect.x-pad,rect.y-pad,rect.w+2*pad,rect.h+2*pad};
        if(shadow_){SDL_SetTextureAlphaMod(shadow_,opacity);SDL_RenderCopy(renderer,shadow_,nullptr,&dst);SDL_SetTextureAlphaMod(shadow_,255);}
    }
    void Draw(SDL_Renderer *renderer,SDL_Rect rect,bool active,Uint8 opacity=255){
        auto *&texture=tiles_[active?1:0];if(!texture)texture=Create(renderer,active);
        if(texture){SDL_SetTextureAlphaMod(texture,opacity);SDL_RenderCopy(renderer,texture,nullptr,&rect);SDL_SetTextureAlphaMod(texture,255);}
    }
    void DrawShelf(SDL_Renderer *renderer,SDL_Rect frame,SDL_Rect icon,float focus,Uint8 opacity){
        auto *&texture=tiles_[1];if(!texture)texture=Create(renderer,true);
        if(!texture)return;
        const int top=icon.y+icon.h-int(21*focus);
        const int sourceTop=std::clamp(int((top-frame.y)*192.f/frame.h),0,191);
        const SDL_Rect source{0,sourceTop,192,192-sourceTop};
        const SDL_Rect target{frame.x,top,frame.w,frame.y+frame.h-top};
        SDL_SetTextureAlphaMod(texture,Uint8(opacity*.94f));
        SDL_RenderCopy(renderer,texture,&source,&target);
        SDL_SetTextureAlphaMod(texture,255);
    }
    void DrawArrow(SDL_Renderer *renderer,SDL_Rect rect,bool left){
        if(!arrow_){
            // Rounded concave silhouette, rasterized once with distance AA.
            // A polygon fan cannot triangulate this silhouette reliably.
            std::vector<SDL_FPoint> points;
            const auto curve=[&](SDL_FPoint a,SDL_FPoint b,SDL_FPoint c){
                for(int i=0;i<16;++i){const float t=i/16.f,u=1-t;
                    points.push_back({u*u*a.x+2*u*t*b.x+t*t*c.x,u*u*a.y+2*u*t*b.y+t*t*c.y});}
            };
            curve({19,12},{10,5},{12,22});
            curve({12,22},{36,96},{12,172});
            curve({12,172},{9,186},{22,177});
            curve({22,177},{62,136},{96,102});
            curve({96,102},{103,96},{96,90});
            curve({96,90},{58,51},{19,12});
            auto *s=SDL_CreateRGBSurfaceWithFormat(0,112,200,32,SDL_PIXELFORMAT_RGBA32);
            if(!s)return;
            for(int y=0;y<200;++y)for(int x=0;x<112;++x){
                const float px=x+.5f,py=y+.5f;bool inside=false;float distance=1000;
                for(unsigned i=0,j=points.size()-1;i<points.size();j=i++){
                    const auto a=points[j],b=points[i];
                    if((a.y>py)!=(b.y>py) && px<(b.x-a.x)*(py-a.y)/(b.y-a.y)+a.x)inside=!inside;
                    const float dx=b.x-a.x,dy=b.y-a.y;
                    const float t=std::clamp(((px-a.x)*dx+(py-a.y)*dy)/(dx*dx+dy*dy),0.f,1.f);
                    distance=std::min(distance,std::hypot(px-a.x-t*dx,py-a.y-t*dy));
                }
                const float coverage=std::clamp(.5f+(inside?distance:-distance),0.f,1.f);
                const float crease=121.f-px*.25f;
                const float bottom=std::clamp((py-crease)/2.f,0.f,1.f);
                const float edge=std::exp(-std::pow((distance-2.2f)/1.1f,2.f));
                const float rim=std::exp(-std::pow(distance/.85f,2.f));
                const float sheen=std::exp(-std::pow((py-crease+3.f)/4.f,2.f));
                const float broad=std::exp(-std::pow((py-crease+19.f)/25.f,2.f));
                const float light=246.f-18*bottom+bottom*(py-crease)*.13f+10*edge+6*sheen+4*broad-43*rim;
                const float alpha=coverage*(248.f+7*edge);
                auto *row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels)+y*s->pitch);
                const auto v=Uint8(std::clamp(light,0.f,255.f));
                row[x]=SDL_MapRGBA(s->format,v,Uint8(std::min(255,int(v)+2)),v,Uint8(alpha));
            }
            arrow_=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);
            if(arrow_){SDL_SetTextureBlendMode(arrow_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(arrow_,SDL_ScaleModeLinear);}
        }
        if(!arrow_)return;
        const auto flip=left?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE;
        SDL_SetTextureColorMod(arrow_,68,83,76);
        for(int spread=5;spread>=1;--spread){
            SDL_Rect shadow{rect.x-spread,rect.y+3-spread,rect.w+2*spread,rect.h+2*spread};
            SDL_SetTextureAlphaMod(arrow_,Uint8(7+spread*2));SDL_RenderCopyEx(renderer,arrow_,nullptr,&shadow,0,nullptr,flip);
        }
        SDL_SetTextureColorMod(arrow_,255,255,255);SDL_SetTextureAlphaMod(arrow_,255);
        SDL_RenderCopyEx(renderer,arrow_,nullptr,&rect,0,nullptr,flip);
    }
    void Clear(){if(arrow_){SDL_DestroyTexture(arrow_);arrow_=nullptr;}if(shadow_){SDL_DestroyTexture(shadow_);shadow_=nullptr;}for(auto *&texture:tiles_){if(texture)SDL_DestroyTexture(texture);texture=nullptr;}}
};
