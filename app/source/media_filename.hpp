#pragma once
#include <string>
#include <algorithm>
namespace MediaFilename {
inline bool TitleId(const std::string& id){return id.size()==16&&id.find_first_not_of("0123456789abcdefABCDEF")==id.npos;}
inline std::string Id(const std::string& stem){
    if(stem.size()<19||stem.back()!=']'||stem.substr(stem.size()-19,2)!=" [")return {};
    const auto id=stem.substr(stem.size()-17,16);return TitleId(id)?id:std::string{};
}
inline std::string Readable(std::string title,const std::string& id){
    for(auto&c:title)if(static_cast<unsigned char>(c)<32||std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c=' ';
    const auto first=title.find_first_not_of(" ."),last=title.find_last_not_of(" .");
    title=first==title.npos?"Untitled":title.substr(first,last-first+1);
    if(title.size()>180){size_t end=180;while(end>0&&(static_cast<unsigned char>(title[end])&0xc0)==0x80)--end;title.resize(end);}
    if(TitleId(id))title+=" ["+id+"]";
    return title;
}
}
