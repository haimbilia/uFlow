#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if FF_FS_READONLY != 1
#error USB filesystem must remain read-only
#endif
enum { START=2048, TOTAL=70000, FATSECT=550, DATA=START+32+FATSECT };
static unsigned char *image;
static int reads;
static void u32(unsigned char *p,unsigned x){for(int i=0;i<4;i++)p[i]=(x>>(i*8))&255;}
static void u16(unsigned char *p,unsigned x){p[0]=x&255;p[1]=x>>8;}
DSTATUS disk_status(BYTE n){return n?STA_NOINIT:0;}
DSTATUS disk_initialize(BYTE n){return disk_status(n);}
DRESULT disk_ioctl(BYTE n,BYTE cmd,void *p){(void)p;return !n&&cmd==CTRL_SYNC?RES_OK:RES_PARERR;}
DRESULT disk_read(BYTE n,BYTE *out,LBA_t sector,UINT count){if(n||!count||sector>=START+TOTAL||count>START+TOTAL-sector)return RES_PARERR;memcpy(out,image+sector*512,count*512);reads++;return RES_OK;}
int main(void){
    image=calloc(START+TOTAL,512);assert(image);
    image[446+4]=0x0c;u32(image+446+8,START);u32(image+446+12,TOTAL);image[510]=0x55;image[511]=0xaa;
    unsigned char *b=image+START*512;b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSWIN4.1",8);
    u16(b+11,512);b[13]=1;u16(b+14,32);b[16]=1;b[21]=0xf8;u32(b+28,START);u32(b+32,TOTAL);u32(b+36,FATSECT);u32(b+44,2);u16(b+48,1);u16(b+50,6);memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    b=image+(START+32)*512;u32(b,0x0ffffff8);u32(b+4,0xffffffff);u32(b+8,0x0fffffff);u32(b+12,0x0fffffff);
    b=image+DATA*512;memcpy(b,"TEST    TXT",11);b[11]=0x20;u16(b+26,3);u32(b+28,5);memcpy(image+(DATA+1)*512,"hello",5);
    FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK);assert(fs.fs_type==FS_FAT32);
    FIL file;assert(f_open(&file,"0:/TEST.TXT",FA_READ)==FR_OK);char bytes[8]={0};UINT got;
    assert(f_read(&file,bytes,8,&got)==FR_OK&&got==5&&!strcmp(bytes,"hello"));
    assert(f_lseek(&file,1)==FR_OK);assert(f_read(&file,bytes,2,&got)==FR_OK&&got==2&&!memcmp(bytes,"el",2));assert(f_close(&file)==FR_OK);
    DIR dir;FILINFO info;assert(f_opendir(&dir,"0:/")==FR_OK);assert(f_readdir(&dir,&info)==FR_OK&&!strcmp(info.fname,"TEST.TXT"));assert(f_readdir(&dir,&info)==FR_OK&&!info.fname[0]);assert(f_closedir(&dir)==FR_OK);
    assert(f_stat("0:/missing.bin",&info)==FR_NO_FILE);assert(reads>0);f_mount(NULL,"0:",0);
    memset(image,0,(START+TOTAL)*512);assert(f_mount(&fs,"0:",1)==FR_NO_FILESYSTEM);f_mount(NULL,"0:",0);free(image);
    puts("PASS: read-only FAT32 MBR mount, file read/seek, directory enumeration, missing file and invalid disk");
}
