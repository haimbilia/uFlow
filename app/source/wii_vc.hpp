#pragma once
#include "library.hpp"
#include "vc_inject.hpp"
#include <sys/stat.h>

namespace WiiVC {
// Check at launch, not per frame. Installed Wii VC still uses the system launcher.
inline bool IsRaw(const GameEntry &game) {
    if(game.installed || game.platform!=Platform::WiiU)return false;
    struct stat st{};
    for(const char *file:{"/content/hif_000000.nfs","/code/nn_hai_user.rpl","/code/frisbiiU.rpx"})
        if(stat((game.absolutePath+file).c_str(),&st)==0 && S_ISREG(st.st_mode))return true;
    return false;
}
bool HasPrepared(const GameEntry &game);
// Creates a private, split WBFS in the SD cache; never edits the source or saves.
// On success only the transient launch target changes. The library entry is retained.
std::string Prepare(GameEntry &game, VCInject::Progress progress = {});
}
