#pragma once
#include <string>
#include <cctype>

// Display presets for common retail packaging. Regional/third-party boxes vary;
// the library's explicit dimensions remain authoritative.
namespace CaseProfiles {
struct Size { int width=135, height=190, thickness=14; };
inline Size For(std::string id, std::string launcher={}, std::string extensions={}) {
    for(auto *text:{&id,&launcher,&extensions})
        for(char &c:*text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto is=[&](const char *system,const char *ext){
        return id==system || launcher==std::string(system)+"-vc" ||
            (!extensions.empty() && extensions.find(ext)!=std::string::npos);
    };
    if(is("ds",".nds") || launcher=="noods" || id=="nintendods" || id=="nintendo_ds" || id=="nintendo-ds" ||
       extensions.find(".srl")!=std::string::npos || extensions.find(".dsi")!=std::string::npos)return {135,122,15};
    // User-supplied NES sleeve: 3366x2100, about 1538/290/1538 px.
    // Normalize its proportions to height 178; not a physical measurement.
    if(is("nes",".nes") || extensions.find(".unf")!=std::string::npos || extensions.find(".unif")!=std::string::npos)return {130,178,25};
    if(is("snes",".sfc") || extensions.find(".smc")!=std::string::npos)return {178,125,30};
    if(is("n64",".z64") || extensions.find(".v64")!=std::string::npos || extensions.find(".n64")!=std::string::npos)return {178,125,30};
    if(is("gba",".gba"))return {127,127,25};
    if(is("pce",".pce"))return {142,125,10};
    if(is("msx",".mx1") || extensions.find(".mx2")!=std::string::npos)return {135,190,25};
    return {};
}
}
