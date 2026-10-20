#include "console_language.hpp"
#include "../module/src/LocalizedHomeMetadata.h"
#include "library.hpp"
#include "raw_launch.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <cstring>
namespace fs=std::filesystem;
struct Meta {
#define FIELD(l) char longname_##l[512]{},shortname_##l[256]{},publisher_##l[256]{};
    FIELD(ja) FIELD(en) FIELD(fr) FIELD(de) FIELD(it) FIELD(es)
    FIELD(zhs) FIELD(ko) FIELD(nl) FIELD(pt) FIELD(ru) FIELD(zht)
#undef FIELD
};
void Write(const std::string &p,const std::string &s) {
    fs::create_directories(fs::path(p).parent_path());std::ofstream(p,std::ios::binary)<<s;
}
int main() {
    const std::string root="fs:/vol/external01/region-tests";
    assert(!fs::exists(root));
    Meta m;SetLocalizedHomeMetadata(m,"日本語 title","uFlow","Author");
#define CHECK(l) assert(std::string(m.longname_##l)=="日本語 title");assert(std::string(m.shortname_##l)=="uFlow");assert(std::string(m.publisher_##l)=="Author");
    CHECK(ja) CHECK(en) CHECK(fr) CHECK(de) CHECK(it) CHECK(es)
    CHECK(zhs) CHECK(ko) CHECK(nl) CHECK(pt) CHECK(ru) CHECK(zht)
#undef CHECK
    std::string meta="<menu>";
    for(auto l:ConsoleLanguage::Tags)meta+="<longname_"+std::string(l)+">Title "+l+"</longname_"+l+">";
    meta+="</menu>";
    for(auto id:{"000500001010dc00","000500001010f500","0005000010113000"}) {
        const auto path=root+"/"+id;
        Write(path+"/code/app.xml",std::string("<app><title_id>")+id+"</title_id></app>");
        Write(path+"/code/cos.xml","<app><argstr>main.rpx</argstr></app>");
        std::string elf(20,'\0');elf[0]=127;elf.replace(1,3,"ELF");elf[4]=1;elf[5]=2;elf[19]=20;
        Write(path+"/code/main.rpx",elf);Write(path+"/meta/meta.xml",meta);
        fs::create_directories(path+"/content");
    }
    SaveContentSources({{Platform::WiiU,root,true,2}});
    for(uint32_t lang=0;lang<12;++lang) {
        ConsoleLanguage::testLanguage=lang;
        auto scan=ScanLibrary();assert(scan.games.size()==3);
        for(const auto &g:scan.games) {
            assert(g.name=="Title "+std::string(ConsoleLanguage::Tags[lang]));
            assert(ValidateRawGame(g).empty());
        }
        assert(SaveLibraryCache(scan.games));
        ConsoleLanguage::testLanguage=(lang+1)%12;
        std::vector<GameEntry> cached;assert(LoadLibraryCache(cached));
        for(const auto &g:cached)assert(g.name=="Title "+std::string(ConsoleLanguage::Tags[(lang+1)%12]));
    }
    ConsoleLanguage::testLanguage=999;assert(ConsoleLanguage::Current()==1);
    // v9 has no locale header; migrate its English names on a JPN console.
    ConsoleLanguage::testLanguage=1;
    auto scan=ScanLibrary();assert(SaveLibraryCache(scan.games));
    const std::string cache="fs:/vol/external01/wiiu/apps/uFlow/cache/library-v1.bin";
    std::ifstream in(cache,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
    bytes.erase(12,4);uint32_t version=9;std::memcpy(bytes.data()+4,&version,4);Write(cache,bytes);
    ConsoleLanguage::testLanguage=0;
    std::vector<GameEntry> migrated;assert(LoadLibraryCache(migrated)&&migrated.size()==3);
    for(const auto &g:migrated)assert(g.name=="Title ja");
    auto onlyJapanese=[](const char *l){return std::string(l)=="ja"?std::string("日本語"):std::string();};
    assert(ConsoleLanguage::Select(onlyJapanese,3)=="日本語");
    auto none=[](const char *){return std::string();};assert(ConsoleLanguage::Select(none,0).empty());
    // Offline entries survive changing locale.
    fs::rename(root,root+"-offline");
    std::vector<GameEntry> offline;assert(LoadLibraryCache(offline)&&offline.size()==3);
    std::cout<<"PASS: 12 locales, USA/EUR/JPN title IDs, language cache migration, offline preservation and HOME metadata\n";
}
