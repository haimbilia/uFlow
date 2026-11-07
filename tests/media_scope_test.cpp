#include "library.hpp"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
namespace fs=std::filesystem;
void Write(const std::string &p){fs::create_directories(fs::path(p).parent_path());std::ofstream(p)<<"image";}
int main(){
 const std::string root="fs:/vol/external01";
 assert(!fs::exists(root));fs::create_directories(root+"/wiiu/apps/uFlow");
 MediaSource nes{MediaType::Cover,-1,root+"/nes-old",true,0,"nes"};
 MediaSource snes{MediaType::Cover,-1,root+"/snes",true,0,"snes"};
 Write(nes.path+"/Game.png");Write(snes.path+"/Game.png");
 assert(SaveMediaSources({nes,snes}));CompleteMediaRefresh();
 GameEntry a;a.libraryId="nes";a.platform=Platform::Files;a.name="Game";a.absolutePath=root+"/Game.nes";
 GameEntry b=a;b.libraryId="snes";b.absolutePath=root+"/Game.sfc";
 std::vector<GameEntry> games{a,b};EnrichLibraryMedia(games);
 const auto oldOther=games[1].coverPath;
 nes.path=root+"/nes-renamed";Write(nes.path+"/Game.png");
 assert(SaveMediaSources({nes,snes}));assert(MediaRefreshNeeded()&&MediaRefreshIsScoped());
 // Pending scope is persisted, not dependent on UI memory. Removing unrelated
 // media makes an accidental refresh observable rather than just counting calls.
 fs::remove(snes.path+"/Game.png");
 std::vector<std::string> paths;
 RefreshPendingLibraryMedia(games,{},[&](const ScanProgress &p){paths.push_back(p.path);});
 assert(games[0].coverPath==nes.path+"/Game.png");assert(games[1].coverPath==oldOther);
 for(const auto &p:paths)assert(p.find(snes.path)==std::string::npos);
 CompleteMediaRefresh();
 // Removing a source clears its old matches; unrelated entries remain intact.
 assert(SaveMediaSources({snes}));RefreshPendingLibraryMedia(games,{});
 assert(games[0].coverPath.empty());assert(games[1].coverPath==oldOther);CompleteMediaRefresh();
 // Multiple edits before exiting settings union both old and new scopes.
 assert(SaveMediaSources({nes,snes}));snes.path=root+"/snes-new";Write(snes.path+"/Game.png");
 assert(SaveMediaSources({nes,snes}));RefreshPendingLibraryMedia(games,{});
 assert(games[0].coverPath==nes.path+"/Game.png"&&games[1].coverPath==snes.path+"/Game.png");CompleteMediaRefresh();
 // Explicit Refresh Media still processes all titles.
 fs::remove(snes.path+"/Game.png");assert(RequestMediaRefresh());assert(!MediaRefreshIsScoped());RefreshPendingLibraryMedia(games,{});
 assert(games[1].coverPath.empty());CompleteMediaRefresh();
 // Shared directories must refresh all eligible content.
 MediaSource shared{MediaType::Logo,-1,root+"/shared",true,0};Write(shared.path+"/Game.png");
 assert(SaveMediaSources({nes,snes,shared}));RefreshPendingLibraryMedia(games,{});
 assert(games[0].logoPath==shared.path+"/Game.png"&&games[1].logoPath==shared.path+"/Game.png");CompleteMediaRefresh();
 // Playback-only options do not schedule media lookup.
 MediaSource video{MediaType::Preview,-1,root+"/video",true,0,"nes"};
 assert(SaveMediaSources({nes,snes,shared,video}));CompleteMediaRefresh();video.video.volume=10;
 assert(SaveMediaSources({nes,snes,shared,video}));assert(!MediaRefreshNeeded());
 // Reassigning a directory refreshes both its previous and new content types.
 CompleteMediaRefresh();nes.libraryId="snes";assert(SaveMediaSources({nes,snes,shared,video}));
 RefreshPendingLibraryMedia(games,{});assert(games[0].coverPath.empty());CompleteMediaRefresh();
 // A VC title presented as SNES must follow SNES edits, not just its Wii U source ID.
 LibraryDefinition type{"snes","SNES",Platform::Files,GameLauncher::WiiFlow,".sfc"};type.mediaByContentType=true;
 GameEntry vc;vc.libraryId="wiiu";vc.platform=Platform::WiiU;vc.navigationLibraryId="snes";vc.name="Game";vc.absolutePath=root+"/injected";
 games={vc,b};ApplyContentTypeMedia(games,{type});
 nes.path=root+"/vc-snes";Write(nes.path+"/Game.png");assert(SaveMediaSources({nes,snes,shared,video}));
 RefreshPendingLibraryMedia(games,{type});assert(games[0].coverPath==nes.path+"/Game.png"&&games[0].mediaLibraryId=="snes");
 assert(games[0].libraryId=="wiiu"&&games[0].absolutePath==vc.absolutePath);
 std::cout<<"PASS: scoped directory edits/removal, persisted union, unrelated files untouched/unindexed, shared/manual refresh and playback-only settings\n";
}
