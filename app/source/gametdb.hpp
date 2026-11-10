#pragma once
// Pure GameTDB naming rules: no network or file access, so they are host-testable.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GameTdb {
enum class Art { Front, Full };
enum class System { WiiU, Wii }; // GameCube artwork is published under the Wii tree.

inline bool ValidId(const std::string &id) {
    return (id.size()==4 || id.size()==6) &&
        std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isdigit(c) || std::isupper(c);});
}
inline std::string Upper(std::string value) {
    for(auto &c:value)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
// Disc games already carry the GameTDB ID in their header.
inline std::string DiscId(const std::string &titleId) {
    const auto id=Upper(titleId);
    return id.size()==6 && ValidId(id) ? id : std::string();
}
// Wii U meta.xml: product_code "WUP-P-AMKE" + company_code "0001" -> "AMKE01".
inline std::string WiiUId(const std::string &productCode,const std::string &companyCode) {
    const auto dash=productCode.find_last_of('-');
    const auto game=Upper(dash==std::string::npos?productCode:productCode.substr(dash+1));
    std::string company;
    for(unsigned char c:companyCode)if(std::isalnum(c))company.push_back(static_cast<char>(std::toupper(c)));
    if(game.size()!=4 || company.size()<2)return {};
    const auto id=game+company.substr(company.size()-2);
    return ValidId(id)?id:std::string();
}
// Preferred GameTDB region folders for a game ID, most specific first.
inline std::vector<std::string> Regions(const std::string &id) {
    std::vector<std::string> regions;
    const char code=id.size()>=4?id[3]:'\0';
    switch(code) {
        case 'E': case 'N': regions={"US"}; break;
        case 'J': regions={"JA"}; break;
        case 'K': case 'Q': case 'T': regions={"KO"}; break;
        case 'W': regions={"ZH"}; break;
        case 'R': regions={"RU","EN"}; break;
        case 'D': regions={"DE","EN"}; break;
        case 'F': regions={"FR","EN"}; break;
        case 'I': regions={"IT","EN"}; break;
        case 'S': regions={"ES","EN"}; break;
        case 'H': regions={"NL","EN"}; break;
        case 'U': regions={"AU","EN"}; break;
        default: regions={"EN"}; break; // P, X, Y, Z and other PAL variants.
    }
    for(const char *fallback:{"EN","US","JA","OTHER"})
        if(std::find(regions.begin(),regions.end(),fallback)==regions.end())regions.push_back(fallback);
    return regions;
}
// Highest-quality variant GameTDB publishes for each system and art type.
inline std::string Folder(System system,Art art) {
    if(system==System::WiiU)return art==Art::Front?"coverHQ":"coverfullHQ";
    return art==Art::Front?"cover":"coverfullHQ"; // Wii has no coverHQ.
}
inline std::string Extension(System system) { return system==System::WiiU?"jpg":"png"; }
inline std::vector<std::string> Urls(System system,Art art,const std::string &id) {
    std::vector<std::string> urls;
    if(!ValidId(id))return urls;
    const std::string base=std::string("https://art.gametdb.com/")+(system==System::WiiU?"wiiu/":"wii/")+Folder(system,art)+"/";
    for(const auto &region:Regions(id))urls.push_back(base+region+"/"+id+"."+Extension(system));
    return urls;
}
// Reject HTML error pages or truncated bodies before they reach the media folders.
inline const char *ImageExtension(const std::string &bytes) {
    if(bytes.size()>8 && bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0)return "png";
    if(bytes.size()>4 && static_cast<unsigned char>(bytes[0])==0xff && static_cast<unsigned char>(bytes[1])==0xd8 &&
       static_cast<unsigned char>(bytes[2])==0xff)return "jpg";
    return nullptr;
}

// ---- wiiutdb.xml / wiitdb.xml metadata -----------------------------------
struct Metadata {
    std::string id,title,synopsis,developer,publisher,genre,rating,region,date,players;
};
// Unfiltered single-file zips: a LANG filter drops GameCube and other entries.
// Wii and GameCube share wiitdb.xml.
inline std::string DatabaseUrl(System system) {
    return system==System::WiiU?"https://www.gametdb.com/wiiutdb.zip":"https://www.gametdb.com/wiitdb.zip";
}
// Cafe language tag (ConsoleLanguage::Tags) -> GameTDB locale code.
inline std::string LocaleFor(const std::string &tag) {
    static const std::map<std::string,std::string> map{{"ja","JA"},{"en","EN"},{"fr","FR"},{"de","DE"},{"it","IT"},
        {"es","ES"},{"zhs","ZHCN"},{"ko","KO"},{"nl","NL"},{"pt","PT"},{"ru","RU"},{"zht","ZHTW"}};
    const auto found=map.find(tag);return found==map.end()?"EN":found->second;
}
inline void AppendUtf8(std::string &out,uint32_t cp) {
    if(cp==0 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return;
    if(cp<0x80)out+=static_cast<char>(cp);
    else if(cp<0x800){out+=static_cast<char>(0xc0|(cp>>6));out+=static_cast<char>(0x80|(cp&0x3f));}
    else if(cp<0x10000){out+=static_cast<char>(0xe0|(cp>>12));out+=static_cast<char>(0x80|((cp>>6)&0x3f));out+=static_cast<char>(0x80|(cp&0x3f));}
    else{out+=static_cast<char>(0xf0|(cp>>18));out+=static_cast<char>(0x80|((cp>>12)&0x3f));out+=static_cast<char>(0x80|((cp>>6)&0x3f));out+=static_cast<char>(0x80|(cp&0x3f));}
}
// Decode XML entities and collapse whitespace runs, keeping paragraph breaks.
inline std::string XmlText(const std::string &raw) {
    std::string decoded;decoded.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i){
        if(raw[i]!='&'){decoded+=raw[i];continue;}
        const auto end=raw.find(';',i);
        if(end==std::string::npos || end-i>10){decoded+='&';continue;}
        const auto name=raw.substr(i+1,end-i-1);
        if(name=="amp")decoded+='&';else if(name=="lt")decoded+='<';else if(name=="gt")decoded+='>';
        else if(name=="quot")decoded+='"';else if(name=="apos")decoded+='\'';
        else if(name.size()>1 && name[0]=='#'){
            const bool hex=name[1]=='x'||name[1]=='X';
            AppendUtf8(decoded,static_cast<uint32_t>(std::strtoul(name.c_str()+(hex?2:1),nullptr,hex?16:10)));
        } else {decoded+='&';continue;}
        i=end;
    }
    std::string out;int newlines=0;bool space=false;
    for(char c:decoded){
        if(c=='\r')continue;
        if(c=='\n'){++newlines;continue;}
        if(c==' '||c=='\t'){space=true;continue;}
        if(!out.empty()){if(newlines>=2)out+="\n\n";else if(newlines||space)out+=' ';}
        newlines=0;space=false;out+=c;
    }
    return out;
}
// Text of <tag>..</tag> inside [begin,end); empty for <tag/>.
inline std::string Element(const std::string &xml,const std::string &tag,std::size_t begin,std::size_t end) {
    const std::string open="<"+tag;
    for(auto at=xml.find(open,begin);at!=std::string::npos && at<end;at=xml.find(open,at+1)){
        const char next=at+open.size()<xml.size()?xml[at+open.size()]:'\0';
        if(next!='>' && next!=' ' && next!='/')continue; // <genre> must not match <genres>
        const auto close=xml.find('>',at);
        if(close==std::string::npos || close>=end || xml[close-1]=='/')return {};
        const auto stop=xml.find("</"+tag+">",close);
        if(stop==std::string::npos || stop>end)return {};
        return XmlText(xml.substr(close+1,stop-close-1));
    }
    return {};
}
inline std::string Attribute(const std::string &xml,const std::string &tag,const std::string &name,std::size_t begin,std::size_t end) {
    const auto at=xml.find("<"+tag+" ",begin);
    if(at==std::string::npos || at>=end)return {};
    const auto close=xml.find('>',at);
    const auto key=xml.find(" "+name+"=\"",at);
    if(key==std::string::npos || key>close)return {};
    const auto value=key+name.size()+3,stop=xml.find('"',value);
    return stop==std::string::npos || stop>close?std::string():XmlText(xml.substr(value,stop-value));
}
inline std::string Region(const std::string &tdbRegion) {
    if(tdbRegion=="NTSC-U")return "USA";
    if(tdbRegion=="NTSC-J")return "Japan";
    if(tdbRegion=="NTSC-K")return "Korea";
    if(tdbRegion.rfind("PAL",0)==0)return "Europe";
    return {};
}
// "action,role-playing" -> "Action, Role-Playing"
inline std::string Genres(const std::string &raw) {
    std::string out;bool start=true;
    for(char c:raw){
        if(c==','){out+=", ";start=true;continue;}
        if(c==' ' && start)continue;
        out+=start?static_cast<char>(std::toupper(static_cast<unsigned char>(c))):c;
        start=c=='-'||c==' ';
    }
    return out;
}
// Parse only the wanted IDs. Locale preference: requested, then EN, then any.
inline std::map<std::string,Metadata> ParseDatabase(const std::string &xml,const std::set<std::string> &wanted,const std::string &locale) {
    std::map<std::string,Metadata> result;
    for(auto game=xml.find("<game ");game!=std::string::npos && result.size()<wanted.size();game=xml.find("<game ",game+1)){
        const auto end=xml.find("</game>",game);
        if(end==std::string::npos)break;
        const auto id=Element(xml,"id",game,end);
        if(!wanted.count(id) || result.count(id))continue;
        Metadata m;m.id=id;
        for(const auto &lang:{locale,std::string("EN"),std::string()}){
            const auto at=xml.find(lang.empty()?std::string("<locale lang=\""):"<locale lang=\""+lang+"\"",game);
            if(at==std::string::npos || at>end)continue;
            const auto stop=std::min(end,xml.find("</locale>",at));
            if(m.title.empty())m.title=Element(xml,"title",at,stop);
            if(m.synopsis.empty())m.synopsis=Element(xml,"synopsis",at,stop);
        }
        m.developer=Element(xml,"developer",game,end);
        m.publisher=Element(xml,"publisher",game,end);
        m.genre=Genres(Element(xml,"genre",game,end));
        m.region=Region(Element(xml,"region",game,end));
        const auto type=Attribute(xml,"rating","type",game,end),value=Attribute(xml,"rating","value",game,end);
        if(!type.empty() && !value.empty())m.rating=type+" "+value;
        const auto year=Attribute(xml,"date","year",game,end);
        if(year.size()==4){
            m.date=year;
            const auto month=std::atoi(Attribute(xml,"date","month",game,end).c_str()),day=std::atoi(Attribute(xml,"date","day",game,end).c_str());
            if(month>=1 && month<=12){
                char text[8];std::snprintf(text,sizeof(text),"-%02d",month);m.date+=text;
                if(day>=1 && day<=31){std::snprintf(text,sizeof(text),"-%02d",day);m.date+=text;}
            }
        }
        const auto players=std::atoi(Attribute(xml,"input","players",game,end).c_str());
        if(players>0)m.players=players==1?"1":"1-"+std::to_string(players);
        result[id]=std::move(m);
    }
    return result;
}
}
