// Read-only FAT USB access. Never writes sectors or changes system mounts.
#include "usb_storage.hpp"
#include <coreinit/filesystem_fsa.h>
#include <mocha/mocha.h>
#include <mocha/fsa.h>
#include <sys/iosupport.h>
#include <fcntl.h>
#include <mutex>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <string>
#include <limits>
#include <limits.h>
#include <cctype>
#ifndef UFLOW_USB_LOADER
#include "usb_read_cache.hpp"
#endif
#ifdef UFLOW_USB_LOADER
#include <content_redirection/redirection.h>
#include <coreinit/ios.h>
#include <vector>
#include "../../payload/source/common/raw_usb_map.h"
#include "hai_companion.hpp"
#define USB_NAME "ufusb"
#else
#define USB_NAME "usb1"
#endif
#define DIR FATDIR
#include "../vendor/fatfs/ff.h"
#undef DIR
#include "../vendor/fatfs/diskio.h"
namespace {
FSAClientHandle client=-1;
int32_t handle=-1;
FATFS volume{};
devoptab_t device{};
std::mutex mutex;
bool registered=false;
unsigned rawDevice=0;
alignas(64) unsigned char bounce[64*1024];
#ifndef UFLOW_USB_LOADER
USBReadCache<> readCache;
#endif
void Log(const char *phase,int code){
    if(FILE *f=fopen("fs:/vol/external01/wiiu/apps/uFlow/usb-storage.log","a")){
        fprintf(f,"%s=%d\n",phase,code);fclose(f);
    }
}
int Fail(_reent *r,FRESULT result){
    r->_errno=result==FR_NO_FILE||result==FR_NO_PATH?ENOENT:result==FR_INVALID_NAME?EINVAL:result==FR_DENIED?EACCES:EIO;return -1;
}
bool Path(const char *path,std::string &out){
    constexpr size_t prefix=sizeof(USB_NAME ":/")-1;
    if(!path||strncmp(path,USB_NAME ":/",prefix)!=0)return false;
    out="0:/";out+=path+prefix;return out.find(':',2)==std::string::npos;
}
void Stat(struct stat *s,FSIZE_t size,bool dir){
    memset(s,0,sizeof(*s));s->st_mode=(dir?S_IFDIR|0555:S_IFREG|0444);s->st_nlink=1;s->st_size=size;s->st_blksize=512;s->st_blocks=(size+511)/512;
}
int Open(_reent *r,void *state,const char *path,int flags,int){
    std::lock_guard lock(mutex);
    if((flags&O_ACCMODE)!=O_RDONLY||(flags&(O_CREAT|O_TRUNC|O_APPEND))){r->_errno=EROFS;return -1;}
    std::string p;if(!Path(path,p)){r->_errno=EINVAL;return -1;}
    memset(state,0,sizeof(FIL));const auto result=f_open(static_cast<FIL*>(state),p.c_str(),FA_READ);
    return result==FR_OK?0:Fail(r,result);
}
int Close(_reent *r,void *state){std::lock_guard lock(mutex);auto v=f_close(static_cast<FIL*>(state));return v==FR_OK?0:Fail(r,v);}
ssize_t Read(_reent *r,void *state,char *data,size_t length){
    std::lock_guard lock(mutex);UINT got=0;auto v=f_read(static_cast<FIL*>(state),data,std::min<size_t>(length,INT32_MAX),&got);return v==FR_OK?static_cast<ssize_t>(got):Fail(r,v);
}
off_t Seek(_reent *r,void *state,off_t offset,int whence){
    std::lock_guard lock(mutex);auto *f=static_cast<FIL*>(state);
    if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END){r->_errno=EINVAL;return -1;}
    int64_t pos=(whence==SEEK_CUR?int64_t(f_tell(f)):whence==SEEK_END?int64_t(f_size(f)):0)+offset;
    if(pos<0||uint64_t(pos)>std::numeric_limits<FSIZE_t>::max()){r->_errno=EINVAL;return -1;}
    auto v=f_lseek(f,FSIZE_t(pos));return v==FR_OK?off_t(f_tell(f)):Fail(r,v);
}
int FileStat(_reent*,void *state,struct stat *s){std::lock_guard lock(mutex);Stat(s,f_size(static_cast<FIL*>(state)),false);return 0;}
int PathStat(_reent *r,const char *path,struct stat *s){
    std::lock_guard lock(mutex);std::string p;if(!Path(path,p)){r->_errno=EINVAL;return -1;}
    if(p=="0:/"&&registered){Stat(s,0,true);return 0;}
    FILINFO info{};auto v=f_stat(p.c_str(),&info);if(v!=FR_OK)return Fail(r,v);Stat(s,info.fsize,info.fattrib&AM_DIR);return 0;
}
DIR_ITER *DirOpen(_reent *r,DIR_ITER *it,const char *path){
    std::lock_guard lock(mutex);std::string p;if(!Path(path,p)){r->_errno=EINVAL;return nullptr;}
    memset(it->dirStruct,0,sizeof(FATDIR));auto v=f_opendir(static_cast<FATDIR*>(it->dirStruct),p.c_str());if(v!=FR_OK){Fail(r,v);return nullptr;}return it;
}
int DirNext(_reent *r,DIR_ITER *it,char *name,struct stat *s){
    std::lock_guard lock(mutex);FILINFO info{};auto v=f_readdir(static_cast<FATDIR*>(it->dirStruct),&info);
    if(v!=FR_OK)return Fail(r,v);
    if(!info.fname[0]){r->_errno=ENOENT;return -1;}
    if(strlen(info.fname)>NAME_MAX){r->_errno=ENAMETOOLONG;return -1;}
    strcpy(name,info.fname);Stat(s,info.fsize,info.fattrib&AM_DIR);return 0;
}
int DirReset(_reent *r,DIR_ITER *it){std::lock_guard lock(mutex);auto v=f_readdir(static_cast<FATDIR*>(it->dirStruct),nullptr);return v==FR_OK?0:Fail(r,v);}
int DirClose(_reent *r,DIR_ITER *it){std::lock_guard lock(mutex);auto v=f_closedir(static_cast<FATDIR*>(it->dirStruct));return v==FR_OK?0:Fail(r,v);}
void CloseRaw(){
#ifndef UFLOW_USB_LOADER
    readCache.Reset();
#endif
    if(handle>=0){FSAEx_RawCloseEx(client,handle);handle=-1;}if(client>=0){FSADelClient(client);client=-1;}
}
}
extern "C" DSTATUS disk_status(BYTE n){return n==0&&handle>=0?0:STA_NOINIT;}
extern "C" DSTATUS disk_initialize(BYTE n){return disk_status(n);}
extern "C" DRESULT disk_read(BYTE n,BYTE *data,LBA_t sector,UINT count){
    if(n||handle<0||!count)return RES_PARERR;
#ifndef UFLOW_USB_LOADER
    return readCache.ReadSectors(data,sector,count,[](unsigned char *target,std::uint32_t start,std::uint32_t size){
        const auto result=FSAEx_RawReadEx(client,bounce,512,size,start,handle);
        if(result!=FS_ERROR_OK){Log("read-error",result);return false;}
        if(target!=bounce)memcpy(target,bounce,size*512);
        return true;
    })?RES_OK:RES_ERROR;
#else
    while(count){const UINT batch=std::min<UINT>(count,sizeof(bounce)/512);
        auto result=FSAEx_RawReadEx(client,bounce,512,batch,sector,handle);
        if(result!=FS_ERROR_OK){Log("read-error",result);return RES_ERROR;}
        if(data!=bounce)memcpy(data,bounce,batch*512);
        data+=batch*512;sector+=batch;count-=batch;
    }return RES_OK;
#endif
}
extern "C" DRESULT disk_ioctl(BYTE,BYTE command,void*){return command==CTRL_SYNC?RES_OK:RES_PARERR;}
bool USBStorage::Mount(){
    std::lock_guard lock(mutex);if(registered)return true;
    Log("usb-readonly-build",2026092007);
    if(FindDevice(USB_NAME ":")>=0){Log("usb-name-in-use",1);return false;}
    auto init=Mocha_InitLibrary();Log("mocha-init",init);if(init!=MOCHA_RESULT_SUCCESS)return false;
    for(const char *raw:{"/dev/usb01","/dev/usb02"}){
        client=FSAAddClient(nullptr);if(client<0){Log("fsa-client",client);continue;}
        auto unlock=Mocha_UnlockFSClientEx(client);Log("unlock",unlock);
        if(unlock!=MOCHA_RESULT_SUCCESS){CloseRaw();continue;}
        auto result=FSAEx_RawOpenEx(client,raw,&handle);Log(raw,result);
        if(result!=FS_ERROR_OK){handle=-1;CloseRaw();continue;}
        auto mount=f_mount(&volume,"0:",1);Log("fat-mount",mount);
        if(mount!=FR_OK||volume.fs_type!=FS_FAT32){f_mount(nullptr,"0:",0);CloseRaw();continue;}
        rawDevice=raw[9]=='1'?1:2;
        device.name=USB_NAME;device.structSize=sizeof(FIL);device.open_r=Open;device.close_r=Close;
        device.read_r=Read;device.seek_r=Seek;device.fstat_r=FileStat;device.stat_r=PathStat;device.lstat_r=PathStat;
        device.dirStateSize=sizeof(FATDIR);device.diropen_r=DirOpen;device.dirnext_r=DirNext;device.dirreset_r=DirReset;device.dirclose_r=DirClose;
        const auto added=AddDevice(&device);Log("register-usb1",added);
        if(added>=0){registered=true;return true;}
        f_mount(nullptr,"0:",0);CloseRaw();
    }return false;
}
void USBStorage::Unmount(){std::lock_guard lock(mutex);if(registered){RemoveDevice(USB_NAME ":");registered=false;}f_mount(nullptr,"0:",0);CloseRaw();}

#ifndef UFLOW_USB_LOADER
void USBStorage::LogReadStats(const char *phase){
    std::lock_guard lock(mutex);const auto s=readCache.Statistics();
    if(FILE *file=fopen("fs:/vol/external01/wiiu/apps/uFlow/usb-storage.log","a")){
        fprintf(file,"cache-%s requested=%llu hits=%llu raw-calls=%llu raw-sectors=%llu errors=%llu\n",phase,
            static_cast<unsigned long long>(s.requested),static_cast<unsigned long long>(s.hits),
            static_cast<unsigned long long>(s.reads),static_cast<unsigned long long>(s.sectors),static_cast<unsigned long long>(s.errors));
        fclose(file);
    }
}
#endif

#ifdef UFLOW_USB_LOADER
namespace {
uint32_t expectedDevice, expectedBoot, expectedMbrHash, expectedBootHash;
bool AddCodeFile(RawUsbMap &map,const std::string &name) {
    if(map.fileCount==UFLOW_USB_FILES||name.size()>=256||!RawUsbSafePath(name.c_str()))return false;
    FIL file{};
    if(f_open(&file,("0:/"+name).c_str(),FA_READ)!=FR_OK)return false;
    std::vector<DWORD> clusters(UFLOW_USB_EXTENTS*2+2);
    clusters[0]=clusters.size();file.cltbl=clusters.data();
    auto result=f_lseek(&file,CREATE_LINKMAP);
    auto &entry=map.files[map.fileCount];
    strcpy(entry.path,name.c_str());entry.size=f_size(&file);entry.first=map.extentCount;
    if(result==FR_OK&&entry.size) {
        uint32_t remaining=(uint64_t(entry.size)+511)/512;
        for(size_t i=1;i+1<clusters.size()&&clusters[i]&&remaining;i+=2) {
            uint64_t sector=uint64_t(volume.database)+uint64_t(clusters[i+1]-2)*volume.csize;
            uint64_t count=std::min<uint64_t>(uint64_t(clusters[i])*volume.csize,remaining);
            if(clusters[i+1]<2||sector>UINT32_MAX||map.extentCount==UFLOW_USB_EXTENTS){result=FR_INT_ERR;break;}
            map.extents[map.extentCount++]={uint32_t(sector),uint32_t(count)};
            remaining-=count;
        }
        if(remaining)result=FR_INT_ERR;
    } else result=FR_INT_ERR;
    file.cltbl=nullptr;f_close(&file);
    entry.count=map.extentCount-entry.first;++map.fileCount;
    return result==FR_OK;
}
}
bool USBStorage::BuildHaiCompanion(const char *contentDirectory,std::vector<unsigned char> &companion) {
    companion.clear();
    if(!contentDirectory||strncmp(contentDirectory,"usb1:/",6)||!Mount())return false;
    std::lock_guard lock(mutex); /* this FatFs build and raw reads use 512-byte sectors */
    std::vector<HaiCompanion::File> files;
    uint64_t imageBytes=0,parts=1;
    for(unsigned index=0;index<parts;++index) {
        char name[32];snprintf(name,sizeof(name),"/hif_%06u.nfs",index);
        const std::string path=std::string(contentDirectory+6)+name;
        FILINFO info{};
        if(f_stat(("0:/"+path).c_str(),&info)!=FR_OK||(info.fattrib&AM_DIR)){Log("hai-missing-image",index);return false;}
        HaiCompanion::File file{};
        FIL handle{};
        if(f_open(&handle,("0:/"+path).c_str(),FA_READ)!=FR_OK){Log("hai-open",index);return false;}
        if(index==0) {
            unsigned char header[512];UINT got=0;
            if(f_read(&handle,header,sizeof(header),&got)!=FR_OK||
               !HaiCompanion::ImageBytes(header,got,imageBytes)) {
                f_close(&handle);Log("hai-image-header",0);return false;
            }
            parts=(imageBytes+HaiCompanion::ImageChunkBytes-1)/HaiCompanion::ImageChunkBytes;
        }
        const uint64_t required=std::min(HaiCompanion::ImageChunkBytes,imageBytes-index*HaiCompanion::ImageChunkBytes);
        if(f_size(&handle)<required||f_size(&handle)>HaiCompanion::ImageChunkBytes) {
            f_close(&handle);Log("hai-truncated-image",index);return false;
        }
        std::vector<DWORD> clusters(UFLOW_USB_EXTENTS*2+2); /* fragments per file, same as code files */
        clusters[0]=clusters.size();handle.cltbl=clusters.data();
        auto result=f_lseek(&handle,CREATE_LINKMAP);
        file.bytes=f_size(&handle);
        uint64_t remaining=file.bytes/512;
        for(size_t i=1;result==FR_OK&&i+1<clusters.size()&&clusters[i]&&remaining;i+=2) {
            const uint64_t sector=uint64_t(volume.database)+uint64_t(clusters[i+1]-2)*volume.csize;
            const uint64_t count=std::min<uint64_t>(uint64_t(clusters[i])*volume.csize,remaining);
            if(clusters[i+1]<2||uint64_t(clusters[i+1])+clusters[i]>volume.n_fatent||sector+count>(uint64_t{1}<<32)){result=FR_INT_ERR;break;}
            file.extents.push_back({uint32_t(sector),uint32_t(count)});
            remaining-=count;
        }
        handle.cltbl=nullptr;f_close(&handle);
        if(result!=FR_OK||remaining){Log("hai-linkmap",index);return false;}
        files.push_back(std::move(file));
    }
    if(files.empty()){Log("hai-images",0);return false;}
    /* nn_hai_user requires split images padded to 512 KiB. */
    for(size_t i=0;i+1<files.size();++i)
        if(files[i].bytes%0x80000){Log("hai-padding",int(i));return false;}
    try {
        companion=HaiCompanion::Build(files,uint64_t(volume.database)+uint64_t(volume.n_fatent-2)*volume.csize);
    } catch(...) {
        Log("hai-companion-invalid",int(files.size()));return false;
    }
    Log("hai-images",int(files.size()));
    Log("hai-companion-bytes",int(companion.size()));
    return true;
}
unsigned USBStorage::RawDevice() {
    std::lock_guard lock(mutex);return registered?rawDevice:0;
}
bool USBStorage::PrepareCode(const char *directory) {
    if(strncmp(directory,"usb1:/",6)||!Mount())return false;
    std::vector<unsigned char> storage(sizeof(RawUsbMap)+4+63);
    auto *request=reinterpret_cast<uint32_t*>((uintptr_t(storage.data())+63)&~uintptr_t(63));
    request[0]=UFLOW_USB_MAP_COMMAND;
    auto &map=*reinterpret_cast<RawUsbMap*>(request+1);memset(&map,0,sizeof(map));
    {
        std::lock_guard lock(mutex);
        map.magic=UFLOW_USB_MAP_MAGIC;map.device=rawDevice;map.bootSector=volume.volbase;
        if(disk_read(0,bounce,0,1)!=RES_OK)return false;
        map.mbrHash=RawUsbHash(bounce,512);
        if(disk_read(0,bounce,map.bootSector,1)!=RES_OK)return false;
        map.bootHash=RawUsbHash(bounce,512);
        FATDIR dir{};
        if(f_opendir(&dir,(std::string("0:/")+(directory+6)).c_str())!=FR_OK)return false;
        bool success=true;FILINFO info{};
        for(;;) {
            if(f_readdir(&dir,&info)!=FR_OK){success=false;break;}
            if(!info.fname[0])break;
            std::string name=info.fname;
            if(!(info.fattrib&AM_DIR)&&name.size()>4) {
                std::string ext=name.substr(name.size()-4);
                for(auto &c:ext)c=tolower(static_cast<unsigned char>(c));
                if((ext==".rpx"||ext==".rpl")&&!AddCodeFile(map,std::string(directory+6)+"/"+name)){success=false;break;}
            }
        }
        f_closedir(&dir);
        if(!success||!RawUsbMapValid(&map)){Log("usb-code-map-invalid",map.fileCount);return false;}
    }
    int mcp=IOS_Open("/dev/mcp",static_cast<IOSOpenMode>(0));
    if(mcp<0)return false;
    alignas(64) uint32_t response[16]{};
    auto result=IOS_Ioctl(mcp,100,request,sizeof(map)+4,response,4);IOS_Close(mcp);
    Log("usb-code-map-ipc",result);
    if(result!=0||response[0]!=UFLOW_USB_MAP_MAGIC)return false;
    expectedDevice=map.device;expectedBoot=map.bootSector;expectedMbrHash=map.mbrHash;expectedBootHash=map.bootHash;
    return true;
}
bool USBStorage::MountRuntime() {
    if(!Mount())return false;
    {
        std::lock_guard lock(mutex);
        if(rawDevice!=expectedDevice||disk_read(0,bounce,0,1)!=RES_OK||RawUsbHash(bounce,512)!=expectedMbrHash||
           disk_read(0,bounce,expectedBoot,1)!=RES_OK||RawUsbHash(bounce,512)!=expectedBootHash)return false;
    }
    int added=-1;
    return ContentRedirection_AddDevice(&device,&added)==CONTENT_REDIRECTION_RESULT_SUCCESS&&added>=0;
}
#endif
