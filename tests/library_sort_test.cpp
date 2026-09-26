#include "library_sort.hpp"
#include "settings.hpp"
#include <cassert>
#include <filesystem>
#include <numeric>
int main() {
    UserSettings controls;assert(controls.bindings[static_cast<int>(ControlAction::Sort)]==0x4000);
    controls.bindings[static_cast<int>(ControlAction::Sort)]=0x2000;
    ResetControlBindings(controls);assert(controls.bindings[static_cast<int>(ControlAction::Sort)]==0x4000);
    std::vector<GameEntry> games(4);
    games[0].name="zebra"; games[0].releaseYear="2001"; games[0].publisher="Zoo";
    games[1].name="Alpha"; games[1].releaseYear="1999"; games[1].publisher="Beta";
    games[2].name="beta"; games[2].releaseYear="unknown";
    games[3].name="alpha"; games[3].releaseYear="2001"; games[3].publisher="Beta";
    for(size_t i=0;i<games.size();++i)games[i].absolutePath="usb1:/Game "+std::to_string(i);
    games[0].libraryId="wii";games[1].libraryId="gamecube";
    LibrarySort::History history;
    auto check=[&](int mode,std::vector<size_t> expected) {
        std::vector<size_t> visible={0,1,2,3};LibrarySort::Apply(visible,games,mode,history);assert(visible==expected);
    };
    check(0,{1,3,2,0});check(1,{0,2,1,3});check(2,{1,3,2,0});
    check(3,{3,0,1,2});check(4,{1,3,0,2});check(5,{1,3,0,2});check(6,{1,0,3,2});
    assert(history.Record(games[0],"history.cfg"));assert(history.Record(games[2],"history.cfg"));
    check(2,{2,0,1,3});
    history.Load("history.cfg");check(2,{2,0,1,3});
    assert(history.Record(games[0],"history.cfg"));check(2,{0,2,1,3});
    const auto last=history.Last(games[1]);
    assert(!history.Record(games[1],"missing-directory/history.cfg"));assert(history.Last(games[1])==last);
    std::ofstream("history.cfg",std::ios::app)<<"broken\n\"bad\" -1\n\"bad\" 18446744073709551615\n";
    history.Load("history.cfg");check(2,{0,2,1,3});
    std::vector<size_t> subset={3,0};LibrarySort::Apply(subset,games,0,history);assert((subset==std::vector<size_t>{3,0}));
    std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    for(int mode=0;mode<LibrarySort::Count;++mode){UserSettings s;s.sortMode=mode;s.bindings[static_cast<int>(ControlAction::Sort)]=0x1000;SaveSettings(s);assert(LoadSettings().sortMode==mode);assert(LoadSettings().bindings[static_cast<int>(ControlAction::Sort)]==0x1000);}
    std::ofstream("fs:/vol/external01/wiiu/apps/uFlow/uflow.cfg")<<"sort_mode=900\n";
    assert(LoadSettings().sortMode==6);
    std::ofstream("fs:/vol/external01/wiiu/apps/uFlow/uflow.cfg")<<"skin=0\n";
    assert(LoadSettings().sortMode==0);
    puts("PASS: seven sort modes, unknown years, ties, filtered subset, persistent history, write failure and settings migration");
}
