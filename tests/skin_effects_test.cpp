#include "skin_effects.hpp"
#include "skin_layout.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
int main(){
    const auto reject=[](const std::string&s){bool failed=false;try{SkinLayout::Parse(s);}catch(...){failed=true;}assert(failed);};
    const std::string base=R"({"schema":1,"nodes":[{"kind":"library","width":600,"height":300}]})";
    auto original=SkinLayout::Parse(base);assert(!original.Enhanced());
    const std::string nodes=R"([{"kind":"library","width":600,"height":300}])";
    auto doc=SkinLayout::Parse("{\"schema\":1,\"particles\":192,\"trails\":3,\"nodes\":"+nodes+",\"variants\":[{\"type\":3,\"layout\":0,\"nodes\":"+nodes+"}]}");
    assert(doc.Enhanced()&&&doc.Nodes(3,0)==&doc.variants[0].nodes&&&doc.Nodes(0,1)==&doc.nodes);
    reject("{\"schema\":1,\"particles\":193,\"nodes\":"+nodes+"}");
    reject("{\"schema\":1,\"trails\":4,\"nodes\":"+nodes+"}");
    reject("{\"schema\":1,\"nodes\":"+nodes+",\"variants\":[{\"type\":3,\"nodes\":"+nodes+"},{\"type\":3,\"nodes\":"+nodes+"}]}");
    reject(R"({"schema":1,"nodes":[{"kind":"effects"},{"kind":"effects"},{"kind":"library"}]})");
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto *surface=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Window*window=nullptr;SDL_Renderer*r=nullptr;
    if(std::getenv("UFLOW_EFFECT_GPU")){window=SDL_CreateWindow("effect performance",0,0,1280,720,SDL_WINDOW_HIDDEN);r=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);}else r=SDL_CreateSoftwareRenderer(surface);assert(r);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SkinEffects::Renderer ambient,focus;SDL_FRect area{0,0,1280,720};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<120;++i){ambient.Ambient(r,192,3,3,i/60.f,area);focus.Focus(r,{400,180,220,320},3,i/60.f);assert(ambient.VertexCount()<=200&&ambient.SpriteDraws()<=50);SDL_RenderFlush(r);}
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120;
    assert(focus.Rebuilds()<=61);auto count=ambient.AtlasBuilds();assert(count==1);
    ambient.Ambient(r,192,3,3,119/60.f,area);assert(ambient.AtlasBuilds()==count);
    ambient.Ambient(r,192,3,0,3,area);assert(ambient.AtlasBuilds()==count&&ambient.SpriteDraws()==0);
    ambient.Ambient(r,48,1,1,3,area);assert(ambient.SpriteDraws()>0&&ambient.VertexCount()<=68);
    if(std::getenv("UFLOW_EFFECT_GPU")){ambient.Ambient(r,0,1,1,3,area);assert(ambient.SpriteDraws()==1);}
    SDL_BlendMode mode;SDL_GetRenderDrawBlendMode(r,&mode);assert(mode==SDL_BLENDMODE_BLEND);
    ambient.Ambient(r,192,3,3,4,{50,50,600,300});assert(ambient.AtlasBuilds()==count);
    std::cout<<"PASS: legacy opt-out, variant selection, limits, at most 200 ambient vertices / 300 expanded indices, single atlas reused, disabled zero-work, blend restoration. Render submission ms/frame="<<ms<<" sprites="<<ambient.SpriteDraws()<<"\n";
    ambient.Release();focus.Release();
    SDL_DestroyRenderer(r);if(window)SDL_DestroyWindow(window);SDL_FreeSurface(surface);SDL_Quit();
}
