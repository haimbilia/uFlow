#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../payload/source/ios_mcp/source/raw_usb.c"

static unsigned char disk[512*400], expected[512*150], output[sizeof(expected)+128];
static int failRead, failOpen, failAlloc, blocks, closed, calls;
void *svcAlloc(u32 heap,u32 size){(void)heap;if(failAlloc)return NULL;void *p=malloc(size);if(p)++blocks;return p;}
void *svcAllocAlign(u32 heap,u32 size,u32 align){(void)align;return svcAlloc(heap,size);}
void svcFree(u32 heap,void *p){(void)heap;free(p);--blocks;}
int svcOpen(char *name,int mode){(void)mode;assert(!strcmp(name,"/dev/fsa"));return failOpen?-1:5;}
int svcClose(int fd){assert(fd==5);++closed;return 0;}
int svcIoctl(int fd,u32 request,void *in,u32 inlen,void *out,u32 outlen){assert(fd==5&&request==0x28&&in&&out&&inlen==64&&outlen==64);return 0;}
int FSA_RawOpen(int fd,char *name,int *out){assert(fd==5);assert(!strcmp(name,"/dev/usb01"));*out=9;return 0;}
int FSA_RawClose(int fd,int h){assert(fd==5&&h==9);return 0;}
int FSA_RawRead(int fd,void *data,u32 size,u32 count,u64 sector,int h){
    assert(fd==5&&h==9&&size==512&&count&&count<=128&&sector+count<=400);
    ++calls;if(failRead)return -1;memcpy(data,disk+sector*512,size*count);return 0;
}
int main(void){
    for(size_t i=0;i<sizeof(disk);++i)disk[i]=(unsigned char)(i*13+i/512);
    RawUsbMap *m=calloc(1,sizeof(*m));assert(m);
    m->magic=UFLOW_USB_MAP_MAGIC;m->device=1;m->bootSector=1;
    m->mbrHash=RawUsbHash(disk,512);m->bootHash=RawUsbHash(disk+512,512);
    m->fileCount=1;m->extentCount=2;
    strcpy(m->files[0].path,"Games/Mario Luigi [Game]/code/main.rpx");
    m->files[0].size=sizeof(expected)-7;m->files[0].count=2;
    m->extents[0]=(RawUsbExtent){3,140};m->extents[1]=(RawUsbExtent){200,10};
    memcpy(expected,disk+3*512,140*512);memcpy(expected+140*512,disk+200*512,10*512);
    assert(RawUsbInstall(m,sizeof(*m))==0&&blocks==2);
    assert(RawUsbHasFile(m->files[0].path)&&!RawUsbHasFile("missing"));
    assert(RawUsbHasFile("games/mario luigi [game]/CODE/MAIN.RPX"));
    for(unsigned pos=0;pos<sizeof(expected)+10;pos+=127) {
        unsigned lengths[]={1,511,512,513,65536,sizeof(output)};
        for(unsigned j=0;j<sizeof(lengths)/sizeof(*lengths);++j) {
            unsigned n=pos>=m->files[0].size?0:m->files[0].size-pos;
            if(n>lengths[j])n=lengths[j];
            memset(output,0xaa,sizeof(output));
            assert(RawUsbRead(m->files[0].path,output,lengths[j],pos)==(int)n);
            if(n)assert(!memcmp(output,expected+pos,n));
            assert(output[n]==0xaa);
        }
    }
    assert(RawUsbRead(m->files[0].path,output,512,UINT32_MAX)==0);
    failRead=1;assert(RawUsbRead(m->files[0].path,output,512,0)<0);failRead=0;
    assert(RawUsbRead("missing",output,512,0)<0);
    m->extentCount=1;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);m->extentCount=2;
    m->extents[0].sector=UINT32_MAX;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);m->extents[0].sector=3;
    m->bootHash^=1;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);m->bootHash^=1;
    failRead=1;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);failRead=0;
    failOpen=1;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);failOpen=0;
    failAlloc=1;assert(RawUsbInstall(m,sizeof(*m))<0&&blocks==0);failAlloc=0;
    assert(RawUsbInstall(m,sizeof(*m)-4)<0&&blocks==0);
    const char *bad[]={"../a","a/../b","a//b","/a","a/","a:b","a\\b","a/./b",""};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!RawUsbSafePath(bad[i]));
    assert(RawUsbInstall(m,sizeof(*m))==0);RawUsbClear();RawUsbClear();assert(blocks==0);
    free(m);printf("PASS: production USB loader fragmented reads, alignment, EOF, bounds, disk identity, errors and cleanup (%d raw reads)\n",calls);
}
