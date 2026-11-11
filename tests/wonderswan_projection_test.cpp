#include "cartridge_models.hpp"
#include <cassert>
#include <cstdio>

int main(){
    using namespace CartridgeModels;
    const auto &profile=Spec(Model::WonderSwan);
    float worst=0;int maximum=0;
    // Exercise the actual cartridge camera and both optical planes, not a
    // screen-space rectangle. Interior points must follow the same rigid pose.
    for(float pitch:{-.35f,.1f,.4f})for(int frame=0;frame<=240;++frame){
        View view(Model::WonderSwan,{0,0,900,600},frame*6.28318530718f/240,pitch,.12f);
        for(auto label:{profile.front,Label{.5f,10.530793f,profile.width-1.f,profile.height-11.030793f,6.305f},Label{.22f,.22f,profile.width-.44f,profile.height-.44f,-.005f}}){
            auto project=[&](float u,float v){
                auto q=view.Rotate(Mm(profile,label.x+u*label.w,label.y+v*label.h,label.z));
                return PerspectiveGeometry::Vertex{{view.Project(q),{255,255,255,255},{u,v}},2.8f-q.z};
            };
            int count=0;
            auto emit=[&](const SDL_Vertex&a,const SDL_Vertex&b,const SDL_Vertex&c){
                ++count;
                for(auto t:{std::array<float,3>{.2f,.3f,.5f},{.5f,.5f,0.f},{.3333333f,.3333333f,.3333334f}}){
                    float u=a.tex_coord.x*t[0]+b.tex_coord.x*t[1]+c.tex_coord.x*t[2];
                    float v=a.tex_coord.y*t[0]+b.tex_coord.y*t[1]+c.tex_coord.y*t[2];
                    auto q=project(u,v).v.position;
                    float x=a.position.x*t[0]+b.position.x*t[1]+c.position.x*t[2];
                    float y=a.position.y*t[0]+b.position.y*t[1]+c.position.y*t[2];
                    worst=std::max(worst,std::hypot(q.x-x,q.y-y));
                }
            };
            PerspectiveGeometry::Triangle(project(0,0),project(1,0),project(1,1),emit);
            PerspectiveGeometry::Triangle(project(0,0),project(1,1),project(0,1),emit);
            maximum=std::max(maximum,count);
        }
    }
    printf("WonderSwan label/front/rear, 3 pitches x 241 poses: maximum error %.4fpx, %d triangles per plane\n",worst,maximum);
    assert(worst<.4f);assert(maximum<=2048);
}
