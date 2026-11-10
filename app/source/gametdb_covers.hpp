#pragma once
#include "library.hpp"
#include <memory>
#include <string>
#include <vector>

// Background GameTDB downloader: front covers, full sleeves and metadata parsed
// from wiiutdb.xml (Wii U) / wiitdb.xml (Wii, GameCube). Files are saved by
// title ID into the configured media folders, so the regular media refresh
// applies them; nothing here touches the library or textures.
namespace GameTdbCovers {
struct Snapshot {
    bool running=false,finished=false,cancelled=false;
    std::size_t done=0,total=0,saved=0,skipped=0,missing=0,failed=0,metadata=0;
    bool databaseError=false; // a GameTDB database could not be downloaded or read
    std::string current,error;
};
struct Targets { std::string front,full,metadata; };
class Downloader {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    Downloader(); ~Downloader();
    Downloader(const Downloader&)=delete; Downloader &operator=(const Downloader&)=delete;
    // Copies only the identifying fields it needs from each game.
    bool Start(const std::vector<GameEntry> &games,const Targets &targets);
    void Cancel();
    Snapshot Poll() const;
    // Returns true once after a finished run that saved at least one file.
    bool TakeNewArtwork();
};
// First enabled shared (All Libraries) folder of each type, else the default media folder.
Targets ChooseTargets(const std::vector<MediaSource> &sources);
}
