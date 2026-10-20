#pragma once
#include <array>
#include <cstdint>

namespace AccentThemes {
struct Preset { const char *name; std::uint8_t r,g,b; };
// Keep IDs 0..3 stable so existing PLATFORM/CYAN/VIOLET/AMBER settings survive.
inline constexpr std::array<Preset,17> Presets{{
    {"PLATFORM",0,0,0},
    {"CYAN",0,208,230}, {"VIOLET",168,112,255}, {"AMBER",255,166,58},
    {"BLUE",66,142,255}, {"TEAL",26,187,169}, {"GREEN",64,190,103},
    {"LIME",159,203,55}, {"YELLOW",241,207,61}, {"ORANGE",255,126,49},
    {"RED",235,78,87}, {"CORAL",255,132,119}, {"PINK",236,113,190},
    {"ROSE",209,78,139}, {"INDIGO",116,120,232}, {"ICE",127,210,242},
    {"SILVER",166,184,200}
}};
inline constexpr int Count=static_cast<int>(Presets.size());
}
