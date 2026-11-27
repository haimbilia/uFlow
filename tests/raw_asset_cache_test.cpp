#include "RawAssetCache.h"
#include <cassert>
#include <fstream>
#include <string>
int main(int argc,char **argv){
 RawAssetCache cache;char out[1024];
 assert(RawAssetHex(RawAssetFingerprint("hello"))=="a430d84680aabd0b");
 {std::ofstream f("/tmp/manifest.ready");f<<"UFLOW-ASSET-CACHE-1\nid\n2\nmodel/a.bin\t000000\nmodel/b.bin\t001041\nEND\n";}
 assert(cache.Load("/tmp","wiiu/cache/test","id"));assert(cache.Size()==2);
 assert(cache.Resolve(out,sizeof(out),"/vol/content/Model//./A.bin","rb",true));assert(std::string(out)=="/vol/external01/wiiu/cache/test/data/00/00/00.bin");
 assert(cache.Resolve(out,sizeof(out),"/vol/content/Model/sub/../b.bin","r",true));assert(std::string(out)=="/vol/external01/wiiu/cache/test/data/01/01/01.bin");
 assert(!cache.Resolve(out,2,"/vol/content/model/a.bin","r",true));
 for(auto mode:{"w","r+","a"})assert(!cache.Resolve(out,sizeof(out),"/vol/content/model/a.bin",mode,true));
 assert(!cache.Resolve(out,sizeof(out),"/vol/save/model/a.bin","r",true));
 assert(!cache.Resolve(out,sizeof(out),"/vol/content/model/a.bin","r",false));
 assert(!cache.Load("/tmp","wiiu/cache/test","wrong"));assert(cache.Size()==0);
 {std::ofstream f("/tmp/manifest.ready");f<<"UFLOW-ASSET-CACHE-1\nid\n1\n../escape\t000000\nEND\n";}
 assert(!cache.Load("/tmp","wiiu/cache/test","id"));
 {std::ofstream f("/tmp/manifest.ready");f<<"UFLOW-ASSET-CACHE-1\nid\n2\na\t000000\nb\t000000\nEND\n";}
 assert(!cache.Load("/tmp","wiiu/cache/test","id"));std::remove("/tmp/manifest.ready");
 if(argc==4){assert(cache.Load(argv[1],argv[2],argv[3]));assert(cache.Size()>0);printf("Actual verified SD manifest parsed: %zu files\n",cache.Size());}
}
