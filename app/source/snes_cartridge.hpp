#pragma once
#include "perspective_geometry.hpp"
#include "snes_cartridge_data.hpp"
#include "nes_back_label.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace SnesCartridge {
constexpr float Height=84.985f, Width=135.f/Height;
inline Point Mm(float x,float y,float z){return {(x-67.5f)/Height,(y-42.4925f)/Height,(z-10.f)/Height};}
struct View {
    SDL_Rect rect;float size,cy,sy,cp,sp,cr,sr;
    View(SDL_Rect r,float yaw,float pitch,float roll):rect(r),size(std::min(r.w/(Width+.12f),r.h/1.1f)),
        cy(std::cos(yaw)),sy(std::sin(yaw)),cp(std::cos(pitch)),sp(std::sin(pitch)),cr(std::cos(roll)),sr(std::sin(roll)){}
    Point Rotate(Point p)const{const float x=p.x*cy+p.z*sy,z=-p.x*sy+p.z*cy,y=p.y*cp-z*sp,d=p.y*sp+z*cp;return {x*cr-y*sr,x*sr+y*cr,d};}
    SDL_FPoint Project(Point p)const{const float s=size*2.8f/(2.8f-p.z);return {rect.x+rect.w*.5f+p.x*s,rect.y+rect.h*.5f-p.y*s};}
    Point Eye()const{return {-sy*cp*2.8f,sp*2.8f,cy*cp*2.8f};}
};
inline SDL_FRect Bounds(SDL_Rect r,float yaw,float pitch,float roll){
    View view(r,yaw,pitch,roll);float l=1.e9f,t=l,b=-l,right=-l;
    for(auto p:NearVertices){auto q=view.Project(view.Rotate(p));l=std::min(l,q.x);right=std::max(right,q.x);t=std::min(t,q.y);b=std::max(b,q.y);}
    return {l,t,right-l,b-t};
}
// Shared, cached ABS material. Small grain is independent of triangle boundaries.
class Material {
    SDL_Texture *texture_=nullptr;
    std::vector<SDL_Vertex> vertices_;
    std::array<std::vector<std::array<Point,3>>,2> normals_;
public:
    Material()=default;Material(const Material&)=delete;Material& operator=(const Material&)=delete;
    ~Material(){if(texture_)SDL_DestroyTexture(texture_);}
    std::vector<SDL_Vertex> &Vertices(){return vertices_;}
    const std::vector<std::array<Point,3>> &Normals(bool near){
        auto &out=normals_[near?0:1];if(!out.empty())return out;
        const auto *p=near?NearVertices:FarVertices;const auto *f=near?NearFaces:FarFaces;
        const size_t np=near?std::size(NearVertices):std::size(FarVertices),nf=near?std::size(NearFaces):std::size(FarFaces);
        std::vector<Point> faceNormals(nf);std::vector<std::vector<size_t>> incident(np);out.resize(nf);
        for(size_t i=0;i<nf;++i){auto a=p[f[i].a],b=p[f[i].b],c=p[f[i].c];
            Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};Point n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);faceNormals[i]={n.x/len,n.y/len,n.z/len};
            for(auto k:{f[i].a,f[i].b,f[i].c})incident[k].push_back(i);
        }
        for(size_t i=0;i<nf;++i){int corner=0;for(auto k:{f[i].a,f[i].b,f[i].c}){
            const auto base=faceNormals[i];Point n{};
            for(auto j:incident[k]){auto q=faceNormals[j];if(base.x*q.x+base.y*q.y+base.z*q.z>.87f){n.x+=q.x;n.y+=q.y;n.z+=q.z;}}
            const float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);out[i][corner++]={n.x/len,n.y/len,n.z/len};
        }}return out;
    }
    SDL_Texture *Get(SDL_Renderer *renderer){
        if(texture_)return texture_;
        constexpr int panel=768,w=panel*2,h=512;std::vector<Uint32> pixels(w*h);uint32_t noise=0x534e4553;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const bool back=x>=panel;const float u=float(x%panel)/(panel-1),v=float(y)/(h-1),px=u*135.f,py=(1.f-v)*Height;
            noise=noise*1664525u+1013904223u;
            float shade=164.f+19.f*(1.f-v)+12.f*std::exp(-std::pow((u-.28f)/.48f,2.f))+float(int(noise>>24)-128)/128.f*1.4f;
            // Dark grooves with a thin highlight on the opposite lip.
            if(px<20.f||px>115.f){for(float groove:{13.5f,27.f,40.5f,54.f,67.5f}){
                const float d=py-groove;shade-=30.f*std::exp(-d*d/0.22f);shade+=12.f*std::exp(-(d-.8f)*(d-.8f)/.25f);}}
            if(!back){
                if(px>23.5f&&px<111.5f&&py>49.7f)shade-=19.f;
                if(px>24.f&&px<111.f&&py>1.f&&py<31.f){
                    shade-=8.f+19.f*std::exp(-std::pow((py-29.8f)/1.9f,2.f));
                    shade+=8.f*std::exp(-std::pow((px-67.5f)/16.f,2.f));
                }
                for(float sx:{11.75f,123.25f}){
                    const float dx=px-sx,dy=py-6.5f,r=std::sqrt(dx*dx+dy*dy);
                    if(r<2.4f){shade=40.f+100.f*std::exp(-std::pow((r-1.85f)/.22f,2.f));
                        if(r<1.5f)shade=std::abs(dx*.7f+dy*.7f)<.28f?25.f:125.f+dy*20.f;}
                }
            }else if(px>31.f&&px<104.f&&py>39.f&&py<69.f){
                const unsigned lx=std::min(NesCartridge::BackLabelWidth-1,unsigned((104.f-px)/73.f*NesCartridge::BackLabelWidth));
                const unsigned ly=std::min(NesCartridge::BackLabelHeight-1,unsigned((69.f-py)/30.f*NesCartridge::BackLabelHeight));
                const unsigned p=ly*NesCartridge::BackLabelWidth+lx;
                shade=62.f+((NesCartridge::BackLabelPixels[p/4]>>(6-2*(p%4)))&3)*(158.f/3.f);
            }
            const Uint8 c=Uint8(std::clamp(shade,20.f,233.f));
            // Slight violet cast of the original North American shell.
            pixels[y*w+x]=(Uint32(c)<<24)|(Uint32(std::max(0,int(c)-2))<<16)|(Uint32(std::min(255,int(c)+7))<<8)|255u;
        }
        texture_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_STATIC,w,h);
        if(texture_){SDL_UpdateTexture(texture_,nullptr,pixels.data(),w*4);SDL_SetTextureBlendMode(texture_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture_,SDL_ScaleModeLinear);}return texture_;
    }
};
inline void Draw(SDL_Renderer *renderer,SDL_Rect rect,SDL_Texture *label,Material &material,bool selected,float opacity=1.f,float yaw=0.f,float pitch=0.f,float roll=0.f,SDL_Texture *edgeLogo=nullptr){
    const bool near=selected||rect.w>=360;const auto *points=near?NearVertices:FarVertices;
    const auto *faces=near?NearFaces:FarFaces;const auto *nodes=near?NearNodes:FarNodes;
    const size_t count=near?std::size(NearVertices):std::size(FarVertices);
    constexpr size_t maxPoints=std::max(std::size(NearVertices),std::size(FarVertices)),maxFaces=std::max(std::size(NearFaces),std::size(FarFaces));
    View view(rect,yaw,pitch,roll);auto eye=view.Eye();const Uint8 alpha=Uint8(255*std::clamp(opacity,0.f,1.f));
    std::array<Point,maxPoints> transformed{};for(size_t i=0;i<count;++i)transformed[i]=view.Rotate(points[i]);
    // Heap-backed reusable draw buffer avoids a large Wii U stack allocation.
    auto &vertices=material.Vertices();vertices.clear();vertices.reserve(maxFaces*3);const auto &normals=material.Normals(near);
    static std::vector<float> clipW;clipW.clear();clipW.reserve(maxFaces*3);
    const auto emit=[&](unsigned i){
        auto f=faces[i];auto a=transformed[f.a],b=transformed[f.b],c=transformed[f.c];
        const Point u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
        const Point n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if(len<1.e-10f||n.x*-a.x+n.y*-a.y+n.z*(2.8f-a.z)<=0)return;
        const auto pa=points[f.a],pb=points[f.b],pc=points[f.c];
        const bool back=(pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x)<0;
        int corner=0;
        for(auto k:{f.a,f.b,f.c}){const auto q=view.Rotate(normals[i][corner++]);
            const float diffuse=std::max(0.f,-.38f*q.x+.48f*q.y+.79f*q.z);
            const float sheen=std::pow(std::max(0.f,-.20f*q.x+.25f*q.y+.947f*q.z),20.f)*.12f;
            const Uint8 l=Uint8(255*std::clamp(.56f+.40f*diffuse+sheen,.36f,1.f));
            clipW.push_back(2.8f-transformed[k].z);
            vertices.push_back({view.Project(transformed[k]),{l,l,l,alpha},
                {(std::clamp(points[k].x/Width+.5f,0.f,1.f)+(back?1.f:0.f))*.5f,std::clamp(.5f-points[k].y,0.f,1.f)}});
        }
    };
    const auto visit=[&](auto &&self,int i)->void{if(i<0)return;const auto &n=nodes[i];const bool front=n.normal.x*eye.x+n.normal.y*eye.y+n.normal.z*eye.z>=n.distance;
        self(self,front?n.back:n.front);for(unsigned k=0;k<n.count;++k)emit(n.start+k);self(self,front?n.front:n.back);};visit(visit,0);
    PerspectiveGeometry::Draw(renderer,material.Get(renderer),vertices.data(),clipW.data(),int(vertices.size()));
    SDL_BlendMode oldBlend;SDL_GetRenderDrawBlendMode(renderer,&oldBlend);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    const auto quad=[&](const std::array<Point,4>&p,SDL_Texture *texture,float v0,float v1,SDL_Color color){
        const Point u{p[1].x-p[0].x,p[1].y-p[0].y,p[1].z-p[0].z},v{p[3].x-p[0].x,p[3].y-p[0].y,p[3].z-p[0].z};
        // Clockwise sticker order, outward normal is v cross u.
        const Point n{v.y*u.z-v.z*u.y,v.z*u.x-v.x*u.z,v.x*u.y-v.y*u.x};
        if(n.x*(eye.x-p[0].x)+n.y*(eye.y-p[0].y)+n.z*(eye.z-p[0].z)<=0)return;
        const SDL_FPoint uv[]={{0,v0},{1,v0},{1,v1},{0,v1}};SDL_Vertex verts[4];const int indices[]={0,1,2,0,2,3};
        float w[4];for(int i=0;i<4;++i){auto q=view.Rotate(p[i]);w[i]=2.8f-q.z;verts[i]={view.Project(q),color,uv[i]};}PerspectiveGeometry::Draw(renderer,texture,verts,w,4,indices,6);
    };
    int tw=0,th=0;float start=0.f,width=83.f,height=32.f,edge=0.f;
    if(label&&SDL_QueryTexture(label,nullptr,nullptr,&tw,&th)==0&&tw>0&&th>0){
        const float aspect=float(tw)/th;
        // US combined scans contain an inverted title band above the front.
        // Front-only landscape scans retain their complete image.
        if(aspect>=1.55f&&aspect<=2.25f)start=.17f;
        const float fit=std::min(width/tw,height/(th*(1.f-start)));
        width=tw*fit;height=th*(1.f-start)*fit;edge=th*start*fit;
    }
    const float left=67.5f-width*.5f,right=67.5f+width*.5f,top=83.07f,bottom=top-height;
    quad({Mm(left,top,19.56f),Mm(right,top,19.56f),Mm(right,bottom,19.56f),Mm(left,bottom,19.56f)},label,start,1.f,label?SDL_Color{249,249,249,alpha}:SDL_Color{43,42,48,alpha});
    if(label)quad({Mm(left,top,19.57f),Mm(right,top,19.57f),Mm(right,bottom,19.57f),Mm(left,bottom,19.57f)},nullptr,0,1,{255,255,255,Uint8(alpha*.035f)});
    const auto edgeQuad=[&](float w,float depth,SDL_Texture *texture,float v0,float v1,SDL_Color tint){
        const float z0=19.56f-depth,y0=83.07f+depth*(1.48f/12.5f);
        quad({Mm(67.5f-w*.5f,y0,z0),Mm(67.5f+w*.5f,y0,z0),Mm(67.5f+w*.5f,83.07f,19.56f),Mm(67.5f-w*.5f,83.07f,19.56f)},texture,v0,v1,tint);
    };
    if(edge>0)edgeQuad(width,std::min(edge,12.f),label,0,start,{228,228,233,alpha});
    else if(edgeLogo){
        edgeQuad(83.f,11.f,nullptr,0,1,{34,33,39,alpha});int w=0,h=0;
        if(SDL_QueryTexture(edgeLogo,nullptr,nullptr,&w,&h)==0&&w>0&&h>0){const float s=std::min(78.f/w,9.f/h);edgeQuad(w*s,h*s,edgeLogo,0,1,{240,240,240,alpha});}
    }
    SDL_SetRenderDrawBlendMode(renderer,oldBlend);
}
}
