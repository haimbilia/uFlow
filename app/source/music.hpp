#pragma once
#include <string>
#include <vector>
#ifndef UFLOW_HOST_PREVIEW
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#endif

class MenuMusic {
public:
    // Call once at app startup, before playback or scanning, even when music is OFF.
    static std::string SilenceStartup();
    ~MenuMusic() { Stop(); }
    std::string Play(const std::string &path,int volume);
    void Stop();
    void Volume(int volume);
    void Pause(bool pause);
private:
#ifndef UFLOW_HOST_PREVIEW
    Mix_Music *music_=nullptr;
    bool opened_=false;
    std::vector<unsigned char> bytes_;
#endif
};
std::vector<std::string> ListMusicTracks();
inline constexpr const char *kMusicDirectory="fs:/vol/external01/wiiu/apps/uFlow/music";
