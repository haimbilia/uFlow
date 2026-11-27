#include "../module/src/RawAddons.h"
#include "../module/src/RawAddonNativePath.h"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
void put(const fs::path &p,const std::string &s){fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
int main(){
    const auto root=fs::temp_directory_path()/"uflow-addons-fixture";
    fs::remove_all(root);fs::create_directories(root);
    auto xml=[](const char *id,const char *v){return std::string("<app><title_id type=\"hexBinary\">")+id+"</title_id><title_version>"+v+"</title_version></app>";};
    put(root/"code/app.xml",xml("00050000101c9400","0"));
    put(root/"code/game.rpx","base exe");put(root/"code/base.rpl","base library");
    put(root/"content/base.bin","original");
    for(const auto &n:{"v9","v10","wrong-region"}){
        auto p=root/"updates"/n;fs::create_directories(p/"content");
        put(p/"code/app.xml",xml(n==std::string("wrong-region")?"0005000e101c9500":"0005000e101c9400",n==std::string("v9")?"9":"d0"));
        put(p/"code/cos.xml","cos");put(p/"code/game.rpx",n);
    }
    auto plan=RawAddons::Discover(root.string());
    assert(plan.title==0x00050000101c9400ULL && plan.version==208);
    assert(plan.update==(root/"updates/v10").string() && plan.dlc.empty());
    put(root/"dlc/code/app.xml",xml("0005000c101c9500","1"));fs::create_directories(root/"dlc/content");
    assert(RawAddons::Discover(root.string()).dlc.empty());
    put(root/"aoc0005000c101c9400/0010/data","DLC");
    assert(RawAddons::Discover(root.string()).dlc==(root/"aoc0005000c101c9400").string());
    plan=RawAddons::Discover(root.string());
    assert(plan.dlcContentIds==std::vector<uint16_t>{0x10} && plan.dlcVersion==0);
    fs::remove_all(root/"aoc0005000c101c9400");
    put(root/"dlc/code/app.xml",xml("0005000c101c9400","0050"));
    for(const auto &name:{"0010","0011","0012","abcd","bogus","00123"})
        put(root/"dlc/content"/name/"data","local");
    put(root/"dlc/content/0013","not a directory");
    plan=RawAddons::Discover(root.string());
    assert(plan.dlcVersion==80);
    assert((plan.dlcContentIds==std::vector<uint16_t>{0x10,0x11,0x12,0xabcd}));
    const auto cache=root/"cache";fs::create_directory(cache);put(cache/"stale.rpl","stale");
    assert(RawAddons::StageCode((root/"code").string(),plan.update+"/code",cache.string()));
    assert(RawAddons::Read((cache/"game.rpx").string())=="v10");
    assert(RawAddons::Read((cache/"base.rpl").string())=="base library");
    assert(!fs::exists(cache/"stale.rpl"));assert(RawAddons::Read((root/"code/game.rpx").string())=="base exe");
    put(root/"updates/v10/code/empty.bin","");
    assert(RawAddons::StageCode((root/"code").string(),plan.update+"/code",cache.string()));
    assert(fs::file_size(cache/"empty.bin")==0);
    struct Info {bool active=true;const char *updateContentPath="game/updates/latest/content",*contentPath="game/content",*codePath="cache/code",*savePath="saves/game",*dlcVolume="/vol/aoc0005000c101c9400",*dlcPath="game/dlc/content";} info;
    char path[1024];auto exists=[](const std::string &p){return p.ends_with("changed.bin");};
    assert(RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/content/changed.bin","rb",exists));
    assert(std::string(path)=="/vol/external01/game/updates/latest/content/changed.bin");
    assert(RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/content/base.bin","r",exists));
    assert(std::string(path)=="/vol/external01/game/content/base.bin");
    assert(RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/code/base.rpl","r",exists));
    assert(std::string(path)=="/vol/external01/cache/code/base.rpl");
    assert(RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/aoc0005000c101c9400/0010/data","r",exists));
    assert(std::string(path)=="/vol/external01/game/dlc/content/0010/data");
    assert(!RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/aoc0005000c101c9500/0010/data","r",exists));
    assert(RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/save/common/save.dat","w",exists));
    assert(std::string(path)=="/vol/external01/saves/game/common/save.dat");
    info.updateContentPath="usb1:/game/updates/latest/content";
    assert(!RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/content/changed.bin","r",exists));
    info.active=false;assert(!RawAddonNativeOpenPath(path,sizeof(path),info,"/vol/content/base.bin","r",exists));
    fs::remove_all(root);std::cout<<"raw add-ons: discovery, region, overlay, RPL fallback, source preservation and DLC passed\n";
}
