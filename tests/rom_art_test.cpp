#include "rom_art.hpp"
#include "library.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
using namespace RomArt;
void Write(const std::string &p,const Bytes &b){fs::create_directories(fs::path(p).parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f);}
void Put(Bytes &b,size_t p,uint32_t v,bool be=false){for(int i=0;i<4;++i)b[p+i]=v>>(8*(be?3-i:i));}
Bytes Cartridge(){
    Bytes b(0x1040);Put(b,0x20,0x200);Put(b,0x2c,4);Put(b,0x68,0x800);b[0x800]=1;
    b[0x820]=0x21;b[0xa22]=31;b[0xa25]=0x7c; // first pixels red, blue
    unsigned crc=0xffff;for(size_t i=0x820;i<0x1040;++i){crc^=b[i];for(int j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xa001:0);}
    b[0x802]=crc;b[0x803]=crc>>8;return b;
}
int main(int argc,char **argv){
    const std::string root="fs:/vol/external01",app=root+"/wiiu/apps/uFlow";
    assert(!fs::exists(root));fs::create_directories(app);
    const std::string ds=root+"/roms/Test.nds",gc=root+"/games/Test.iso";
    auto b=Cartridge();Write(ds,b);Reader r(ds);auto icon=DS(r);
    assert(icon.width==32&&icon.height==32&&icon.rgba[0]==255&&icon.rgba[3]==255&&icon.rgba[6]==255&&icon.rgba[11]==0);
    auto card=Card(icon,true);assert(card.size()==18+256*256*4&&card[17]==0x28);
    auto path=Cached(ds,true,app);assert(!path.empty()&&fs::file_size(path)==card.size());
    auto stamp=fs::last_write_time(path);assert(Cached(ds,true,app)==path&&fs::last_write_time(path)==stamp);
    auto bad=b;bad[0x820]^=1;Write(root+"/bad.nds",bad);Reader badReader(root+"/bad.nds");assert(DS(badReader).rgba.empty());
    bad=b;Put(bad,0x68,0xfffffff0);Write(root+"/bad.nds",bad);Reader bounds(root+"/bad.nds");assert(DS(bounds).rgba.empty());
    Bytes disc(0x4000);Put(disc,0x1c,0xc2339f3d,true);Put(disc,0x424,0x500,true);Put(disc,0x428,36,true);
    disc[0x500]=1;Put(disc,0x508,2,true);Put(disc,0x510,0x1000,true);Put(disc,0x514,0x1960,true);
    const std::string name="opening.bnr";std::copy(name.begin(),name.end(),disc.begin()+0x518);
    disc[0x1000]='B';disc[0x1001]='N';disc[0x1002]='R';disc[0x1003]='1';disc[0x1020]=0xfc;disc[0x1021]=0;
    Write(gc,disc);Reader gr(gc);auto banner=GC(gr);assert(banner.width==96&&banner.rgba[0]==255&&banner.rgba[3]==255);
    assert(!Cached(gc,false,app).empty());disc[0x50c]=1;Put(disc,0x514,1,true);Write(root+"/bad.iso",disc);Reader loop(root+"/bad.iso");assert(GC(loop).rgba.empty());
    // Real artwork > extraction > default; a donor icon is never opened.
    const auto art=root+"/art";Write(art+"/Default.png",{1});
    MediaSource source{MediaType::FullCover,-1,art,true,0,"ds"};source.defaultFile="Default.png";
    assert(SaveMediaSources({source}));GameEntry game;game.name="Test";game.libraryId="ds";game.platform=Platform::Files;game.absolutePath=ds;
    std::vector<GameEntry> games{game};EnrichLibraryMedia(games);assert(games[0].coverPath==path&&games[0].fullCoverPath.empty());
    Write(art+"/Test.png",{1});games={game};EnrichLibraryMedia(games);assert(games[0].fullCoverPath==art+"/Test.png"&&games[0].coverPath.empty());
    game.name="Other";game.absolutePath=root+"/absent.nds";games={game};EnrichLibraryMedia(games);assert(games[0].fullCoverPath==art+"/Default.png"&&games[0].coverPath.empty());
    assert(SaveLibraryCache(games));games.clear();assert(LoadLibraryCache(games)&&games[0].fullCoverPath==art+"/Default.png");
    std::cout<<"PASS: DS palette/tiles/CRC/bounds, GC FST/RGB5A3/bounds, fitted cached fronts, artwork precedence and missing-source fallback\n";
    if(argc==3){Reader real(argv[1]);const auto decoded=DS(real);assert(decoded.width==32);Write(argv[2],Card(decoded,true));std::cout<<"PASS: supplied DS cartridge icon decoded and rendered\n";}
}
