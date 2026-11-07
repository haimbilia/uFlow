#pragma once
#include <SDL2/SDL.h>
#include <array>
#include "cover_projection.hpp"

namespace CardboardBox {
struct Point {float x,y,z;};
struct Face {std::array<Point,4> points;int artwork;Uint8 shade;};
// Six flat panels; textured faces subdivide for perspective-correct artwork.
// No plastic rim, hinge or disc-case clips.
inline constexpr Face Faces[]={
    {{{{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}}},0,255},
    {{{{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f}}},1,230},
    {{{{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f}}},2,185},
    {{{{.5f,.5f,.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f}}},-1,175},
    {{{{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}}},-1,245},
    {{{{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f}}},-1,150}
};
template<class Project,class Artwork>
void Draw(SDL_Renderer *renderer,Project project,Artwork artwork,SDL_Color material={189,184,171,255}){
    for(const auto &face:Faces){
        std::array<SDL_Vertex,4> vertices{};
        for(int i=0;i<4;++i){auto p=face.points[i];vertices[i].position=project(p.x,p.y,p.z).point;
            vertices[i].color={face.shade,face.shade,face.shade,255};vertices[i].tex_coord={(i==1||i==2)?1.f:0.f,i>=2?1.f:0.f};}
        auto a=vertices[0].position,b=vertices[1].position,c=vertices[2].position;
        if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<=0)continue;
        SDL_Texture *texture=artwork(face.artwork,vertices);
        if(!texture)for(auto &v:vertices)v.color={Uint8(face.shade*material.r/255),Uint8(face.shade*material.g/255),Uint8(face.shade*material.b/255),255};
        if(!texture){
            constexpr int indices[]={0,1,2,0,2,3};
            SDL_RenderGeometry(renderer,nullptr,vertices.data(),4,indices,6);
            continue;
        }
        std::array<CoverProjection::Point,4> projected{};
        for(int i=0;i<4;++i){const auto &p=face.points[i];const auto q=project(p.x,p.y,p.z);
            projected[i]={q.point.x,q.point.y,project.camera-q.depth};}
        std::array<SDL_Vertex,CoverProjection::kMaxVertices> mesh{};
        std::array<int,CoverProjection::kMaxIndices> indices{};
        int count=0;
        const int n=CoverProjection::Build(projected,0,1,mesh.data(),indices.data(),count);
        for(int i=0;i<count;++i){
            const float u=mesh[i].tex_coord.x,v=mesh[i].tex_coord.y;
            const float weights[]={(1-u)*(1-v),u*(1-v),u*v,(1-u)*v};
            SDL_FPoint uv{};
            for(int j=0;j<4;++j){uv.x+=weights[j]*vertices[j].tex_coord.x;uv.y+=weights[j]*vertices[j].tex_coord.y;}
            mesh[i].tex_coord=uv;mesh[i].color=vertices[0].color;
        }
        SDL_RenderGeometry(renderer,texture,mesh.data(),count,indices.data(),n);
    }
}
}
