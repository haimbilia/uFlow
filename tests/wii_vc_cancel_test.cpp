#include "wii_vc.hpp"
#include "wii_vc_image.hpp"
#include "preparation_progress.hpp"
#include "injection_dialog.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace fs=std::filesystem;
using namespace WiiVC::Image;
static std::vector<Byte> ReadAll(const std::string &path){
    std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
static void WriteAll(const std::string &path,const std::vector<Byte> &data){
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());assert(f.good());
}
int main(){
    // Synthetic encrypted fixture, no Nintendo key or retail game data.
    fs::create_directories("source/code");fs::create_directories("source/content");
    Key key{};WriteAll("source/code/htk.bin",std::vector<Byte>(16));
    std::vector<Byte> disc(Block);std::memcpy(disc.data(),"TEST01",6);Put32(disc.data()+24,0x5d1c9ea3);
    disc[0x61]=1;Put32(disc.data()+0x40000,1);Put32(disc.data()+0x40004,0x40020/4);
    Put32(disc.data()+0x40020,0x80000/4);Put32(disc.data()+0x802a4+0x18,8*Sector/4);
    std::vector<Byte> nfs(512+disc.size());std::memcpy(nfs.data(),"EGGS",4);std::memcpy(nfs.data()+508,"SGGE",4);
    Put32(nfs.data()+16,1);Put32(nfs.data()+24,64);std::copy(disc.begin(),disc.end(),nfs.begin()+512);
    AES aes;aes.Init(key,false);
    for(unsigned i=0;i<64;++i){Key iv{};Put32(iv.data()+12,i);aes.Crypt(nfs.data()+512+i*Sector,Sector,iv,false);}
    WriteAll("source/content/hif_000000.nfs",nfs);
    GameEntry original{};original.platform=Platform::WiiU;original.absolutePath="source";original.titleId="0005000012345678";
    const std::string base="fs:/vol/external01/wiiu/apps/uFlow/wii-vc-cache-v2/"+original.titleId;
    for(int cancelAt=0;cancelAt<5;++cancelAt){
        GameEntry game=original;InjectionDialog dialog;dialog.Start("Test Wii VC");
        PreparationProgress pump;unsigned time=0,repeated=0,polls=0;bool cancelled=false;
        const auto error=WiiVC::Prepare(game,[&](const std::string &phase,uint64_t done,uint64_t total){
            return pump.Tick(time++,[&]{
                ++polls;
                const bool cancel=(cancelAt==0 && phase=="READING WII VC SOURCE") ||
                    (cancelAt==1 && phase=="PREPARING WII GAME (ONE TIME)" && done==0) ||
                    (cancelAt==2 && phase=="PREPARING WII GAME (ONE TIME)" && done==256*1024) ||
                    (cancelAt==3 && phase=="PREPARING WII GAME (ONE TIME)" && done==Block && ++repeated==3) ||
                    (cancelAt==4 && phase=="CHECKING PREPARED COPY (ONE TIME)" && done>=256*1024);
                if(cancel){dialog.Input(0,false,true);cancelled=true;}
                return !dialog.cancel;
            },[&]{dialog.Progress(phase,done,total);});
        });
        assert(cancelled && polls && error.find("CANCELLED")!=std::string::npos);
        assert(game.absolutePath==original.absolutePath && game.titleId==original.titleId);
        assert(!WiiVC::HasPrepared(game));
        assert(!fs::exists(base+".wbfs") && !fs::exists(base+".ready") && !fs::exists(base+".staging.wbfs"));
        assert(ReadAll("source/content/hif_000000.nfs")==nfs);
    }
    GameEntry prepared=original;assert(WiiVC::Prepare(prepared).empty());assert(WiiVC::HasPrepared(original));
    auto output=ReadAll(base+".wbfs");assert(output.size()==2*Block && !std::memcmp(output.data(),"WBFS",4));
    disc[0x61]=0;assert(std::equal(disc.begin(),disc.end(),output.begin()+Block));
    assert(ReadAll("source/content/hif_000000.nfs")==nfs);
    GameEntry cached=original;unsigned callbacks=0;
    assert(WiiVC::Prepare(cached,[&](auto&,auto,auto){++callbacks;return false;}).empty());
    assert(callbacks==0 && cached.absolutePath==prepared.absolutePath && cached.titleId=="TEST01");
    // Cancellation is polled between rendered frames, and touch/system exit use
    // the same stop path. The first frame works even before tick 80 / after wrap.
    PreparationProgress pump;unsigned polls=0,frames=0;InjectionDialog dialog;dialog.Start("Test");
    auto poll=[&]{++polls;return !dialog.cancel;};auto render=[&]{++frames;};
    assert(pump.Tick(UINT32_MAX-20,poll,render) && frames==1);
    assert(pump.Tick(0,poll,render) && polls==2 && frames==1);
    dialog.Touch(640,480);assert(!pump.Tick(1,poll,render) && polls==3 && frames==2);
    assert(!pump.Tick(2,[]{return false;},render));
    puts("PASS: Wii VC cancel during source open, initial write, write slices, decode slices and verification; partial output cleanup; unchanged source; retry/cache launch; live input independent of rendering");
}
