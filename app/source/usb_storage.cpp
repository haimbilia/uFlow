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
alignas(64) unsigned char bounce[64*1024];
void Log(const char *phase,int code){
    if(FILE *f=fopen("fs:/vol/external01/wiiu/apps/uFlow/usb-storage.log","a")){
        fprintf(f,"%s=%d\n",phase,code);fclose(f);
    }
}
int Fail(_reent *r,FRESULT result){
    r->_errno=result==FR_NO_FILE||result==FR_NO_PATH?ENOENT:result==FR_INVALID_NAME?EINVAL:result==FR_DENIED?EACCES:EIO;return -1;
}
bool Path(const char *path,std::string &out){
    if(!path||strncmp(path,"usb1:/",6)!=0)return false;
    out="0:/";out+=path+6;return out.find(':',2)==std::string::npos;
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
void CloseRaw(){if(handle>=0){FSAEx_RawCloseEx(client,handle);handle=-1;}if(client>=0){FSADelClient(client);client=-1;}}
}
extern "C" DSTATUS disk_status(BYTE n){return n==0&&handle>=0?0:STA_NOINIT;}
extern "C" DSTATUS disk_initialize(BYTE n){return disk_status(n);}
extern "C" DRESULT disk_read(BYTE n,BYTE *data,LBA_t sector,UINT count){
    if(n||handle<0||!count)return RES_PARERR;
    while(count){const UINT batch=std::min<UINT>(count,sizeof(bounce)/512);
        auto result=FSAEx_RawReadEx(client,bounce,512,batch,sector,handle);
        if(result!=FS_ERROR_OK){Log("read-error",result);return RES_ERROR;}
        memcpy(data,bounce,batch*512);data+=batch*512;sector+=batch;count-=batch;
    }return RES_OK;
}
extern "C" DRESULT disk_ioctl(BYTE,BYTE command,void*){return command==CTRL_SYNC?RES_OK:RES_PARERR;}
bool USBStorage::Mount(){
    std::lock_guard lock(mutex);if(registered)return true;
    Log("usb-readonly-build",2026092007);
    if(FindDevice("usb1:")>=0){Log("usb1-name-in-use",1);return false;}
    auto init=Mocha_InitLibrary();Log("mocha-init",init);if(init!=MOCHA_RESULT_SUCCESS)return false;
    for(const char *raw:{"/dev/usb01","/dev/usb02"}){
        client=FSAAddClient(nullptr);if(client<0){Log("fsa-client",client);continue;}
        auto unlock=Mocha_UnlockFSClientEx(client);Log("unlock",unlock);
        if(unlock!=MOCHA_RESULT_SUCCESS){CloseRaw();continue;}
        auto result=FSAEx_RawOpenEx(client,raw,&handle);Log(raw,result);
        if(result!=FS_ERROR_OK){handle=-1;CloseRaw();continue;}
        auto mount=f_mount(&volume,"0:",1);Log("fat-mount",mount);
        if(mount!=FR_OK||volume.fs_type!=FS_FAT32){f_mount(nullptr,"0:",0);CloseRaw();continue;}
        device.name="usb1";device.structSize=sizeof(FIL);device.open_r=Open;device.close_r=Close;
        device.read_r=Read;device.seek_r=Seek;device.fstat_r=FileStat;device.stat_r=PathStat;device.lstat_r=PathStat;
        device.dirStateSize=sizeof(FATDIR);device.diropen_r=DirOpen;device.dirnext_r=DirNext;device.dirreset_r=DirReset;device.dirclose_r=DirClose;
        const auto added=AddDevice(&device);Log("register-usb1",added);
        if(added>=0){registered=true;return true;}
        f_mount(nullptr,"0:",0);CloseRaw();
    }return false;
}
void USBStorage::Unmount(){std::lock_guard lock(mutex);if(registered){RemoveDevice("usb1:");registered=false;}f_mount(nullptr,"0:",0);CloseRaw();}
