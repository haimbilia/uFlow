#include "title_install.hpp"
#include <atomic>
#include <memory>
#include <malloc.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <sys/statvfs.h>
#ifdef __WIIU__
#include <coreinit/mcp.h>
#include <coreinit/ios.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/filesystem_fsa.h>
#include <mocha/mocha.h>
#endif
namespace TitleInstall {
bool Refresh(std::vector<Package>& packages){
#ifdef __WIIU__
    bool success=false;int fd=MCP_Open();if(fd<0)return false;int n=MCP_TitleCount(fd);
    if(n>0&&n<32768){
        std::unique_ptr<MCPTitleListType,decltype(&std::free)> storage(static_cast<MCPTitleListType*>(memalign(64,n*sizeof(MCPTitleListType))),std::free);
        if(!storage){MCP_Close(fd);return false;}auto *titles=storage.get();uint32_t count=n;
        if(MCP_TitleList(fd,&count,titles,n*sizeof(titles[0]))>=0&&count<=unsigned(n)){
            success=true;
            for(auto &p:packages){p.installedVersion[0]=p.installedVersion[1]=-1;
                for(size_t i=0;i<count;++i)if(titles[i].titleId==p.titleId){const std::string device(titles[i].indexedDevice,3);if(device!="usb"&&device!="mlc")continue;int target=device=="usb"?1:0;p.installedVersion[target]=std::max(p.installedVersion[target],int(titles[i].titleVersion));}}
        }
    }else if(n==0){success=true;for(auto &p:packages)p.installedVersion[0]=p.installedVersion[1]=-1;}
    MCP_Close(fd);return success;
#else
    (void)packages;return true;
#endif
}
bool FreeSpace(int target,uint64_t &bytes,std::string &error){
    bytes=0;error.clear();
    if(target<0||target>1){error="Invalid installation destination";return false;}
#ifdef __WIIU__
    // Query the real destination volume, not the SD-mounted fs: devoptab.
    const auto fail=[&](const char *phase,int result){char code[24];std::snprintf(code,sizeof(code)," (0x%08X)",unsigned(result));error=std::string(phase)+code;return false;};
    const auto init=Mocha_InitLibrary();if(init!=MOCHA_RESULT_SUCCESS)return fail("Storage access initialization failed",int(init));
    const auto client=FSAAddClient(nullptr);if(client<0)return fail("Cannot open storage client",int(client));
    const auto unlock=Mocha_UnlockFSClientEx(client);
    if(unlock!=MOCHA_RESULT_SUCCESS){FSADelClient(client);return fail("Storage access failed",int(unlock));}
    uint64_t available=0;
    const auto result=FSAGetFreeSpaceSize(client,target?"/vol/storage_usb01/usr":"/vol/storage_mlc01/usr",&available);
    FSADelClient(client);
    if(result<0)return fail(target?"Cannot read USB free space. Use Wii U-formatted storage":"Cannot read internal storage free space",int(result));
    bytes=available;return true;
#else
    struct statvfs s{};const char *path=target?"fs:/vol/storage_usb01":"fs:/vol/storage_mlc01";
    if(statvfs(path,&s)!=0){error=target?"USB storage unavailable. Use a Wii U-formatted drive.":"Cannot read internal storage free space.";return false;}
    bytes=uint64_t(s.f_bavail)*s.f_frsize;return true;
#endif
}
struct Job::Impl {mutable std::mutex mutex;Snapshot state;std::thread thread;
    void Set(Snapshot s){std::lock_guard lock(mutex);state=std::move(s);}
    void Run(std::vector<Package> packages,int target){
        Snapshot s;s.busy=true;s.message="Checking installation files";Set(s);
        const auto fail=[&](std::string error){s.busy=false;s.message=std::move(error);Set(s);};
        for(auto &p:packages){Package current;if(!ReadPackage(p.path,current)||!current.error.empty()||current.titleId!=p.titleId||current.version!=p.version||current.kind!=p.kind||current.bytes!=p.bytes){fail(current.error.empty()?"Package changed. Scan the library again.":current.error);return;}}
        Refresh(packages);uint64_t required=0;for(const auto &p:packages)if(!p.Installed(target))required+=p.bytes;
        uint64_t free=0;std::string error;if(!FreeSpace(target,free,error)){fail(error);return;}
        if(free<required){fail("Not enough free space: need "+Size(required)+", available "+Size(free));return;}
#ifdef __WIIU__
        const int fd=MCP_Open();if(fd<0){fail("Cannot open system title installer");return;}
        const auto closeFail=[&](const char *phase,int code){char b[100];if(code==-265150)std::snprintf(b,sizeof(b),"Ticket signature rejected. Not installed (0x%08X)",unsigned(code));else std::snprintf(b,sizeof(b),"%s failed (0x%08X)",phase,unsigned(code));MCP_Close(fd);fail(b);};
        int result=MCP_InstallSetTargetDevice(fd,target?MCP_INSTALL_TARGET_USB:MCP_INSTALL_TARGET_MLC);
        if(result){closeFail("Destination selection",result);return;}
        if(target&&(result=MCP_InstallSetTargetUsb(fd,1))){closeFail("USB selection",result);return;}
        s.total=required;uint64_t finished=0;
        for(size_t i=0;i<packages.size();++i){auto &p=packages[i];if(p.Installed(target))continue;
            constexpr const char *prefix="fs:/vol/external01/";
            if(p.path.rfind(prefix,0)!=0||(p.path.find("/../")!=std::string::npos||p.path.ends_with("/.."))){MCP_Close(fd);fail("Installation source must be on the SD card");return;}
            const auto native=std::string("/vol/app_sd/")+p.path.substr(std::char_traits<char>::length(prefix));
            struct Request {alignas(64) char path[0x280]{};alignas(64) IOSVec vector{};alignas(64) MCPInstallInfo info{};alignas(64) MCPInstallProgress progress{};std::atomic<bool> done{false};std::atomic<int> result{0};};
            // Heap-backed and alive until the IOS callback, never on a retail-sized stack.
            auto r=std::make_unique<Request>();if(native.size()>=sizeof(r->path)){MCP_Close(fd);fail("Installation path is too long");return;}
            std::copy(native.begin(),native.end(),r->path);
            result=MCP_InstallGetInfo(fd,r->path,&r->info);if(result){closeFail("Package validation",result);return;}
            if(Big(reinterpret_cast<unsigned char*>(&r->info),8)!=p.titleId){MCP_Close(fd);fail("System package identity does not match its metadata");return;}
            r->vector.vaddr=r->path;r->vector.len=0x27f;
            const auto callback=[](IOSError code,void *context){auto *r=static_cast<Request*>(context);r->result.store(int(code));r->done.store(true);};
            result=IOS_IoctlvAsync(fd,0x81,1,0,&r->vector,callback,r.get());if(result){closeFail("Install submission",result);return;}
            s.package=i;s.message="Installing "+std::string(Name(p.kind))+" ("+std::to_string(i+1)+"/"+std::to_string(packages.size())+")";
            while(!r->done.load()){
                if(MCP_InstallGetProgress(fd,&r->progress)==0&&r->progress.inProgress)s.done=finished+std::min(p.bytes,uint64_t(r->progress.sizeProgress));
                Set(s);std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            result=r->result.load();if(result){closeFail("Installation",result);return;}
            if(!Refresh(packages)||!p.Installed(target)){MCP_Close(fd);fail("Installation finished, but the installed title could not be verified");return;}
            finished+=p.bytes;s.done=finished;Set(s);
        }
        MCP_Close(fd);s.busy=false;s.complete=true;s.done=s.total;s.message="Installation complete. Source files kept.";Set(s);
#else
        fail("Installation requires a Wii U");
#endif
    }
};
Job::Job():impl_(new Impl){}Job::~Job(){if(impl_->thread.joinable())impl_->thread.join();delete impl_;}
Snapshot Job::Poll()const{std::lock_guard lock(impl_->mutex);return impl_->state;}
bool Job::Start(const std::vector<Package>& packages,int target){
    if(Poll().busy||target<0||target>1||!Available())return false;
    std::vector<Package> selected;for(const auto &p:packages)if(p.selected&&!p.Installed(target)){if(!p.error.empty())return false;selected.push_back(p);}
    if(selected.empty())return false;
    std::sort(selected.begin(),selected.end(),[](const auto&a,const auto&b){return a.kind<b.kind;});
    for(size_t i=1;i<selected.size();++i)if(selected[i-1].kind==selected[i].kind)return false;
    if(impl_->thread.joinable())impl_->thread.join();
    impl_->Set({true,false,0,0,"Preparing installation",0});
    try{impl_->thread=std::thread([this,selected,target]{try{impl_->Run(selected,target);}catch(...){impl_->Set({false,false,0,0,"Installer could not allocate resources",0});}});}catch(...){impl_->Set({false,false,0,0,"Cannot start installer",0});return false;}return true;
}
}
