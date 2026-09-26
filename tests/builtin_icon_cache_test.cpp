#include "builtin_icon_cache.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
void Write(const std::string &p,const std::string &b){std::ofstream f(p,std::ios::binary);f.write(b.data(),b.size());}
int main(){
    char temp[]="/tmp/uflow-icons-XXXXXX";std::string root=mkdtemp(temp);
    const auto source=root+"/icon.tga",cache=root+"/sd";
    std::string image(18+128*128*4,'\0');image[2]=2;image[12]=128;image[14]=128;image[16]=32;image[17]=0x28;
    Write(source,image);
    auto path=BuiltinIconCache::Store(source,cache);assert(!path.empty());
    assert(path.find("/cache/icons/")!=std::string::npos && fs::file_size(path)==image.size());
    assert(BuiltinIconCache::Store(source,cache)==path);
    Write(path,"partial");assert(BuiltinIconCache::Store(source,cache)==path && fs::file_size(path)==image.size());
    Write(path,std::string(image.size(),'\0'));assert(BuiltinIconCache::Store(source,cache)==path && BuiltinIconCache::Valid(path,image.size()));
    const auto other=root+"/other.tga";Write(other,image);
    assert(BuiltinIconCache::Store(other,cache)!=path); // distinct sources cannot alias by title ID
    Write(source,image+"footer");assert(BuiltinIconCache::Store(source,cache)!=path);
    Write(source,image.substr(0,20));assert(BuiltinIconCache::Store(source,cache).empty());
    Write(source,image);Write(root+"/blocked","file");
    assert(BuiltinIconCache::Store(source,root+"/blocked").empty());
    assert(fs::file_size(source)==image.size());
    fs::remove_all(root);
    std::cout<<"PASS: built-in icon cache reuse, sharding, truncated/corrupt recovery, source isolation, revision change, failed destination preserves source\n";
}
