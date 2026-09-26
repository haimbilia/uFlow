#include "library.hpp"
#include "raw_launch.hpp"
#include "launchers.hpp"
#include "vc_package.hpp"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
const std::string root = "fs:/vol/external01";

void Write(const std::string &path, const std::string &data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}

void Title(const std::string &path, const std::string &id, const std::string &name = {}) {
    Write(path + "/code/app.xml", "<app><title_id>" + id + "</title_id><title_version>0041</title_version></app>");
    Write(path + "/code/cos.xml", "<app><argstr>project.rpx</argstr></app>");
    std::string elf(20, '\0');
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F'; elf[4] = 1; elf[5] = 2; elf[19] = 20;
    Write(path + "/code/project.rpx", elf);
    fs::create_directories(path + "/content");
    if (!name.empty()) {
        // Real update meta.xml uses the base ID, unlike its executable app.xml.
        Write(path + "/meta/meta.xml", "<menu><title_id>000500001012f000</title_id><longname_en>" + name + "</longname_en></menu>");
        Write(path + "/meta/iconTex.tga", "icon-fixture");
    }
}

int main() {
    // Run in a disposable working directory, never against user game files.
    assert(!fs::exists(root));
    const std::string library = root + "/test-library";
    const std::string base = library + "/FAST Racing NEO [WFSE]";
    Title(base, "000500001012F000");
    Title(library + "/extras/update", "0005000E1012F000", "FAST Racing NEO");
    Title(library + "/extras/dlc", "0005000C1012F000", "FAST Racing NEO DLC");
    std::vector<ContentSource> sources {{Platform::WiiU, library, true, 3}};
    SaveContentSources(sources);
    auto scan = ScanLibrary();
    assert(scan.games.size() == 1);
    const auto &game = scan.games.front();
    assert(!game.installed && game.absolutePath == base);
    assert(game.titleId == "000500001012f000");
    assert(game.name == "FAST Racing NEO");
    if (game.coverPath != library + "/extras/update/meta/iconTex.tga") std::cerr << "Unexpected cover: " << game.coverPath << '\n';
    assert(game.coverPath == library + "/extras/update/meta/iconTex.tga");
    assert(game.rpxRelative == "test-library/FAST Racing NEO [WFSE]/code/project.rpx");
    assert(ValidateRawGame(game).empty());
    assert(SaveLibraryCache(scan.games));
    std::vector<GameEntry> cached;
    assert(LoadLibraryCache(cached));
    assert(cached.size() == 1 && cached.front().absolutePath == game.absolutePath &&
           cached.front().coverPath == game.coverPath && cached.front().rpxRelative == game.rpxRelative);
    // Old catalogs retained folder names for Japan-only titles forever.
    {
        std::fstream old(root+"/wiiu/apps/uFlow/cache/library-v1.bin",std::ios::in|std::ios::out|std::ios::binary);
        const std::uint32_t version=4;old.seekp(4);old.write(reinterpret_cast<const char *>(&version),sizeof(version));
    }
    assert(!LoadLibraryCache(cached));
    assert(SaveLibraryCache(scan.games)&&LoadLibraryCache(cached));
    const std::string japanese="\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80";
    Write(base+"/meta/meta.xml","<menu><longname_en> </longname_en><longname_ja>"+japanese+"\n Title</longname_ja><publisher_ja>Publisher</publisher_ja></menu>");
    auto multilingual=ScanLibrary();
    assert(multilingual.games.size()==1&&multilingual.games[0].name==japanese+" Title"&&multilingual.games[0].publisher=="Publisher");
    fs::remove(base+"/meta/meta.xml");
    Write(base + "/code/project.rpx", "corrupt");
    assert(!ValidateRawGame(game).empty());
    Title(base, "000500001012F000");

    // Overlapping sources must not duplicate the same game directory.
    sources.push_back({Platform::WiiU, base, true, 0});
    SaveContentSources(sources);
    assert(!LoadLibraryCache(cached)); // changing sources forces a fresh catalog
    assert(ScanLibrary().games.size() == 1);
    sources.resize(1);
    sources[0].depth = 0;
    SaveContentSources(sources);
    assert(ScanLibrary().games.empty());
    sources[0].depth = 1;
    SaveContentSources(sources);
    scan = ScanLibrary();
    assert(scan.games.size() == 1 && scan.games.front().name == "FAST Racing NEO");
    assert(scan.games.front().coverPath.empty()); // update is outside scan depth
    sources[0].enabled = false;
    SaveContentSources(sources);
    assert(ScanLibrary().games.empty());
    SaveContentSources({});
    assert(LoadContentSources().empty());
    assert(ScanLibrary().games.empty());
    std::cout << "PASS: base/update/DLC classification, metadata fallback, executable validation, overlap, depth, disabled/empty sources\n";
    std::string iso(0x4000,'\0');
    iso.replace(0,6,"GMSE01"); iso.replace(0x1c,4,"\xc2\x33\x9f\x3d",4);
    iso.replace(32,9,"Disc Name");
    iso[0x422]=0x25; iso[0x426]=0x30; iso[0x42b]=12;
    Write(root+"/games/Untagged folder/game.iso",iso);
    Write(root+"/games/Untagged folder/disc2.iso",iso);
    SaveContentSources({{Platform::GameCube,root+"/games",true,1}});
    scan=ScanLibrary();
    assert(scan.games.size()==1 && scan.games[0].titleId=="GMSE01");
    assert(scan.games[0].name=="Disc Name" && scan.games[0].platform==Platform::GameCube);
    std::cout << "PASS: GameCube header-derived ID/title and disc2 exclusion\n";
    SaveContentSources({{Platform::GameCube,root+"/games",true,1,GameLauncher::USBLoaderGX}});
    assert(LoadContentSources()[0].launcher==GameLauncher::USBLoaderGX);
    assert(ScanLibrary().games[0].launcher==GameLauncher::USBLoaderGX);
    Write(root+"/wiiu/apps/uFlow/content_sources.cfg","gamecube\t1\t1\t"+root+"/games\n");
    assert(LoadContentSources()[0].launcher==GameLauncher::WiiFlow);
    std::cout << "PASS: per-directory launcher roundtrip, scan propagation, legacy default\n";
    auto types=LoadLibraries();
    types.push_back({"ds","Nintendo DS",Platform::Files,GameLauncher::WiiFlow,".nds",true,3});
    assert(SaveLibraries(types)); assert(LoadLibraries().back().name=="Nintendo DS");
    types.back().caseWidthMm=140;types.back().caseHeightMm=125;types.back().caseThicknessMm=17;
    assert(SaveLibraries(types));
    const auto sized=LoadLibraries().back();
    assert(sized.caseWidthMm==140&&sized.caseHeightMm==125&&sized.caseThicknessMm==17);
    auto invalidTypes=types;invalidTypes.back().caseWidthMm=0;
    assert(!SaveLibraries(invalidTypes));assert(LoadLibraries().back().caseWidthMm==140);
    Write(root+"/wiiu/apps/uFlow/libraries.cfg","custom_ds\tNintendo DS\tfiles\twiiflow\t.nds\t1\t0\tnoods\n");
    auto legacy=LoadLibraries().front();
    assert(legacy.caseWidthMm==135&&legacy.caseHeightMm==122&&legacy.caseThicknessMm==15);
    Write(root+"/wiiu/apps/uFlow/libraries.cfg","custom_ds\tNintendo DS\tfiles\twiiflow\t.nds\t1\t0\tnoods\t0\tbad\t999\n");
    legacy=LoadLibraries().front();
    assert(legacy.caseWidthMm==135&&legacy.caseHeightMm==122&&legacy.caseThicknessMm==15);
    assert(SaveLibraries(types));
    std::cout << "PASS: case dimensions persist, legacy DS defaults, invalid values rejected safely\n";
    Write(root+"/nds/Example.NDS","fixture"); Write(root+"/nds/ignore.txt","fixture");
    assert(SaveContentSources({{Platform::Files,root+"/nds",true,1,GameLauncher::WiiFlow,"ds"}}));
    scan=ScanLibrary(); assert(scan.games.size()==1 && scan.games[0].libraryId=="ds" && scan.games[0].platform==Platform::Files);
    types.back().enabled=false; assert(SaveLibraries(types)); assert(ScanLibrary().games.empty());
    types.back().name="Renamed DS"; types.back().enabled=true; assert(SaveLibraries(types));
    assert(ScanLibrary().games[0].libraryId=="ds");
    std::cout << "PASS: custom type persistence, extension filtering, disabled scanning and stable rename\n";
    auto launchers=LoadLaunchers();assert(launchers.size()==5);
    assert(std::count_if(launchers.begin(),launchers.end(),[](const auto &l){return l.kind==LauncherKind::VirtualConsole;})==0);
    assert(SaveLaunchers(launchers));assert(LoadLaunchers().size()==5);
    assert(LauncherAccepts(launchers.back(),Platform::GameCube));assert(!LauncherAccepts(launchers.back(),Platform::Wii));
    launchers.push_back({"custom_emulator","Custom Emulator",LauncherKind::Emulator,"Nintendo DS",".nds","wiiu/apps/Test Emulator/test.rpx",true});
    assert(SaveLaunchers(launchers));assert(LoadLaunchers().back().executable==launchers.back().executable);
    launchers.back().enabled=false;assert(SaveLaunchers(launchers));assert(!LoadLaunchers().back().enabled);
    launchers.back().name="Bad\tName";assert(!SaveLaunchers(launchers));assert(LoadLaunchers().back().name=="Custom Emulator");
    launchers.back().name="Custom Emulator";launchers.back().id="native";assert(!SaveLaunchers(launchers));
    assert(LauncherSDPath("sd:/wiiu/apps/Example emulator/app.WUHB")=="wiiu/apps/Example emulator/app.WUHB");
    assert(LauncherSDPath("fs:/vol/external01/wiiu/apps/test.rpx")=="wiiu/apps/test.rpx");
    assert(LauncherMatchesFile(DefaultLaunchers()[3],"Example.NDS"));
    assert(!LauncherMatchesFile(DefaultLaunchers()[3],"Example.iso"));
    for(const char *invalid:{"../test.rpx","sd:/../test.rpx","/vol/storage_mlc01/title/x.rpx","wiiu/../test.rpx","wiiu/test.exe","wiiu//test.rpx"})assert(LauncherSDPath(invalid).empty());
    assert(!LauncherExecutableExists("wiiu/apps/test.rpx"));
    Write(root+"/wiiu/apps/test.rpx","fixture");assert(LauncherExecutableExists("wiiu/apps/test.rpx"));
    types.back().launcherId="vc_ds";assert(SaveLibraries(types));assert(LoadLibraries().back().launcherId=="vc_ds");
    auto rom=ScanLibrary().games.front();GameEntry injection;
    injection.platform=Platform::WiiU;injection.titleId="000500001234ABCD";injection.installed=true;
    assert(!ResolveVCInjection(rom,{injection}).empty());assert(SaveVCMapping(rom,injection));
    auto mapped=rom;assert(ResolveVCInjection(mapped,{injection}).empty());assert(mapped.installed&&mapped.titleId==injection.titleId);
    fs::rename(rom.absolutePath+".uflow-title",rom.absolutePath+".uflow-title.bak");
    mapped=rom;assert(ResolveVCInjection(mapped,{injection}).empty()); // interrupted replacement recovers old mapping
    fs::rename(rom.absolutePath+".uflow-title.bak",rom.absolutePath+".uflow-title");
    assert(!ResolveVCInjection(rom,{}).empty());
    injection.titleId="000500101234abcd";assert(!SaveVCMapping(rom,injection));
    Write(rom.absolutePath+".uflow-title","000500101234abcd\n");assert(!ResolveVCInjection(rom,{injection}).empty());
    assert(SaveLaunchers({}));assert(LoadLaunchers().empty());
    std::cout<<"PASS: launcher defaults, persistence, validation, SD executable paths, type assignment, VC mapping and missing target rejection\n";
    // Reproduce the PCE source change reported on the console.
    assert(SaveLibraries({{"pce","PC Engine",Platform::Files,GameLauncher::WiiFlow,".pce",true,0,"pce-vc"}}));
    Write(root+"/wiiu/roms/pce/Bomberman.pce","fixture");
    Write(root+"/pce/Bonk's Adventure (USA).pce",std::string(393216,'\0'));
    const ContentSource oldPce{Platform::Files,root+"/wiiu/roms/pce",true,3,GameLauncher::WiiFlow,"pce","pce-vc"};
    assert(SaveContentSources({oldPce}));
    scan=ScanLibrary();assert(scan.games.size()==1&&scan.games[0].name=="Bomberman");
    assert(SaveLibraryCache(scan.games));assert(SaveLibraryCache(scan.games));
    auto newPce=oldPce;newPce.path=root+"/pce";
    assert(SaveContentSources({newPce}));
    assert(LoadContentSources().front().path==newPce.path);
    assert(!LoadLibraryCache(cached));
    scan=ScanLibrary();assert(scan.games.size()==1&&scan.games[0].name=="Bonk's Adventure (USA)");
    assert(scan.games[0].libraryId=="pce");
    assert(SaveLibraryCache(scan.games));assert(LoadLibraryCache(cached)&&cached[0].name==scan.games[0].name);
    const auto config=root+"/wiiu/apps/uFlow/content_sources.cfg";
    fs::rename(config,config+".bak");
    assert(LoadContentSources().front().path==newPce.path);
    assert(SaveContentSources({newPce}));
    std::cout<<"PASS: PCE directory change, apostrophe filename, cache replacement and config backup recovery\n";
    // Packages represent the original game, not the embedded VC base/BIOS.
    auto original=scan.games.front();original.favorite=true;SaveFavorites({original});
    original.name="Bonk's Adventure (USA)";original.titleId="BONK";
    const auto package=VCPackage::Path(original),generation=package+"/game-test";
    Title(generation,"0005000010166d00","WRONG BASE NAME");
    Write(generation+"/uflow.complete",std::string(64,'a'));
    Write(generation+"/content/BIOS.pce","must not appear");
    GameEntry prepared;prepared.platform=Platform::WiiU;prepared.absolutePath=generation;
    prepared.titleId="0005000010166d00";prepared.rpxRelative=UFlowRom::RelativeROM(generation)+"/code/project.rpx";
    prepared.saveRelative="wiiu/vc-saves/pce/unchanged/0005000010166d00";
    const auto record=VCPackage::Record(original,prepared);assert(!record.empty());Write(package+"/game.cfg",record);
    const auto art=root+"/art/pce";Write(art+"/BONK.png","fixture");
    assert(SaveMediaSources({{MediaType::FullCover,-1,art,true,0,"pce"}}));
    GameEntry identity,target;assert(VCPackage::Load(package,identity,target));assert(identity.titleId=="BONK"&&target.titleId==prepared.titleId);
    newPce.depth=0;assert(SaveContentSources({newPce}));
    scan=ScanLibrary();assert(scan.games.size()==1&&scan.games[0].name==original.name&&scan.games[0].favorite&&scan.games[0].titleId=="BONK");
    fs::remove(original.absolutePath);
    scan=ScanLibrary();assert(scan.games.size()==1&&scan.games[0].absolutePath==original.absolutePath&&scan.games[0].libraryId=="pce"&&scan.games[0].favorite);
    assert(scan.games[0].fullCoverPath==art+"/BONK.png");
    assert(SaveLibraryCache(scan.games));assert(LoadLibraryCache(cached)&&cached[0].absolutePath==original.absolutePath);
    // Backup recovery supports the console's no-overwrite rename semantics.
    fs::rename(package+"/game.cfg",package+"/game.cfg.bak");assert(ScanLibrary().games.size()==1);
    fs::rename(package+"/game.cfg.bak",package+"/game.cfg");
    auto escaped=record;const auto position=escaped.find("game-test");escaped.replace(position,9,"game-../../escape");
    Write(package+"/game.cfg",escaped);assert(!VCPackage::Load(package,identity,target));
    newPce.depth=8;assert(SaveContentSources({newPce}));assert(ScanLibrary().games.empty());
    Write(package+"/game.cfg",record);
    assert(SaveContentSources({{Platform::WiiU,root+"/pce",true,8,GameLauncher::WiiFlow,"wiiu"}}));assert(ScanLibrary().games.empty());
    newPce.enabled=false;assert(SaveContentSources({newPce}));assert(ScanLibrary().games.empty());
    newPce.enabled=true;assert(SaveContentSources({newPce}));
    auto pceTypes=LoadLibraries();pceTypes[0].extensions=".nds";assert(SaveLibraries(pceTypes));assert(ScanLibrary().games.empty());
    pceTypes[0].extensions=".pce";assert(SaveLibraries(pceTypes));assert(ScanLibrary().games.size()==1);
    // Pre-upgrade injections remain discoverable even if the original was removed.
    const auto legacyRoot=root+"/wiiu/apps/uFlow/vc-cache/pce/legacy";
    Title(legacyRoot,"0005000010166d00");Write(legacyRoot+"/uflow.complete",std::string(64,'b'));
    const auto legacyRom=root+"/pce/Legacy.pce";
    Write(root+"/wiiu/apps/uFlow/injections/pce/"+std::string(64,'c')+".cfg",
          "UFLOW-SAVED-VC-1\n"+legacyRom+"\n"+legacyRoot+"\n"+UFlowRom::RelativeROM(legacyRoot)+"/code/project.rpx\nwiiu/vc-saves/pce/legacy/0005000010166d00\n0005000010166d00\n");
    scan=ScanLibrary();assert(scan.games.size()==2);
    Write(legacyRom,"fixture");assert(ScanLibrary().games.size()==2);
    std::cout<<"PASS: sibling package deduplication, ROM deletion, stable identity/favorites/cache, metadata, depth/extension/disabled filtering, backup recovery, traversal and BIOS exclusion, legacy discovery\n";
    // New NES aliases must upgrade old libraries without making patches or
    // non-NES ZIPs into games; a saved ZIP identity survives source removal.
    LibraryDefinition nes{"nes","NES",Platform::Files,GameLauncher::WiiFlow,".nes",true,0,"nes-vc"};
    LauncherDefinition oldNes{"nes-vc","NES",LauncherKind::DSInjection,"NES",".nes","",true,"nes-vc"};
    assert(LauncherMatchesFile(oldNes,"Lolo.zip")&&LauncherMatchesFile(oldNes,"Lolo.UNIF")&&!LauncherMatchesFile(oldNes,"patch.ips"));
    assert(SaveLibraries({nes}));assert(LoadLibraries()[0].extensions==VCSystems::Find("nes")->extensions);
    ContentSource nesSource{Platform::Files,root+"/nes",true,1,GameLauncher::WiiFlow,"nes","nes-vc"};
    assert(SaveContentSources({nesSource}));Write(root+"/nes/patch.ips","PATCHfixture");Write(root+"/nes/not-a-rom.zip","invalid");
    Write(root+"/nes/test.UNIF","UNIFfixture");assert(ScanLibrary().games.size()==1);
    GameEntry zipRom;zipRom.platform=Platform::Files;zipRom.absolutePath=root+"/nes/Lolo.zip";zipRom.name="Lolo";zipRom.folder="Lolo.zip";
    auto zipPrepared=prepared;zipPrepared.absolutePath=zipRom.absolutePath+".uflow/game-test";
    Title(zipPrepared.absolutePath,"0005000010133100");Write(zipPrepared.absolutePath+"/uflow.complete",std::string(64,'c'));
    zipPrepared.rpxRelative=UFlowRom::RelativeROM(zipPrepared.absolutePath)+"/code/project.rpx";
    zipPrepared.saveRelative="wiiu/vc-saves/nes/test/0005000010133100";zipPrepared.titleId="0005000010133100";
    Write(VCPackage::Path(zipRom)+"/game.cfg",VCPackage::Record(zipRom,zipPrepared));
    assert(VCPackage::Load(VCPackage::Path(zipRom),identity,target));assert(identity.absolutePath==zipRom.absolutePath);
    assert(ScanLibrary().games.size()==2);assert(VCSystems::File(zipRom.absolutePath)=="nes");
    std::cout<<"PASS: legacy NES library upgrade, UNIF discovery, patch/non-NES ZIP exclusion, saved ZIP without original\n";
}
