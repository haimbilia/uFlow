#define UFLOW_VC_TEST_IO
#include "../app/source/vc_inject.cpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
void file(const std::string &path,const std::string &data){fs::create_directories(fs::path(path).parent_path());check(VCInject::Write(path,data),"fixture write");}
std::string rom(int variant){std::string s(4096,'\0');s[0]=char(variant);auto put=[&](int pos,unsigned n){for(int i=0;i<4;++i)s[pos+i]=char(n>>(i*8));};put(0x20,512);put(0x2c,64);put(0x30,1024);put(0x3c,64);return s;}
std::string unzip(const std::string &p){auto s=VCInject::Read(p,512*1024*1024);auto name=VCInject::U16(s,26),extra=VCInject::U16(s,28);auto packed=VCInject::U32(s,18),size=VCInject::U32(s,22);std::string data=s.substr(30+name+extra,packed);if(VCInject::U16(s,8)){std::string out(size,'\0');z_stream z{};check(inflateInit2(&z,-MAX_WBITS)==Z_OK,"inflate init");z.next_in=reinterpret_cast<Bytef*>(&data[0]);z.avail_in=data.size();z.next_out=reinterpret_cast<Bytef*>(&out[0]);z.avail_out=out.size();check(inflate(&z,Z_FINISH)==Z_STREAM_END,"ZIP deflate stream");inflateEnd(&z);data=out;}check(crc32(0,reinterpret_cast<const Bytef*>(data.data()),data.size())==VCInject::U32(s,14),"ZIP CRC");return data;}
int main(int argc,char **argv){try{
    VCInject::SHA hash;hash.Add("abc",3);check(hash.End()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 vector");
    const std::string million(1000000,'a');VCInject::SHA longHash;
    for(size_t i=0;i<million.size();i+=63)longHash.Add(million.data()+i,std::min(size_t(63),million.size()-i));
    check(longHash.End()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 block-boundary/million-byte vector");
    const std::string sd="fs:/vol/external01/";VCInject::Settings options;options.base="bases/test";
    if(argc==3){fs::copy(argv[1],sd+options.base,fs::copy_options::recursive);fs::create_directories(sd+"nds");fs::copy_file(argv[2],sd+"nds/test.nds");GameEntry g;g.name="VC test";g.absolutePath=sd+"nds/test.nds";auto error=VCInject::Prepare(g,options);check(error.empty(),error.c_str());std::cout<<"REAL BASE PREPARATION PASSED "<<g.absolutePath<<"\n";check(unzip(g.absolutePath+"/content/0010/rom.zip")==VCInject::Read(sd+"nds/test.nds",512*1024*1024),"real ROM comparison");return 0;}
    file(sd+"nds/one.nds",rom(1));file(sd+"nds/two.nds",rom(2));
    const auto base=sd+options.base+"/";
    file(base+"code/app.xml","<app><title_id type=\"hexBinary\" length=\"8\">0005000010179e00</title_id></app>");
    file(base+"code/cos.xml","<app/>");file(base+"meta/meta.xml","<menu/>");
    auto elf=std::string(32,'\0');elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=1;elf[5]=2;elf[19]=20;file(base+"code/test.rpx",elf);
    file(base+"content/0010/configuration_cafe.json","{\"configuration\":{\"Display\":{\"Brightness\":80,\"PixelArtUpscaler\":0}}}");
    VCInject::ZipROM(sd+"nds/one.nds",base+"content/0010/rom.zip","WUP-N-TEST.srl",1,{});
    const auto original=VCInject::HashFile(base+"content/0010/rom.zip",{});
    check(VCInject::CheckBase(options).empty(),"base validation");check(VCInject::Save("ds-vc",options),"settings save");check(VCInject::Load("ds-vc").base==options.base,"settings reload");
    GameEntry source;source.name="Test";source.platform=Platform::Files;source.absolutePath=sd+"nds/two.nds";
    GameEntry g=source;auto error=VCInject::Prepare(g,options);check(error.empty(),error.c_str());auto cached=g.absolutePath;
    check(unzip(cached+"/content/0010/rom.zip")==rom(2),"injected ROM bytes");check(VCInject::HashFile(base+"content/0010/rom.zip",{})==original,"base unchanged");
    GameEntry again=source;check(VCInject::Prepare(again,options).empty()&&again.absolutePath==cached,"cache reuse");
    check(!VCInject::HasSaved(source),"not selectable before saved");
    check(VCInject::SavePrepared(source,g).empty()&&VCInject::HasSaved(source),"saved injection registry");
    cached=g.absolutePath;
    check(cached.rfind(source.absolutePath+".uflow/game-",0)==0,"published beside ROM");
    const auto launchReads=VCInject::verificationReadBytes;
    for(int i=0;i<3;++i){auto quick=source;
        check(VCInject::LaunchSaved(quick,[](const std::string &phase,auto,auto){return phase.find("VERIFY")==std::string::npos;}).empty()&&quick.absolutePath==cached,"repeat launch skips verification");}
    check(VCInject::verificationReadBytes==launchReads,"repeat launches do not hash/copy content");
    fs::rename(base,sd+"bases/hidden");fs::rename(source.absolutePath,source.absolutePath+".hidden");
    auto saved=source;check(VCInject::LaunchSaved(saved).empty()&&saved.absolutePath==cached,"saved launch needs neither base nor original ROM");
    fs::rename(sd+"bases/hidden",base);fs::rename(source.absolutePath+".hidden",source.absolutePath);
    options.reuse=false;auto replacement=source;
    check(VCInject::Prepare(replacement,options).empty()&&replacement.absolutePath!=cached,"explicit re-injection makes separate output");
    saved=source;check(VCInject::LaunchSaved(saved).empty()&&saved.absolutePath==cached,"old saved injection stays selected until commit");
    check(VCInject::SavePrepared(source,replacement).empty(),"replace saved injection record");
    saved=source;check(VCInject::LaunchSaved(saved).empty()&&saved.absolutePath==replacement.absolutePath,"saved launch follows replacement");
    check(saved.saveRelative==g.saveRelative,"re-injection preserves save namespace");
    options.reuse=true;
    auto automatic=source;
    check(VCInject::LaunchOrPrepare(automatic,options).empty()&&automatic.absolutePath==replacement.absolutePath,"automatic DS launch reuses saved result");
    file(sd+"nds/automatic.nds",rom(3));auto newSource=source;newSource.absolutePath=sd+"nds/automatic.nds";
    auto firstPlay=newSource;
    check(VCInject::LaunchOrPrepare(firstPlay,options).empty()&&VCInject::HasSaved(newSource)&&firstPlay.platform==Platform::WiiU,"automatic DS first launch injects and saves");
    GameEntry other=source;other.absolutePath=sd+"nds/one.nds";check(VCInject::Prepare(other,options).empty()&&other.saveRelative!=g.saveRelative,"save isolation");
    options.brightness=90;options.upscale=1;GameEntry patched=source;check(VCInject::Prepare(patched,options).empty(),"display patch prepare");auto json=VCInject::Read(patched.absolutePath+"/content/0010/configuration_cafe.json");check(json.find("\"Brightness\":90")!=std::string::npos&&json.find("\"PixelArtUpscaler\":1")!=std::string::npos,"display JSON patch");check(patched.saveRelative==g.saveRelative,"save stable across display changes");
    for(int level:{0,6}){options.compression=level;GameEntry next=source;check(VCInject::Prepare(next,options).empty(),"compression option");check(unzip(next.absolutePath+"/content/0010/rom.zip")==rom(2),"compression decode");}
    options.compression=1;options.brightness=-1;options.upscale=-1;file(replacement.absolutePath+"/content/0010/configuration_cafe.json","corrupt");again=source;check(VCInject::Prepare(again,options).find("CACHE DAMAGED")!=std::string::npos,"reject corrupt cache");
    options.reuse=false;auto before=std::distance(fs::directory_iterator(sd+"wiiu/apps/uFlow/vc-cache/ds"),fs::directory_iterator{});again=source;error=VCInject::Prepare(again,options,[](const std::string &phase,auto,auto){return phase!="INJECTING DS ROM";});check(error.find("CANCELLED")!=std::string::npos,"cancel");auto after=std::distance(fs::directory_iterator(sd+"wiiu/apps/uFlow/vc-cache/ds"),fs::directory_iterator{});check(before==after,"cancel cleanup");
    bool tampered=false;again=source;
    error=VCInject::Prepare(again,options,[&](const std::string &phase,auto n,auto total){
        if(!tampered&&phase=="CHECKING BASE FILES"&&n==total){file(base+"meta/meta.xml","changed");tampered=true;}return true;
    });
    check(tampered&&error.find("BASE CHANGED")!=std::string::npos,"copy hashes detect changed source");
    file(base+"meta/meta.xml","<menu/>");
    // Verify the final manifest pass still rejects damaged unchanged files.
    const auto checkRoot=sd+"readback-test";file(checkRoot+"/meta.txt","bad");VCInject::SHA expected;expected.Add("good",4);
    bool rejected=false;try{VCInject::Seal(checkRoot,{},{{"meta.txt",expected.End()}});}catch(...){rejected=true;}
    check(rejected,"final readback rejects copy corruption");
    // Legacy migration preserves the old cache and works with no source/base.
    file(sd+"nds/legacy.nds",rom(4));auto legacySource=source;legacySource.absolutePath=sd+"nds/legacy.nds";
    auto legacyTarget=legacySource;check(VCInject::Prepare(legacyTarget,options).empty(),"prepare legacy fixture");
    const auto oldOutput=legacyTarget.absolutePath,oldManifest=VCInject::Seal(oldOutput,{});
    file(VCInject::SavedRecord(legacySource),"UFLOW-SAVED-DS-1\n"+legacySource.absolutePath+"\n"+oldOutput+"\n"+legacyTarget.rpxRelative+"\n"+legacyTarget.saveRelative+"\n"+legacyTarget.titleId+"\n");
    file(sd+"nds/alias.nds",rom(4));auto alias=source;alias.absolutePath=sd+"nds/alias.nds";auto aliasTarget=alias;
    options.reuse=true;check(VCInject::Prepare(aliasTarget,options).empty()&&aliasTarget.absolutePath==oldOutput&&!aliasTarget.privateInjectionStage,"reused cache is not privately owned");
    check(VCInject::SavePrepared(alias,aliasTarget).empty()&&VCInject::Seal(oldOutput,{})==oldManifest&&VCInject::HasSaved(legacySource),"publishing an alias preserves another ROM's cache reference");
    options.reuse=false;
    fs::remove(legacySource.absolutePath);fs::rename(base,sd+"bases/hidden");
    auto migrate=legacySource;bool copying=false;
    error=VCInject::LaunchSaved(migrate,[&](const std::string &phase,auto,auto){if(phase=="COPYING LEGACY INJECTION"){copying=true;return false;}return true;});
    check(copying&&error.find("CANCELLED")!=std::string::npos&&VCInject::HasSaved(legacySource),"cancel migration preserves legacy reference");
    check(VCInject::Seal(oldOutput,{})==oldManifest,"cancel migration preserves old files");
    migrate=legacySource;check(VCInject::LaunchSaved(migrate).empty(),"migrate without original ROM or base");
    check(migrate.absolutePath.rfind(legacySource.absolutePath+".uflow/game-",0)==0&&migrate.saveRelative==legacyTarget.saveRelative,"migration sibling path and saves");
    check(VCInject::Seal(oldOutput,{})==oldManifest,"migration keeps old cache intact");
    auto afterMigration=legacySource;
    check(VCInject::LaunchSaved(afterMigration,[](const std::string &phase,auto,auto){return phase!="COPYING LEGACY INJECTION";}).empty()&&afterMigration.absolutePath==migrate.absolutePath,"migration only once");
    check(VCInject::Prepare(afterMigration,options).find("ORIGINAL ROM REQUIRED")!=std::string::npos,"reinjection needs original but saved Play does not");
    fs::rename(sd+"bases/hidden",base);
    // An unrelated .uflow directory must never be adopted or overwritten.
    file(sd+"nds/collision.nds",rom(5));auto collision=source;collision.absolutePath=sd+"nds/collision.nds";
    file(VCPackage::Path(collision)+"/user.txt","keep");auto collisionTarget=collision;
    check(VCInject::Prepare(collisionTarget,options).empty(),"collision preparation");
    check(!VCInject::SavePrepared(collision,collisionTarget).empty()&&VCInject::Read(VCPackage::Path(collision)+"/user.txt")=="keep","unknown sibling folder untouched");
    // A failed record commit leaves the prior generation selected.
    file(sd+"nds/commit.nds",rom(6));auto commitSource=source;commitSource.absolutePath=sd+"nds/commit.nds";
    auto first=commitSource;check(VCInject::LaunchOrPrepare(first,options).empty(),"first package generation");
    auto second=commitSource;check(VCInject::Prepare(second,options).empty(),"second package generation");
    const auto blockedTemp=VCPackage::Path(commitSource)+"/game.cfg.tmp";fs::create_directory(blockedTemp);
    check(!VCInject::SavePrepared(commitSource,second).empty(),"failed publication reported");
    auto stillFirst=commitSource;check(VCInject::LaunchSaved(stillFirst).empty()&&stillFirst.absolutePath==first.absolutePath,"failed commit keeps prior working generation");
    fs::remove(blockedTemp);
    std::cout<<"PASS: sibling publication, legacy migration without ROM/base, migration cancel/reuse, save preservation, collision rejection and failed commit recovery\n";
    options.base="../escape";again=source;check(!VCInject::Prepare(again,options).empty(),"path traversal");
    std::cout<<"PASS: SHA-256, ZIP/CRC, base preservation, cache validation, display settings, save isolation, cancellation cleanup, traversal rejection\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}catch(...){std::cerr<<"unexpected backend failure\n";return 2;}}
