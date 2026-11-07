#include "settings_writer.hpp"
#include <cassert>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
static SDL_sem *entered,*releaseWrite;
static std::atomic<int> calls{0},last{0};
static bool SlowWrite(const UserSettings &s){
    if(++calls==1){SDL_SemPost(entered);SDL_SemWait(releaseWrite);}
    last=s.coverZoom;return true;
}
static bool Fail(const UserSettings &){return false;}
int main(){
    SDL_Init(0);entered=SDL_CreateSemaphore(0);releaseWrite=SDL_CreateSemaphore(0);
    {
        SettingsWriter writer(SlowWrite);UserSettings s;s.coverZoom=100;assert(writer.Submit(s));
        assert(SDL_SemWaitTimeout(entered,2000)==0);assert(!writer.Idle());
        const auto start=SDL_GetTicks();
        for(int i=101;i<=200;++i){s.coverZoom=i;assert(writer.Submit(s));}
        assert(SDL_GetTicks()-start<250);assert(calls==1); // Queue never waits for the blocked disk callback.
        SDL_SemPost(releaseWrite);writer.Drain();
        assert(writer.Idle()&&calls==2&&last==200&&writer.Poll().ok);
    }
    {SettingsWriter writer(Fail);assert(writer.Submit(UserSettings{}));writer.Drain();assert(!writer.Poll().ok);}
    calls=0;
    {SettingsWriter writer(SlowWrite);SDL_SemPost(releaseWrite);UserSettings s;s.coverZoom=173;assert(writer.Submit(s));}
    assert(last==173); // Destruction drains the latest pending snapshot.
    const auto root=std::filesystem::path("fs:/vol/external01/wiiu/apps/uFlow");std::filesystem::create_directories(root);
    UserSettings s;s.coverZoom=111;assert(SaveSettingsChecked(s));
    std::filesystem::create_directory(root/"uflow.cfg.tmp"); // Failure must preserve the previous settings.
    s.coverZoom=144;assert(!SaveSettingsChecked(s));assert(LoadSettings().coverZoom==111);
    std::filesystem::remove(root/"uflow.cfg.tmp");assert(SaveSettingsChecked(s));assert(LoadSettings().coverZoom==144);
    SDL_DestroySemaphore(entered);SDL_DestroySemaphore(releaseWrite);SDL_Quit();
    std::cout<<"PASS: blocked I/O does not block submissions, latest snapshot wins, failure reported, shutdown drains, failed save preserves settings\n";
}
