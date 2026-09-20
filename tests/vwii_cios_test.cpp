#include "ios_common.h"
#include "d2x_patcher.h"
#include "StateUtils.h"
#include "EndianUtils.h"
#include "tinyxml2.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
std::string directory;int failure=0,queued=0;
std::vector<uint8_t> HexBytes(const char *s){std::vector<uint8_t> out;while(s&&*s){char *end=nullptr;out.push_back(strtoul(s,&end,16));s=*end==','?end+1:end;}return out;}
MemIOS::~MemIOS(){free(ticket);free(tmd);if(contents){for(unsigned i=0;i<numContents;++i)free(contents[i].data);free(contents);}}
void Patcher_Log(const std::string &){}
bool State::AppRunning(){return true;}
extern "C" void sha(const uint8_t *data,unsigned n,uint8_t *hash){memset(hash,0,20);for(unsigned i=0;i<n;++i)hash[i%20]^=data[i];}
bool ReadFileToBuffer(const std::string &path,uint8_t **data,uint32_t *size){
    if(failure==2&&path.ends_with(".app"))return false;
    std::ifstream f(path,std::ios::binary);if(!f)return false;std::string bytes((std::istreambuf_iterator<char>(f)),{});
    if(failure==4&&path.ends_with(".xml")){auto p=bytes.find("tmdmoduleid=\"3\"");assert(p!=std::string::npos);bytes.replace(p,15,"tmdmoduleid=\"99\"");}
    *data=static_cast<uint8_t*>(malloc(bytes.size()));memcpy(*data,bytes.data(),bytes.size());*size=bytes.size();return true;
}
std::unique_ptr<MemIOS> ReadBaseIOS(uint32_t base){
    tinyxml2::XMLDocument doc;assert(doc.LoadFile((directory+"/ciosmaps.xml").c_str())==tinyxml2::XML_SUCCESS);
    auto *el=doc.RootElement()->FirstChildElement("ciosgroup")->FirstChildElement("base");while(el&&el->UnsignedAttribute("ios")!=base)el=el->NextSiblingElement("base");assert(el);
    auto ios=std::make_unique<MemIOS>();ios->maxContents=64;ios->tmdSize=sizeof(TitleTmd);ios->tmd=static_cast<TitleTmd*>(calloc(1,sizeof(TitleTmd)+64*sizeof(TitleContentRecord)));
    ios->tmd->titleVersion=ToBE16(el->UnsignedAttribute("version")+(failure==3));ios->contents=static_cast<MemContent*>(calloc(64,sizeof(MemContent)));
    for(auto *c=el->FirstChildElement("content");c;c=c->NextSiblingElement("content")){
        if(c->Attribute("module"))continue;auto &content=ios->contents[ios->numContents];content.cid=c->UnsignedAttribute("id");
        std::vector<uint8_t> data(64);for(auto *p=c->FirstChildElement("patch");p;p=p->NextSiblingElement("patch")){auto orig=HexBytes(p->Attribute("originalbytes"));auto offset=p->UnsignedAttribute("offset");data.resize(std::max(data.size(),offset+orig.size()));std::copy(orig.begin(),orig.end(),data.begin()+offset);}
        if(failure==1&&ios->numContents==0)data[0]^=1;
        content.size=data.size();content.data=static_cast<uint8_t*>(malloc(data.size()));memcpy(content.data,data.data(),data.size());
        auto &record=ios->tmd->contents[ios->numContents];record.contentId=ToBE32(content.cid);record.index=ToBE16(ios->numContents);record.size=ToBE64(content.size);record.type=ToBE16(1);
        ++ios->numContents;ios->tmdSize+=sizeof(TitleContentRecord);
    }ios->tmd->numContents=ToBE16(ios->numContents);return ios;
}
bool WritePatchedIOS(uint32_t slot,MemIOS &ios){assert(slot>=248&&slot<=251);assert(ios.numContents>=20&&FromBE16(ios.tmd->numContents)==ios.numContents);assert(ios.contents[0].data[0]==0x1e);++queued;return true;}
int main(int argc,char **argv){assert(argc==2);directory=argv[1];
    for(failure=0;failure<5;++failure){queued=0;std::vector<std::pair<int,bool>> result;PrepareD2XBatch(directory+"/d2x-v11-beta3-vWii",result);
        if(!failure){assert(queued==4&&result.size()==4&&std::all_of(result.begin(),result.end(),[](auto p){return p.second;}));}
        else assert(queued==0&&!result.empty()&&!result.back().second);
    }
    std::cout<<"PASS: actual vWii ciosmaps/modules, four slot preparations, mismatch/revision/missing-module/bounds rejection before queueing\n";
}
