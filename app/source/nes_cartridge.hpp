#pragma once
#include "perspective_geometry.hpp"
#include "nes_cartridge_data.hpp"
#include "nes_back_label.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace NesCartridge {
// One reusable material per dashboard, released before its SDL renderer.
class Material {
    SDL_Texture *texture_=nullptr;
public:
    Material()=default;
    Material(const Material&)=delete;
    Material& operator=(const Material&)=delete;
    ~Material(){if(texture_)SDL_DestroyTexture(texture_);}
    SDL_Texture *Get(SDL_Renderer *renderer) {
        if(texture_)return texture_;
        constexpr int panelWidth=512,width=panelWidth*2,height=576;
        std::vector<Uint32> pixels(width*height);
        uint32_t noise=0x4e455343;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const bool back=x>=panelWidth;
            const float u=float(x%panelWidth)/(panelWidth-1),v=float(y)/(height-1);
            const float px=(u-.5f)*.9f,py=.5f-v;
            noise=noise*1664525u+1013904223u;
            // Broad softbox reflection and fine matte grain, baked once.
            const float glow=std::exp(-std::pow((u-.72f)/.37f,2.f)-std::pow((v-.18f)/.65f,2.f));
            // Even, low-contrast moulded ABS grain, including at inspection zoom.
            const float grain=float(int(noise>>24)-128)/128.f;
            float shade=153.f+34.f*glow+17.f*(1.f-v)+grain*1.8f;
            // Recessed grip channel. Texture relief is stable at either mesh LOD.
            if(px>.107f && px<.314f && (!back || py>.365f)) {
                const float edge=std::min(px-.107f,.314f-px);
                shade-=22.f*std::exp(-edge*330.f);
                if((!back && py<.37f && py>-.45f) || (back && py>.37f)) {
                    const float wave=(.37f-py)/.024f;
                    const float phase=wave-std::floor(wave);
                    shade+=24.f*std::exp(-std::pow((phase-.20f)/.15f,2.f))
                          -38.f*std::exp(-std::pow((phase-.79f)/.18f,2.f));
                } else shade-=15.f;
            }
            // Panel inset and subtle bevel on the shell's long outer edges.
            if(!back && px>-.338f && px<.099f && py>-.190f && py<.495f)shade-=18.f;
            if(!back && px>-.333f && px<.090f && py>-.180f && py<.493f)shade+=8.f;
            // Shaded inset aligned to the STL's actual insertion arrow.
            const float arrowT=(-.24429f-py)/.06607f;
            const float arrowEdge=.03829f*(1.f-arrowT)-std::abs(px+.00122f);
            if(!back && arrowT>=0.f && arrowT<=1.f && arrowEdge>=0.f){
                shade-=9.f+32.f*std::exp(-arrowT*24.f);
                shade+=(px<-.00122f?18.f:-26.f)*std::exp(-arrowEdge*600.f);
            }
            const float edge=std::min(u,1.f-u);
            if(edge<.012f)shade+=(u<.5f?13.f:-24.f)*(1.f-edge/.012f);
            if(back && px>-.315f && px<.315f && py>-.246f && py<.011f){
                const unsigned lx=std::min(BackLabelWidth-1,unsigned((px+.315f)/.630f*BackLabelWidth));
                const unsigned ly=std::min(BackLabelHeight-1,unsigned((.011f-py)/.257f*BackLabelHeight));
                const unsigned pixel=ly*BackLabelWidth+lx;
                const unsigned ink=(BackLabelPixels[pixel/4]>>(6-2*(pixel%4)))&3;
                shade=64.f+ink*(158.f/3.f);
            }
            const Uint8 c=Uint8(std::clamp(shade,60.f,230.f));
            pixels[y*width+x]=(Uint32(c)<<24)|(Uint32(c)<<16)|(Uint32(c-2)<<8)|255u;
        }
        texture_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_STATIC,width,height);
        if(texture_){SDL_UpdateTexture(texture_,nullptr,pixels.data(),width*sizeof(Uint32));
            SDL_SetTextureBlendMode(texture_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture_,SDL_ScaleModeLinear);}
        return texture_;
    }
};
// Project the actual mesh for a common shelf baseline, including its bevels.
inline SDL_FRect Bounds(SDL_Rect rect,float yaw,float pitch,float roll){
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
    const float size=std::min(rect.w/1.02f,rect.h/1.10f);
    float left=1.e9f,top=1.e9f,right=-1.e9f,bottom=-1.e9f;
    for(const auto &p:NearVertices){
        const float x=-p.x*cy+p.z*sy,z=p.x*sy+p.z*cy,y=p.y*cp-z*sp,depth=p.y*sp+z*cp;
        const float scale=size*2.8f/(2.8f-depth);
        const float sx=rect.x+rect.w*.5f+(x*cr-y*sr)*scale,syy=rect.y+rect.h*.5f-(x*sr+y*cr)*scale;
        left=std::min(left,sx);right=std::max(right,sx);top=std::min(top,syy);bottom=std::max(bottom,syy);
    }
    return {left,top,right-left,bottom-top};
}
// Fixed-size mesh buffers; only the reusable material allocates on its first draw.
inline void Draw(SDL_Renderer *renderer, SDL_Rect rect, SDL_Texture *label, Material &material,
                 bool selected, float opacity=1.f, float yaw=0.f, float pitch=0.f, float roll=0.f, SDL_Texture *edgeLogo=nullptr) {
    const bool near=selected || rect.h>=280;
    const Point *points=near?NearVertices:FarVertices;
    const Triangle *faces=near?NearFaces:FarFaces;
    const int pointCount=near?int(std::size(NearVertices)):int(std::size(FarVertices));
    const auto *nodes=near?NearNodes:FarNodes;
    constexpr size_t maxPoints=std::max(std::size(NearVertices),std::size(FarVertices));
    constexpr size_t maxFaces=std::max(std::size(NearFaces),std::size(FarFaces));
    const float cy=std::cos(yaw),sy=std::sin(yaw);
    const float size=std::min(rect.w/1.02f,rect.h/1.10f);
    const float centerX=rect.x+rect.w*.5f,centerY=rect.y+rect.h*.5f;
    // The imported STL faces the opposite way: grip left, label right.
    const float cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
    const auto rotate=[&](Point p){
        const float x=-p.x*cy+p.z*sy,z=p.x*sy+p.z*cy;
        const float y=p.y*cp-z*sp,depth=p.y*sp+z*cp;
        return Point{x*cr-y*sr,x*sr+y*cr,depth};
    };
    const auto project=[&](Point p){const float perspective=2.8f/(2.8f-p.z);return SDL_FPoint{centerX+p.x*size*perspective,centerY-p.y*size*perspective};};
    std::array<Point,maxPoints> transformed{};
    for(int i=0;i<pointCount;++i)transformed[i]=rotate(points[i]);
    std::array<SDL_Vertex,maxFaces*3> vertices{};static std::array<float,maxFaces*3> clipW;int used=0;
    const Uint8 alpha=Uint8(255*std::clamp(opacity,0.f,1.f));
    // Inverse-rotated eye in source coordinates. The compiled BSP splits
    // intersecting depth ranges; centroid sorting is not sufficient for recesses.
    const Point eye{sy*cp*2.8f,sp*2.8f,cy*cp*2.8f};
    const auto emit=[&](unsigned index){
        const auto f=faces[index];const auto a=transformed[f.a],b=transformed[f.b],c=transformed[f.c];
        const Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
        const Point n{u.z*v.y-u.y*v.z,u.x*v.z-u.z*v.x,u.y*v.x-u.x*v.y};
        const float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if(length<1.e-10f || n.x*(-a.x)+n.y*(-a.y)+n.z*(2.8f-a.z)<=0.f)return;
        const Uint8 l=Uint8(255*std::clamp(.44f+.56f*(-.35f*n.x+.4f*n.y+.85f*n.z)/length,.24f,1.f));
        const auto pa=points[f.a],pb=points[f.b],pc=points[f.c];
        const bool back=(pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x)<0;
        for(auto k:{f.a,f.b,f.c}){clipW[used]=2.8f-transformed[k].z;vertices[used++]={project(transformed[k]),{l,l,l,alpha},
            {(std::clamp(points[k].x/.9f+.5f,0.f,1.f)+(back?1.f:0.f))*.5f,std::clamp(.5f-points[k].y,0.f,1.f)}};}
    };
    const auto visit=[&](auto &&self,int index)->void{
        if(index<0)return;
        const auto &node=nodes[index];
        const bool front=node.normal.x*eye.x+node.normal.y*eye.y+node.normal.z*eye.z>=node.distance;
        self(self,front?node.back:node.front);
        for(unsigned i=0;i<node.count;++i)emit(node.start+i);
        self(self,front?node.front:node.back);
    };
    visit(visit,0);
    if(PerspectiveGeometry::Draw(renderer,material.Get(renderer),vertices.data(),clipW.data(),used)<0)SDL_Log("NES shell draw: %s",SDL_GetError());
    // The source mesh has a recessed label panel right of the grip ridges.
    float labelWidth=.413f,labelHeight=.675f,frontStart=0.f,wrapDepth=0.f;
    int textureWidth=0,textureHeight=0;
    if(label && SDL_QueryTexture(label,nullptr,nullptr,&textureWidth,&textureHeight)==0 && textureWidth>0 && textureHeight>0){
        const float aspect=float(textureWidth)/textureHeight;
        const float physicalHeight=labelWidth/aspect;
        // Dedicated cartridge labels may include a short top-edge title strip.
        // Keep one scale on both surfaces; do not stretch the full sticker onto the front.
        if(physicalHeight>labelHeight && physicalHeight<=labelHeight+.135f){
            wrapDepth=physicalHeight-labelHeight;frontStart=wrapDepth/physicalHeight;
        }else if(aspect>labelWidth/labelHeight)labelHeight=physicalHeight;
        else labelWidth=labelHeight*aspect;
    }
    constexpr float labelX=-.1215f,labelY=.1575f;
    const int indices[]={0,1,2,0,2,3};
    if(label && wrapDepth>0.f && eye.y>.499f){
        const Point top[]={{labelX+labelWidth*.5f,.499f,.0683f-wrapDepth},
            {labelX-labelWidth*.5f,.499f,.0683f-wrapDepth},
            {labelX-labelWidth*.5f,.499f,.0683f},
            {labelX+labelWidth*.5f,.499f,.0683f}};
        const SDL_FPoint topUv[]={{0,0},{1,0},{1,frontStart},{0,frontStart}};
        SDL_Vertex band[4];float w[4];
        for(int i=0;i<4;++i){auto q=rotate(top[i]);w[i]=2.8f-q.z;band[i]={project(q),{225,225,225,alpha},topUv[i]};}
        PerspectiveGeometry::Draw(renderer,label,band,w,4,indices,6);
    }
    if(wrapDepth==0.f && edgeLogo && eye.y>.499f){
        const auto topQuad=[&](float width,float depth,SDL_Texture *texture,SDL_Color tint){
            const Point top[]={{labelX+width*.5f,.500f,-depth*.5f},
                {labelX-width*.5f,.500f,-depth*.5f},{labelX-width*.5f,.500f,depth*.5f},
                {labelX+width*.5f,.500f,depth*.5f}};
            const SDL_FPoint uv[]={{1,1},{0,1},{0,0},{1,0}};
            SDL_Vertex band[4];float w[4];
            for(int i=0;i<4;++i){auto q=rotate(top[i]);w[i]=2.8f-q.z;band[i]={project(q),tint,uv[i]};}
            PerspectiveGeometry::Draw(renderer,texture,band,w,4,indices,6);
        };
        topQuad(.413f,.115f,nullptr,{37,39,42,alpha});
        int w=0,h=0;
        if(SDL_QueryTexture(edgeLogo,nullptr,nullptr,&w,&h)==0 && w>0 && h>0){
            const float scale=std::min(.385f/w,.091f/h);
            topQuad(w*scale,h*scale,edgeLogo,{235,235,235,alpha});
        }
    }
    if(cy*cp<=0.f)return; // Never paint the front sticker through the rear shell.
    const Point corners[]={{labelX+labelWidth*.5f,labelY+labelHeight*.5f,.0683f},
        {labelX-labelWidth*.5f,labelY+labelHeight*.5f,.0683f},
        {labelX-labelWidth*.5f,labelY-labelHeight*.5f,.0683f},
        {labelX+labelWidth*.5f,labelY-labelHeight*.5f,.0683f}};
    const SDL_FPoint uv[]={{0,frontStart},{1,frontStart},{1,1},{0,1}};
    SDL_Vertex sticker[4];float w[4];
    const SDL_Color tint=label?SDL_Color{255,255,255,alpha}:SDL_Color{139,141,138,alpha};
    for(int i=0;i<4;++i){auto q=rotate(corners[i]);w[i]=2.8f-q.z;sticker[i]={project(q),tint,uv[i]};}
    PerspectiveGeometry::Draw(renderer,label,sticker,w,4,indices,6);
    // Restrained laminate sheen follows the same plane as the printed label.
    if(label){
        for(int i=0;i<4;++i){sticker[i].color={255,255,255,Uint8(alpha*(i<2?.10f:.01f))};}
        SDL_RenderGeometry(renderer,nullptr,sticker,4,indices,6);
    }

}
}
