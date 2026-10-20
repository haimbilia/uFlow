#pragma once
#include <algorithm>
#include <array>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
namespace Skin {
struct Definition {
    std::string id,name,root,backgroundImage;
    int engine=2,avatarX=60,avatarY=38,avatarSize=54,dockX=350,dockY=546,dockGap=116;
    int rowX=100,rowY=322,tileSize=256,gridColumns=9,gridRows=3,gridTop=180,gridHeight=320;
    bool gridFullscreen=true;
    std::array<unsigned char,4> background{237,237,237,255},ink{45,45,45,255},muted{196,196,196,255},accent{0,166,179,255};
    std::array<std::string,6> icons;
};
inline bool SafeAsset(const std::string &s){return !s.empty()&&s.size()<180&&s.find("..")==std::string::npos&&s.find_first_of("\\:\r\n")==std::string::npos&&s[0]!='/';}
inline bool Read(const std::string &root,const std::string &id,Definition &d) {
    std::ifstream f(root+"/skin.ini");if(!f)return false;
    d={};d.id=id;d.name=id;d.root=root;bool version=false;
    std::string line;
    while(std::getline(f,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto eq=line.find('=');if(eq==std::string::npos||line[0]=='#')continue;
        const auto k=line.substr(0,eq),v=line.substr(eq+1);
        try {
            if(k=="version")version=v=="1";
            else if(k=="name"&&v.size()<=60)d.name=v;
            else if(k=="engine")d.engine=v=="pixel"?0:v=="glass"?1:v=="switch-dark"?3:2;
            else if(k=="background_image"&&SafeAsset(v))d.backgroundImage=root+"/"+v;
            else if(k.rfind("icon_",0)==0){int i=std::stoi(k.substr(5));if(i>=0&&i<6&&SafeAsset(v))d.icons[i]=root+"/"+v;}
            else if(k=="background"||k=="ink"||k=="muted"||k=="accent") {
                if(v.size()!=6||v.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)continue;
                const auto n=std::stoul(v,nullptr,16);auto &c=k=="background"?d.background:k=="ink"?d.ink:k=="muted"?d.muted:d.accent;
                c={static_cast<unsigned char>(n>>16),static_cast<unsigned char>(n>>8),static_cast<unsigned char>(n),255};
            } else if(k=="avatar_x")d.avatarX=std::clamp(std::stoi(v),16,1200);
            else if(k=="avatar_y")d.avatarY=std::clamp(std::stoi(v),12,630);
            else if(k=="avatar_size")d.avatarSize=std::clamp(std::stoi(v),32,80);
            else if(k=="dock_x")d.dockX=std::clamp(std::stoi(v),60,600);
            else if(k=="dock_y")d.dockY=std::clamp(std::stoi(v),80,610);
            else if(k=="row_x")d.rowX=std::clamp(std::stoi(v),16,900);
            else if(k=="row_y")d.rowY=std::clamp(std::stoi(v),180,450);
            else if(k=="tile_size")d.tileSize=std::clamp(std::stoi(v),100,340);
            else if(k=="grid_fullscreen")d.gridFullscreen=std::stoi(v)!=0;
            else if(k=="grid_columns")d.gridColumns=std::clamp(std::stoi(v),2,12);
            else if(k=="grid_rows")d.gridRows=std::clamp(std::stoi(v),1,4);
            else if(k=="grid_top")d.gridTop=std::clamp(std::stoi(v),100,250);
            else if(k=="grid_height")d.gridHeight=std::clamp(std::stoi(v),200,480);
            else if(k=="dock_gap")d.dockGap=std::clamp(std::stoi(v),70,120);
        }catch(...){}
    }
    return version;
}
inline std::vector<Definition> Discover(const std::string &bundled,const std::string &user) {
    std::vector<Definition> list;
    for(const char *id:{"pixel","glass","switch-light","switch-dark"}){Definition d;if(Read(bundled+"/"+id,id,d))list.push_back(d);}
    if(DIR *dir=opendir(user.c_str())) {
        std::vector<std::string> names;while(const auto *e=readdir(dir))if(e->d_name[0]!='.')names.push_back(e->d_name);closedir(dir);
        std::sort(names.begin(),names.end());
        for(const auto &id:names){Definition d;if(!Read(user+"/"+id,id,d))continue;
            auto old=std::find_if(list.begin(),list.end(),[&](const auto &s){return s.id==id;});if(old==list.end())list.push_back(d);else *old=d;}
    }
    if(list.empty()){Definition d;d.id="fallback";d.name="Default";list.push_back(d);}
    return list;
}
}
