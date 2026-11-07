#pragma once
#include "n64_cartridge_data.hpp"
#include "gba_cartridge_data.hpp"
#include "gb_cartridge_data.hpp"
#include "ds_cartridge_data.hpp"
#include "hucard_cartridge_data.hpp"
#include "genesis_cartridge_data.hpp"
#include "genesis_relief.hpp"
#include "nes_back_label.hpp"
#include "cartridge_brand_masks.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace CartridgeModels {
using namespace CartridgeGeometry;
enum class Model {N64,GBA,GB,DS,HuCard,Genesis,Count};
struct Label {float x,y,w,h,z;};
struct Profile {float width,height,depth;Label front,back;SDL_Color plastic;};
inline const Profile &Spec(Model model){
    static const Profile profiles[]={
        {115.5f,75.96896f,18.6f,{31.5f,12.f,52.f,51.f,18.53f},{26.f,20.f,63.f,25.6f,.08f},{173,175,176,255}},
        {59.98818f,34.417f,9.3218f,{9.f,5.5f,42.f,19.7f,7.59f},{16.5f,7.f,27.f,11.f,.2f},{91,83,132,255}},
        {57.f,65.f,7.7f,{5.f,9.5f,47.f,36.f,7.48f},{8.f,20.f,41.f,16.65f,-.02f},{177,179,169,255}},
        {34.84874f,35.f,2.64092f,{2.98f,2.92f,29.84f,25.35f,1.77f},{0,0,0,0,0},{65,67,69,255}},
        {54.f,84.965f,3.5f,{2.7f,7.5f,48.6f,42.f,2.52f},{},{223,222,215,255}},
        {108.5f,69.5f,16.5f,{16.05f,7.3f,76.4f,58.7f,16.035f},{},{32,33,35,255}}
    };return profiles[int(model)];
}
inline Mesh Geometry(Model model,bool near){
#define UFLOW_MESH(ns,lod) Mesh{ns::lod##Vertices,ns::lod##Faces,ns::lod##Nodes,unsigned(std::size(ns::lod##Vertices)),unsigned(std::size(ns::lod##Faces))}
    switch(model){
        case Model::N64:return near?UFLOW_MESH(N64Cartridge,Near):UFLOW_MESH(N64Cartridge,Far);
        case Model::GBA:return near?UFLOW_MESH(GbaCartridge,Near):UFLOW_MESH(GbaCartridge,Far);
        case Model::GB:return near?UFLOW_MESH(GbCartridge,Near):UFLOW_MESH(GbCartridge,Far);
        case Model::Genesis:return near?UFLOW_MESH(GenesisCartridge,Near):UFLOW_MESH(GenesisCartridge,Far);
        case Model::Count:break;
        case Model::HuCard:return near?UFLOW_MESH(HuCardCartridge,Near):UFLOW_MESH(HuCardCartridge,Far);
        case Model::DS:return near?UFLOW_MESH(DsCartridge,Near):UFLOW_MESH(DsCartridge,Far);
    }return {};
#undef UFLOW_MESH
}
inline Point Mm(const Profile &p,float x,float y,float z){return {(x-p.width*.5f)/p.height,(y-p.height*.5f)/p.height,(z-p.depth*.5f)/p.height};}
struct View {
    SDL_Rect rect;float size,cy,sy,cp,sp,cr,sr;
    View(Model model,SDL_Rect r,float yaw,float pitch,float roll):rect(r),
        size(std::min(r.w/(Spec(model).width/Spec(model).height+.12f),r.h/1.1f)),
        cy(std::cos(yaw)),sy(std::sin(yaw)),cp(std::cos(pitch)),sp(std::sin(pitch)),cr(std::cos(roll)),sr(std::sin(roll)){}
    Point Rotate(Point p)const{const float x=p.x*cy+p.z*sy,z=-p.x*sy+p.z*cy,y=p.y*cp-z*sp,d=p.y*sp+z*cp;return {x*cr-y*sr,x*sr+y*cr,d};}
    SDL_FPoint Project(Point p)const{const float s=size*2.8f/(2.8f-p.z);return {rect.x+rect.w*.5f+p.x*s,rect.y+rect.h*.5f-p.y*s};}
    Point Eye()const{return {-sy*cp*2.8f,sp*2.8f,cy*cp*2.8f};}
};
inline SDL_FRect Bounds(Model model,SDL_Rect r,float yaw,float pitch,float roll){
    View view(model,r,yaw,pitch,roll);const auto mesh=Geometry(model,true);float l=1.e9f,t=l,b=-l,right=-l;
    for(unsigned i=0;i<mesh.pointCount;++i){auto q=view.Project(view.Rotate(mesh.points[i]));l=std::min(l,q.x);right=std::max(right,q.x);t=std::min(t,q.y);b=std::max(b,q.y);}
    return {l,t,right-l,b-t};
}
class Materials {
    std::array<SDL_Texture*,int(Model::Count)> textures_{};
    std::array<std::vector<std::array<Point,3>>,2*int(Model::Count)> normals_;
public:
    std::vector<SDL_Vertex> vertices;std::vector<Point> transformed;
    Materials()=default;Materials(const Materials&)=delete;Materials& operator=(const Materials&)=delete;
    ~Materials(){for(auto *t:textures_)if(t)SDL_DestroyTexture(t);}
    const std::vector<std::array<Point,3>> &Normals(Model model,bool near){
        auto &out=normals_[int(model)*2+(near?0:1)];if(!out.empty())return out;
        const auto mesh=Geometry(model,near);const auto *p=mesh.points;const auto *f=mesh.faces;
        const size_t np=mesh.pointCount,nf=mesh.faceCount;
        std::vector<Point> faceNormals(nf);std::vector<std::vector<size_t>> incident(np);out.resize(nf);
        for(size_t i=0;i<nf;++i){auto a=p[f[i].a],b=p[f[i].b],c=p[f[i].c];
            Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};Point n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);faceNormals[i]={n.x/len,n.y/len,n.z/len};
            for(auto k:{f[i].a,f[i].b,f[i].c})incident[k].push_back(i);
        }
        for(size_t i=0;i<nf;++i){int corner=0;for(auto k:{f[i].a,f[i].b,f[i].c}){
            const auto base=faceNormals[i];
            if((model==Model::HuCard||model==Model::Genesis) && std::abs(base.z)>.9999f){
                out[i][corner++]=base;
                continue;
            }
            Point n{};
            for(auto j:incident[k]){auto q=faceNormals[j];if(base.x*q.x+base.y*q.y+base.z*q.z>.87f){n.x+=q.x;n.y+=q.y;n.z+=q.z;}}
            const float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);out[i][corner++]={n.x/len,n.y/len,n.z/len};
        }}return out;
    }

    SDL_Texture *Get(SDL_Renderer *renderer,Model model){
        auto *&texture=textures_[int(model)];if(texture)return texture;
        constexpr int panel=512,h=512;const int w=panel*(model==Model::Genesis?3:2);std::vector<Uint32> pixels(w*h);uint32_t noise=0x43415254;
        const auto &p=Spec(model);
        std::array<float,panel> highlights{};
        for(int x=0;x<panel;++x){const float q=(float(x)/(panel-1)-.25f)/.5f;highlights[x]=9.f*std::exp(-q*q);}
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const bool back=x>=panel;const float u=float(x%panel)/(panel-1),v=float(y)/(h-1),px=u*p.width,py=(1.f-v)*p.height;
            noise=noise*1664525u+1013904223u;
            float glow=14.f*(1.f-v)+highlights[x%panel];
            float grain=float(int(noise>>24)-128)/128.f*1.4f;
            float r=p.plastic.r+glow+grain,g=p.plastic.g+glow+grain,b=p.plastic.b+glow+grain;
            const Label brands[]={{34,66,48,7,0},{9,27,42,5,0},{8,53,41,7,0},{4,29,27,4,0},{},{}};
            const auto &brand=brands[int(model)];
            if(!back&&px>brand.x&&px<brand.x+brand.w&&py>brand.y&&py<brand.y+brand.h){
                const unsigned bx=std::min(BrandWidth-1,unsigned((px-brand.x)/brand.w*BrandWidth));
                const unsigned by=std::min(BrandHeight-1,unsigned((brand.y+brand.h-py)/brand.h*BrandHeight));
                const unsigned pixel=by*BrandWidth+bx;
                const float ink=((BrandPixels[int(model)][pixel/4]>>(6-2*(pixel%4)))&3)/3.f;
                r-=ink*28.f;g-=ink*28.f;b-=ink*28.f;
            }
            const auto &label=p.back;
            if(back&&label.w>0&&px>label.x&&px<label.x+label.w&&py>label.y&&py<label.y+label.h){
                const unsigned lx=std::min(NesCartridge::BackLabelWidth-1,unsigned((label.x+label.w-px)/label.w*NesCartridge::BackLabelWidth));
                const unsigned ly=std::min(NesCartridge::BackLabelHeight-1,unsigned((label.y+label.h-py)/label.h*NesCartridge::BackLabelHeight));
                const unsigned k=ly*NesCartridge::BackLabelWidth+lx;
                r=g=b=59.f+((NesCartridge::BackLabelPixels[k/4]>>(6-2*(k%4)))&3)*52.f;
            }
            if(back&&model==Model::DS&&py>1.8f&&py<9.5f&&px>4.8f&&px<30.7f){
                const float phase=std::fmod(px-4.8f,25.9f/17.f);
                if(phase<1.1f){r=188.f+glow;g=151.f+glow;b=62.f+glow*.5f;}else{r=28;g=29;b=30;}
            }
            if(!back && model==Model::HuCard){
                // Rounded epoxy contact block with fine mould grain and plated fingers.
                const float side=std::max(3.35f-std::min(px,p.width-px),0.f);
                const float end=std::max(std::max(p.height-30.9f-py,py-(p.height-3.45f)),0.f);
                if(side*side+end*end<2.1f*2.1f){
                    float shade=24.f+glow*.53f+grain*5.f;
                    r=shade;g=shade-1.f;b=shade+2.f;
                    const float pitch=47.6f/38.f;
                    if(px>=3.2f && px<50.8f && py>p.height-6.4f && py<p.height-1.85f){
                        float phase=std::fmod(px-3.2f,pitch)/pitch;
                        if(phase>.13f && phase<.87f){
                            float glint=21.f*(1.f-phase)+grain*2.f+(py-p.height+6.4f)*4.f;
                            r=200.f+glint;g=162.f+glint;b=85.f+glint;
                        }
                    }
                }
                // Shallow moulded insertion arrow, picked out by light on its ribs.
                float arrowTop=5.45f-std::abs(px-p.width*.5f)*.36f;
                if(std::abs(px-p.width*.5f)<7.f && py>1.75f && py<arrowTop){
                    float relief=std::fmod(py-1.75f,.82f)<.35f?9.f:-15.f;
                    if(arrowTop-py<.25f)relief=-20.f;
                    r+=relief;g+=relief;b+=relief;
                }
            }
            if(model==Model::Genesis){
                r+=grain*2.f;g+=grain*2.f;b+=grain*2.f;
                if(back){
                    // Rear relief reads left-to-right from behind the shell.
                    const float rearX=p.width-px;
                    if(py>6.f && py<48.f && rearX>10.f && rearX<98.5f){
                        unsigned tx=std::min(GenesisCartridge::ReliefWidth-1,
                            unsigned((rearX-10.f)/88.5f*GenesisCartridge::ReliefWidth));
                        unsigned ty=std::min(GenesisCartridge::ReliefHeight-1,
                            unsigned((48.f-py)/42.f*GenesisCartridge::ReliefHeight));
                        auto ink=[&](unsigned y){
                            unsigned k=y*GenesisCartridge::ReliefWidth+tx;
                            return float((GenesisCartridge::ReliefPixels[k/4]>>(6-2*(k%4)))&3);
                        };
                        float relief=(ink(ty)-ink(std::min(ty+1,GenesisCartridge::ReliefHeight-1)))*5.f;
                        r+=relief;g+=relief;b+=relief;
                        // The two shallow mould panels have their own satin finish.
                        bool panel=(py<31.f)||(py>35.5f && rearX>39.f && rearX<69.5f);
                        if(panel){r-=3.f+grain;g-=3.f+grain;b-=3.f+grain;}
                    }
                    for(float cx:{19.25f,89.25f}){
                        float dx=px-cx,dy=py-40.25f;
                        if(dx*dx+dy*dy<1.65f*1.65f){
                            float steel=91.f+glow*1.7f+dx*9.f-dy*7.f;
                            if((std::abs(dx)<.28f||std::abs(dy)<.28f)&&dx*dx+dy*dy<1.35f*1.35f)steel=19.f;
                            r=steel;g=steel;b=steel*.95f;
                        }
                    }
                }
            }
            if(model==Model::Genesis && x>=panel*2){
                const float phase=std::fmod(std::max(0.f,px-13.61f),2.54f);
                r=86.f+grain;g=75.f+grain;b=39.f+grain;
                if(px>13.61f && px<94.89f && py<7.f && phase>.22f && phase<2.32f){
                    r=183.f+glow;g=148.f+glow;b=67.f+glow*.5f;
                }
            }
            pixels[y*w+x]=(Uint32(std::clamp(r,0.f,255.f))<<24)|(Uint32(std::clamp(g,0.f,255.f))<<16)|(Uint32(std::clamp(b,0.f,255.f))<<8)|255u;
        }
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_STATIC,w,h);
        if(texture){SDL_UpdateTexture(texture,nullptr,pixels.data(),w*4);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);}return texture;
    }
};
inline void Draw(SDL_Renderer *renderer,Model model,SDL_Rect rect,SDL_Texture *label,Materials &material,bool selected,float opacity=1.f,float yaw=0.f,float pitch=0.f,float roll=0.f,SDL_Texture *logo=nullptr){
    const bool near=selected||std::max(rect.w,rect.h)>=360;const auto mesh=Geometry(model,near);const auto &profile=Spec(model);
    View view(model,rect,yaw,pitch,roll);auto eye=view.Eye();const Uint8 alpha=Uint8(255*std::clamp(opacity,0.f,1.f));
    auto &transformed=material.transformed;transformed.resize(mesh.pointCount);for(unsigned i=0;i<mesh.pointCount;++i)transformed[i]=view.Rotate(mesh.points[i]);
    auto &vertices=material.vertices;vertices.clear();vertices.reserve(mesh.faceCount*3);const auto &normals=material.Normals(model,near);
    const auto emit=[&](unsigned i){auto f=mesh.faces[i];auto a=transformed[f.a],b=transformed[f.b],c=transformed[f.c];
        const Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z},n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        if(n.x*-a.x+n.y*-a.y+n.z*(2.8f-a.z)<=0)return;
        const auto pa=mesh.points[f.a],pb=mesh.points[f.b],pc=mesh.points[f.c];const bool back=(pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x)<0;
        const auto pcbPoint=[&](Point p){
            const float z=p.z*profile.height+profile.depth*.5f;
            const float y=p.y*profile.height+profile.height*.5f;
            return y<=8.51f&&(std::abs(z-8.9f)<.03f||std::abs(z-7.4f)<.03f);
        };
        const bool pcb=model==Model::Genesis&&pcbPoint(pa)&&pcbPoint(pb)&&pcbPoint(pc);
        const float texturePanel=pcb?2.f:back?1.f:0.f;
        const float texturePanels=model==Model::Genesis?3.f:2.f;
        int corner=0;for(auto k:{f.a,f.b,f.c}){auto q=view.Rotate(normals[i][corner++]);
            const float diffuse=std::max(0.f,-.38f*q.x+.48f*q.y+.79f*q.z),shine=std::max(0.f,-.20f*q.x+.25f*q.y+.947f*q.z);
            const float s2=shine*shine,s4=s2*s2,s8=s4*s4,sheen=s8*s8*s4*.12f;
            const Uint8 l=Uint8(255*std::clamp(.56f+.40f*diffuse+sheen,.36f,1.f));
            const float u=(std::clamp(mesh.points[k].x/(profile.width/profile.height)+.5f,0.f,1.f)+texturePanel)/texturePanels;
            // Keep Genesis's bottom connector samples inside the atlas texels.
            // SDL's software rasterizer can read past the texture at v == 1.
            const float inset=model==Model::Genesis?.5f/512.f:0.f;
            const float v=std::clamp(.5f-mesh.points[k].y,inset,1.f-inset);
            vertices.push_back({view.Project(transformed[k]),{l,l,l,alpha},{u,v}});
        }};
    const auto visit=[&](auto &&self,int i)->void{if(i<0)return;const auto &n=mesh.nodes[i];const bool front=n.normal.x*eye.x+n.normal.y*eye.y+n.normal.z*eye.z>=n.distance;
        self(self,front?n.back:n.front);for(unsigned k=0;k<n.count;++k)emit(n.start+k);self(self,front?n.front:n.back);};visit(visit,0);
    SDL_RenderGeometry(renderer,material.Get(renderer,model),vertices.data(),int(vertices.size()),nullptr,0);
    SDL_BlendMode old;SDL_GetRenderDrawBlendMode(renderer,&old);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    const auto quad=[&](const std::array<Point,4>&p,SDL_Texture *texture,SDL_Color color,float v0=0.f,float v1=1.f){
        const Point u{p[1].x-p[0].x,p[1].y-p[0].y,p[1].z-p[0].z},v{p[3].x-p[0].x,p[3].y-p[0].y,p[3].z-p[0].z};
        const Point n{v.y*u.z-v.z*u.y,v.z*u.x-v.x*u.z,v.x*u.y-v.y*u.x};
        if(n.x*(eye.x-p[0].x)+n.y*(eye.y-p[0].y)+n.z*(eye.z-p[0].z)<=0)return;
        SDL_Vertex out[4];const SDL_FPoint uv[]={{0,v0},{1,v0},{1,v1},{0,v1}};const int indices[]={0,1,2,0,2,3};
        for(int i=0;i<4;++i)out[i]={view.Project(view.Rotate(p[i])),color,uv[i]};SDL_RenderGeometry(renderer,texture,out,4,indices,6);
    };
    const auto plane=[&](Label l,SDL_Texture *t,SDL_Color color){quad({Mm(profile,l.x,l.y+l.h,l.z),Mm(profile,l.x+l.w,l.y+l.h,l.z),Mm(profile,l.x+l.w,l.y,l.z),Mm(profile,l.x,l.y,l.z)},t,color);};
    if(model==Model::Genesis && label){
        // The sticker follows the source mesh's 3 mm front-to-top fillet.
        const auto &l=profile.front;
        quad({Mm(profile,l.x,l.y+l.h,l.z),Mm(profile,l.x+l.w,l.y+l.h,l.z),
              Mm(profile,l.x+l.w,l.y,l.z),Mm(profile,l.x,l.y,l.z)},label,{249,249,249,alpha},.075f,1.f);
        constexpr int segments=8;
        for(int i=0;i<segments;++i){
            float a=i*1.5707963f/segments,b=(i+1)*1.5707963f/segments;
            float ya=66.f+3.035f*std::sin(a),za=13.f+3.035f*std::cos(a);
            float yb=66.f+3.035f*std::sin(b),zb=13.f+3.035f*std::cos(b);
            quad({Mm(profile,l.x,yb,zb),Mm(profile,l.x+l.w,yb,zb),
                  Mm(profile,l.x+l.w,ya,za),Mm(profile,l.x,ya,za)},label,
                  {235,235,235,alpha},.075f*(1.f-float(i+1)/segments),.075f*(1.f-float(i)/segments));
        }
        // Keep the title on the narrow upper spine readable on a shelf.
        if(logo){
            int w=0,h=0;
            if(SDL_QueryTexture(logo,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){
                const float fit=std::min(l.w*.9f/w,5.f/h),width=w*fit,depth=h*fit;
                float x=profile.width*.5f;
                quad({Mm(profile,x-width*.5f,69.035f,10.f-depth*.5f),
                      Mm(profile,x+width*.5f,69.035f,10.f-depth*.5f),
                      Mm(profile,x+width*.5f,69.035f,10.f+depth*.5f),
                      Mm(profile,x-width*.5f,69.035f,10.f+depth*.5f)},logo,{242,242,242,alpha});
            }
        }
        SDL_SetRenderDrawBlendMode(renderer,old);
        return;
    }
    auto box=profile.front;
    if(model!=Model::HuCard)plane(box,nullptr,{35,37,40,alpha});
    auto *art=label?label:logo;
    if(art){int w=0,h=0;if(SDL_QueryTexture(art,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){const float fit=std::min(box.w/w,box.h/h)*(label?1.f:.85f);box.x+=(box.w-w*fit)*.5f;box.y+=(box.h-h*fit)*.5f;box.w=w*fit;box.h=h*fit;
        plane(box,art,{249,249,249,alpha});plane(box,nullptr,{255,255,255,Uint8(alpha*.035f)});}}
    // These systems have separate front stickers; synthesize the shelf edge
    // from a logo, retaining aspect ratio on the cartridge's top surface.
    if(logo && model!=Model::HuCard){
        const float width=profile.width*.55f,depth=model==Model::Genesis?5.8f:profile.depth*.65f;
        const float top=model==Model::Genesis?69.035f:profile.height+.03f;
        const auto edge=[&](float w,float d,SDL_Texture *t,SDL_Color c){const float x=profile.width*.5f,z=model==Model::Genesis?10.1f:profile.depth*.5f;
            quad({Mm(profile,x-w*.5f,top,z-d*.5f),Mm(profile,x+w*.5f,top,z-d*.5f),Mm(profile,x+w*.5f,top,z+d*.5f),Mm(profile,x-w*.5f,top,z+d*.5f)},t,c);};
        edge(width,depth,nullptr,{35,37,40,alpha});int w=0,h=0;if(SDL_QueryTexture(logo,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){float fit=std::min(width*.9f/w,depth*.8f/h);edge(w*fit,h*fit,logo,{242,242,242,alpha});}
    }
    SDL_SetRenderDrawBlendMode(renderer,old);
}
}
