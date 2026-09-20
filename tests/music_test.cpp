#include "music.hpp"
#include "settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char **argv) {
    // Caller supplies an isolated temporary working directory and generated tones.
    namespace fs=std::filesystem;
    assert(!fs::exists("fs:")); fs::create_directories(kMusicDirectory);
    for(const auto *ext:{"wav","mp3","ogg"}) fs::copy_file(std::string("tone.")+ext,std::string(kMusicDirectory)+"/tone."+ext);
    std::ofstream(std::string(kMusicDirectory)+"/ignored.txt")<<"not audio";
    fs::create_directory(std::string(kMusicDirectory)+"/directory.mp3");
    assert(ListMusicTracks().size()==3);
    UserSettings settings; settings.musicEnabled=true; settings.musicVolume=65; settings.musicTrack="tone.ogg";
    SaveSettings(settings); const auto loaded=LoadSettings();
    assert(loaded.musicEnabled && loaded.musicVolume==65 && loaded.musicTrack=="tone.ogg");
    settings.musicEnabled=false; SaveSettings(settings);
    assert(!LoadSettings().musicEnabled);
    assert(MenuMusic::SilenceStartup().empty());
    assert(SDL_WasInit(SDL_INIT_AUDIO)==0);
    assert(!Mix_PlayingMusic());
    assert(!LoadSettings().musicEnabled && LoadSettings().musicTrack=="tone.ogg");
    assert(MenuMusic::SilenceStartup().empty());
    assert(SDL_WasInit(SDL_INIT_AUDIO)==0);
    MenuMusic music;
    for(const auto *ext:{"wav","mp3","ogg"}) {
        assert(music.Play(std::string(kMusicDirectory)+"/tone."+ext,35).empty());
        SDL_Delay(80); assert(Mix_PlayingMusic());
        music.Pause(true); assert(Mix_PausedMusic()); music.Pause(false);
        music.Volume(200); assert(Mix_VolumeMusic(-1)==MIX_MAX_VOLUME);
        music.Stop(); music.Stop();
    }
    assert(!music.Play("missing.wav",35).empty());
    std::ofstream("broken.wav")<<"invalid";
    assert(!music.Play("broken.wav",35).empty());
    std::ofstream("large.wav")<<"x"; fs::resize_file("large.wav",33*1024*1024);
    assert(!music.Play("large.wav",35).empty());
    for(int i=1;i<argc;++i) {
        assert(music.Play(argv[i],35).empty()); SDL_Delay(100); assert(Mix_PlayingMusic()); music.Stop();
        std::cout<<"PASS: supplied track decoded and played from memory\n";
    }
    std::cout<<"PASS: silent startup/OFF persistence, MP3/OGG/WAV memory playback, pause/resume/stop, volume, settings persistence, filtering, invalid/missing/oversize files\n";
}
