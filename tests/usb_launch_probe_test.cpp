#include "usb_launch_probe.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using WiiUsbProbe::Result;
static void Write(const std::string &path,const std::string &bytes) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path,std::ios::binary).write(bytes.data(),bytes.size());
}
int main() {
    const std::string original="usb1:/wbfs/Wii Sports [RSPE01]/RSPE01.wbfs";
    const std::string second="usb2:/"+original.substr(6);
    std::string wbfs(1024,'\0');
    wbfs.replace(0,4,"WBFS");wbfs[8]=9;wbfs[9]=21;wbfs[12]=1;
    wbfs.replace(512,6,"RSPE01");wbfs.replace(536,4,"\x5d\x1c\x9e\xa3",4);
    WiiRequest::Request request{"RSPE01",original};
    bool mounted=false;int retries=0,inspections=0;
    auto inspect=[&](int n) {
        ++inspections;return WiiUsbProbe::Partition{mounted&&n==2,true,"FAT32"};
    };
    auto retry=[&](){++retries;mounted=true;return true;};
    FILE *log=tmpfile();assert(log);
    Write(second,wbfs);
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,retry,log)==Result::Ready);
    assert(retries==1&&request.path==second);
    request.path=original;retries=0;
    fs::remove(second);
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,retry,log)==Result::Missing);
    assert(retries==0&&request.path==original);
    Write(second,"bad header");
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,retry,log)==Result::InvalidHeader);
    assert(retries==0);
    auto wrong=wbfs;wrong.replace(512,6,"RMCE01");Write(second,wrong);
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,retry,log)==Result::WrongId);
    assert(retries==0&&request.path==original);
    Write(second,wbfs);Write(original,wbfs);
    auto both=[](int n){return WiiUsbProbe::Partition{n<=2,true,"FAT32"};};
    assert(WiiUsbProbe::ResolveWithRetry(request,both,retry,log)==Result::Ambiguous);
    assert(retries==0&&request.path==original);
    mounted=false;
    auto failedRetry=[&](){++retries;return false;};
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,failedRetry,log)==Result::NoFat);
    assert(retries==1&&request.path==original);
    request.path="sd:/wbfs/game.wbfs";inspections=0;retries=0;
    assert(WiiUsbProbe::ResolveWithRetry(request,inspect,retry,log)==Result::Ready);
    assert(inspections==0&&retries==0);
    rewind(log);std::string output;char line[1024];
    while(fgets(line,sizeof(line),log))output+=line;
    fclose(log);
    for(auto text:{"usb_resolution=no-fat-partition","usb_reinitialize=begin",
        "usb_resolution=ready","usb_resolution=file-missing","errno=2",
        "usb_resolution=invalid-wbfs-header","actual_id=RMCE01 expected_id=RSPE01",
        "usb_resolution=ambiguous-copies"})assert(output.find(text)!=std::string::npos);
    std::cout<<"USB mount retry and failure diagnostics passed\n";
}
