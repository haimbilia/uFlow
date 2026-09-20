#include "update_transaction.hpp"
#include "update_trust.hpp"
#include "update_handoff.hpp"
#include "glass_ttf.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <whb/sdcard.h>
#include <coreinit/systeminfo.h>
#include <rpxloader/rpxloader.h>
#include <vpad/input.h>
// Aroma can remount the previous WUHB even for a bare RPX. Explicitly detach
// that inherited mount; this helper's code and embedded font do not need it.
int main(int,char **){
    if(!WHBMountSdCard())return 1;
    if(SDL_Init(SDL_INIT_VIDEO)!=0){WHBUnmountSdCard();return 1;}
    auto *window=SDL_CreateWindow("uFlow Update",0,0,1280,720,SDL_WINDOW_SHOWN);
    auto *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):nullptr;
    TTF_Init();auto *font=TTF_OpenFontRW(SDL_RWFromConstMem(glass_ttf,glass_ttf_size),1,26);
    bool running=true,launched=false,failed=false;std::string error;
    auto draw=[&](const std::string &message){
        if(!renderer||!font)return;
        SDL_SetRenderDrawColor(renderer,227,239,243,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,247,251,253,255);SDL_Rect panel{220,180,840,360};SDL_RenderFillRect(renderer,&panel);
        auto line=[&](const std::string &text,int y){auto *s=TTF_RenderUTF8_Blended_Wrapped(font,text.c_str(),{35,65,80,255},760);if(!s)return;auto *t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d{260,y,s->w,s->h};SDL_RenderCopy(renderer,t,nullptr,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);};
        line("uFlow Update",215);line(message,280);line(failed?"A / B: return to uFlow":"Keep the console on and the SD card inserted.",465);SDL_RenderPresent(renderer);
    };
    RPXLoader_InitLibrary();OSEnableHomeButtonMenu(false);
    try{
        Setup::Need(renderer&&font,"Cannot display update installer");draw("Verifying downloaded update...");
        UFlowUpdate::InstallDetached([]{return RPXLoader_UnmountCurrentRunningBundle()==RPX_LOADER_RESULT_SUCCESS;},[&]{
            UFlowUpdate::Transaction tx("fs:/vol/external01",UFlowUpdate::PublicKey,"stable");
            tx.InstallPrepared([&](int point){draw(point<3?"Installing verified update...":"Confirming installation...");});
        });
        draw("Update installed. Opening uFlow...");
    }catch(const std::exception &e){failed=true;error=e.what();draw(error);
        if(FILE *log=std::fopen("fs:/vol/external01/wiiu/apps/uFlow/cache/update/helper-error.txt","wb")){std::fprintf(log,"%s\n",error.c_str());std::fclose(log);}}
    OSEnableHomeButtonMenu(true);
    while(running){
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT)running=false;}
        if(!running)break;
        VPADStatus input{};VPADReadError status;VPADRead(VPAD_CHAN_0,&input,1,&status);
        if(!launched&&(!failed||(status==VPAD_READ_SUCCESS&&(input.trigger&(VPAD_BUTTON_A|VPAD_BUTTON_B))))){
            auto result=RPXLoader_PrepareLaunchFromSD("wiiu/apps/uFlow/uFlow.wuhb");
            if(result==RPX_LOADER_RESULT_SUCCESS)result=RPXLoader_LaunchPreparedHomebrew();
            if(result==RPX_LOADER_RESULT_SUCCESS)launched=true;
            else{failed=true;error=std::string("Cannot reopen uFlow: ")+RPXLoader_GetStatusStr(result);}
        }
        if(!launched)draw(error);else SDL_Delay(4);
    }
    if(font)TTF_CloseFont(font);TTF_Quit();if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);
    RPXLoader_DeInitLibrary();SDL_Quit();WHBUnmountSdCard();return failed?1:0;
}
