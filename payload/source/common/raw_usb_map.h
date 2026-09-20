#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Private protocol: file allocation metadata only, never staged game data. */
#define UFLOW_USB_MAP_COMMAND 0x55465342u
#define UFLOW_USB_TARGET 0x55425331u
#define UFLOW_USB_MAP_MAGIC 0x55464d31u
#define UFLOW_USB_FILES 64u
#define UFLOW_USB_EXTENTS 4096u
typedef struct { uint32_t sector, count; } RawUsbExtent;
typedef struct {
    char path[256];
    uint32_t size, first, count;
} RawUsbFile;
typedef struct {
    uint32_t magic, device, bootSector, mbrHash, bootHash, fileCount, extentCount;
    RawUsbFile files[UFLOW_USB_FILES];
    RawUsbExtent extents[UFLOW_USB_EXTENTS];
} RawUsbMap;

static inline uint32_t RawUsbHash(const unsigned char *data, size_t size) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<size;++i)hash=(hash^data[i])*16777619u;
    return hash;
}
static inline int RawUsbPathEqual(const char *a,const char *b) {
    for(unsigned i=0;i<256;++i) {
        unsigned char x=a[i],y=b[i];
        if(x>='A'&&x<='Z')x+='a'-'A';
        if(y>='A'&&y<='Z')y+='a'-'A';
        if(x!=y)return 0;
        if(!x)return 1;
    }
    return 0;
}
static inline int RawUsbSafePath(const char *p) {
    size_t begin=0;
    for(size_t i=0;i<256;++i) {
        unsigned char c=p[i];
        if(c==0||c=='/') {
            size_t n=i-begin;
            if(!n||(n==1&&p[begin]=='.')||(n==2&&p[begin]=='.'&&p[begin+1]=='.'))return 0;
            if(!c)return 1;
            begin=i+1;
        } else if(c<32||c==':'||c=='\\'||c=='|'||c=='~')return 0;
    }
    return 0;
}
static inline int RawUsbMapValid(const RawUsbMap *m) {
    if(m->magic!=UFLOW_USB_MAP_MAGIC || (m->device!=1&&m->device!=2) ||
       !m->fileCount||m->fileCount>UFLOW_USB_FILES||m->extentCount>UFLOW_USB_EXTENTS)return 0;
    for(uint32_t i=0;i<m->fileCount;++i) {
        const RawUsbFile *f=&m->files[i];
        if(!RawUsbSafePath(f->path)||!f->size||!f->count||f->first>m->extentCount||f->count>m->extentCount-f->first)return 0;
        uint64_t sectors=0;
        for(uint32_t j=0;j<f->count;++j) {
            const RawUsbExtent *e=&m->extents[f->first+j];
            if(!e->sector||!e->count||(uint64_t)e->sector+e->count>0x100000000ull)return 0;
            sectors+=e->count;
        }
        if(sectors!=((uint64_t)f->size+511)/512)return 0;
        for(uint32_t j=0;j<i;++j)if(RawUsbPathEqual(f->path,m->files[j].path))return 0;
    }
    return 1;
}
