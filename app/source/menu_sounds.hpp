#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <array>
#include <string>
#include <sys/stat.h>

// Share SDL_mixer with menu music and the preview post-mix callback. Retain
// one audio-device reference; playing a cue never opens files or allocates.
class MenuSounds {
public:
    enum Cue { Move, Confirm, Back, Tab, Count };
    ~MenuSounds() { Stop(); }
    void Load(const std::string &directory) {
        Stop();
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0)return;
        if(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)!=0){SDL_QuitSubSystem(SDL_INIT_AUDIO);return;}
        opened_=true;
        const char *names[]={"move.wav","confirm.wav","back.wav","tab.wav"};
        for(int i=0;i<Count;++i) {
            const auto path=directory+"/"+names[i];
            struct stat info{};
            if(!stat(path.c_str(),&info) && S_ISREG(info.st_mode) && info.st_size>0 && info.st_size<=1024*1024)
                chunks_[i]=Mix_LoadWAV(path.c_str());
        }
        if(std::none_of(chunks_.begin(),chunks_.end(),[](auto *s){return s!=nullptr;}))Stop();
    }
    void Stop() {
        if(opened_) {
            if(channel_>=0)Mix_HaltChannel(channel_);
            for(auto *&s:chunks_){if(s)Mix_FreeChunk(s);s=nullptr;}
            Mix_CloseAudio();SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        opened_=false;channel_=-1;played_=false;
    }
    bool Available() const {return opened_;}
    void Play(Cue cue,bool enabled,int volume) {
        if(!enabled || volume<=0 || !opened_ || !chunks_[cue])return;
        const auto now=SDL_GetTicks();
        // Held scrolling must not stack dozens of clicks or cut every cue short.
        if(played_ && (cue==Move || cue==Tab) && now-lastPlayed_<65)return;
        if(channel_>=0)Mix_HaltChannel(channel_);
        Mix_VolumeChunk(chunks_[cue],std::clamp(volume,0,100)*MIX_MAX_VOLUME/100);
        channel_=Mix_PlayChannel(-1,chunks_[cue],0);
        if(channel_>=0){lastPlayed_=now;played_=true;}
    }
private:
    std::array<Mix_Chunk *,Count> chunks_{};
    bool opened_=false,played_=false;
    int channel_=-1;
    Uint32 lastPlayed_=0;
};
