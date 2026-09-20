#include "vc_formats.hpp"
#include "launcher_modules.hpp"
#include "rpx_layout_check.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
using VCFormat::Bytes;
void Check(bool ok,const char *s){if(!ok)throw std::runtime_error(s);}
template<class F> void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,"expected rejection");}
Bytes Header(){Bytes b("NES\x1a",4);b.resize(16);b[4]=1;return b;}
Bytes Zip(const Bytes &rom,std::string name="test.nes",bool compressed=false){
    Bytes packed=rom;if(compressed){auto z=VCFormat::Deflate(rom);packed=z.substr(2,z.size()-6);}
    const auto crc=crc32(0,reinterpret_cast<const Bytef*>(rom.data()),rom.size());
    Bytes b(30,0);VCFormat::Put(b,0,0x04034b50);VCFormat::Put(b,4,20,2);VCFormat::Put(b,8,compressed?8:0,2);
    VCFormat::Put(b,14,crc);VCFormat::Put(b,18,packed.size());VCFormat::Put(b,22,rom.size());VCFormat::Put(b,26,name.size(),2);
    b+=name;b+=packed;auto cd=b.size();Bytes c(46,0);VCFormat::Put(c,0,0x02014b50);VCFormat::Put(c,6,20,2);
    VCFormat::Put(c,10,compressed?8:0,2);VCFormat::Put(c,16,crc);VCFormat::Put(c,20,packed.size());VCFormat::Put(c,24,rom.size());VCFormat::Put(c,28,name.size(),2);c+=name;b+=c;
    Bytes e(22,0);VCFormat::Put(e,0,0x06054b50);VCFormat::Put(e,8,1,2);VCFormat::Put(e,10,1,2);VCFormat::Put(e,12,c.size());VCFormat::Put(e,16,cd);b+=e;return b;
}
int main(int argc,char **argv){try{
    const auto canonical=Header()+Bytes(16384,'q');
    Check(NesRom::Normalize(canonical)==canonical,"clean iNES unchanged");
    Check(NesRom::Normalize(canonical+Bytes(128,'x'))==canonical,"legacy annotation trimmed");
    auto dirty=canonical;dirty.replace(7,9,"DiskDude!");Check(NesRom::Normalize(dirty)==canonical,"DiskDude mapper64 corrected to0");
    dirty=canonical;dirty.replace(13,3,"MJR");Check(NesRom::Normalize(dirty)==canonical,"legacy signature cleared");
    Reject([&]{NesRom::Normalize(canonical.substr(0,canonical.size()-1));});
    Reject([&]{NesRom::Normalize(Bytes(65536,'?'));});
    dirty=canonical;dirty[6]=4;Reject([&]{NesRom::Normalize(dirty);});
    auto n2=canonical;n2[7]=8;Check(NesRom::Normalize(n2)==canonical,"NES2 compatible conversion");
    n2[8]=1;Reject([&]{NesRom::Normalize(n2);});
    n2[8]=16;Reject([&]{NesRom::Normalize(n2);});
    n2[8]=0;n2[10]=8;Reject([&]{NesRom::Normalize(n2);});
    n2[10]=0;n2[14]=1;Reject([&]{NesRom::Normalize(n2);});
    n2[14]=0;n2[4]=14<<2;n2[9]=15;Check(NesRom::Normalize(n2)==canonical,"NES2 exponent size");
    n2[4]=255;Reject([&]{NesRom::Normalize(n2);});
    auto unknown=Bytes("UNIF",4);unknown.resize(32);Reject([&]{NesRom::Normalize(unknown);});
    unknown+="PRG0";unknown+=Bytes(4,'\xff');Reject([&]{NesRom::Normalize(unknown);});
    for(bool deflate:{false,true}){
        auto z=Zip(canonical,"folder/GAME.NES",deflate);VCFormat::Write("test.zip",z);
        Check(RomArchive::Extract("test.zip")==canonical,"ZIP roundtrip and CRC");
        Check(VCFormat::Cartridge("test.zip","nes")==canonical,"ZIP normalized cartridge");
        Check(VCSystems::File("test.zip")=="nes","ZIP classification");
        z[30+15]^=1;VCFormat::Write("bad.zip",z);Reject([]{RomArchive::Extract("bad.zip");});
    }
    VCFormat::Write("unsafe.zip",Zip(canonical,"../escape.nes"));Reject([]{RomArchive::Inspect("unsafe.zip");});
    VCFormat::Write("wrong.zip",Zip(canonical,"game.sfc"));Check(VCSystems::File("wrong.zip").empty(),"non-NES ZIP not misclassified");
    auto z=Zip(canonical);auto cd=VCFormat::Number(z,z.size()-6);VCFormat::Put(z,cd+24,0x40000000);VCFormat::Write("bomb.zip",z);Reject([]{RomArchive::Inspect("bomb.zip");});
    z=Zip(canonical);VCFormat::Put(z,6,1,2);cd=VCFormat::Number(z,z.size()-6);VCFormat::Put(z,cd+8,1,2);VCFormat::Write("encrypted.zip",z);Reject([]{RomArchive::Extract("encrypted.zip");});
    z=Zip(canonical);cd=VCFormat::Number(z,z.size()-6);const auto central=z.substr(cd,z.size()-22-cd);
    z.insert(z.size()-22,central);VCFormat::Put(z,z.size()-22+8,2,2);VCFormat::Put(z,z.size()-22+10,2,2);VCFormat::Put(z,z.size()-22+12,central.size()*2);
    VCFormat::Write("multi.zip",z);Reject([]{RomArchive::Inspect("multi.zip");});
    fs::create_directories("removed.zip.uflow");VCFormat::Write("removed.zip.uflow/game.cfg","UFLOW-ROM-PACKAGE-1\n\"removed.zip\"\n\"Name\"\n\"Folder\"\n\"\"\n\"\"\n\"game-key\"\n\"code/a.rpx\"\n\"wiiu/vc-saves/nes/key\"\n\"0005000012345678\"\n");
    Check(VCSystems::File("removed.zip")=="nes","archive identity after original deletion");
    LauncherModule module;Check(ParseLauncherModule("UFLOW-MODULE-1\nid=nes-vc\nname=NES\nversion=1\nsystem=NES\nextensions=.nes\nprotocol=vc-nes-v1\n",module)&&module.extensions==VCSystems::Find("nes")->extensions,"old NES module upgrade");
    std::cout<<"PASS: NES normalization, ZIP safety/CRC/bounds, old modules, deleted archive identity\n";
    if(argc>1){
        const auto lolo=VCFormat::Read(std::string(argv[1])+"/Adventures of Lolo (USA).nes");
        Bytes unif("UNIF",4);unif.resize(32);auto chunk=[&](const char *tag,const Bytes &payload){
            unif+=tag;Bytes length(4,0);VCFormat::Put(length,0,payload.size());unif+=length;unif+=payload;};
        chunk("PRG0",lolo.substr(0,32768));chunk("CHR0",lolo.substr(32768));
        Check(NesRom::Normalize(unif)==NesRom::Normalize(lolo),"known UNIF hash reconstruction");
        auto truncated=unif.substr(0,unif.size()-1);Reject([&]{NesRom::Normalize(truncated);});
        chunk("PRG0",lolo.substr(0,32768));Reject([&]{NesRom::Normalize(unif);});
        Bytes base; if(argc>2)for(const auto &p:fs::directory_iterator(std::string(argv[2])+"/code"))if(p.path().extension()==".rpx")base=VCFormat::Read(p.path().string());
        unsigned good=0,bad=0,patches=0;
        for(const auto &p:fs::recursive_directory_iterator(argv[1]))if(p.is_regular_file()){
            if(p.path().extension()==".ips"){++patches;continue;}
            if(!RomArchive::NesName(p.path().string())&&RomArchive::Ext(p.path().string())!=".zip")continue;
            try{
                auto normalized=VCFormat::Cartridge(p.path().string(),"nes");
                if(!base.empty()){auto injected=base;VCFormat::InjectRpx(injected,normalized,"nes");CheckRpxLayout(injected);VCFormat::Rpx r(injected);auto slot=VCFormat::RomSlot(r,"nes");Check(r.sections[slot.section].data.substr(slot.offset,normalized.size())==normalized,"actual RPX cartridge roundtrip");}
                std::cout<<"OK "<<p.path().filename()<<" normalized="<<normalized.size()<<" mapper="<<((uint8_t(normalized[6])>>4)|(uint8_t(normalized[7])&240))<<"\n";++good;
            }catch(const std::exception &e){std::cout<<"BLOCKED "<<p.path().filename()<<": "<<e.what()<<"\n";++bad;}
        }
        std::cout<<"AUDIT: "<<good<<" ready, "<<bad<<" blocked, "<<patches<<" standalone patches (not games)\n";
        Check(bad==0,"actual folder has unresolved format/base problems");
    }
    return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
