#include "raw_usb.h"
#include "fsa.h"
#include "svc.h"
#include <string.h>

/* Used only on MCP's serialized load-file/custom-ioctl thread. */
static RawUsbMap *map;
static int client, device;
static unsigned char *bounce;
static int opened;
void RawUsbClear(void) {
    if(opened) { FSA_RawClose(client,device);svcClose(client);opened=0; }
    if(bounce){svcFree(0xCAFF,bounce);bounce=0;}
    if(map){svcFree(0xCAFF,map);map=0;}
}
int RawUsbInstall(const void *data,unsigned size) {
    RawUsbClear();
    if(size!=sizeof(RawUsbMap))return -1;
    map=svcAlloc(0xCAFF,sizeof(*map));
    if(!map)return -1;
    memcpy(map,data,sizeof(*map));
    if(!RawUsbMapValid(map)){RawUsbClear();return -1;}
    bounce=svcAllocAlign(0xCAFF,0x10000,0x40);
    if(!bounce){RawUsbClear();return -1;}
    client=svcOpen("/dev/fsa",0);
    if(client<0){RawUsbClear();return -1;}
    /* MCP needs its own explicitly unlocked client, independent of uFlow's. */
    memset(bounce,0,64);
    if(svcIoctl(client,0x28,bounce,64,bounce,64)<0){svcClose(client);RawUsbClear();return -1;}
    int result=FSA_RawOpen(client,map->device==1?"/dev/usb01":"/dev/usb02",&device);
    if(result<0){svcClose(client);RawUsbClear();return -1;}
    opened=1;
    if(FSA_RawRead(client,bounce,512,1,0,device)<0||RawUsbHash(bounce,512)!=map->mbrHash||
       FSA_RawRead(client,bounce,512,1,map->bootSector,device)<0||RawUsbHash(bounce,512)!=map->bootHash) {
        RawUsbClear();return -1;
    }
    return 0;
}
static const RawUsbFile *Find(const char *path) {
    if(map)for(unsigned i=0;i<map->fileCount;++i)if(RawUsbPathEqual(path,map->files[i].path))return &map->files[i];
    return 0;
}
int RawUsbHasFile(const char *path){return Find(path)!=0;}
int RawUsbRead(const char *path,void *output,unsigned length,unsigned position) {
    const RawUsbFile *file=Find(path);
    if(!file||!opened)return -1;
    if(position>=file->size)return 0;
    if(length>file->size-position)length=file->size-position;
    unsigned remaining=length, skip=position/512, offset=position%512;
    unsigned char *out=output;
    for(unsigned i=0;i<file->count&&remaining;++i) {
        const RawUsbExtent *e=&map->extents[file->first+i];
        if(skip>=e->count){skip-=e->count;continue;}
        unsigned sector=e->sector+skip, available=e->count-skip;skip=0;
        while(available&&remaining) {
            unsigned batch=available<128?available:128;
            unsigned needed=(unsigned)(((uint64_t)remaining+offset+511)/512);
            if(batch>needed)batch=needed;
            if(FSA_RawRead(client,bounce,512,batch,sector,device)<0)return -1;
            unsigned take=batch*512-offset;if(take>remaining)take=remaining;
            memcpy(out,bounce+offset,take);out+=take;remaining-=take;offset=0;
            sector+=batch;available-=batch;
        }
    }
    return remaining?-1:(int)length;
}
