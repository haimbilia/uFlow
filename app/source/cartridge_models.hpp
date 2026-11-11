#pragma once
#include "perspective_geometry.hpp"
#include "n64_cartridge_data.hpp"
#include "n64_label_shape.hpp"
#include "gba_cartridge_data.hpp"
#include "gb_cartridge_data.hpp"
#include "ds_cartridge_data.hpp"
#include "hucard_cartridge_data.hpp"
#include "genesis_cartridge_data.hpp"
#include "genesis_relief.hpp"
#include "wonderswan_cartridge_data.hpp"
#include "wonderswan_material_data.hpp"
#include <zlib.h>
#include "nes_back_label.hpp"
#include "cartridge_brand_masks.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace CartridgeModels {
using namespace CartridgeGeometry;
enum class Model {N64,GBA,GB,DS,HuCard,Genesis,WonderSwan,Count};
struct Label {float x,y,w,h,z;};
struct Profile {float width,height,depth;Label front,back;SDL_Color plastic;};
inline const Profile &Spec(Model model){
    static const Profile profiles[]={
        {115.5f,75.96896f,18.6f,{30.25f,5.4f,55.f,62.5f,18.01f},{25.f,15.f,65.5f,36.f,.08f},{173,175,176,255}},
        {59.98818f,34.417f,9.3218f,{9.f,5.5f,42.f,19.7f,7.59f},{16.5f,7.f,27.f,11.f,.2f},{91,83,132,255}},
        {57.f,65.f,7.7f,{5.f,9.5f,47.f,36.f,7.48f},{8.f,20.f,41.f,16.65f,-.02f},{177,179,169,255}},
        {34.84874f,35.f,2.64092f,{2.98f,2.92f,29.84f,25.35f,1.77f},{0,0,0,0,0},{65,67,69,255}},
        {54.f,84.965f,3.5f,{2.7f,7.5f,48.6f,42.f,2.52f},{},{223,222,215,255}},
        {108.5f,69.5f,16.5f,{16.05f,7.3f,76.4f,58.7f,16.035f},{},{32,33,35,255}},
        {66.727936f,41.663967f,6.3f,{2.67f,13.33f,61.387936f,22.92f,6.32f},{},{160,183,181,255}}
    };return profiles[int(model)];
}
inline Mesh Geometry(Model model,bool near){
#define UFLOW_MESH(ns,lod) Mesh{ns::lod##Vertices,ns::lod##Faces,ns::lod##Nodes,unsigned(std::size(ns::lod##Vertices)),unsigned(std::size(ns::lod##Faces))}
    switch(model){
        case Model::N64:return near?UFLOW_MESH(N64Cartridge,Near):UFLOW_MESH(N64Cartridge,Far);
        case Model::GBA:return near?UFLOW_MESH(GbaCartridge,Near):UFLOW_MESH(GbaCartridge,Far);
        case Model::GB:return near?UFLOW_MESH(GbCartridge,Near):UFLOW_MESH(GbCartridge,Far);
        case Model::Genesis:return near?UFLOW_MESH(GenesisCartridge,Near):UFLOW_MESH(GenesisCartridge,Far);
        case Model::WonderSwan:return near?UFLOW_MESH(WonderSwanCartridge,Near):UFLOW_MESH(WonderSwanCartridge,Far);
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
            if((model==Model::N64||model==Model::HuCard||model==Model::Genesis||model==Model::WonderSwan) && std::abs(base.z)>.9999f){
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
        if(model==Model::WonderSwan){
            std::vector<unsigned char> rgba(WonderSwanMaterial::Width*WonderSwanMaterial::Height*4);
            uLongf size=rgba.size();
            if(uncompress(rgba.data(),&size,WonderSwanMaterial::Compressed,sizeof(WonderSwanMaterial::Compressed))!=Z_OK||size!=rgba.size())return nullptr;
            // Material grading happens once on upload, never per frame.
            // Clear margins blend with the scene; the underlying PCB remains
            // opaque. Feather the boundary instead of exposing a hard mask.
            constexpr int panel=WonderSwanMaterial::Width/2;
            for(int y=0;y<WonderSwanMaterial::Height;++y)for(int x=0;x<WonderSwanMaterial::Width;++x){
                const size_t i=(y*WonderSwanMaterial::Width+x)*4;
                for(int c=0;c<3;++c)rgba[i+c]=Uint8(std::clamp((rgba[i+c]-128.f)*1.08f+128.f,0.f,255.f));
                const float u=float(x%panel)/panel,v=float(y)/WonderSwanMaterial::Height;
                const bool rear=x>=panel;
                // A recessed contact catches less light at its root, then a
                // broad highlight, with a soft falloff at the connector tip.
                // Select gold pixels so gaps, silver vias and plastic retain
                // their own material. This is part of the one-time upload.
                if(v>.80f&&v<.97f&&u>.045f&&u<.955f&&rgba[i]>rgba[i+2]*1.3f&&rgba[i+1]>rgba[i+2]*1.18f){
                    const float t=(v-.80f)/.17f;
                    const float root=std::clamp(t/.26f,0.f,1.f);
                    const float tip=std::clamp((t-.77f)/.23f,0.f,1.f);
                    const float light=.70f+.30f*root*root*(3.f-2.f*root)-.13f*tip*tip*(3.f-2.f*tip);
                    for(int c=0;c<3;++c)rgba[i+c]=Uint8(rgba[i+c]*light);
                }
                const float pcbEdge=std::min({u-.055f,.945f-u,v-(rear?.16f:.77f),.98f-v});
                const float pcb=std::clamp(pcbEdge/.018f,0.f,1.f);
                rgba[i+3]=Uint8(rgba[i+3]*(.72f+.28f*pcb));
            }
            texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,WonderSwanMaterial::Width,WonderSwanMaterial::Height);
            if(texture){SDL_UpdateTexture(texture,nullptr,rgba.data(),WonderSwanMaterial::Width*4);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);}
            return texture;
        }
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
            const Label brands[]={{},{9,27,42,5,0},{8,53,41,7,0},{4,29,27,4,0},{},{}};
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
            if(model==Model::N64){
                // Fine moulded ABS grain, with restrained seam and screw relief.
                if(!(back&&px>25&&px<90.5f&&py>15&&py<51)){r+=grain*2.f;g+=grain*2.f;b+=grain*2.f;}
                if(back){
                    const float seam=std::min(std::abs(px-22.5f),std::abs(px-93.f));
                    if(seam<.22f){r-=22;g-=22;b-=22;}
                    for(float cx:{22.5f,93.f}){
                        const float dx=px-cx,dy=py-63.f,d=std::sqrt(dx*dx+dy*dy);
                        if(d<2.15f){float shade=d>1.65f?62.f:140.f+dy*14.f;
                            if(d<.75f || (d<1.25f && std::cos(6.f*std::atan2(dy,dx))>.25f))shade=55.f;
                            r=shade;g=shade*.91f;b=shade*.69f;}
                    }
                }
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
    SDL_BlendMode old;SDL_GetRenderDrawBlendMode(renderer,&old);
    if(model==Model::WonderSwan)SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    auto &transformed=material.transformed;transformed.resize(mesh.pointCount);for(unsigned i=0;i<mesh.pointCount;++i)transformed[i]=view.Rotate(mesh.points[i]);
    auto &vertices=material.vertices;vertices.clear();vertices.reserve(mesh.faceCount*3);const auto &normals=material.Normals(model,near);
    static std::vector<float> clipW;clipW.clear();clipW.reserve(vertices.capacity());
    const auto emit=[&](unsigned i){auto f=mesh.faces[i];auto a=transformed[f.a],b=transformed[f.b],c=transformed[f.c];
        const Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z},n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        if(n.x*-a.x+n.y*-a.y+n.z*(2.8f-a.z)<=0)return;
        const auto pa=mesh.points[f.a],pb=mesh.points[f.b],pc=mesh.points[f.c];const bool back=(pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x)<0;
        if(model==Model::WonderSwan){
            const Point u{pb.x-pa.x,pb.y-pa.y,pb.z-pa.z},v{pc.x-pa.x,pc.y-pa.y,pc.z-pa.z};
            const Point n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            // Flat optical surfaces use regular grids below. Tiny BSP-split
            // textured triangles produce unstable sampling on SDL renderers.
            if(n.z*n.z>.999f*(n.x*n.x+n.y*n.y+n.z*n.z))return;
        }

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
            const float inset=model==Model::WonderSwan?.5f/WonderSwanMaterial::Height:model==Model::Genesis?.5f/512.f:0.f;
            const float v=std::clamp(.5f-mesh.points[k].y,inset,1.f-inset);
            // Dense molded edges are optically thicker than the clear face.
            // Use less transparency than the thin face to retain edge depth.
            const SDL_Color color=model==Model::WonderSwan?SDL_Color{Uint8(l*.88f),Uint8(l*.91f),Uint8(l*.89f),Uint8(alpha*.82f)}:SDL_Color{l,l,l,alpha};
            vertices.push_back({view.Project(transformed[k]),color,{u,v}});
            clipW.push_back(2.8f-transformed[k].z);
        }};
    const auto visit=[&](auto &&self,int i)->void{if(i<0)return;const auto &n=mesh.nodes[i];const bool front=n.normal.x*eye.x+n.normal.y*eye.y+n.normal.z*eye.z>=n.distance;
        self(self,front?n.back:n.front);for(unsigned k=0;k<n.count;++k)emit(n.start+k);self(self,front?n.front:n.back);};visit(visit,0);
    PerspectiveGeometry::Draw(renderer,model==Model::WonderSwan?nullptr:material.Get(renderer,model),vertices.data(),clipW.data(),int(vertices.size()));
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    const auto quad=[&](const std::array<Point,4>&p,SDL_Texture *texture,SDL_Color color,float v0=0.f,float v1=1.f){
        const Point u{p[1].x-p[0].x,p[1].y-p[0].y,p[1].z-p[0].z},v{p[3].x-p[0].x,p[3].y-p[0].y,p[3].z-p[0].z};
        const Point n{v.y*u.z-v.z*u.y,v.z*u.x-v.x*u.z,v.x*u.y-v.y*u.x};
        if(n.x*(eye.x-p[0].x)+n.y*(eye.y-p[0].y)+n.z*(eye.z-p[0].z)<=0)return;
        SDL_Vertex out[4];const SDL_FPoint uv[]={{0,v0},{1,v0},{1,v1},{0,v1}};const int indices[]={0,1,2,0,2,3};
        float w[4];for(int i=0;i<4;++i){auto q=view.Rotate(p[i]);out[i]={view.Project(q),color,uv[i]};w[i]=2.8f-q.z;}PerspectiveGeometry::Draw(renderer,texture,out,w,4,indices,6);
    };
    const auto plane=[&](Label l,SDL_Texture *t,SDL_Color color){quad({Mm(profile,l.x,l.y+l.h,l.z),Mm(profile,l.x+l.w,l.y+l.h,l.z),Mm(profile,l.x+l.w,l.y,l.z),Mm(profile,l.x,l.y,l.z)},t,color);};
    if(model==Model::WonderSwan){
        // Render the baked PCB/clear-plastic surfaces with stable, bounded
        // regular UV grids. No runtime sorting/refraction or internal meshes.
        const bool front=eye.z>0;
        auto *atlas=material.Get(renderer,model);
        const auto surface=[&](float x0,float y0,float x1,float y1,float z,bool rear){
            // One continuous surface avoids seams through the contact fingers
            // and PCB when a software renderer rounds grid coordinates.
            constexpr int columns=1,rows=1;
            std::array<SDL_Vertex,(columns+1)*(rows+1)> grid{};
            std::array<float,(columns+1)*(rows+1)> w{};
            std::array<int,columns*rows*6> indices{};
            const auto normal=view.Rotate({0,0,rear?-1.f:1.f});
            const float diffuse=std::max(0.f,-.38f*normal.x+.48f*normal.y+.79f*normal.z);
            const Uint8 shade=Uint8(255*(.75f+.25f*diffuse));
            for(int j=0;j<=rows;++j)for(int i=0;i<=columns;++i){
                const float x=x0+(x1-x0)*i/columns,y=y0+(y1-y0)*j/rows;
                const auto q=view.Rotate(Mm(profile,x,y,z));w[j*(columns+1)+i]=2.8f-q.z;
                grid[j*(columns+1)+i]={view.Project(q),{shade,shade,shade,alpha},
                    {(rear?2.f-x/profile.width:x/profile.width)*.5f,std::clamp(1.f-y/profile.height,.5f/WonderSwanMaterial::Height,1.f-.5f/WonderSwanMaterial::Height)}};
            }
            int count=0;for(int j=0;j<rows;++j)for(int i=0;i<columns;++i){
                const int a=j*(columns+1)+i,b=a+1,c=a+columns+1,d=c+1;
                for(int k:{a,b,d,a,d,c})indices[count++]=k;
            }
            PerspectiveGeometry::Draw(renderer,atlas,grid.data(),w.data(),int(grid.size()),indices.data(),count);
            // A broad view-dependent reflection, drawn before the opaque label.
            // Only the central clear panel receives it; keep the rim transparent.
            for(auto &v:grid){
                const float x=(v.tex_coord.x-(rear?.5f:0.f))*2.f,y=v.tex_coord.y;
                float band=std::max(0.f,1.f-std::abs(x+y*.28f-(.48f+view.sy*.42f))/.23f);
                band=band*band*(3.f-2.f*band);
                const float edge=std::clamp(std::min({x,1.f-x,y,1.f-y})*18.f,0.f,1.f);
                v.color={246,248,242,Uint8(alpha*band*edge*(rear?.16f:.10f))};
            }
            SDL_RenderGeometry(renderer,nullptr,grid.data(),int(grid.size()),indices.data(),count);
        };
        if(front){
            // Connector PCB is recessed below the front shell. Its exposed
            // edge has thickness rather than being painted onto the plastic.
            plane({2.1f,.7f,profile.width-4.2f,9.5f,2.57f},nullptr,{25,53,37,alpha});
            quad({Mm(profile,2.1f,.7f,2.57f),Mm(profile,profile.width-2.1f,.7f,2.57f),
                  Mm(profile,profile.width-2.1f,.7f,1.3f),Mm(profile,2.1f,.7f,1.3f)},nullptr,{92,91,55,alpha});
            surface(1.3f,.22f,profile.width-1.3f,10.030793f,2.59f,false);
            surface(.5f,10.530793f,profile.width-.5f,profile.height-.5f,6.305f,false);
        }else surface(.22f,.22f,profile.width-.22f,profile.height-.22f,-.005f,true);
        if(front){
            auto box=profile.front;
            // Rounded opaque paper sticker, independent from plastic tint.
            const auto sticker=[&](Label b,SDL_Texture *texture,SDL_Color color){
                constexpr int steps=6,count=4*(steps+1);
                std::array<SDL_Vertex,count+1> vs{};std::array<float,count+1> w{};int wi=0;std::array<int,count*3> is{};
                const float radius=.95f;
                const auto vertex=[&](float x,float y){auto q=view.Rotate(Mm(profile,x,y,b.z));w[wi++]=2.8f-q.z;return SDL_Vertex{view.Project(q),color,{std::clamp((x-b.x)/b.w,0.f,1.f),std::clamp(1.f-(y-b.y)/b.h,0.f,1.f)}};};
                vs[0]=vertex(b.x+b.w*.5f,b.y+b.h*.5f);
                int k=1;for(int corner=0;corner<4;++corner){
                    const float cx=(corner==0||corner==3)?b.x+b.w-radius:b.x+radius;
                    const float cy=corner<2?b.y+b.h-radius:b.y+radius;
                    for(int j=0;j<=steps;++j){const float angle=(corner+j/float(steps))*1.5707963f;
                        vs[k++]=vertex(cx+radius*std::cos(angle),cy+radius*std::sin(angle));}
                }
                for(int j=0;j<count;++j){is[j*3]=0;is[j*3+1]=j+1;is[j*3+2]=(j+1)%count+1;}
                PerspectiveGeometry::Draw(renderer,texture,vs.data(),w.data(),int(vs.size()),is.data(),int(is.size()));
            };
            if(label||logo)sticker(box,nullptr,{224,229,224,alpha});
            if(label)sticker(box,label,{249,249,249,alpha});
            else if(logo){int w=0,h=0;SDL_QueryTexture(logo,nullptr,nullptr,&w,&h);if(w>0&&h>0){const float fit=std::min(box.w*.85f/w,box.h*.8f/h);box.x+=(box.w-w*fit)*.5f;box.y+=(box.h-h*fit)*.5f;box.w=w*fit;box.h=h*fit;plane(box,logo,{249,249,249,alpha});}}
        }
        SDL_SetRenderDrawBlendMode(renderer,old);return;
    }
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
    if(model==Model::N64){
        // Only the underside view exposes the recessed socket.
        if(eye.y<-.5f){
        // Recessed socket with two rows of plated contact fingers.
        quad({Mm(profile,22.5f,6,4),Mm(profile,93,6,4),Mm(profile,93,0,4),Mm(profile,22.5f,0,4)},nullptr,{40,41,40,alpha});
        quad({Mm(profile,93,6,14.5f),Mm(profile,22.5f,6,14.5f),Mm(profile,22.5f,0,14.5f),Mm(profile,93,0,14.5f)},nullptr,{40,41,40,alpha});
        quad({Mm(profile,22.5f,6,14.5f),Mm(profile,93,6,14.5f),Mm(profile,93,6,4),Mm(profile,22.5f,6,4)},nullptr,{24,27,25,alpha});
        for(bool rear:{false,true}){
            const float z=rear?8.15f:9.85f;
            const auto pcb=[&](float x,float y,float width,float height,SDL_Color color){
                if(rear)quad({Mm(profile,x+width,y+height,z),Mm(profile,x,y+height,z),Mm(profile,x,y,z),Mm(profile,x+width,y,z)},nullptr,color);
                else plane({x,y,width,height,z},nullptr,color);
            };
            pcb(24,1.3f,67.5f,5.5f,{41,70,42,alpha});
            for(int i=0;i<25;++i)pcb(26.85f+i*2.5f,1.5f,1.8f,4.3f,{190,153,73,alpha});
        }
        }
        if(eye.z>0){
            const auto b=profile.front;
            constexpr int count=int(N64Label::Outline.size());
            std::array<SDL_Vertex,count+1> vs;std::array<float,count+1> w;std::array<int,count*3> indices;
            const auto vertex=[&](int i,float x,float y){auto q=view.Rotate(Mm(profile,b.x+x,b.y+y,b.z));w[i]=2.8f-q.z;
                vs[i]={view.Project(q),{249,249,249,alpha},{std::clamp(x/b.w,0.f,1.f),std::clamp(1.f-y/b.h,0.f,1.f)}};};
            vertex(0,b.w*.5f,b.h*.5f);
            for(int i=0;i<count;++i){const auto p=N64Label::Outline[i];vertex(i+1,p.x,p.y);
                indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=(i+1)%count+1;}
            if(label)PerspectiveGeometry::Draw(renderer,label,vs.data(),w.data(),count+1,indices.data(),count*3);
            else if(logo){int width=0,height=0;SDL_QueryTexture(logo,nullptr,nullptr,&width,&height);
                if(width>0&&height>0){const float scale=std::min(b.w*.85f/width,b.h*.8f/height);
                    plane({b.x+(b.w-width*scale)*.5f,b.y+(b.h-height*scale)*.5f,width*scale,height*scale,b.z},logo,{249,249,249,alpha});}}
        }
        SDL_SetRenderDrawBlendMode(renderer,old);return;
    }
    auto box=profile.front;
    if(model!=Model::HuCard)plane(box,nullptr,{35,37,40,alpha});
    auto *art=label?label:logo;
    if(art){int w=0,h=0;if(SDL_QueryTexture(art,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){const float fit=std::min(box.w/w,box.h/h)*(label?1.f:.85f);box.x+=(box.w-w*fit)*.5f;box.y+=(box.h-h*fit)*.5f;box.w=w*fit;box.h=h*fit;
        plane(box,art,{249,249,249,alpha});plane(box,nullptr,{255,255,255,Uint8(alpha*.035f)});}}
    // These systems have separate front stickers; synthesize the shelf edge
    // from a logo, retaining aspect ratio on the cartridge's top surface.
    if(logo && model!=Model::HuCard && model!=Model::WonderSwan){
        const float width=profile.width*.55f,depth=model==Model::Genesis?5.8f:profile.depth*.65f;
        const float top=model==Model::Genesis?69.035f:profile.height+.03f;
        const auto edge=[&](float w,float d,SDL_Texture *t,SDL_Color c){const float x=profile.width*.5f,z=model==Model::Genesis?10.1f:profile.depth*.5f;
            quad({Mm(profile,x-w*.5f,top,z-d*.5f),Mm(profile,x+w*.5f,top,z-d*.5f),Mm(profile,x+w*.5f,top,z+d*.5f),Mm(profile,x-w*.5f,top,z+d*.5f)},t,c);};
        edge(width,depth,nullptr,{35,37,40,alpha});int w=0,h=0;if(SDL_QueryTexture(logo,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){float fit=std::min(width*.9f/w,depth*.8f/h);edge(w*fit,h*fit,logo,{242,242,242,alpha});}
    }
    SDL_SetRenderDrawBlendMode(renderer,old);
}
}
