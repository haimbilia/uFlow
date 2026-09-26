// Compiles production validation/provider code with only platform I/O mocked.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "utils/ini.h"
struct MochaRPXLoadInfo { char path[256]; };
struct MetaInformation { char longname[256]{},shortname[256]{},author[256]{}; };
constexpr unsigned ICON_SIZE=32;
struct { struct { struct { char toMountPath[255]; } bundleMountInformation; } contentReplacementInfo; } gReplacementInfo;
constexpr int WUHB_UTILS_RESULT_SUCCESS=0,BundleSource_FileDescriptor_CafeOS=0;
struct WUHBRPXInfo {};
void OSReport(const char *,...) {}
std::string metadata;
bool failUnmount=false;
int WUHBUtils_GetRPXInfo(const char *,int,WUHBRPXInfo *) {return 0;}
int WUHBUtils_MountBundle(const char *,const char *,int,int *result){*result=0;return 0;}
int WUHBUtils_UnmountBundle(const char *,int *result){*result=failUnmount?-1:0;return 0;}
int WUHBUtils_ReadWholeFile(const char *path,uint8_t **buffer,uint32_t *size) {
    if(std::string(path).find("meta.ini")==std::string::npos)return -1;
    *size=metadata.size();*buffer=static_cast<uint8_t *>(malloc(std::max<std::size_t>(1,*size)));
    memcpy(*buffer,metadata.data(),*size);return 0;
}
// PRODUCTION_PROVIDER
} // production anonymous namespace
int main() {
    namespace fs=std::filesystem;
    fs::create_directories("fs:/vol/external01/cores");
    std::ofstream("fs:/vol/external01/cores/Test.RPX")<<"core";
    assert(ValidCorePath("cores/Test.RPX"));
    assert(!ValidCorePath("../cores/Test.RPX"));
    const std::string provider="apps/"+std::string(254-std::strlen(kVolumePrefix)-10,'a')+".WUHB";
    fs::create_directories("fs:/vol/external01/apps");
    std::ofstream("fs:/vol/external01/"+provider)<<"provider";
    assert(ValidProviderPath(provider.c_str())); // full 254-byte mount path fits
    assert(!ValidProviderPath(("x"+provider).c_str()));
    ProviderMeta out;std::string why;
    metadata="[menu]\nlongname=RetroArch\nauthor=Author"; // no final newline
    assert(ReadProvider(provider,out,why)&&out.metaLoaded);
    assert(std::string(out.meta.author)=="Author");
    out={};metadata.clear();
    assert(ReadProvider(provider,out,why)&&!out.metaLoaded); // zero-byte allocation is valid
    out={};metadata="[menu]\nbroken\nlongname=Partial";
    assert(ReadProvider(provider,out,why)&&!out.metaLoaded); // inih returns positive error lines
    failUnmount=true;
    assert(!ReadProvider(provider,out,why)&&why=="provider unmount failed");
    puts("PASS: production RetroArch provider metadata, path boundaries/case, malformed INI and unmount failure");
}
