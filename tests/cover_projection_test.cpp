#include "cover_projection.hpp"
#include "case_profiles.hpp"
#include "case_sleeve.hpp"
#include <cassert>
#include <iostream>

struct Vec { float x,y; };
struct Vertex { Vec position,tex_coord; };
int main() {
    using namespace CoverProjection;
    std::array<Vertex,kMaxVertices> vertices;
    std::array<int,kMaxIndices> indices;
    const std::array<Point,4> nearFace={Point{-200,-100,.2f},Point{1500,50,2.f},Point{1600,1000,2.f},Point{-300,900,.2f}};
    for(int cap:{1,2,4,8}){int count=0;const auto n=Build(nearFace,0,1,vertices.data(),indices.data(),count,cap);
        assert(count<=(cap+1)*(cap+1) && n<=6*cap*cap);
        for(int i=0;i<n;++i)assert(indices[i]>=0 && indices[i]<count);
    }
    float maxError=0,oldCenterError=0;
    int poses=0;
    for(const char *id: {"wiiu","wii","gc","ds","nes","snes","gba","n64","pce","msx"}) {
        const auto size=CaseProfiles::For(id);
        const auto rails=CaseSleeve::Rails(size.width,size.thickness);
        for(int angle=0;angle<=360;angle+=3)for(float pitch: {0.f,-.025f,-.075f}) {
            const float yaw=angle*3.14159265359f/180.f,roll=pitch==-.075f?.17f:0.f;
            const auto project=[&](float x,float y,float z) {
                x*=size.width/190.f;y*=size.height/190.f;z*=size.thickness/190.f;
                const float xx=x*std::cos(yaw)+z*std::sin(yaw),zz=-x*std::sin(yaw)+z*std::cos(yaw);
                const float yy=y*std::cos(pitch)-zz*std::sin(pitch),depth=y*std::sin(pitch)+zz*std::cos(pitch);
                const float rx=xx*std::cos(roll)-yy*std::sin(roll),ry=xx*std::sin(roll)+yy*std::cos(roll);
                const float w=2.45f-depth;
                return Point{640+rx*500*2.45f/w,350-ry*500*2.45f/w,w};
            };
            for(int panel=0;panel<5;++panel) {
                const auto l=rails[panel],r=rails[panel+1];
                const std::array<Point,4> p={project(l.x,.482f,l.z),project(r.x,.482f,r.z),
                    project(r.x,-.482f,r.z),project(l.x,-.482f,l.z)};
                int count=0;const int n=Build(p,.47f,.96f,vertices.data(),indices.data(),count);
                assert(count<=kMaxVertices && n<=kMaxIndices && count>=4 && n>=6);
                const auto center=Sample(p,.5f,.5f);
                const auto realCenter=project((l.x+r.x)*.5f,0,(l.z+r.z)*.5f);
                assert(std::hypot(center.x-realCenter.x,center.y-realCenter.y)<.001f);
                oldCenterError=std::max(oldCenterError,std::abs((p[0].y+p[2].y)*.5f-center.y));
                if(pitch==0)assert(std::abs(center.y-350)<.001f);
                if(Steps(p)>1) {
                    const auto v=vertices[count/2];
                    assert(std::hypot(v.position.x-center.x,v.position.y-center.y)<.001f);
                }
                for(int i=0;i<n;i+=3) {
                    // Check interior and edge landmarks, not just mesh nodes.
                    const auto a=vertices[indices[i]],b=vertices[indices[i+1]],c=vertices[indices[i+2]];
                    for(const auto weights: {std::array<float,3>{1.f/3,1.f/3,1.f/3}, {0.5f,0.5f,0.f}, {0.f,0.5f,0.5f}, {0.5f,0.f,0.5f}}) {
                        const float u=(a.tex_coord.x*weights[0]+b.tex_coord.x*weights[1]+c.tex_coord.x*weights[2]-.47f)/.49f;
                        const float v=a.tex_coord.y*weights[0]+b.tex_coord.y*weights[1]+c.tex_coord.y*weights[2];
                        const auto exact=Sample(p,u,v);
                        const float x=a.position.x*weights[0]+b.position.x*weights[1]+c.position.x*weights[2];
                        const float y=a.position.y*weights[0]+b.position.y*weights[1]+c.position.y*weights[2];
                        maxError=std::max(maxError,std::hypot(x-exact.x,y-exact.y));
                    }
                }
                ++poses;
            }
        }
    }
    const std::array<Point,4> flat={Point{0,0,2},Point{100,0,2},Point{100,100,2},Point{0,100,2}};
    assert(Steps(flat)==1);
    assert(oldCenterError>20); // Reproduce the original diagonal bend/bob.
    assert(maxError<.35f);
    std::cout<<"PASS "<<poses<<" panel poses; max mesh error "<<maxError
             <<" px; old center bob "<<oldCenterError<<" px; fixed center exact\n";
}
