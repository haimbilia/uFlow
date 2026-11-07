#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// One small reusable soft-light atlas; bounded quads, no decoder or file I/O.
namespace SkinEffects {
inline constexpr int MaxParticles=192, MaxTrails=3, Segments=128;
inline float Unit(uint32_t seed) {seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;return float(seed&65535)/65535.f;}
inline float Fract(float x){return x-std::floor(x);}
struct Point {float x,y;};
inline Point Particle(int i,float seconds) {
    const float u=Unit(uint32_t(i)*17+31),v=Unit(uint32_t(i)*47+91);
    return {1280.f*Fract(u+seconds*(.002f+.003f*v)),110.f+570.f*Fract(v-seconds*(.008f+.012f*u))};
}
inline Point Ribbon(float p,int band,float seconds,SDL_FRect area) {
    const float angle=p*6.2831853f+band*.26f+seconds*.09f;
    return {area.x+area.w*(.04f+.92f*p),
            area.y+area.h*(.49f+.29f*std::sin(angle)+band*.035f)};
}
class Renderer {
    SDL_Texture *atlas_=nullptr;SDL_Renderer *atlasRenderer_=nullptr;
    unsigned atlasBuilds_=0,spriteDraws_=0;
    bool Atlas(SDL_Renderer*r){
        if(atlas_&&atlasRenderer_==r)return true;
        Release();auto*s=SDL_CreateRGBSurfaceWithFormat(0,512,192,32,SDL_PIXELFORMAT_RGBA32);if(!s)return false;
        // Reuse the ribbon's one-dimensional alpha profile. The old loop
        // repeated exp/sin/pow for every pixel and stalled first-use on Wii U.
        std::array<float,1025> profile{};
        std::array<float,512> center{},fade{};
        for(size_t i=0;i<profile.size();++i){const float d=float(i)/8;profile[i]=55*std::exp(-d*d/100.f)+85*std::exp(-d*d/9.f);}
        for(int x=0;x<512;++x){const float p=x/511.f,e=std::max(0.f,std::sin(p*3.14159265f));center[x]=64+24*std::sin(p*6.2831853f);fade[x]=e*std::sqrt(e);}
        const auto white=SDL_MapRGBA(s->format,255,255,255,0);
        for(int y=0;y<192;++y){auto*row=reinterpret_cast<Uint32*>(static_cast<char*>(s->pixels)+y*s->pitch);
            for(int x=0;x<512;++x){float a=0;
                if(y<128){const float d=std::min(1024.f,std::abs(y-center[x])*8);const int i=std::min(1023,int(d));
                    a=(profile[i]+(profile[i+1]-profile[i])*(d-i))*fade[x];}
                else if(x<64){const float dx=(x-31.5f)/31.5f,dy=(y-159.5f)/31.5f;a=170*std::max(0.f,(std::exp(-(dx*dx+dy*dy)*5.f)-.007f));}
                const auto alpha=Uint8(std::clamp(a,0.f,255.f));
                row[x]=white|(Uint32(alpha>>s->format->Aloss)<<s->format->Ashift);}}
        atlas_=SDL_CreateTextureFromSurface(r,s);SDL_FreeSurface(s);if(!atlas_)return false;
        atlasRenderer_=r;SDL_SetTextureBlendMode(atlas_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(atlas_,SDL_ScaleModeLinear);++atlasBuilds_;return true;
    }
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
    SDL_Renderer *renderer_=nullptr,*capabilityRenderer_=nullptr;bool accelerated_=false;
    bool Accelerated(SDL_Renderer*r){if(r!=capabilityRenderer_){SDL_RendererInfo info{};SDL_GetRendererInfo(r,&info);accelerated_=(info.flags&SDL_RENDERER_ACCELERATED)!=0;capabilityRenderer_=r;cachedCount_=-1;}return accelerated_;}
    float cachedTime_=-1;SDL_FRect cachedArea_{};int cachedCount_=-1,cachedTrails_=-1,cachedIntensity_=-1;
    unsigned rebuilds_=0; SDL_Color tint_{255,194,95,255};
    SDL_Color Tint(SDL_Color c) const {return {Uint8(std::min(255,int(c.r)*tint_.r/255)),Uint8(std::min(255,int(c.g)*tint_.g/194)),Uint8(std::min(255,int(c.b)*tint_.b/95)),c.a};}
    bool Prepare(SDL_Renderer*r,int count,int trails,int intensity,float seconds,SDL_FRect area){
        const bool same=cachedCount_==count&&cachedTrails_==trails&&cachedIntensity_==intensity&&cachedArea_.x==area.x&&cachedArea_.y==area.y&&cachedArea_.w==area.w&&cachedArea_.h==area.h;
        if(same&&seconds>=cachedTime_&&seconds-cachedTime_<1.f/30.f){renderer_=r;SDL_GetRenderDrawBlendMode(r,&previous_);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);End();return false;}
        cachedCount_=count;cachedTrails_=trails;cachedIntensity_=intensity;cachedTime_=seconds;cachedArea_=area;++rebuilds_;Begin(r);return true;
    }
    SDL_BlendMode previous_=SDL_BLENDMODE_BLEND;
    void Room(int,int){} // Maximum geometry is bounded by the public count limits.
    void Flush(){if(!vertices_.empty())SDL_RenderGeometry(renderer_,nullptr,vertices_.data(),int(vertices_.size()),indices_.data(),int(indices_.size()));}
public:
    Renderer()=default;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    ~Renderer(){Release();}
    void Release(){if(atlas_)SDL_DestroyTexture(atlas_);atlas_=nullptr;atlasRenderer_=nullptr;}
    unsigned AtlasBuilds()const{return atlasBuilds_;}
    unsigned SpriteDraws()const{return spriteDraws_;}
    void SetTint(SDL_Color color){if(color.r!=tint_.r||color.g!=tint_.g||color.b!=tint_.b){tint_=color;cachedCount_=-1;}}
    size_t VertexCount() const{return vertices_.size();}
    unsigned Rebuilds() const{return rebuilds_;}
    void Begin(SDL_Renderer *r){renderer_=r;vertices_.clear();indices_.clear();if(vertices_.capacity()<8192){vertices_.reserve(8192);indices_.reserve(24576);}SDL_GetRenderDrawBlendMode(r,&previous_);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);}
    void End(){Flush();SDL_SetRenderDrawBlendMode(renderer_,previous_);}
    void Spark(float x,float y,float radius,SDL_Color color){
        color=Tint(color);Room(13,36);const int first=int(vertices_.size());
        vertices_.push_back({{x,y},color,{0,0}});
        for(int j=0;j<12;++j){static constexpr float dx[]={1,.8660254f,.5f,0,-.5f,-.8660254f,-1,-.8660254f,-.5f,0,.5f,.8660254f};static constexpr float dy[]={0,.5f,.8660254f,1,.8660254f,.5f,0,-.5f,-.8660254f,-1,-.8660254f,-.5f};vertices_.push_back({{x+radius*dx[j],y+radius*dy[j]},{color.r,color.g,color.b,0},{0,0}});}
        for(int j=0;j<12;++j){indices_.push_back(first);indices_.push_back(first+1+j);indices_.push_back(first+1+(j+1)%12);}
    }
    void Line(Point a,Point b,float width,SDL_Color color){
        const float dx=b.x-a.x,dy=b.y-a.y,length=std::sqrt(dx*dx+dy*dy);if(length<.001f)return;
        color=Tint(color);Room(4,6);const int first=int(vertices_.size());float nx=-dy/length*width,ny=dx/length*width;
        for(const auto&p:{Point{a.x+nx,a.y+ny},Point{b.x+nx,b.y+ny},Point{b.x-nx,b.y-ny},Point{a.x-nx,a.y-ny}})vertices_.push_back({{p.x,p.y},color,{0,0}});
        for(int i:{0,1,2,0,2,3})indices_.push_back(first+i);
    }
    void Ambient(SDL_Renderer*r,int count,int trails,int intensity,float seconds,SDL_FRect area){
        spriteDraws_=0;vertices_.clear();indices_.clear();intensity=std::clamp(intensity,0,3);if(!intensity)return;
        const bool accelerated=Accelerated(r);if(!Atlas(r))return;
        if(vertices_.capacity()<256){vertices_.reserve(256);indices_.reserve(384);}
        const auto quad=[&](SDL_Rect source,SDL_FRect dest,float angle,Uint8 alpha){
            const int first=int(vertices_.size());const float c=std::cos(angle),s=std::sin(angle),cx=dest.x+dest.w/2,cy=dest.y+dest.h/2;
            for(int k=0;k<4;++k){const float u=k==1||k==2?1.f:0.f,v=k>=2?1.f:0.f,x=(u-.5f)*dest.w,y=(v-.5f)*dest.h;
                vertices_.push_back({{cx+x*c-y*s,cy+x*s+y*c},{tint_.r,tint_.g,tint_.b,alpha},{(source.x+u*source.w)/512.f,(source.y+v*source.h)/192.f}});}
            for(int i:{0,1,2,0,2,3})indices_.push_back(first+i);++spriteDraws_;
        };
        count=std::clamp(count,0,accelerated?48:16)*intensity/3;
        trails=accelerated?std::clamp(trails,0,intensity==1?1:2):0;
        const SDL_Rect ribbon{0,0,512,128},glow{0,128,64,64};
        for(int band=0;band<trails;++band){SDL_FRect dest=area;dest.y+=band*8.f+3.f*std::sin(seconds*.13f+band);
            quad(ribbon,dest,(band*5.f-2.f+std::sin(seconds*.07f))*.01745329252f,Uint8(115+25*std::sin(seconds*.18f+band)));}
        for(int i=0;i<count;++i){const auto p=Particle(i,seconds*.45f);const float u=Unit(i+5),size=5+u*10;
            SDL_FRect dest{area.x+p.x/1280.f*area.w-size/2,area.y+(p.y-110)/570.f*area.h-size/2,size,size};
            quad(glow,dest,0,Uint8(55+65*(.5f+.5f*std::sin(seconds*.4f+i))));}
        if(!vertices_.empty())SDL_RenderGeometry(r,atlas_,vertices_.data(),int(vertices_.size()),indices_.data(),int(indices_.size()));
    }
    void Focus(SDL_Renderer*r,SDL_Rect box,int intensity,float seconds){
        intensity=std::clamp(intensity,0,Accelerated(r)?3:1);if(intensity<=0||box.w<=0||box.h<=0)return;
        if(!Prepare(r,0,0,intensity,seconds,{float(box.x),float(box.y),float(box.w),float(box.h)}))return;
        const float pulse=.5f+.5f*std::sin(seconds*1.6f);
        const Point p[]={{float(box.x),float(box.y)},{float(box.x+box.w),float(box.y)},{float(box.x+box.w),float(box.y+box.h)},{float(box.x),float(box.y+box.h)}};
        for(int k=0;k<4;++k){Line(p[k],p[(k+1)%4],5,{255,167,61,Uint8(5+5*pulse)});Line(p[k],p[(k+1)%4],1,{255,225,164,Uint8(130+45*pulse)});}
        const float perimeter=float(2*(box.w+box.h));
        for(int k=0;k<intensity*3;++k){float s=Fract(seconds*.12f+k/float(intensity*3))*perimeter;Point point;
            if(s<box.w)point={box.x+s,float(box.y)};else if((s-=box.w)<box.h)point={float(box.x+box.w),box.y+s};else if((s-=box.h)<box.w)point={box.x+box.w-s,float(box.y+box.h)};else {s-=box.w;point={float(box.x),box.y+box.h-s};}
            Spark(point.x,point.y,10,{255,187,86,90});Spark(point.x,point.y,2.5f,{255,243,192,230});
        }End();
    }
};
}
