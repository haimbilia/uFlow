#include "ps1_jewel.hpp"
#include <cassert>
#include <cstdio>
struct View {
    float camera=2.45f;
    struct Result {SDL_FPoint point;float depth;};
    Result operator()(float x,float y,float z)const{
        x*=142.f/190;y*=125.f/190;z*=14.f/190;
        float d=-x*.29552f+z*.955337f;
        return {{256+(x*.955337f+z*.29552f)*650*camera/(camera-d),256-y*650*camera/(camera-d)},d};
    }
};
int main(){
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto*w=SDL_CreateWindow("Jewel regression",0,0,512,512,SDL_WINDOW_HIDDEN);
    auto*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED);assert(r);
    {
        Ps1Jewel::Materials materials;
        std::vector<unsigned char> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto*p=&pixels[(y*64+x)*4];p[0]=x<32?240:20;p[1]=y<32?200:30;p[2]=50;}
        auto*t=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,64,64);assert(t);SDL_UpdateTexture(t,nullptr,pixels.data(),64*4);
        const auto render=[&](bool branded,bool media,float angle){
            SDL_SetRenderDrawColor(r,20,25,30,255);SDL_RenderClear(r);
            Ps1Jewel::Draw(r,View{},materials,media,{t},{t},{},t,angle,1,branded);
            std::vector<unsigned char> result(512*512*4);
            assert(SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_RGBA32,result.data(),512*4)==0);return result;
        };
        for(bool branded:{false,true}){
            auto box=render(branded,false,0);
            assert(box==render(branded,false,1.5707963f));
            auto disc=render(branded,true,0);
            assert(box!=disc);assert(disc!=render(branded,true,1.5707963f));
            assert(disc==render(branded,true,Ps1Jewel::DiscAngle(3000)));
        }
        assert(render(false,true,0)!=render(true,true,0));
        assert(materials.PlainShell(r)==materials.PlainShell(r));
        SDL_DestroyTexture(t);
    }
    SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
    puts("PASS: both jewel cases display cover or disc; only visible disc artwork spins; full turn restores image; distinct cached unbranded shell");
}
