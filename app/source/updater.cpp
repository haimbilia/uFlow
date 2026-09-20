#include "updater.hpp"
#include "update_transaction.hpp"
#include "update_trust.hpp"
#include <curl/curl.h>
#include <atomic>
#include <mutex>
#include <thread>
namespace UFlowUpdate {
struct Client::Impl {
    std::mutex lock;Snapshot state;Release release;std::string envelope;
    std::thread worker;std::atomic<bool> cancel{false};
    const std::string sd="fs:/vol/external01",channel="stable";
    void Set(State s,const std::string &message="") {std::lock_guard<std::mutex> g(lock);state.state=s;state.message=message;}
    void Progress(uint64_t done,uint64_t total){std::lock_guard<std::mutex> g(lock);state.done=done;state.total=total;}
    void Join(){if(worker.joinable())worker.join();}
    struct Transfer {Impl *self;FILE *file=nullptr;std::string text;uint64_t size=0,limit=0;};
    static size_t Write(char *data,size_t size,size_t count,void *user){
        auto &t=*static_cast<Transfer*>(user);const auto n=size*count;
        if(t.self->cancel||n>t.limit-t.size)return 0;
        if(t.file){if(std::fwrite(data,1,n,t.file)!=n)return 0;}else t.text.append(data,n);
        t.size+=n;if(t.file)t.self->Progress(t.size,t.limit);return n;
    }
    static int Tick(void *user,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Impl*>(user)->cancel?1:0;}
    std::string Fetch(const std::string &relative,uint64_t limit,const std::string &path=""){
        CURL *curl=curl_easy_init();Setup::Need(curl!=nullptr,"Network setup failed");
        Transfer t{this,nullptr,{},0,limit};
        if(!path.empty())t.file=std::fopen(path.c_str(),"wb");
        if(!path.empty()&&!t.file){curl_easy_cleanup(curl);throw std::runtime_error("Cannot save download to SD");}
        const auto url=std::string(BaseURL)+relative;
        curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
        curl_easy_setopt(curl,CURLOPT_CAINFO,"fs:/vol/content/updates/cacert.pem");
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,path.empty()?20L:900L);
        curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,30L);
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_USERAGENT,"uFlow-Updater/1");
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Write);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&t);
        curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Tick);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,this);curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
        const auto result=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
        curl_easy_cleanup(curl);bool flushed=true;
        if(t.file){flushed=std::fflush(t.file)==0;if(flushed&&fsync(fileno(t.file))!=0&&errno!=ENOSYS&&errno!=ENOTSUP)flushed=false;if(std::fclose(t.file)!=0)flushed=false;}
        Setup::Need(!cancel,"Update cancelled");Setup::Need(flushed,"SD write failed");
        Setup::Need(result==CURLE_OK,"Download failed: "+std::string(curl_easy_strerror(result)));
        Setup::Need(status==200,"Update server returned HTTP "+std::to_string(status));
        if(!path.empty())Setup::Need(t.size==limit,"Incomplete download");return t.text;
    }
    void Check(){
        Join();cancel=false;Set(State::Checking);
        worker=std::thread([this]{try {
            Transaction tx(sd,PublicKey,channel);bool recovered=tx.Recover();
            uint64_t current=BuildSequence;auto installed=ReadSmall(tx.receipt);
            if(!installed.empty()){auto r=Parse(installed,PublicKey,channel);current=std::max(current,r.sequence);
                if(recovered&&ReadSmall(tx.receipt+".shown")!=std::to_string(r.sequence)){release=r;{std::lock_guard<std::mutex> g(lock);state.version=r.version;state.notes=r.notes;}Set(State::Success);return;}}
            envelope=Fetch(channel+".json",32768);release=Parse(envelope,PublicKey,channel);
            if(release.sequence<=current){Set(State::Idle);return;}
            {std::lock_guard<std::mutex> g(lock);state.version=release.version;state.notes=release.notes;state.done=0;state.total=release.size;}
            Set(State::Available);
        }catch(const std::exception &e){std::fprintf(stderr,"uFlow update check: %s\n",e.what());Set(State::Idle);}});
    }
    void Install(){
        Join();cancel=false;Set(State::Downloading,"Downloading update...");
        worker=std::thread([this]{try{
            Transaction tx(sd,PublicKey,channel);tx.Recover();Fetch(release.url,release.size,tx.stage);
            Setup::Need(!cancel,"Update cancelled");Set(State::Installing,"Verifying and installing...");
            const std::string source="fs:/vol/content/updates/install-helper.rpx";
            Setup::Need(Setup::Size(source)>0&&Setup::Size(source)<8*1024*1024,"Update helper is missing");
            {std::ifstream input(source,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});Setup::Need(input.eof()||bool(input),"Cannot read update helper");Durable(HelperPath,bytes);}
            Setup::Need(tx.Hash(source)==tx.Hash(HelperPath),"Update helper failed verification");
            tx.Prepare(envelope);Set(State::Handoff,"Opening update installer...");
        }catch(const std::exception &e){Set(State::Error,e.what());}});
    }
};
Client::Client():impl(std::make_unique<Impl>()){curl_global_init(CURL_GLOBAL_DEFAULT);}
Client::~Client(){Stop();}
Snapshot Client::Poll(){std::lock_guard<std::mutex> g(impl->lock);return impl->state;}
void Client::Check(){if(Poll().state==State::Idle)impl->Check();}
void Client::Accept(){if(Poll().state==State::Available)impl->Install();}
void Client::Dismiss(){auto s=Poll().state;
    if(s==State::Success){impl->Join();try{Transaction tx(impl->sd,PublicKey,impl->channel);Durable(tx.receipt+".shown",std::to_string(impl->release.sequence));}catch(const std::exception &e){std::fprintf(stderr,"uFlow update acknowledgement: %s\n",e.what());}}
    if(s==State::Available||s==State::Success||s==State::Error)impl->Set(State::Idle);
}
void Client::Stop(){impl->cancel=true;impl->Join();}
void Client::HandoffFailed(const std::string &message){impl->Join();impl->Set(State::Error,message);}
}
