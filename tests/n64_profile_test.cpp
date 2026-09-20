#include "n64_profile.hpp"
#include <cassert>
#include <map>
#include <iostream>
int main(){
    std::string rom(4096,'\0');rom.replace(0,4,"\x80\x37\x12\x40",4);rom.replace(0x3b,4,"NKTE");
    std::map<std::string,std::string> files={
        {"Undoe0.556.ini","[Cheat]\nCheat0=1\n"},
        {"Unkte0.024.ini",";MarioCart US\n[RomOption]\nRumble=0\n"},
        {"UNKTJ1.020.ini","Japanese revision 1"},
        {"UNKTP1.041.ini","PAL revision 1"},
        {"Unkte1.999.ini","US revision 1"},
        {"Unkte0.024.ini.bak","bad backup"}};
    std::vector<std::string> names;for(const auto &[name,text]:files)names.push_back(name);
    auto select=[&]{return N64Profile::Select(rom,names,[&](const auto &name){return files.at(name);});};
    assert(N64Profile::Identity(rom)=="nkte0");
    auto profile=select();assert(profile.source=="Unkte0.024.ini"&&profile.text==files.at(profile.source));
    rom[0x3f]=1;assert(select().source=="Unkte1.999.ini");
    rom[0x3e]='J';assert(select().source=="UNKTJ1.020.ini");
    rom[0x3f]=0;assert(select().source=="generic"); // Never apply a different revision.
    rom.replace(0x3b,4,"ZZZZ");profile=select();assert(profile.source=="generic"&&profile.text.find("Cheat")==std::string::npos);
    rom.replace(0x3b,4,"NKTE");files["Unkte0.025.ini"]="different";names.push_back("Unkte0.025.ini");
    bool rejected=false;try{select();}catch(const std::exception&){rejected=true;}assert(rejected);
    files["Unkte0.025.ini"]=files["Unkte0.024.ini"];assert(select().source=="Unkte0.024.ini");
    files["Unkte0.025.ini"]=files["Unkte0.024.ini"]="";assert(select().text.empty()); // Empty Nintendo profiles are valid.
    assert(!N64Profile::Matches("../Unkte0.024.ini","nkte0"));
    assert(!N64Profile::Matches("Unkte0.x.ini","nkte0"));
    assert(!N64Profile::Matches("Unkte0..ini","nkte0"));
    assert(!N64Profile::Matches("Unkte00.024.ini","nkte0"));
    assert(N64Profile::Matches("UNKTE0.024.INI","nkte0"));
    rom.resize(30);rejected=false;try{select();}catch(const std::exception&){rejected=true;}assert(rejected);
    std::cout<<"PASS: exact N64 ID/region/revision, case, unrelated and backup rejection, unknown generic, ambiguity, empty profile and bounds.\n";
}
