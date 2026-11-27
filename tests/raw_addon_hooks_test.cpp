#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <iostream>
struct {bool active=false;const char *dlcPath="";const char *dlcVolume="/vol/aoc0005000c101c9400";uint16_t dlcVersion=80,dlcContentCount=3,dlcContentIds[256]={0x10,0x11,0x12};uint64_t baseTitleId=0x00050000101c9400ULL;} gLooseTitleInfo;
struct RawAOCTitle;
int realCalls=0;
int real_Raw_ACPGetAddOnUniqueId(unsigned *,int){++realCalls;return -17;}
int real_Raw_AOC_OpenTitle(char *,RawAOCTitle *,void *,unsigned){++realCalls;return -18;}
int real_Raw_AOC_ListTitle(uint32_t *,RawAOCTitle *,uint32_t,void *,uint32_t){++realCalls;return -19;}
int real_Raw_AOC_GetPurchaseInfo(uint32_t *,uint64_t,uint16_t *,uint32_t,void *,uint32_t){++realCalls;return -20;}
int real_Raw_AOC_CloseTitle(RawAOCTitle *){++realCalls;return -21;}
#define DECL_FUNCTION(ret,name,...) ret name(__VA_ARGS__)
// PRODUCTION_AOC_HOOKS
using CRLayerHandle=int;
using RPXLoaderStatus=int;
constexpr int RPX_LOADER_RESULT_SUCCESS=0, RPX_LOADER_RESULT_UNKNOWN_ERROR=-1, CONTENT_REDIRECTION_RESULT_SUCCESS=0;
CRLayerHandle contentLayerHandle=1,codeLayerHandle=2,metaLayerHandle=3,saveLayerHandle=4,updateLayerHandle=5,dlcLayerHandle=6;
int fail=0;
std::vector<std::pair<int,bool>> actions;
int ContentRedirection_SetActive(int handle,bool active){actions.emplace_back(handle,active);return handle==fail?-1:0;}
// PRODUCTION_LAYER_TOGGLE
int main(){
    unsigned id=99;char path[64]="";
    assert(Raw_ACPGetAddOnUniqueId(&id,1)==-17 && id==99);
    assert(Raw_AOC_OpenTitle(path,nullptr,nullptr,0)==-18);
    gLooseTitleInfo.active=true;
    assert(Raw_ACPGetAddOnUniqueId(&id,1)==-17);
    gLooseTitleInfo.dlcPath="game/aoc0005000c101c9400";
    assert(Raw_ACPGetAddOnUniqueId(&id,1)==0 && id==0x1c94);
    assert(Raw_ACPGetAddOnUniqueId(nullptr,0)==-17);
    // Reproduce BOTW's sequence: enumerate, check 0010/0011/0012, then open.
    struct {RawAOCTitle title;uint64_t guard=0xfeedface;} result{};
    uint32_t count=99;
    assert(Raw_AOC_ListTitle(&count,&result.title,1,nullptr,0)==0 && count==1);
    assert(result.guard==0xfeedface && result.title.titleId==0x0005000c101c9400ULL);
    assert(result.title.groupId==0x1c94 && result.title.version==80);
    uint16_t ids[]={0x10,0x11,0x12,0x13}; uint32_t owned[]={99,99,99,99};
    assert(Raw_AOC_GetPurchaseInfo(owned,result.title.titleId,ids,4,nullptr,0)==0);
    assert(owned[0]==1 && owned[1]==1 && owned[2]==1 && owned[3]==0);
    gLooseTitleInfo.dlcContentCount=1;
    assert(Raw_AOC_GetPurchaseInfo(owned,result.title.titleId,ids,4,nullptr,0)==0);
    assert(owned[0]==1 && owned[1]==0 && owned[2]==0);
    assert(Raw_AOC_OpenTitle(path,&result.title,nullptr,0)==0 && std::string(path)==gLooseTitleInfo.dlcVolume);
    assert(Raw_AOC_CloseTitle(&result.title)==0);
    assert(Raw_AOC_ListTitle(&count,nullptr,0,nullptr,0)==0 && count==0);
    assert(Raw_AOC_ListTitle(nullptr,&result.title,1,nullptr,0)==-19);
    assert(Raw_AOC_GetPurchaseInfo(nullptr,result.title.titleId,ids,4,nullptr,0)==-20);
    ++result.title.titleId;
    assert(Raw_AOC_OpenTitle(path,&result.title,nullptr,0)==-18);
    assert(Raw_AOC_CloseTitle(&result.title)==-21);
    assert(Raw_AOC_GetPurchaseInfo(owned,result.title.titleId,ids,4,nullptr,0)==-20);
    gLooseTitleInfo.active=false;
    assert(Raw_AOC_ListTitle(&count,&result.title,1,nullptr,0)==-19);
    gLooseTitleInfo.active=false;assert(Raw_AOC_OpenTitle(path,nullptr,nullptr,0)==-18);
    assert(SetRawLayersActive(false)==0 && actions.size()==6 && actions.back()==std::make_pair(6,false));
    actions.clear();fail=6;
    assert(SetRawLayersActive(true)==-1 && actions.size()==11);
    for(int i=0;i<5;++i)assert(actions[i+6]==std::make_pair(i+1,false));
    std::cout<<"actual add-on hooks: inactive passthrough, DLC identity/open, layer lifecycle and rollback passed\n";
}
