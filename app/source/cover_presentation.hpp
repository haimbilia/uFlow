#pragma once
#include <algorithm>
#include <string>
#include <cctype>

namespace CoverPresentation {
enum Type { Box, Logo, Media, Thumbnails, TypeCount };
inline constexpr const char *Types[]={"BOX","LOGO","MEDIA","THUMBNAILS"};
inline constexpr const char *BoxLayouts[]={"CLASSIC FLOW","CAROUSEL","FLAT ROW","STACKED","FAN","ARC","GALLERY","WALL","BOOKSHELF","CURVED WALL"};
inline constexpr const char *LogoLayouts[]={"LOGO ROW","WHEEL RIGHT","WHEEL LEFT","WHEEL TOP","WHEEL BOTTOM","LOGO ARC","LOGO WALL"};
inline constexpr const char *MediaLayouts[]={"MEDIA ROW","MEDIA ARC","MEDIA FAN","MEDIA WALL","MEDIA BOOKSHELF"};
inline constexpr const char *ThumbnailLayouts[]={"THUMBNAIL ROW","THUMBNAIL WALL","WII U MENU"};
inline int LayoutCount(int type){return type==Box?10:type==Logo?7:type==Thumbnails?3:5;}
inline const char *LayoutName(int type,int layout) {
    layout=std::clamp(layout,0,LayoutCount(type)-1);
    return type==Thumbnails?ThumbnailLayouts[layout]:type==Logo?LogoLayouts[layout]:type==Media?MediaLayouts[layout]:BoxLayouts[layout];
}
enum class MediaShape { Disc, MiniDisc, NES, SNES, N64, GBA, DS, HuCard, Cartridge, GB, Genesis, WonderSwan };
inline MediaShape Shape(std::string id,std::string path={}) {
    for(auto *s:{&id,&path})for(char &c:*s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto ext=path.substr(path.find_last_of('.')==std::string::npos?path.size():path.find_last_of('.'));
    if(id=="ds" || id=="nintendods" || ext==".nds" || ext==".srl")return MediaShape::DS;
    if(id=="nes" || ext==".nes" || ext==".unf" || ext==".unif")return MediaShape::NES;
    if(id=="snes" || ext==".sfc" || ext==".smc")return MediaShape::SNES;
    if(id=="n64" || ext==".z64" || ext==".v64" || ext==".n64")return MediaShape::N64;
    if(id=="gba" || ext==".gba")return MediaShape::GBA;
    if(id=="gb" || id=="gbc" || id=="gameboy" || id=="gameboycolor" || ext==".gb" || ext==".gbc")return MediaShape::GB;
    if(id=="genesis"||id=="megadrive"||id=="md"||ext==".md"||ext==".gen"||ext==".smd")return MediaShape::Genesis;
    if(id=="wonderswan"||id=="wonderswancolor"||id=="ws"||id=="wsc"||ext==".ws"||ext==".wsc")return MediaShape::WonderSwan;
    if(id=="hucard" || id=="pce" || ext==".pce")return MediaShape::HuCard;
    if(id=="mini-disc" || id=="gamecube" || id=="gc" || ext==".gcm")return MediaShape::MiniDisc;
    if(id=="wiiu" || id=="wii" || id=="ps1" || id=="playstation" || ext==".iso" || ext==".chd" || ext==".cue" || ext==".wbfs")return MediaShape::Disc;
    return MediaShape::Cartridge;
}
}
