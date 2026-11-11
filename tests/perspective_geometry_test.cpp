#include "perspective_geometry.hpp"
#include <cassert>
#include <cstdio>
#include <algorithm>
int main(){
    float worst=0;int maximum=0;
    for(int frame=0;frame<=240;++frame){
        float angle=frame*6.28318530718f/240;
        auto project=[&](float u,float v){
            float x=(u-.5f)*1.5f,y=(v-.5f),z=-x*std::sin(angle);
            x*=std::cos(angle);float yp=y*.94f-z*.342f,zp=y*.342f+z*.94f,w=2.8f-zp;
            return PerspectiveGeometry::Vertex{{{640+x*600*2.8f/w,360+yp*600*2.8f/w},{255,255,255,255},{u,v}},w};
        };
        int count=0;
        auto emit=[&](const SDL_Vertex&a,const SDL_Vertex&b,const SDL_Vertex&c){
            ++count;
            // Independent exact projection of interior UV samples. This detects
            // incorrect screen-space subdivision as well as affine stretching.
            for(auto weights:{std::array<float,3>{.3333333f,.3333333f,.3333334f},{.2f,.3f,.5f},{.5f,.5f,0.f}}){
                float u=a.tex_coord.x*weights[0]+b.tex_coord.x*weights[1]+c.tex_coord.x*weights[2];
                float v=a.tex_coord.y*weights[0]+b.tex_coord.y*weights[1]+c.tex_coord.y*weights[2];
                auto exact=project(u,v).v.position;
                float x=a.position.x*weights[0]+b.position.x*weights[1]+c.position.x*weights[2];
                float y=a.position.y*weights[0]+b.position.y*weights[1]+c.position.y*weights[2];
                worst=std::max(worst,std::hypot(x-exact.x,y-exact.y));
            }
        };
        PerspectiveGeometry::Triangle(project(0,0),project(1,0),project(1,1),emit);
        PerspectiveGeometry::Triangle(project(0,0),project(1,1),project(0,1),emit);
        maximum=std::max(maximum,count);
    }
    printf("240-angle perspective sweep: worst %.4f px, maximum %d triangles per 900px face\n",worst,maximum);
    assert(worst<.4f);assert(maximum<=2048);
}
