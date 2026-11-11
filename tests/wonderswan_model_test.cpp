#include "cartridge_models.hpp"
#include "prepared_models.hpp"
#include "cover_presentation.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
int main(int argc,char **argv){
 assert(argc>=2);const std::string output=argv[1];
 using M=CartridgeModels::Model;using namespace CartridgeModels;
 for(const char *id:{"wonderswan","wonderswancolor","ws","wsc"}){
  assert(PreparedModels::For(id).media=="wonderswan");assert(CoverPresentation::Shape(id)==CoverPresentation::MediaShape::WonderSwan);
 }
 assert(PreparedModels::For("custom","",".ws,.wsc").media=="wonderswan");
 assert(PreparedModels::For("gba").media=="gba");
 assert(PreparedModels::Valid("wonderswan",false)&&!PreparedModels::Valid("wonderswan",true));
 assert(Geometry(M::WonderSwan,true).faceCount<=600&&Geometry(M::WonderSwan,false).faceCount<=350);
 for(bool near:{false,true}){
  auto mesh=Geometry(M::WonderSwan,near);float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
  for(unsigned i=0;i<mesh.pointCount;++i){auto p=mesh.points[i];const float v[]={p.x,p.y,p.z};for(int a=0;a<3;++a){assert(std::isfinite(v[a]));lo[a]=std::min(lo[a],v[a]);hi[a]=std::max(hi[a],v[a]);}}
  const auto &spec=Spec(M::WonderSwan);assert(std::abs((hi[0]-lo[0])*spec.height-spec.width)<.01f);assert(std::abs((hi[1]-lo[1])*spec.height-spec.height)<.01f);assert(std::abs((hi[2]-lo[2])*spec.height-spec.depth)<.01f);
  for(unsigned i=0;i<mesh.faceCount;++i){auto f=mesh.faces[i];assert(f.a<mesh.pointCount&&f.b<mesh.pointCount&&f.c<mesh.pointCount);}
 }
 SDL_Surface *screen=SDL_CreateRGBSurfaceWithFormat(0,1280,760,32,SDL_PIXELFORMAT_RGBA32);assert(screen);
 SDL_Renderer *r=SDL_CreateSoftwareRenderer(screen);assert(r);
 {
 Materials m;
 SDL_Surface *ls=argc>2?SDL_LoadBMP(argv[2]):SDL_CreateRGBSurfaceWithFormat(0,600,238,32,SDL_PIXELFORMAT_RGBA32);assert(ls);
 if(argc<=2)SDL_FillRect(ls,nullptr,SDL_MapRGBA(ls->format,224,229,224,255));auto *label=SDL_CreateTextureFromSurface(r,ls);SDL_FreeSurface(ls);assert(label);
 SDL_SetRenderDrawColor(r,31,35,42,255);SDL_RenderClear(r);
 for(int i=0;i<4;++i){SDL_ClearError();Draw(r,M::WonderSwan,{(i%2)*640,(i/2)*380,640,380},label,m,true,1,i==0?0.f:i==1?-.6f:i==2?3.14159f:2.5f,.1f,0);assert(!*SDL_GetError());}
 SDL_RenderPresent(r);assert(SDL_SaveBMP(screen,output.c_str())==0);
 auto *atlas=m.Get(r,M::WonderSwan);assert(atlas==m.Get(r,M::WonderSwan));int w,h;SDL_QueryTexture(atlas,nullptr,nullptr,&w,&h);assert(w*h*4==1310720);
 auto start=std::chrono::steady_clock::now();
 for(int i=0;i<120;++i){SDL_SetRenderDrawColor(r,31,35,42,255);SDL_RenderClear(r);SDL_ClearError();Draw(r,M::WonderSwan,{400,120,480,320},label,m,true,1,i*.0523599f,.1f,0);assert(!*SDL_GetError());SDL_RenderPresent(r);}
 printf("PASS: model selection, <=600/350 triangle LODs, shared 1.25 MiB atlas, full-turn rendering. Host software renderer %.2f ms/frame (not Wii U timing).\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120);
 SDL_DestroyTexture(label);
 }
 SDL_DestroyRenderer(r);SDL_FreeSurface(screen);
}
