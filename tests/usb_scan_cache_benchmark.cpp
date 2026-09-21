#include "usb_read_cache.hpp"
extern "C" {
#include "ff.h"
#include "diskio.h"
}
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
constexpr unsigned Start=2048,Total=70000,FatSectors=550,Data=Start+32+FatSectors,Games=3000;
static std::vector<unsigned char> image((Start+Total)*512);
static USBReadCache<> cache;
static bool caching=false;
static std::uint64_t rawCalls=0;
static void Put16(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;}
static void Put32(unsigned char *p,unsigned x){for(int i=0;i<4;++i)p[i]=x>>(8*i);}
static unsigned char *Cluster(unsigned c){return image.data()+(Data+c-2)*512;}
static void Fat(unsigned c,unsigned next=0x0fffffff){Put32(image.data()+(Start+32)*512+c*4,next);}
static void Entry(unsigned char *p,const char *name,unsigned cluster,unsigned attr=0x10,unsigned size=0){
    std::memcpy(p,name,11);p[11]=attr;Put16(p+20,cluster>>16);Put16(p+26,cluster);Put32(p+28,size);
}
static unsigned char Checksum(const char *name){unsigned char sum=0;for(int i=0;i<11;++i)sum=((sum&1)?128:0)+(sum>>1)+name[i];return sum;}
static void LongEntry(unsigned char *p,const std::string &name,unsigned seq,unsigned last,unsigned char checksum){
    std::memset(p,0xff,32);p[0]=seq|(seq==last?0x40:0);p[11]=15;p[12]=0;p[13]=checksum;Put16(p+26,0);
    constexpr int pos[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(unsigned i=0;i<13;++i){const unsigned offset=(seq-1)*13+i;Put16(p+pos[i],offset<name.size()?name[offset]:offset==name.size()?0:0xffff);}
}
static std::string Name(unsigned i){char n[16];std::snprintf(n,sizeof(n),"%04u",i);return std::string("A Long Wii U Game Title Number ")+n+" (USA) (En,Fr,Es)";}
extern "C" DSTATUS disk_status(BYTE n){return n?STA_NOINIT:0;}
extern "C" DSTATUS disk_initialize(BYTE n){return disk_status(n);}
extern "C" DRESULT disk_ioctl(BYTE n,BYTE cmd,void *){return !n&&cmd==CTRL_SYNC?RES_OK:RES_PARERR;}
extern "C" DRESULT disk_read(BYTE n,BYTE *out,LBA_t sector,UINT count){
    if(n||!count||sector>=Start+Total||count>Start+Total-sector)return RES_PARERR;
    auto raw=[](unsigned char *out,unsigned sector,unsigned count){++rawCalls;std::memcpy(out,image.data()+sector*512,count*512);return true;};
    return (caching?cache.ReadSectors(out,sector,count,raw):raw(out,sector,count))?RES_OK:RES_ERROR;
}
static void Read(const std::string &path){FIL file;assert(f_open(&file,path.c_str(),FA_READ)==FR_OK);char buf[64];UINT got;assert(f_read(&file,buf,sizeof(buf),&got)==FR_OK && got==12 && !std::memcmp(buf,"fixture-data",12));assert(f_close(&file)==FR_OK);}
static std::uint64_t Scan(bool enabled){
    caching=enabled;cache.Reset();rawCalls=0;FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK);
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<Games;++i){
        auto base="0:/LIBRARY/"+Name(i);FILINFO info;
        assert(f_stat(base.c_str(),&info)==FR_OK && (info.fattrib&AM_DIR));
        Read(base+"/code/app.xml");Read(base+"/code/cos.xml");
        assert(f_stat((base+"/code/main.rpx").c_str(),&info)==FR_OK);
        assert(f_stat((base+"/content").c_str(),&info)==FR_OK);
        Read(base+"/meta/meta.xml");
        assert(f_stat((base+"/cover.png").c_str(),&info)==FR_NO_FILE);
        assert(f_stat((base+"/meta/iconTex.tga").c_str(),&info)==FR_OK);
    }
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    f_mount(nullptr,"0:",0);
    std::printf("%s: %u long-name game folders; raw reads=%llu; host CPU+memory time=%.2fs\n",enabled?"Cached":"Uncached",Games,static_cast<unsigned long long>(rawCalls),seconds);std::fflush(stdout);
    return rawCalls;
}
int main(){
    auto *m=image.data();m[450]=12;Put32(m+454,Start);Put32(m+458,Total);m[510]=0x55;m[511]=0xaa;
    auto *b=m+Start*512;b[0]=0xeb;b[1]=0x58;b[2]=0x90;std::memcpy(b+3,"MSWIN4.1",8);Put16(b+11,512);b[13]=1;Put16(b+14,32);b[16]=1;b[21]=0xf8;Put32(b+28,Start);Put32(b+32,Total);Put32(b+36,FatSectors);Put32(b+44,2);Put16(b+48,1);Put16(b+50,6);std::memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    Fat(0,0x0ffffff8);Fat(1);Fat(2);Entry(Cluster(2),"LIBRARY    ",3);
    unsigned offset=0;
    for(unsigned i=0;i<Games;++i){
        char shortName[12];std::snprintf(shortName,sizeof(shortName),"G%07u   ",i);
        const auto name=Name(i);const unsigned slots=(name.size()+13)/13;
        for(unsigned seq=slots;seq;--seq){LongEntry(Cluster(3)+offset,name,seq,slots,Checksum(shortName));offset+=32;}
        Entry(Cluster(3)+offset,shortName,2000+i);offset+=32;
        Fat(2000+i);Fat(5000+i);Fat(8000+i);Fat(11000+i);
        Entry(Cluster(2000+i),"CODE       ",5000+i);Entry(Cluster(2000+i)+32,"CONTENT    ",8000+i);Entry(Cluster(2000+i)+64,"META       ",11000+i);
        Entry(Cluster(5000+i),"APP     XML",15000,0x20,12);Entry(Cluster(5000+i)+32,"COS     XML",15000,0x20,12);Entry(Cluster(5000+i)+64,"MAIN    RPX",15000,0x20,12);
        Entry(Cluster(11000+i),"META    XML",15000,0x20,12);Entry(Cluster(11000+i)+32,"ICONTEX TGA",15000,0x20,12);
    }
    unsigned sectors=(offset+32+511)/512;assert(sectors+3<2000);
    for(unsigned i=0;i<sectors;++i)Fat(3+i,i+1<sectors?4+i:0x0fffffff);
    Fat(15000);std::memcpy(Cluster(15000),"fixture-data",12);
    const auto uncached=Scan(false),cached=Scan(true);
    assert(cached*20<uncached);
    std::printf("PASS: identical metadata reads; %.1fx fewer physical read calls (not a Wii U timing claim)\n",double(uncached)/cached);
}
