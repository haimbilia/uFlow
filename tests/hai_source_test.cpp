#include "hai_companion.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
using DWORD=uint32_t;using UINT=unsigned;
constexpr unsigned UFLOW_USB_EXTENTS=4096,AM_DIR=16,FA_READ=1,CREATE_LINKMAP=~0u;
constexpr int FR_OK=0,FR_NO_FILE=4,FR_DISK_ERR=1,FR_INT_ERR=2;
struct FILINFO {unsigned fattrib=0;};
struct FIL {DWORD *cltbl=nullptr;unsigned index=0;};
struct {uint32_t database=2048,csize=1,n_fatent=3000000;} volume;
std::mutex mutex;
std::vector<uint64_t> sizes;
std::vector<uint8_t> header(512);
int missing=-1,readError=-1;
void Log(const char *,int) {}
int Index(const char *p){return std::stoi(std::string(p).substr(std::string(p).find("hif_")+4,6));}
int f_stat(const char *p,FILINFO *){const int i=Index(p);return i==missing||i>=int(sizes.size())?FR_NO_FILE:i==readError?FR_DISK_ERR:FR_OK;}
int f_open(FIL *f,const char *p,unsigned){f->index=Index(p);return FR_OK;}
int f_read(FIL *,void *out,unsigned n,UINT *got){memcpy(out,header.data(),n);*got=n;return FR_OK;}
uint64_t f_size(FIL *f){return sizes.at(f->index);}
int f_close(FIL *){return FR_OK;}
int f_lseek(FIL *f,unsigned){f->cltbl[1]=f_size(f)/512;f->cltbl[2]=2+f->index*(HaiCompanion::ImageChunkBytes/512);f->cltbl[3]=0;return FR_OK;}
namespace USBStorage {bool Mount(){return true;}bool BuildHaiCompanion(const char *,std::vector<unsigned char>&);}
// PRODUCTION_HAI_SOURCE
int main(){
    memcpy(header.data(),"EGGS",4);memcpy(header.data()+508,"SGGE",4);
    const auto put=[](size_t at,uint32_t n){for(int i=3;i>=0;--i){header[at+i]=n;n>>=8;}};
    put(16,1);put(24,24000);
    const uint64_t packed=512+24000ULL*0x8000;
    sizes={HaiCompanion::ImageChunkBytes,HaiCompanion::ImageChunkBytes,HaiCompanion::ImageChunkBytes,packed-3*HaiCompanion::ImageChunkBytes};
    std::vector<unsigned char> result;
    assert(USBStorage::BuildHaiCompanion("usb1:/game/content",result)&&!result.empty());
    missing=1;assert(!USBStorage::BuildHaiCompanion("usb1:/game/content",result)&&result.empty());missing=-1;
    sizes.pop_back();assert(!USBStorage::BuildHaiCompanion("usb1:/game/content",result));
    sizes.push_back(packed-3*HaiCompanion::ImageChunkBytes-512);
    assert(!USBStorage::BuildHaiCompanion("usb1:/game/content",result));sizes.back()+=512;
    readError=2;assert(!USBStorage::BuildHaiCompanion("usb1:/game/content",result));readError=-1;
    volume.n_fatent=100;assert(!USBStorage::BuildHaiCompanion("usb1:/game/content",result));
    assert(!USBStorage::BuildHaiCompanion(nullptr,result));
    puts("PASS: production HAI source refuses gaps, truncation, read errors and out-of-volume allocation");
}
