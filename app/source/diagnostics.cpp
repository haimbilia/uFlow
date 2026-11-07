#include "diagnostics.hpp"
#include "update_model.hpp"
#include "saved_file.hpp"
#include "../../native-installer/source/installer.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace {
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<unsigned> handoffSequence{0};
std::atomic<unsigned> handoffEvents[64]{};
const char *HandoffName(unsigned id) {
    static const char *names[]={"none","draw-release begin","draw-release returned","process-messages begin","process-messages returned",
        "release callback begin","release GPU wait begin","release GPU wait succeeded","release GPU wait FAILED",
        "scan buffers freed","MEM1 heap destroy begin","MEM1 heap destroyed","foreground heap destroyed",
        "display enables restored","release callback complete","acquire callback begin","acquire callback complete",
        "save callback begin","save callback complete","app GPU wait FAILED","app GPU wait succeeded"};
    return id<std::size(names)?names[id]:"unknown";
}
}
extern "C" void UFlow_HandoffStage(unsigned stage) {
    const unsigned seq=handoffSequence.fetch_add(1)+1;
    handoffEvents[seq%64].store((seq<<8)|(stage&255),std::memory_order_release);
}
namespace UFlowSupport {
std::string Quote(const std::string &s) {
    const char hex[]="0123456789abcdef"; std::string out="\"";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<32 || c>=127){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
std::string Redact(const std::string &s) {
    // Drop whole path/address-bearing lines, not just the first whitespace token:
    // paths may contain spaces, names and identifiers. Limit before parsing.
    std::istringstream input(s.substr(0,32768)); std::string line,out;
    while(std::getline(input,line)) {
        bool sensitive=line.find('/')!=std::string::npos || line.find('\\')!=std::string::npos ||
            line.find('@')!=std::string::npos || line.find(':')!=std::string::npos;
        int dots=0; for(char c:line)if(c=='.')++dots;
        if(dots>=3)sensitive=true;
        out+=sensitive?"[path or address line omitted]\n":line.substr(0,512)+"\n";
    }
    return out;
}
namespace {
std::string Tail(const std::string &path) {
    std::ifstream f(path,std::ios::binary); if(!f)return "[not available]\n";
    f.seekg(0,std::ios::end);const auto length=f.tellg(); if(length<0)return "[read failed]\n";
    const auto size=std::min<std::streamoff>(length,16384); f.seekg(length-size);
    std::string data(static_cast<std::size_t>(size),'\0');f.read(data.data(),size);data.resize(f.gcount());
    if(length>size){const auto nl=data.find('\n');data=nl==std::string::npos?"":data.substr(nl+1);}
    return Redact(data);
}
bool Write(const std::string &path,const std::string &data) {
    // A complete temporary file is promoted only after a successful flush/close.
    auto tmp=path+".tmp";FILE *f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fwrite(data.data(),1,data.size(),f)==data.size();
    if(std::fclose(f)!=0)ok=false;
    if(ok && SavedFile::Commit(tmp,path))return true;
    std::remove(tmp.c_str());return false;
}
std::string NumericSettings(const std::string &root) {
    std::ifstream input(root+"/uflow.cfg");std::string line,out;int count=0;
    while(count++<256 && std::getline(input,line)) {
        if(line.size()>128)continue;
        auto equal=line.find('=');if(equal==std::string::npos)continue;
        auto key=line.substr(0,equal),value=line.substr(equal+1);
        if(key.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789")!=std::string::npos ||
           value.empty() || value.find_first_not_of("0123456789-.\r")!=std::string::npos)continue;
        out+=line+'\n';
    }
    return out;
}
}
namespace {
std::string Configuration(const std::string &root,const std::string &name) {
    std::ifstream input(root+"/"+name);std::string line,out;int rows=0;
    while(rows++<128 && std::getline(input,line)){
        if(line.size()>4096)continue;
        std::istringstream fields(line);std::string field;int col=0;
        while(std::getline(fields,field,'\t')){
            if(col)out+='\t';
            const bool path=(name=="content_sources.cfg"&&col==3)||(name=="media_sources.cfg"&&(col==4||col==8));
            if(path)out+=field.rfind("usb1:/",0)==0?"[FAT32 USB]":field.rfind("fs:/vol/external01",0)==0?"[SD]":"[path omitted]";
            else if((name=="libraries.cfg"&&col==1)||field.size()>128||field.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.;\r")!=std::string::npos)out+="[text omitted]";
            else out+=field;
            ++col;
        }
        out+='\n';
    }
    return out;
}
std::string Inventory(const std::string &root) {
    std::string out;
    for(const char *relative:{"modules/RPXLoadingModule.wms","modules/ContentRedirectionModule.wms","modules/setup/00_mocha.rpx"}){
        const auto path=root+"/../../environments/aroma/"+relative;struct stat st{};
        out+=std::string(relative)+" ";
        if(stat(path.c_str(),&st)!=0)out+="missing\n";
        else if(st.st_size<0 || st.st_size>32*1024*1024)out+="unexpected size\n";
        else {try{out+=Setup::HashFile(path,{},"")+"\n";}catch(...){out+="unreadable\n";}}
    }
    return out;
}
}
struct FreezeWatch::Impl {
    std::string root;
    std::atomic<bool> stop{false},active{false};
    std::atomic<uint32_t> beat{0};
    std::atomic<int> phase{0};
    std::mutex mutex; std::condition_variable wake; std::thread worker;
    bool pending=false; int timeout;
    static uint32_t Now(){return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    void Capture(const std::string &reason) {
        const std::string body="Build "+std::to_string(UFlowUpdate::BuildSequence)+"\n"+reason+
            "\nLast session checkpoint\n"+Tail(root+"/session-active").substr(0,512)+
            "\nPrevious startup log\n"+Tail(root+"/uflow.log").substr(0,5500)+
            "\nPrevious support log\n"+Tail(root+"/support-session.log").substr(0,6000)+
            "\nSDL foreground handoff\n"+Tail(root+"/handoff.log");
        if(Write(root+"/freeze-report.log",body))Write(root+"/freeze-pending","1\n");
    }
    Impl(std::string r,int ms):root(std::move(r)),timeout(ms) {
        pending=std::ifstream(root+"/freeze-pending").good();
        if(std::ifstream(root+"/session-active").good()){
            Capture("Previous session ended unexpectedly. Freeze or power loss possible.");
            pending=std::ifstream(root+"/freeze-pending").good();
        }
        worker=std::thread([this]{
            bool reported=false;unsigned savedSequence=0;std::string handoffLog;
            while(!stop){
                std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(250),[&]{return stop.load();});lock.unlock();
                if(stop)break;
                // Also retain callback progress while normal background waiting is paused.
                const unsigned end=handoffSequence.load();bool changed=false;
                for(unsigned seq=std::max(savedSequence+1,end>63?end-63:1u);seq<=end;++seq) {
                    const unsigned event=handoffEvents[seq%64].load(std::memory_order_acquire);
                    if((event>>8)!=seq)break;
                    handoffLog+=std::to_string(seq)+" "+HandoffName(event&255)+"\n";
                    savedSequence=seq;changed=true;
                }
                if(changed){if(handoffLog.size()>12000)handoffLog.erase(0,handoffLog.find('\n',handoffLog.size()-10000)+1);
                    Write(root+"/handoff.log",handoffLog);}
                if(!active){reported=false;continue;}
                const auto lastBeat=beat.load();
                const auto elapsed=Now()-lastBeat;
                if(elapsed<static_cast<uint32_t>(timeout)){reported=false;continue;}
                if(!reported){
                    Capture("Main loop unresponsive for "+std::to_string(elapsed/1000)+" seconds. Phase "+std::to_string(phase.load())+" (0 events, 1 input/update, 2 render, 3 HOME/exit cleanup).");
                    reported=true;
                }
            }
        });
    }
    ~Impl(){stop=true;wake.notify_all();if(worker.joinable())worker.join();}
};
FreezeWatch::FreezeWatch(std::string root,int timeoutMs):impl(new Impl(std::move(root),timeoutMs)){}
FreezeWatch::~FreezeWatch()=default;
void FreezeWatch::Beat(int phase){impl->phase=phase;impl->beat=Impl::Now();}
void FreezeWatch::Active(bool active){
    if(active==impl->active.load())return;
    Beat();
    if(active)Write(impl->root+"/session-active","active\n");
    impl->active=active;
}
void FreezeWatch::Transition(const std::string &stage){
    Beat(3);impl->active=true;
    Write(impl->root+"/session-active","transition "+Redact(stage).substr(0,384)+"\n");
}
void FreezeWatch::CleanExit(){
    impl->active=false;impl->stop=true;impl->wake.notify_all();
    if(impl->worker.joinable())impl->worker.join();
    std::remove((impl->root+"/session-active").c_str());
}
bool FreezeWatch::Pending()const{return impl->pending;}
void FreezeWatch::Acknowledge(){impl->pending=false;std::remove((impl->root+"/freeze-pending").c_str());}

struct Client::Impl {
    std::string root,report;std::mutex mutex;std::condition_variable wake;
    Snapshot state;std::deque<std::string> events;std::thread worker;
    std::atomic<bool> stop{false}; int job=0;std::size_t games=0,libraries=0,media=0;
    bool dirty=false;std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    explicit Impl(std::string r):root(std::move(r)) { worker=std::thread([this]{Run();}); }
    ~Impl(){stop=true;wake.notify_all();if(worker.joinable())worker.join();}
    std::string Events(){std::string out;for(const auto &e:events)out+=e;return out;}
    void Run() {
        while(!stop) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock,std::chrono::seconds(2),[&]{return stop || job;});
            if(stop)break;
            const int task=job;job=0;const auto recent=Events();const auto g=games,l=libraries,m=media;
            const bool flush=dirty;dirty=false;lock.unlock();
            try {
                if(flush && !Write(root+"/support-session.log",recent)) {
                    std::lock_guard guard(mutex);state.recording=false;state.message="Recording stopped: SD write failed.";
                }
                if(task==1) {
                    std::string body="{\"schema\":1,\"build\":"+std::to_string(UFlowUpdate::BuildSequence)+
                        ",\"redacted\":true,\"games\":"+std::to_string(g)+",\"libraries\":"+std::to_string(l)+
                        ",\"media\":"+std::to_string(m)+",\"settings\":"+Quote(NumericSettings(root)+Inventory(root))+
                        ",\"events\":"+Quote(Tail(root+"/freeze-report.log")+recent)+",\"logs\":{";
                    bool first=true;
                    for(const char *name:{"uflow.log","installed-scan.log","usb-storage.log","wii-launch.log","support-session.log","handoff.log"}) {
                        if(!first)body+=',';
                        first=false;body+=Quote(name)+":"+Quote(Tail(root+"/"+name));
                    }
                    for(const char *name:{"content_sources.cfg","media_sources.cfg","libraries.cfg"})
                        body+=","+Quote(name)+":"+Quote(Configuration(root,name));
                    body+="}}";
                    if(body.size()>262144 || !Write(root+"/support-report.json",body))throw std::runtime_error("Cannot save report. Check SD free space.");
                    report=std::move(body);
                    {std::lock_guard guard(mutex);state.saved=true;state.message="Saved: SD:/wiiu/apps/uFlow/support-report.json";}
                }
            } catch(const std::exception &e){std::lock_guard guard(mutex);state.message=state.saved?"Saved: SD:/wiiu/apps/uFlow/support-report.json":e.what();}
              catch(...){std::lock_guard guard(mutex);state.message=state.saved?"Saved: SD:/wiiu/apps/uFlow/support-report.json":"Report operation failed. Try again.";}
            if(task){std::lock_guard guard(mutex);state.busy=false;}
        }
    }
};
Client::Client(std::string root):impl(new Impl(std::move(root))){}
Client::~Client()=default;
Snapshot Client::Poll(){std::lock_guard lock(impl->mutex);return impl->state;}
void Client::Event(const std::string &event){std::lock_guard lock(impl->mutex);if(!impl->state.recording)return;
    const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-impl->started).count();
    impl->events.push_back(std::to_string(seconds)+"s "+Redact(event.substr(0,512)));
    while(impl->events.size()>64)impl->events.pop_front();
    impl->dirty=true;
}
void Client::Record(bool enabled){std::lock_guard lock(impl->mutex);impl->state.recording=enabled;
    impl->state.message=enabled?"Recording app status. Reproduce the issue, then send a report.":"Recording stopped. Saved log is kept on SD.";}
void Client::Send(std::size_t g,std::size_t l,std::size_t m){std::lock_guard lock(impl->mutex);if(impl->state.busy)return;
    impl->games=g;impl->libraries=l;impl->media=m;impl->job=1;impl->state.busy=true;impl->state.saved=false;impl->state.reportId.clear();impl->state.message="Preparing report...";impl->wake.notify_all();}
}
