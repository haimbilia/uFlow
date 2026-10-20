#pragma once
#include <string>

// Product codes from the bundled original VC bases. Wii/GC injections use
// the embedded disc ID (TeconMoon Form1.cs); only interpret those IDs when
// the title actually contains a Wii VC firmware image.
namespace WiiUVC {
inline std::string System(const std::string &product, bool wiiContainer=false,
                          bool pceContent=false, bool gbaEngine=false) {
    if(product.size()!=10 || product.rfind("WUP-N-",0)!=0)
        return wiiContainer?"wii":pceContent?"pce":gbaEngine?"gba":"wiiu";
    const char code=product[6];
    if(wiiContainer)return code=='G'||code=='D'?"gamecube":"wii";
    if(pceContent)return "pce";
    if(gbaEngine)return "gba";
    switch(code){
        case 'F':return "nes";
        case 'J':return "snes";
        case 'N':return "n64";
        case 'D':return "ds";
        case 'M':return "msx";
        // P is shared by GBA and PC Engine; PN is the PC Engine family.
        case 'P':return product[7]=='N'?"pce":"gba";
        case 'V':return "wii";
        default:return "wiiu";
    }
}
inline bool Recognized(const std::string &id){
    return id=="nes"||id=="snes"||id=="n64"||id=="ds"||id=="gba"||
           id=="pce"||id=="msx"||id=="wii"||id=="gamecube";
}
}
