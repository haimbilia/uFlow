#pragma once
#include "library.hpp"
#include <algorithm>
#include <cmath>

namespace CoverFaces {
enum class Face { Back, Spine, Front };
struct Selection { std::string path; float u0=0, u1=1; };
// A full sleeve wins. Otherwise each face independently falls back to the
// corresponding section of the default sleeve. Front images are never guessed
// to be sleeves from their aspect ratio (landscape boxes are valid fronts).
template<class Available>
Selection Resolve(const GameEntry &game,Face face,float backEnd,float frontStart,Available available) {
    auto sleeve=[&](const std::string &path) {
        const bool authored=path==game.defaultSleevePath && game.defaultBackEnd>0 && game.defaultFrontStart>game.defaultBackEnd && game.defaultFrontStart<1;
        const float back=authored?game.defaultBackEnd:backEnd,front=authored?game.defaultFrontStart:frontStart;
        return Selection{path,face==Face::Back?0.f:face==Face::Spine?back:front,
                              face==Face::Back?back:face==Face::Spine?front:1.f};
    };
    if(!game.fullCoverPath.empty() && available(game.fullCoverPath))return sleeve(game.fullCoverPath);
    const auto &part=face==Face::Front?game.coverPath:face==Face::Back?game.backCoverPath:game.spinePath;
    if(!part.empty() && available(part))return {part,0,1};
    if(!game.defaultSleevePath.empty() && available(game.defaultSleevePath))return sleeve(game.defaultSleevePath);
    return {};
}
struct Fit { float x0=0,y0=0,x1=1,y1=1; };
inline Fit Contain(float sourceAspect,float faceAspect) {
    Fit fit;
    if(sourceAspect<=0 || faceAspect<=0)return fit;
    // Small trim/scan differences should not add visible borders.
    if(std::abs(sourceAspect/faceAspect-1.f)<.08f)return fit;
    if(sourceAspect>faceAspect){const float h=faceAspect/sourceAspect;fit.y0=(1-h)*.5f;fit.y1=1-fit.y0;}
    else {const float w=sourceAspect/faceAspect;fit.x0=(1-w)*.5f;fit.x1=1-fit.x0;}
    return fit;
}
}
