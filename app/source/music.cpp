#include "music.hpp"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <new>
#include <sys/stat.h>

std::string MenuMusic::SilenceStartup() {
#ifndef UFLOW_HOST_PREVIEW
    // The Wii U launch sound can keep playing until an application takes over AX.
    // An OFF preference must still exercise the audio-device open/close lifecycle.
    // Do not play a track, load codecs/files, or keep a silent streaming thread alive.
    // Let SDL perform AX setup/teardown on its required core instead of calling AX here.
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) return "STARTUP AUDIO INITIALIZATION FAILED";
    if(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)!=0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return "STARTUP AUDIO DEVICE UNAVAILABLE";
    }
    Mix_VolumeMusic(0);
    Mix_HaltMusic();
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
    return {};
}

std::vector<std::string> ListMusicTracks() {
    std::vector<std::string> tracks;
    DIR *dir=opendir(kMusicDirectory);
    if(!dir) return tracks;
    while(auto *entry=readdir(dir)) {
        const std::string name=entry->d_name;
        const auto dot=name.find_last_of('.');
        if(dot==std::string::npos) continue;
        std::string ext=name.substr(dot);
        for(auto &c:ext) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(ext!=".mp3" && ext!=".ogg" && ext!=".wav") continue;
        struct stat info{};
        if(!stat((std::string(kMusicDirectory)+"/"+name).c_str(),&info) && S_ISREG(info.st_mode)) tracks.push_back(name);
    }
    closedir(dir); std::sort(tracks.begin(),tracks.end()); return tracks;
}
void MenuMusic::Stop() {
#ifndef UFLOW_HOST_PREVIEW
    if(music_) { Mix_HaltMusic(); Mix_FreeMusic(music_); music_=nullptr; }
    if(opened_) { Mix_CloseAudio(); Mix_Quit(); SDL_QuitSubSystem(SDL_INIT_AUDIO); opened_=false; }
    std::vector<unsigned char>().swap(bytes_);
#endif
}
void MenuMusic::Volume(int volume) {
#ifndef UFLOW_HOST_PREVIEW
    if(opened_) Mix_VolumeMusic(std::clamp(volume,0,100)*MIX_MAX_VOLUME/100);
#else
    (void)volume;
#endif
}
void MenuMusic::Pause(bool pause) {
#ifndef UFLOW_HOST_PREVIEW
    if(music_) { if(pause) Mix_PauseMusic(); else Mix_ResumeMusic(); }
#else
    (void)pause;
#endif
}
std::string MenuMusic::Play(const std::string &path,int volume) {
    Stop();
#ifndef UFLOW_HOST_PREVIEW
    // Retain just one bounded compressed file; decoder never reads from a sleeping drive.
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) return "MUSIC FILE UNAVAILABLE";
    const auto length=file.tellg();
    if(length<=0 || length>32*1024*1024) return "MUSIC MUST BE BETWEEN 1 BYTE AND 32 MIB";
    try { bytes_.resize(static_cast<std::size_t>(length)); }
    catch(const std::bad_alloc &) { return "NOT ENOUGH MEMORY FOR THIS MUSIC FILE"; }
    file.seekg(0);
    if(!file.read(reinterpret_cast<char *>(bytes_.data()),length)) { Stop(); return "MUSIC READ FAILED"; }
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) { Stop(); return "AUDIO INITIALIZATION FAILED"; }
    Mix_Init(MIX_INIT_MP3|MIX_INIT_OGG);
    if(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)!=0) {
        Mix_Quit(); SDL_QuitSubSystem(SDL_INIT_AUDIO); Stop(); return "AUDIO DEVICE UNAVAILABLE";
    }
    opened_=true;
    SDL_RWops *rw=SDL_RWFromConstMem(bytes_.data(),static_cast<int>(bytes_.size()));
    music_=rw?Mix_LoadMUS_RW(rw,1):nullptr;
    if(!music_) { Stop(); return "UNSUPPORTED OR DAMAGED MUSIC FILE"; }
    Volume(volume);
    if(Mix_PlayMusic(music_,-1)!=0) { Stop(); return "MUSIC PLAYBACK FAILED"; }
#else
    (void)path; (void)volume;
#endif
    return {};
}
