#pragma once
#include "../vendor/picojson/picojson.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "skin_shelf.hpp"
namespace SkinLayout {
inline void Need(bool b,const char *message){if(!b)throw std::runtime_error(message);}
inline picojson::object Object(const std::string &text){Need(text.size()<=256*1024,"Skin document too large");int depth=0;bool quoted=false,escaped=false;for(char c:text){if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c=='{'||c=='[')Need(++depth<=16,"Skin JSON too deeply nested");else if(c=='}'||c==']')Need(--depth>=0,"Unbalanced skin JSON");}Need(!quoted&&depth==0,"Unbalanced skin JSON");picojson::value v;auto e=picojson::parse(v,text);Need(e.empty()&&v.is<picojson::object>(),"Invalid skin JSON");return v.get<picojson::object>();}
inline std::string Text(const picojson::object&o,const char*k,std::string fallback={}){auto i=o.find(k);if(i==o.end())return fallback;Need(i->second.is<std::string>(),"Invalid skin text");auto s=i->second.get<std::string>();Need(s.size()<=2048&&s.find('\0')==s.npos,"Skin text too long");return s;}
inline int Number(const picojson::object&o,const char*k,int fallback,int lo,int hi){auto i=o.find(k);if(i==o.end())return fallback;Need(i->second.is<double>(),"Invalid skin number");double n=i->second.get<double>();Need(std::isfinite(n)&&n>=lo&&n<=hi&&std::floor(n)==n,"Skin number outside limits");return int(n);}
inline bool Relative(const std::string&s){if(s.empty()||s.size()>180||s.front()=='/'||s.find_first_of("\\:\r\n\t")!=s.npos||s.find('\0')!=s.npos)return false;size_t at=0;do{auto end=s.find('/',at);auto part=s.substr(at,end==s.npos?s.npos:end-at);if(part.empty()||part=="."||part=="..")return false;if(end==s.npos)break;at=end+1;}while(true);return true;}
struct Node {std::string kind,source,text,action,color="ink",mode="grid",fit="contain",style;int x=0,y=0,w=100,h=100,scale=2,columns=5,rows=1,gap=16,radius=0,opacity=255;std::vector<int> shelfAngles{80,80,12,12,12,12,12},shelfBaselines,shelfSlopes;int shelfHeight=210,selectedAngle=12,selectedScale=120,pullout=14,animationMs=220;};
struct Variant {int type=0,layout=-1;std::vector<Node> nodes;};
struct Document {
 std::vector<Node> nodes;std::vector<Variant> variants;int particles=0,trails=0,schema=1;
 bool Enhanced() const {return schema>=2||!variants.empty()||particles||trails;}
 const std::vector<Node>& Nodes(int type,int layout) const {
  for(const auto&v:variants)if(v.type==type&&v.layout==layout)return v.nodes;
  for(const auto&v:variants)if(v.type==type&&v.layout==-1)return v.nodes;
  return nodes;
 }
};
inline Document Parse(const std::string &text){auto o=Object(text);const int schema=Number(o,"schema",0,1,2);auto it=o.find("nodes");Need(it!=o.end()&&it->second.is<picojson::array>(),"Missing layout nodes");auto &nodes=it->second.get<picojson::array>();Need(!nodes.empty()&&nodes.size()<=64,"Invalid layout node count");Document d;d.schema=schema;int libraries=0;
 for(const auto&value:nodes){Need(value.is<picojson::object>(),"Invalid layout node");const auto &v=value.get<picojson::object>();Node n;n.kind=Text(v,"kind");Need(std::set<std::string>{"panel","text","image","model","library","button","video","effects","shade"}.contains(n.kind),"Unknown layout node");n.source=Text(v,"source");n.text=Text(v,"text");n.action=Text(v,"action");n.color=Text(v,"color","ink");Need(std::set<std::string>{"background","ink","muted","accent","panel"}.contains(n.color),"Unknown layout color");n.x=Number(v,"x",0,0,1279);n.y=Number(v,"y",0,0,719);n.w=Number(v,"width",100,1,1280);n.h=Number(v,"height",100,1,720);Need(n.x+n.w<=1280&&n.y+n.h<=720,"Layout outside screen");n.scale=Number(v,"scale",2,1,5);n.columns=Number(v,"columns",5,1,30);n.rows=Number(v,"rows",1,1,5);n.gap=Number(v,"gap",16,0,64);n.radius=Number(v,"radius",0,0,32);
 n.opacity=Number(v,"opacity",255,0,255);n.mode=Text(v,"mode","grid");n.fit=Text(v,"fit","contain");n.style=Text(v,"style");
 Need(std::set<std::string>{"grid","row","carousel","shelf"}.contains(n.mode),"Unknown library mode");
 Need(n.mode=="shelf"?n.columns*n.rows<=60:n.columns<=12,"Too many library cells");
 Need(n.fit=="contain"||n.fit=="cover","Unknown image fit");
 Need(n.style.empty()||n.style=="text"||n.style=="primary"||n.style=="outline"||n.style=="logo-title"||n.style=="profile","Unknown scene style");
 if(n.kind=="library"&&(n.mode=="row"||n.mode=="carousel"))Need(n.rows==1,"Rows/carousels require one row");
 if(n.mode=="shelf"){
   Need(schema>=2&&n.kind=="library","Shelf requires layout schema 2 and a library node");
   Need(n.source=="box","Shelf currently requires box models");
   n.shelfHeight=Number(v,"case-height",210,48,480);
   n.selectedAngle=Number(v,"selected-angle",12,-180,180);
   n.selectedScale=Number(v,"selected-scale",120,100,150);
   n.pullout=Number(v,"pullout",14,0,40);
   n.animationMs=Number(v,"animation-ms",220,80,600);
   if(auto angles=v.find("angles");angles!=v.end()){
     Need(angles->second.is<picojson::array>(),"Shelf angles must be an array");
     const auto &a=angles->second.get<picojson::array>();Need(!a.empty()&&a.size()<=12,"Invalid shelf angle count");n.shelfAngles.clear();
     for(const auto &value:a){Need(value.is<double>(),"Invalid shelf angle");const double angle=value.get<double>();Need(std::isfinite(angle)&&angle>=-180&&angle<=180&&std::floor(angle)==angle,"Invalid shelf angle");n.shelfAngles.push_back(int(angle));}
   }
   for(const auto &key:{"shelf-baselines","shelf-slopes"}){
     if(auto field=v.find(key);field!=v.end()){
       Need(field->second.is<picojson::array>(),"Shelf row values must be arrays");const auto &a=field->second.get<picojson::array>();
       Need(a.size()==size_t(n.rows),"Shelf row values must match rows");
       auto &values=std::string(key)=="shelf-baselines"?n.shelfBaselines:n.shelfSlopes;
       for(const auto &value:a){Need(value.is<double>(),"Invalid shelf row value");const double v=value.get<double>();
         const bool baseline=std::string(key)=="shelf-baselines";
         Need(std::isfinite(v)&&std::floor(v)==v&&v>=(baseline?0:-80)&&v<=(baseline?n.h:80),"Shelf row value outside bounds");values.push_back(int(v));}
     }
   }
   for(int row=0;row<n.rows;++row){
     const int baseline=n.shelfBaselines.empty()?(row+1)*n.h/n.rows-n.pullout:n.shelfBaselines[row];
     const int slope=n.shelfSlopes.empty()?0:n.shelfSlopes[row];
     Need(baseline+std::min(0,slope)>=n.shelfHeight*n.selectedScale/100-n.pullout&&baseline+std::max(0,slope)+n.pullout<=n.h,"Shelf model exceeds viewport");
   }
   Need(n.shelfHeight*n.selectedScale/100+n.pullout<=n.h/n.rows,"Selected case exceeds shelf row");
 }
 if(n.kind=="library"){Need(++libraries==1,"Only one library node is supported");Need(n.w/n.columns>n.gap&&n.h/n.rows>n.gap,"Library cells are too small");}
 if(n.kind=="button")Need(std::set<std::string>{"launch","details","settings","search","next-library","previous-library","next-game","previous-game","effects","favorites","all","sort","favorite"}.contains(n.action),"Unsupported skin action");
 if(n.kind=="image"&&!n.source.empty()&&n.source!="background"&&n.source!="cover"&&n.source!="logo")Need(Relative(n.source),"Unsafe skin image path");
 if(n.kind=="model"||n.kind=="library")Need(n.source.empty()||std::set<std::string>{"box","media","logo","thumbnail","cover","background"}.contains(n.source),"Unknown artwork presentation");
 d.nodes.push_back(n);}
 Need(libraries==1,"Layout needs one library node");
 Need(std::count_if(d.nodes.begin(),d.nodes.end(),[](const auto&n){return n.kind=="effects";})<=1,"Only one ambient effects layer allowed");
 d.particles=Number(o,"particles",0,0,192);d.trails=Number(o,"trails",0,0,3);
 auto variants=o.find("variants");
 if(variants!=o.end()) {
   Need(variants->second.is<picojson::array>(),"Invalid layout variants");
   const auto &list=variants->second.get<picojson::array>();Need(list.size()<=8,"Too many layout variants");
   std::set<std::pair<int,int>> selectors;
   for(const auto&entry:list) {
     Need(entry.is<picojson::object>(),"Invalid layout variant");const auto&v=entry.get<picojson::object>();
     Need(!v.contains("variants"),"Nested variants forbidden");
     Variant variant;variant.type=Number(v,"type",0,0,3);variant.layout=Number(v,"layout",-1,-1,9);
     Need(selectors.insert({variant.type,variant.layout}).second,"Duplicate variant selector");
     auto nested=v;nested["schema"]=picojson::value(double(schema));
     variant.nodes=Parse(picojson::value(nested).serialize()).nodes;
     d.variants.push_back(std::move(variant));
   }
 }
 return d;}
inline bool Read(const std::string&path,Document&d){try{std::ifstream f(path,std::ios::binary);if(!f)return false;std::string s;char buf[4096];while(f.read(buf,sizeof(buf))||f.gcount()){s.append(buf,f.gcount());if(s.size()>256*1024)return false;}d=Parse(s);return true;}catch(...){return false;}}
}
