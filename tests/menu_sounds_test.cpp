#include "menu_sounds.hpp"
#include <cassert>
#include <cstdio>

int main(int argc,char **argv) {
    assert(argc==2);
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    assert(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)==0);
    MenuSounds sounds;
    sounds.Load(argv[1]);assert(sounds.Available());
    sounds.Play(MenuSounds::Move,false,35);assert(Mix_Playing(-1)==0);
    sounds.Play(MenuSounds::Move,true,0);assert(Mix_Playing(-1)==0);
    sounds.Play(MenuSounds::Move,true,35);assert(Mix_Playing(-1)==1);
    for(int i=0;i<100;++i)sounds.Play(MenuSounds::Move,true,35);
    assert(Mix_Playing(-1)==1);
    // A music/video client closing its reference must not close the SFX device.
    Mix_CloseAudio();assert(Mix_QuerySpec(nullptr,nullptr,nullptr));
    sounds.Play(MenuSounds::Back,true,50);assert(Mix_Playing(-1)==1);
    assert(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)==0);
    sounds.Stop();assert(Mix_Playing(-1)==0);
    assert(Mix_QuerySpec(nullptr,nullptr,nullptr));
    sounds.Load("/missing-menu-sounds");assert(!sounds.Available());
    assert(Mix_QuerySpec(nullptr,nullptr,nullptr));
    Mix_CloseAudio();
    // HOME/background release must close the last reference, then resume cleanly.
    sounds.Load(argv[1]);assert(sounds.Available());
    sounds.Play(MenuSounds::Confirm,true,35);
    sounds.Stop();assert(!Mix_QuerySpec(nullptr,nullptr,nullptr));
    sounds.Load(argv[1]);assert(sounds.Available());
    sounds.Play(MenuSounds::Back,true,35);assert(Mix_Playing(-1)==1);
    sounds.Stop();assert(!Mix_QuerySpec(nullptr,nullptr,nullptr));
    SDL_Quit();
    puts("PASS: SFX toggle/volume, bounded repeats, replacement cues, shared mixer lifetime, missing assets");
}
