#include "title_install.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace TitleInstall;
void put(const std::string &p,const std::vector<unsigned char>&b){std::filesystem::create_directories(std::filesystem::path(p).parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());}
void number(std::vector<unsigned char>&b,size_t off,uint64_t v,int len){while(len--){b[off+len]=v&255;v>>=8;}}
void package(const std::string &p,uint64_t id,int version){std::vector<unsigned char>b(0xb04+48);number(b,0,0x10004,4);number(b,0x18c,id,8);number(b,0x1dc,version,2);number(b,0x1de,1,2);number(b,0xb04,1,4);number(b,0xb0c,64,8);put(p+"/title.tmd",b);put(p+"/title.tik",{1});put(p+"/title.cert",{1});put(p+"/00000001.app",std::vector<unsigned char>(64));}
#ifndef TITLE_INSTALL_BACKEND_TEST
int main(){
    package("install/Game",0x0005000012345600ULL,0);
    package("install/Update",0x0005000e12345600ULL,32);
    package("install/Old update",0x0005000e12345600ULL,16);
    package("install/DLC",0x0005000c12345600ULL,1);
    auto groups=Scan("install",3);assert(groups.size()==1&&groups[0].packages.size()==4&&groups[0].name=="Game");
    auto &ps=groups[0].packages;assert(ps[0].kind==Kind::Game&&ps[1].version==32&&ps[2].version==16&&ps[3].kind==Kind::DLC);
    assert(ps[0].selected&&ps[1].selected&&!ps[2].selected&&ps[3].selected);
    Package p;assert(ReadPackage("install/Game",p)&&p.error.empty()&&p.bytes==64);
    p.installedVersion[1]=3;assert(p.Installed(1)&&!p.Installed(0));p.version=4;assert(!p.Installed(1));
    std::filesystem::remove("install/Game/00000001.app");assert(ReadPackage("install/Game",p)&&!p.error.empty());
    package("invalid/system",0x0005001012345600ULL,1);assert(ReadPackage("invalid/system",p)&&p.error=="Unsupported title type");
    put("invalid/bad/title.tmd",{1,2,3});assert(ReadPackage("invalid/bad",p)&&!p.error.empty());
    auto bad=std::vector<unsigned char>(0xb04);number(bad,0,0x10004,4);number(bad,0x18c,0x0005000012345600ULL,8);number(bad,0x1de,4097,2);put("invalid/table/title.tmd",bad);
    assert(ReadPackage("invalid/table",p)&&!p.error.empty());
    assert(Scan("missing",3).empty());assert(BaseId(0x0005000212345600ULL)==0x0005000212345600ULL);
    std::cout<<"PASS: package identity, grouping/order, latest selection, installed versions, missing content, malformed TMD and system-title rejection\n";
}

#endif
