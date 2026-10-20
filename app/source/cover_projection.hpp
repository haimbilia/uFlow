#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace CoverProjection {
// SDL_RenderGeometry has no clip W and interpolates UVs affinely. Subdivide in
// the original planar coordinates, then project each sample, NOT screen space.
struct Point { float x, y, w; };
inline constexpr int kMaxSteps = 32;
inline constexpr int kMaxVertices = (kMaxSteps+1)*(kMaxSteps+1);
inline constexpr int kMaxIndices = kMaxSteps*kMaxSteps*6;
inline Point Sample(const std::array<Point,4> &p, float u, float v) {
    const float weights[]={(1-u)*(1-v),u*(1-v),u*v,(1-u)*v};
    float x=0,y=0,w=0;
    for(int i=0;i<4;++i) {
        const float f=weights[i]*p[i].w;
        x+=p[i].x*f; y+=p[i].y*f; w+=f;
    }
    return {x/w,y/w,w};
}
inline int Steps(const std::array<Point,4> &p) {
    float lo=p[0].w,hi=lo,span=0;
    for(int i=0;i<4;++i) {
        lo=std::min(lo,p[i].w);hi=std::max(hi,p[i].w);
        for(int j=0;j<i;++j)span=std::max(span,std::hypot(p[i].x-p[j].x,p[i].y-p[j].y));
    }
    // Screen-error budget, not a fixed expensive mesh for every tiny spine.
    // Planar faces with equal depth remain a single quad. Bound work at 32x32.
    const float error=span*(hi-lo)/std::max(.001f,lo)*(hi/std::max(.001f,lo));
    int steps=1;
    while(steps<kMaxSteps && error/(steps*steps)>.35f)steps*=2;
    return steps;
}
template<class Vertex>
inline int Build(const std::array<Point,4> &p, float u0,float u1,
                 Vertex *vertices,int *indices,int &vertexCount,int maximumSteps=kMaxSteps) {
    const int limit=std::clamp(maximumSteps,1,kMaxSteps);
    const int steps=limit==1?1:std::min(Steps(p),limit),stride=steps+1;
    vertexCount=stride*stride;
    for(int y=0;y<=steps;++y)for(int x=0;x<=steps;++x) {
        const float u=float(x)/steps,v=float(y)/steps;
        const auto q=Sample(p,u,v);
        auto &out=vertices[y*stride+x];
        out.position={q.x,q.y}; out.tex_coord={u0+(u1-u0)*u,v};
    }
    int count=0;
    for(int y=0;y<steps;++y)for(int x=0;x<steps;++x) {
        const int a=y*stride+x,b=a+1,c=b+stride,d=a+stride;
        for(int i:{a,b,c,a,c,d})indices[count++]=i;
    }
    return count;
}
} // namespace CoverProjection
