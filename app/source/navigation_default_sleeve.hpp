#pragma once
#include "library_tabs.hpp"
#include <map>

// Presentation fallback is resolved from the displayed system, even before an
// asynchronous media scan has finished. Never replace real game artwork.
inline void ApplyNavigationDefaultSleeve(GameEntry &game,const std::map<std::string,GameMedia> &defaults){
    const auto id=GameLibraryId(game);
    if(game.platform!=Platform::WiiU||!game.installPackages.empty())return;
    if(id==game.libraryId){
        if(game.sourceMedia&&game.mediaLibraryId.empty()&&game.defaultMediaLibraryId.empty())RestoreSourceMedia(game);
        return;
    }
    const auto original=game.sourceMedia?*game.sourceMedia:CaptureGameMedia(game);
    const auto source=defaults.find(game.libraryId);
    const auto generic=[&](const std::string &path){return !path.empty() &&
        (path==original.sleeve||(source!=defaults.end()&&path==source->second.sleeve));};
    if(!generic(game.defaultSleevePath)&&!generic(game.fullCoverPath)&&!generic(game.coverPath))return;
    if(!game.sourceMedia)game.sourceMedia=std::make_shared<GameMedia>(original);
    for(auto *path:{&game.coverPath,&game.fullCoverPath,&game.backCoverPath,&game.spinePath})if(generic(*path))path->clear();
    const auto target=defaults.find(id);
    game.defaultSleevePath=target==defaults.end()?"":target->second.sleeve;
    game.defaultBackEnd=target==defaults.end()?0:target->second.backEnd;
    game.defaultFrontStart=target==defaults.end()?0:target->second.frontStart;
    // Leave worker completion markers alone: real artwork may still need work.
}
