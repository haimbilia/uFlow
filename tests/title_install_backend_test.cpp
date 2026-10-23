#define TITLE_INSTALL_BACKEND_TEST
#include "title_install_test.cpp"
#include <coreinit/mcp.h>
#include <coreinit/ios.h>
#include <sys/statvfs.h>
#include <thread>
#include <chrono>
#include <atomic>
std::vector<MCPTitleListType> installed;
std::vector<uint64_t> submitted;
IOSAsyncCallbackFn completion=nullptr;void *context=nullptr;
int target=0,polls=0,completionError=0;uint64_t freeBytes=1ULL<<32;Package pending;
int storageInit=0,storageOpen=11,storageUnlock=0,storageQuery=0,storageClients=0;std::string storagePath;
int Mocha_InitLibrary(){return storageInit;}
int FSAAddClient(void*){if(storageOpen>=0)++storageClients;return storageOpen;}
int FSADelClient(int fd){assert(fd==11);--storageClients;return 0;}
int Mocha_UnlockFSClientEx(int fd){assert(fd==11);return storageUnlock;}
int FSAGetFreeSpaceSize(int fd,const char *path,uint64_t *bytes){assert(fd==11);storagePath=path;*bytes=freeBytes;return storageQuery;}
int MCP_Open(){return 7;}int MCP_Close(int){return 0;}
int MCP_TitleCount(int){return installed.size();}
int MCP_TitleList(int,uint32_t *count,MCPTitleListType *out,uint32_t){std::copy(installed.begin(),installed.end(),out);*count=installed.size();return 0;}
int MCP_InstallSetTargetDevice(int,MCPInstallTarget t){target=t;return 0;}int MCP_InstallSetTargetUsb(int,int){return 0;}
int MCP_InstallGetInfo(int,const char *path,MCPInstallInfo *info){
    std::string p=path;assert(p.starts_with("/vol/app_sd/"));p="fs:/vol/external01/"+p.substr(12);
    assert(ReadPackage(p,pending));for(int i=0;i<8;++i)info->data[i]=pending.titleId>>(56-i*8);return 0;
}
int IOS_IoctlvAsync(int,uint32_t command,uint32_t in,uint32_t out,IOSVec *v,IOSAsyncCallbackFn cb,void *arg){
    assert(command==0x81&&in==1&&out==0&&v->len==0x27f);assert(std::string((char*)v->vaddr).starts_with("/vol/app_sd/"));
    submitted.push_back(pending.titleId);completion=cb;context=arg;polls=0;return 0;
}
int MCP_InstallGetProgress(int,MCPInstallProgress *p){
    *p={1,pending.titleId,pending.bytes,pending.bytes/2};
    if(++polls==3){if(!completionError){MCPTitleListType t{pending.titleId,pending.version,{}};std::snprintf(t.indexedDevice,sizeof(t.indexedDevice),"%s",target?"usb01":"mlc01");installed.push_back(t);}completion(completionError,context);}
    return 0;
}
Snapshot wait(Job &job){for(int i=0;i<100;++i){auto s=job.Poll();if(!s.busy)return s;std::this_thread::sleep_for(std::chrono::milliseconds(20));}assert(false);return {};}
int main(){
    uint64_t space=0;std::string error;
    assert(FreeSpace(0,space,error)&&space==freeBytes&&storagePath=="/vol/storage_mlc01/usr"&&storageClients==0);
    assert(FreeSpace(1,space,error)&&storagePath=="/vol/storage_usb01/usr"&&storageClients==0);
    storageQuery=-5;assert(!FreeSpace(0,space,error)&&space==0&&storageClients==0&&error.find("FFFFFFFB")!=std::string::npos);storageQuery=0;
    storageUnlock=-6;assert(!FreeSpace(1,space,error)&&storageClients==0);storageUnlock=0;
    storageOpen=-7;assert(!FreeSpace(0,space,error)&&storageClients==0);storageOpen=11;
    storageInit=-8;assert(!FreeSpace(0,space,error)&&storageClients==0);storageInit=0;
    assert(!FreeSpace(2,space,error)&&storageClients==0);
    std::filesystem::create_directories(std::filesystem::path(ModulePath).parent_path());std::ofstream(ModulePath)<<"uflow-title-installer-v1\n";
    assert(Available());std::filesystem::remove(ModulePath);assert(Available());
    // Module changes take effect on next app launch, not during rendering.
    std::string root="fs:/vol/external01/install";package(root+"/Game",0x0005000012345600ULL,0);package(root+"/Update",0x0005000e12345600ULL,16);package(root+"/DLC",0x0005000c12345600ULL,1);
    auto ps=Scan(root,3)[0].packages;Job job;
    installed.push_back({ps[0].titleId,ps[0].version,{'o','d','d',0}});Refresh(ps);assert(!ps[0].Installed(0)&&!ps[0].Installed(1));installed.clear();
    auto reverse=ps;std::reverse(reverse.begin(),reverse.end());assert(job.Start(reverse,1));assert(job.Poll().busy);assert(!job.Start(ps,0));
    auto s=wait(job);assert(s.complete&&!s.busy&&s.done==192&&submitted.size()==3);assert(s.elapsedMs>=400&&s.progressIdleMs<=s.elapsedMs);assert(submitted[0]==ps[0].titleId&&submitted[1]==ps[1].titleId);
    Refresh(ps);for(auto &p:ps)assert(p.Installed(1)&&!p.Installed(0));assert(!job.Start(ps,1));
    completionError=-42;assert(job.Start(ps,0));s=wait(job);assert(!s.complete&&!s.busy&&s.message.find("FFFFFFD6")!=std::string::npos);
    Refresh(ps);for(auto &p:ps)assert(!p.Installed(0));
    completionError=-265150;assert(job.Start(ps,0));s=wait(job);assert(!s.complete&&s.message.find("Ticket signature rejected. Not installed")!=std::string::npos);
    completionError=0;auto n=submitted.size();storageQuery=-5;assert(job.Start(ps,0));s=wait(job);assert(!s.complete&&submitted.size()==n&&storageClients==0);storageQuery=0;freeBytes=0;assert(job.Start(ps,0));s=wait(job);assert(!s.complete&&submitted.size()==n&&s.message.find("Not enough")!=std::string::npos);
    freeBytes=1ULL<<32;auto duplicate=ps;duplicate.push_back(ps[0]);assert(!job.Start(duplicate,0));
    std::filesystem::remove(root+"/Game/00000001.app");assert(job.Start(ps,0));s=wait(job);assert(!s.complete&&submitted.size()==n);
    std::cout<<"PASS: real installer worker, callback completion, queue ordering, destination/version verification, failure, free space, duplicates and changed package rejection\n";
}
