#include <SDL2/SDL.h>
#include "cardboard_box.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
struct Projection {
 float camera=2.45f,angle=0,scale=700;
 struct Result{SDL_FPoint point;float depth;};
 Result operator()(float x,float y,float z) const {
  z*=.14f;float xx=x*std::cos(angle)+z*std::sin(angle),zz=-x*std::sin(angle)+z*std::cos(angle);
  return {{640+xx*scale*camera/(camera-zz),360-y*scale*camera/(camera-zz)},zz};
 }
} projection;
int faceIndex=0,subdivided=0,draws=0;
extern "C" int SDL_RenderGeometry(SDL_Renderer*,SDL_Texture *texture,const SDL_Vertex *v,int count,const int *indices,int n){
 if(!texture)return 0;
 assert(count<=CoverProjection::kMaxVertices);if(count>4)++subdivided;++draws;
 const auto &face=CardboardBox::Faces[faceIndex];
 for(int i=0;i<count;++i){
  // Artwork callback rotates and crops the UVs. Verify projection preserves it.
  float u=1-v[i].tex_coord.y,w=(v[i].tex_coord.x-.2f)/.6f;
  float weights[]={(1-u)*(1-w),u*(1-w),u*w,(1-u)*w};
  CardboardBox::Point p{};for(int j=0;j<4;++j){p.x+=weights[j]*face.points[j].x;p.y+=weights[j]*face.points[j].y;p.z+=weights[j]*face.points[j].z;}
  auto q=projection(p.x,p.y,p.z).point;
  assert(std::hypot(q.x-v[i].position.x,q.y-v[i].position.y)<.002f);
 }
 for(int i=0;i<n;++i)assert(indices[i]>=0&&indices[i]<count);
 return 0;
}
int main(){
 for(float scale:{250.f,700.f,1400.f})for(int angle=0;angle<360;angle+=3){
  projection.scale=scale;projection.angle=angle*3.14159265f/180;
  CardboardBox::Draw(nullptr,projection,[](int side,std::array<SDL_Vertex,4>&v)->SDL_Texture*{
   if(side<0)return nullptr;faceIndex=side;
   for(auto &p:v){auto old=p.tex_coord;p.tex_coord={.2f+.6f*old.y,1-old.x};}
   return reinterpret_cast<SDL_Texture*>(1);
  });
 }
 assert(subdivided>100 && draws>300);
 std::cout<<"PASS: cardboard front/back/spine perspective across 360 degrees and three zoom levels; rotated/cropped UVs preserved; bounded mesh\n";
}
