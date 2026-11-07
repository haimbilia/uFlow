#pragma once
#include <algorithm>
#include <set>
#include <string>
#include <vector>
namespace MediaNameMatch {
// Only bracketed filename tags are regions; words in a game's title are not.
inline unsigned RegionMask(std::string text,bool tagsOnly=true){
    unsigned mask=0;std::string token;bool inside=!tagsOnly;
    auto flush=[&]{
        if(token=="usa"||token=="us"||token=="america")mask|=1;
        else if(token=="europe"||token=="eur"||token=="eu"||token=="pal")mask|=2;
        else if(token=="japan"||token=="jpn"||token=="jp")mask|=4;
        else if(token=="korea"||token=="kor"||token=="kr")mask|=8;
        else if(token=="world")mask|=15;
        token.clear();
    };
    for(unsigned char c:text){
        if(c=='('||c=='['){flush();inside=true;continue;}
        if(c==')'||c==']'){flush();inside=!tagsOnly;continue;}
        if(!inside)continue;
        if(c>='A'&&c<='Z')c+=32;
        if(c>='a'&&c<='z')token+=char(c);else flush();
    }
    flush();return mask;
}
inline int RegionRank(unsigned wanted,unsigned available){
    if(!available)return 1;
    if(!wanted)return 0;
    if(!(wanted&available))return -1;
    return wanted==available?3:2;
}
inline std::string Compact(const std::string& value){std::string out;for(unsigned char c:value){if(c>='A'&&c<='Z')c+=32;if((c>='a'&&c<='z')||(c>='0'&&c<='9'))out+=char(c);}return out;}
inline std::set<std::string> Edition(const std::string& value){
    static const std::set<std::string> words={"i","ii","iii","iv","v","vi","vii","viii","ix","x","demo","trial","dlc","bundle","collection","deluxe","remastered","remake","hd","edition","alternate","beta"};
    std::set<std::string> result;std::string token;
    auto flush=[&]{if(!token.empty()&&(words.count(token)||token.find_first_of("0123456789")!=token.npos))result.insert(token);token.clear();};
    for(unsigned char c:value){if(c>='A'&&c<='Z')c+=32;if((c>='a'&&c<='z')||(c>='0'&&c<='9'))token+=char(c);else flush();}flush();return result;
}
inline int Score(const std::string& a,const std::string& b){
    const auto large=std::max(a.size(),b.size()),small=std::min(a.size(),b.size());
    if(small<8||100*(large-small)>11*large)return 0;
    std::vector<unsigned> row(b.size()+1);for(unsigned j=0;j<row.size();++j)row[j]=j;
    for(unsigned i=1;i<=a.size();++i){unsigned diagonal=row[0];row[0]=i;for(unsigned j=1;j<=b.size();++j){const unsigned old=row[j];row[j]=std::min({row[j]+1,row[j-1]+1,diagonal+(a[i-1]!=b[j-1])});diagonal=old;}}
    return int(100*(1.0-double(row.back())/large));
}
inline std::string SearchTitle(const std::string& name,const std::string& publisher,const std::string& developer){
    std::string lower=name;for(auto&c:lower)if(c>='A'&&c<='Z')c+=32;
    const auto at=lower.rfind(" by ");if(at==lower.npos)return name;
    const auto suffix=Compact(name.substr(at+4));
    for(const auto& company:{publisher,developer}){const auto key=Compact(company);if(!key.empty()&&(suffix==key||suffix==key+"llc"||suffix==key+"inc"||suffix==key+"ltd"))return name.substr(0,at);}
    return name;
}
}
