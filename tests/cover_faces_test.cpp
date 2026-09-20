#include "cover_faces.hpp"
#include "case_profiles.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
namespace fs=std::filesystem;
int main() {
    using CoverFaces::Face;
    GameEntry g;g.defaultSleevePath="default";
    std::set<std::string> available{"default","front","back","spine","full"};
    auto resolve=[&](Face face){return CoverFaces::Resolve(g,face,.46f,.54f,[&](const auto &p){return available.count(p);});};
    assert(resolve(Face::Front).path=="default" && resolve(Face::Front).u0==.54f);
    g.coverPath="front";
    assert(resolve(Face::Front).path=="front" && resolve(Face::Front).u0==0 && resolve(Face::Front).u1==1);
    assert(resolve(Face::Back).path=="default" && resolve(Face::Back).u1==.46f);
    assert(resolve(Face::Spine).path=="default" && resolve(Face::Spine).u0==.46f && resolve(Face::Spine).u1==.54f);
    g.backCoverPath="back";assert(resolve(Face::Back).path=="back" && resolve(Face::Back).u1==1);
    g.spinePath="spine";assert(resolve(Face::Spine).path=="spine" && resolve(Face::Spine).u0==0);
    g.fullCoverPath="full";for(auto face:{Face::Front,Face::Back,Face::Spine})assert(resolve(face).path=="full");
    // Decode failure of a full sleeve or separate face still permits fallback.
    available.erase("full");available.erase("back");assert(resolve(Face::Back).path=="default");
    available.erase("default");assert(resolve(Face::Back).path.empty());assert(resolve(Face::Front).path=="front");
    available.insert("default");g.defaultBackEnd=.47f;g.defaultFrontStart=.53f;
    assert(resolve(Face::Back).u1==.47f);
    const auto fit=CoverFaces::Contain(.7f,1.4f);assert(fit.x0==.25f && fit.x1==.75f && fit.y0==0);
    const auto wide=CoverFaces::Contain(2.f,1.f);assert(wide.y0==.25f && wide.y1==.75f && wide.x0==0);
    assert(CoverFaces::Contain(1.f,1.f).x1==1);
    const std::string root="fs:/vol/external01/wiiu/apps/uFlow";
    assert(!fs::exists(root));fs::create_directories(root);
    auto write=[&](const std::string &p,const std::string &s="image") {fs::create_directories(fs::path(p).parent_path());std::ofstream(p)<<s;};
    const auto base=root+"/media/nes";
    write(base+"/fullcovers/Default.png");write(base+"/front/Game.png");
    write(base+"/back/Game.png");write(base+"/spine/Default.png");
    write(base+"/fullcovers/Default.png.layout","UFLOW-SLEEVE-1 5 0.47 0.53\n");
    assert(SaveMediaSources({{MediaType::FullCover,-1,base+"/fullcovers",true,0,"nes"}}));
    auto sources=LoadMediaSources();assert(sources.size()==4);
    assert(SaveMediaSources(sources));assert(LoadMediaSources().size()==4);
    GameEntry game;game.name="Game";game.libraryId="nes";game.platform=Platform::Files;
    game.absolutePath="fs:/vol/external01/roms/Game.nes";
    std::vector<GameEntry> games{game};EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath.empty() && games[0].defaultSleevePath==base+"/fullcovers/Default.png");
    assert(games[0].coverPath==base+"/front/Game.png" && games[0].backCoverPath==base+"/back/Game.png");
    assert(games[0].spinePath==base+"/spine/Default.png");
    assert(games[0].defaultBackEnd==.47f && games[0].defaultFrontStart==.53f);
    assert(SaveLibraryCache(games));games.clear();assert(LoadLibraryCache(games));
    assert(games[0].spinePath==base+"/spine/Default.png" && games[0].backCoverPath==base+"/back/Game.png" && !games[0].defaultSleevePath.empty());
    assert(games[0].defaultBackEnd==.47f && games[0].defaultFrontStart==.53f);
    write(base+"/fullcovers/Default.png.layout","UFLOW-SLEEVE-1 6 0.47 0.53\n");
    games={game};EnrichLibraryMedia(games);assert(games[0].defaultBackEnd==0); // replaced image / wrong-size metadata ignored
    sources[2].enabled=false;assert(SaveMediaSources(sources));games={game};EnrichLibraryMedia(games);assert(games[0].backCoverPath.empty());
    write(base+"/fullcovers/Game.png");games={game};EnrichLibraryMedia(games);assert(games[0].fullCoverPath==base+"/fullcovers/Game.png");
    write(root+"/media_sources.cfg","front\tid:nes\t1\t0\t"+base+"/front\nback\tid:nes\t1\t0\t"+base+"/back\nspine\tid:nes\t1\t0\t"+base+"/spine\n");
    assert(LoadMediaSources().size()==3 && LoadMediaSources()[0].type==MediaType::Cover);
    // Per-system defaults and pre-preset installer migration; explicit custom
    // dimensions survive, even when they intentionally equal the old DVD size.
    for(const auto *id:{"wiiu","wii","gamecube","ds","nes","snes","gba","n64","pce","msx"}) {
        const auto size=CaseProfiles::For(id);
        write(root+"/libraries.cfg",std::string(id)+"\tTest\tfiles\twiiflow\t.rom\t1\t0\t"+id+"-vc\t135\t190\t14\n");
        auto libraries=LoadLibraries();assert(libraries.size()==1);
        assert(libraries[0].caseWidthMm==size.width && libraries[0].caseHeightMm==size.height && libraries[0].caseThicknessMm==size.thickness);
        libraries[0].caseWidthMm=135;libraries[0].caseHeightMm=190;libraries[0].caseThicknessMm=14;
        assert(SaveLibraries(libraries));libraries=LoadLibraries();assert(libraries[0].caseWidthMm==135 && libraries[0].caseHeightMm==190 && libraries[0].caseThicknessMm==14);
    }
    assert(CaseProfiles::For("custom",{},".gba").height==127);
    assert(CaseProfiles::For("custom","n64-vc").width==178);
    for(const auto size:{CaseProfiles::For("nes"),CaseProfiles::For("custom","nes-vc"),CaseProfiles::For("custom",{},".unif"),CaseProfiles::For("custom",{},".unf"),CaseProfiles::For("custom",{},".nes")}) {
        assert(size.width==130 && size.height==178 && size.thickness==25);
        // Match the user's 3366x2100 sleeve, including its central spine.
        assert(std::abs(float(2*size.width+size.thickness)/size.height-3366.f/2100.f)<.003f);
        assert(std::abs(float(size.width)/size.height-1538.f/2100.f)<.003f);
    }
    std::cout<<"PASS: independent faces, sleeve/part/default priority, failed-decode fallback, sibling sources, disabled sources, cache roundtrip and system size migration\n";
}
