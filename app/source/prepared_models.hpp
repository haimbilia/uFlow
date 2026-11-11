#pragma once
#include <string>
#include <string_view>
#include <iterator>
#include <algorithm>
#include <cctype>
namespace PreparedModels {
struct Model {const char *id,*name;bool box;};
inline constexpr Model Models[]={
    {"auto","Automatic",true},{"cardboard","Cardboard box",true},{"dvd","DVD case",true},{"jewel","Jewel case",true},{"ps1-jewel","PS1 jewel case",true},
    {"nes","NES cartridge",false},{"snes","SNES cartridge",false},{"n64","N64 cartridge",false},
    {"gba","GBA cartridge",false},{"gb","Game Boy cartridge",false},{"ds","DS cartridge",false},{"genesis","Genesis cartridge",false},{"hucard","HuCard",false},{"wonderswan","WonderSwan cartridge",false},{"disc","Disc",false},{"mini-disc","GameCube mini disc",false}};
struct Defaults {std::string box="auto",media="auto";};
inline Defaults For(std::string id,std::string launcher={},std::string extensions={}){
    for(auto *s:{&id,&launcher,&extensions})for(char &c:*s)c=char(std::tolower(static_cast<unsigned char>(c)));
    const auto is=[&](const std::string &system,const char *ext){return id==system||launcher==system+"-vc"||launcher==system+"-retroarch"||extensions.find(ext)!=std::string::npos;};
    if(is("ds",".nds")||id=="nintendods"||launcher=="noods"||extensions.find(".srl")!=std::string::npos)return {"auto","ds"};
    if(is("snes",".sfc")||extensions.find(".smc")!=std::string::npos)return {"cardboard","snes"};
    if(is("nes",".nes")||extensions.find(".unf")!=std::string::npos)return {"cardboard","nes"};
    if(is("n64",".z64")||extensions.find(".n64")!=std::string::npos||extensions.find(".v64")!=std::string::npos)return {"cardboard","n64"};
    if(is("gba",".gba"))return {"cardboard","gba"};
    if(id=="wonderswan"||id=="wonderswancolor"||id=="ws"||id=="wsc"||launcher=="mednafen-wswan"||extensions.find(".ws")!=std::string::npos)return {"cardboard","wonderswan"};
    if(is("gb",".gb")||id=="gbc"||id=="gameboy"||id=="gameboycolor")return {"cardboard","gb"};
    if(id=="ps1"||id=="playstation"||launcher=="ps1")return {"ps1-jewel","ps1-jewel"};
    if(id=="gamecube"||id=="gc")return {"auto","mini-disc"};
    if(id=="wii"||id=="wiiu")return {"auto","disc"};
    if(id=="genesis"||id=="megadrive"||id=="md"||launcher=="genesis-plus-gx"||is("genesis",".md")||extensions.find(".gen")!=std::string::npos||extensions.find(".smd")!=std::string::npos)return {"cardboard","genesis"};
    if(is("pce",".pce"))return {"jewel","hucard"};
    if(is("msx",".mx1")||extensions.find(".mx2")!=std::string::npos)return {"cardboard","auto"};
    return {};
}
inline bool Valid(const std::string &id,bool box){for(auto &m:Models)if(id==m.id)return !box||m.box;return false;}
inline const char *Name(const std::string &id){for(auto &m:Models)if(id==m.id)return m.name;return "Automatic";}
inline std::string Cycle(const std::string &id,int direction,bool box){
    int i=0;for(int n=0;n<int(std::size(Models));++n)if(id==Models[n].id)i=n;
    do{i=(i+direction+int(std::size(Models)))%int(std::size(Models));}while(box&&!Models[i].box);
    return Models[i].id;
}
}
