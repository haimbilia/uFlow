#pragma once
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace N64Profile {
inline constexpr const char *Version="UFLOW-N64-PROFILE-1";
inline constexpr const char *Marker="uflow.n64-profile";
inline std::string Lower(std::string text){for(auto &c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
inline std::string Identity(const std::string &rom){
    if(rom.size()<64||rom.compare(0,4,"\x80\x37\x12\x40",4)!=0)throw std::runtime_error("INVALID NORMALIZED N64 HEADER");
    return Lower(rom.substr(0x3b,4))+std::to_string(static_cast<unsigned char>(rom[0x3f]));
}
inline bool Matches(const std::string &filename,const std::string &identity){
    const auto name=Lower(filename);const auto prefix="u"+identity+".";
    if(name.rfind(prefix,0)!=0||name.size()<=prefix.size()+4||name.substr(name.size()-4)!=".ini")return false;
    // Profiles encode the four-character cartridge ID (including region), its
    // revision and a numeric database suffix. Never use the user's filename.
    return std::all_of(name.begin()+prefix.size(),name.end()-4,[](unsigned char c){return c>='0'&&c<='9';});
}
struct Selection {std::string source,text;};
template<class Reader> Selection Select(const std::string &rom,const std::vector<std::string> &names,Reader read){
    const auto id=Identity(rom);Selection selected;bool found=false;
    for(const auto &name:names)if(Matches(name,id)){
        auto text=read(name);
        if(text.size()>128*1024||text.find('\0')!=std::string::npos)throw std::runtime_error("INVALID N64 GAME PROFILE");
        if(found&&text!=selected.text)throw std::runtime_error("AMBIGUOUS N64 PROFILE - SUPPLY ROM.INI");
        if(!found||name<selected.source)selected={name,std::move(text)};
        found=true;
    }
    // Unknown games must not inherit another title's cheats, memory patches or
    // timing hacks. Explicit ROM.ini remains the override for compatibility.
    if(!found)selected={"generic","; uFlow: no exact cartridge/region/revision profile\n[RomOption]\n"};
    return selected;
}
}
