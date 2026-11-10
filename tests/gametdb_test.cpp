#include "gametdb.hpp"
#include "gametdb_database.hpp"
#include "library.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

// Minimal single-entry zip (deflate) built with zlib, matching GameTDB's archives.
static std::string Zip(const std::string &name,const std::string &data) {
    z_stream z{};deflateInit2(&z,9,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY);
    std::string packed(deflateBound(&z,data.size()),'\0');
    z.next_in=(Bytef*)data.data();z.avail_in=data.size();z.next_out=(Bytef*)packed.data();z.avail_out=packed.size();
    assert(deflate(&z,Z_FINISH)==Z_STREAM_END);packed.resize(z.total_out);deflateEnd(&z);
    const auto crc=crc32(0,(const Bytef*)data.data(),data.size());
    auto le=[](std::string &o,uint32_t v,int n){for(int i=0;i<n;++i)o+=char((v>>(8*i))&0xff);};
    std::string zip;
    le(zip,0x04034b50,4);le(zip,20,2);le(zip,0,2);le(zip,8,2);le(zip,0,4);le(zip,crc,4);le(zip,packed.size(),4);le(zip,data.size(),4);le(zip,name.size(),2);le(zip,0,2);zip+=name+packed;
    const auto central=zip.size();
    le(zip,0x02014b50,4);le(zip,20,2);le(zip,20,2);le(zip,0,2);le(zip,8,2);le(zip,0,4);le(zip,crc,4);le(zip,packed.size(),4);le(zip,data.size(),4);
    le(zip,name.size(),2);le(zip,0,2);le(zip,0,2);le(zip,0,2);le(zip,0,2);le(zip,0,4);le(zip,0,4);zip+=name;
    const auto size=zip.size()-central;
    le(zip,0x06054b50,4);le(zip,0,2);le(zip,0,2);le(zip,1,2);le(zip,1,2);le(zip,size,4);le(zip,central,4);le(zip,0,2);
    return zip;
}

int main() {
    // IDs
    assert(GameTdb::WiiUId("WUP-P-AMKE","0001")=="AMKE01");
    assert(GameTdb::WiiUId("wup-p-arpp","00A4")=="ARPPA4");
    assert(GameTdb::WiiUId("WUP-P-AM","0001").empty() && GameTdb::WiiUId("WUP-P-AMKE","").empty());
    assert(GameTdb::DiscId("rmge01")=="RMGE01" && GameTdb::DiscId("GALE01")=="GALE01");
    assert(GameTdb::DiscId("00050000101c9400").empty() && GameTdb::DiscId("RMG-01").empty());

    // Regions: specific first, then EN/US/JA/OTHER without duplicates.
    auto r=GameTdb::Regions("RMGE01");
    assert(r.front()=="US" && r.size()==4 && r[1]=="EN");
    r=GameTdb::Regions("RMGP01");assert(r.front()=="EN" && r[1]=="US");
    r=GameTdb::Regions("RMGD01");assert(r[0]=="DE" && r[1]=="EN");
    r=GameTdb::Regions("RMGJ01");assert(r[0]=="JA" && r[1]=="EN" && r.size()==4);

    // URLs
    auto u=GameTdb::Urls(GameTdb::System::WiiU,GameTdb::Art::Front,"AMKE01");
    assert(u.front()=="https://art.gametdb.com/wiiu/coverHQ/US/AMKE01.jpg");
    u=GameTdb::Urls(GameTdb::System::Wii,GameTdb::Art::Full,"GALP01");
    assert(u.front()=="https://art.gametdb.com/wii/coverfullHQ/EN/GALP01.png");
    u=GameTdb::Urls(GameTdb::System::Wii,GameTdb::Art::Front,"RMGE01");
    assert(u.front()=="https://art.gametdb.com/wii/cover/US/RMGE01.png");
    assert(GameTdb::Urls(GameTdb::System::Wii,GameTdb::Art::Front,"bad id").empty());

    // Payload sniffing rejects HTML error pages.
    assert(std::string(GameTdb::ImageExtension(std::string("\x89PNG\r\n\x1a\n....",12)))=="png");
    assert(std::string(GameTdb::ImageExtension(std::string("\xff\xd8\xff\xe0....",8)))=="jpg");
    assert(!GameTdb::ImageExtension("<html>404</html>"));

    // Library lookup from a raw Wii U title's meta.xml and from disc headers.
    const char *tmp=std::getenv("TMPDIR");
    std::string base=std::string(tmp&&*tmp?tmp:"/tmp")+"/uflow-gametdb.XXXXXX";assert(mkdtemp(base.data()));
    const std::string root=base;mkdir((root+"/meta").c_str(),0777);
    std::ofstream(root+"/meta/meta.xml")<<"<?xml version=\"1.0\"?><menu><product_code type=\"string\" length=\"32\">WUP-P-AMKE</product_code>"
        "<company_code type=\"string\" length=\"8\">0001</company_code></menu>";
    GameEntry wiiu;wiiu.platform=Platform::WiiU;wiiu.titleId="00050000101c9400";wiiu.absolutePath=root;
    assert(GameTdbId(wiiu)=="AMKE01");
    wiiu.vcSystem="wiiu";assert(GameTdbId(wiiu)=="AMKE01");
    wiiu.vcSystem="nes";assert(GameTdbId(wiiu).empty()); // injected VC titles have no retail art
    GameEntry wii;wii.platform=Platform::Wii;wii.titleId="RMGE01";assert(GameTdbId(wii)=="RMGE01");
    GameEntry gc;gc.platform=Platform::GameCube;gc.titleId="GALE01";assert(GameTdbId(gc)=="GALE01");
    GameEntry files;files.platform=Platform::Files;files.titleId="RMGE01";assert(GameTdbId(files).empty());
    GameEntry package=wii;package.installPackages.resize(1);assert(GameTdbId(package).empty());
    // wiiutdb.xml / wiitdb.xml parsing.
    assert(GameTdb::XmlText("Tom &amp; Jerry&#8217;s &#x263A;\n \n Next   line")=="Tom & Jerry’s ☺\n\nNext line");
    assert(GameTdb::Genres("action,role-playing")=="Action, Role-Playing");
    assert(GameTdb::DatabaseUrl(GameTdb::System::WiiU)=="https://www.gametdb.com/wiiutdb.zip");
    assert(GameTdb::DatabaseUrl(GameTdb::System::Wii)=="https://www.gametdb.com/wiitdb.zip");
    assert(GameTdb::LocaleFor("zht")=="ZHTW" && GameTdb::LocaleFor("xx")=="EN");
    std::string xml="<?xml version=\"1.0\"?><datafile><companies><company code=\"01\" name=\"Nintendo\"/></companies>";
    for(int i=0;i<400;++i)xml+="<game name=\"Filler "+std::to_string(i)+"\"><id>F"+std::to_string(10000+i)+"</id><locale lang=\"EN\"><title>Filler</title></locale></game>\n";
    xml+="<game name=\"Mario Kart 8 (USA) (EN)\">\n\t<id>AMKE01</id>\n\t<type>WiiU</type>\n\t<region>NTSC-U</region>\n"
         "\t<locale lang=\"EN\">\n\t\t<title>Mario Kart 8</title>\n\t\t<synopsis>Race &amp; drift.\n \nUpside down!</synopsis>\n\t</locale>\n"
         "\t<locale lang=\"FR\">\n\t\t<title>Mario Kart 8 FR</title>\n\t\t<synopsis/>\n\t</locale>\n"
         "\t<developer>Nintendo</developer>\n\t<publisher>Nintendo</publisher>\n\t<date year=\"2014\" month=\"5\" day=\"30\"/>\n"
         "\t<genre>racing</genre>\n\t<rating type=\"ESRB\" value=\"E\">\n\t\t<descriptor>comic mischief</descriptor>\n\t</rating>\n"
         "\t<wi-fi players=\"12\"/>\n\t<input players=\"4\"/>\n</game>\n"
         "<game name=\"Melee\"><id>GALE01</id><type>GameCube</type><region>NTSC-U</region><locale lang=\"KO\"><title>KO title</title></locale>"
         "<date year=\"2001\" month=\"\" day=\"\"/><genre/><input players=\"1\"/></game></datafile>";
    auto parsed=GameTdb::ParseDatabase(xml,{"AMKE01","GALE01","NOPE01"},"FR");
    assert(parsed.size()==2);
    const auto &mk=parsed.at("AMKE01");
    assert(mk.title=="Mario Kart 8 FR"); // console language first
    assert(mk.synopsis=="Race & drift.\n\nUpside down!"); // empty FR synopsis falls back to EN
    assert(mk.developer=="Nintendo" && mk.publisher=="Nintendo" && mk.genre=="Racing" && mk.region=="USA");
    assert(mk.rating=="ESRB E" && mk.date=="2014-05-30" && mk.players=="1-4"); // input players, not wi-fi
    const auto &melee=parsed.at("GALE01");
    assert(melee.title=="KO title" && melee.date=="2001" && melee.genre.empty() && melee.players=="1");
    // Streaming inflate across tiny and large chunks gives the same answer.
    const auto zip=Zip("wiitdb.xml",xml);
    std::string inflated;assert(GameTdb::InflateFirstEntry(zip,[&](const char *d,std::size_t n){inflated.append(d,n);return true;}));
    assert(inflated==xml);
    std::map<std::string,GameTdb::Metadata> streamed;
    assert(GameTdb::ParseZippedDatabase(zip,{"AMKE01","GALE01"},"FR",streamed));
    assert(streamed.size()==2 && streamed.at("AMKE01").synopsis==mk.synopsis);
    streamed.clear();assert(GameTdb::ParseZippedDatabase(zip,{"F10007"},"EN",streamed) && streamed.at("F10007").title=="Filler");
    streamed.clear();assert(!GameTdb::ParseZippedDatabase(zip,{"AMKE01"},"EN",streamed,[]{return true;})); // cancelled
    assert(!GameTdb::ParseZippedDatabase(zip.substr(0,zip.size()/2),{"AMKE01"},"EN",streamed)); // truncated
    assert(!GameTdb::ParseZippedDatabase("not a zip",{"AMKE01"},"EN",streamed));
    // The saved JSON reads back through uFlow's metadata reader.
    const auto json=root+"/amke01.json";
    std::ofstream(json)<<GameTdb::MetadataJson(mk);
    GameEntry reader;ReadMediaFile(reader,MediaType::Metadata,json);
    assert(reader.name=="Mario Kart 8 FR" && reader.description==mk.synopsis && reader.publisher=="Nintendo");
    assert(reader.developer=="Nintendo" && reader.genre=="Racing" && reader.region=="USA" && reader.rating=="ESRB E");
    assert(reader.releaseYear=="2014" && reader.players=="1-4" && reader.metadataPath==json);
    std::remove(json.c_str());
    std::remove((root+"/meta/meta.xml").c_str());rmdir((root+"/meta").c_str());rmdir(root.c_str());
    std::cout<<"PASS: GameTDB IDs from meta.xml and disc headers, region fallback order, HQ URLs, image sniffing, streamed wiiutdb/wiitdb parsing and metadata JSON round trip\n";
}
