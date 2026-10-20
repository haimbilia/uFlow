#include "skin.hpp"
#include "user_statistics.hpp"
#include <filesystem>
#include <cassert>
#include <iostream>
namespace fs=std::filesystem;
int main(){
 const auto root=fs::temp_directory_path()/"uflow-skin-stats-test";assert(!fs::exists(root));fs::create_directories(root/"user/custom");
 std::ofstream(root/"user/custom/skin.ini")<<"version=1\nname=My Theme\nengine=switch-dark\nbackground=112233\navatar_x=99999\nicon_0=../secret.png\nicon_1=browser.png\ngrid_fullscreen=0\ngrid_height=400\n";
 auto skins=Skin::Discover("/project/app/content/skins",(root/"user").string());assert(skins.size()==5);assert(skins.front().gridFullscreen);
 const auto &custom=skins.back();assert(!custom.gridFullscreen&&custom.gridHeight==400);assert(custom.name=="My Theme"&&custom.engine==3&&custom.background[0]==17&&custom.avatarX==1200&&custom.icons[0].empty()&&!custom.icons[1].empty());
 assert(!Skin::SafeAsset("/absolute.png")&&!Skin::SafeAsset("../escape")&&!Skin::SafeAsset("C:\\image.png"));
 fs::create_directories(root/"user/future");std::ofstream(root/"user/future/skin.ini")<<"version=99\n";assert(Skin::Discover("/project/app/content/skins",(root/"user").string()).size()==5);
 UserStatistics::Store one,two;one.Load((root/"one.cfg").string());two.Load((root/"two.cfg").string());
 GameEntry game;game.absolutePath="usb1:/games/game.nds";game.platform=Platform::Files;game.libraryId="ds";game.name="Game";
 assert(one.Get(game).launches==0&&one.Get(game).rating==0);
 assert(one.Launch(game)&&one.Launch(game)&&one.Rate(game));one.Load((root/"one.cfg").string());
 assert(one.Get(game).launches==2&&one.Get(game).rating==1&&two.Get(game).launches==0&&two.Get(game).rating==0);
 for(int i=0;i<5;++i)assert(one.Rate(game));assert(one.Get(game).rating==0);
 std::cout<<"PASS: bundled/custom skin discovery, validation, colors/assets, per-account statistics persistence and rating cycle\n";
}
