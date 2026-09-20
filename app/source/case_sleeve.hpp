#pragma once
#include <array>
#include <cmath>

namespace CaseSleeve {
struct Rail { float x, z, u; };
inline constexpr float kHalfHeight=.482f;

// One continuous back -> rounded hinge -> spine -> hinge -> front strip.
// Adjacent panels use the SAME position and UV rail; no floating decal edges.
inline std::array<Rail,6> Rails(float widthMm,float thicknessMm) {
    const float total=2.f*widthMm+thicknessMm;
    const float backEnd=widthMm/total,frontStart=(widthMm+thicknessMm)/total;
    const float hingeLength=std::hypot(.024f*widthMm,.015f*thicknessMm);
    const float hingeU=(frontStart-backEnd)*hingeLength/(thicknessMm+2.f*hingeLength);
    return {{{.478f,-.515f,0.f}, {-.478f,-.515f,backEnd},
             {-.502f,-.5f,backEnd+hingeU}, {-.502f,.5f,frontStart-hingeU},
             {-.478f,.515f,frontStart}, {.478f,.515f,1.f}}};
}
} // namespace CaseSleeve
