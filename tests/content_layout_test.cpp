#include "library.hpp"
#include "cover_presentation.hpp"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
 namespace fs=std::filesystem;
 const std::string app="fs:/vol/external01/wiiu/apps/uFlow";fs::create_directories(app);
 {std::ofstream f(app+"/libraries.cfg");f<<"gba\tGBA\tfiles\twiiflow\t.gba\t1\t0\tgba-retroarch\t127\t127\t25\t1\t0\tauto\tgba\n";}
 auto libs=LoadLibraries();assert(libs.size()==1&&libs[0].viewType==-1);
 assert(libs[0].boxColor==-1);
 for(int color:{-1,0,0x008fba,0xffffff}){libs[0].boxColor=color;assert(SaveLibraries(libs,false));assert(LoadLibraries()[0].boxColor==color);}
 auto invalid=libs;invalid[0].boxColor=0x1000000;assert(!SaveLibraries(invalid,false));invalid[0].boxColor=-2;assert(!SaveLibraries(invalid,false));
 libs[0].viewType=CoverPresentation::Media;libs[0].viewLayout=4;
 assert(SaveLibraries(libs,false));auto saved=LoadLibraries();assert(saved[0].viewType==CoverPresentation::Media&&saved[0].viewLayout==4&&saved[0].mediaModel=="gba");
 auto bad=saved;bad[0].viewLayout=5;assert(!SaveLibraries(bad,false));assert(LoadLibraries()[0].viewLayout==4);
 bad=saved;bad[0].viewType=4;assert(!SaveLibraries(bad,false));
 saved[0].viewType=-1;assert(SaveLibraries(saved,false));assert(LoadLibraries()[0].viewType==-1);
 std::cout<<"PASS: legacy display inheritance, layout persistence, invalid values rejected without changing config, return to global default\n";
}
