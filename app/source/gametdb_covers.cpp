#include "gametdb_covers.hpp"
#include "gametdb_database.hpp"
#include "console_language.hpp"
#include <curl/curl.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#ifndef UFLOW_GAMETDB_CA
#define UFLOW_GAMETDB_CA "fs:/vol/content/updates/cacert.pem"
#endif

namespace GameTdbCovers {
namespace {
constexpr const char *kMediaRoot="fs:/vol/external01/wiiu/apps/uFlow/media";
constexpr std::size_t kMaxImage=8*1024*1024,kMaxDatabase=32*1024*1024;
bool Exists(const std::string &path){struct stat info{};return stat(path.c_str(),&info)==0;}
void MakeDirectories(const std::string &path){
    const auto root=path.find(":/");
    for(auto slash=path.find('/',root==std::string::npos?1:root+2);slash!=std::string::npos;slash=path.find('/',slash+1))
        mkdir(path.substr(0,slash).c_str(),0777);
    mkdir(path.c_str(),0777);
}
std::string Lower(std::string value){for(auto &c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
bool HasFile(const std::string &folder,const std::string &stem,std::initializer_list<const char*> extensions){
    for(const char *ext:extensions)if(Exists(folder+"/"+stem+ext))return true;
    return false;
}
}

struct Downloader::Impl {
    struct Job { GameEntry game; std::string id,stem; GameTdb::System system=GameTdb::System::Wii; };
    mutable std::mutex lock; Snapshot state; bool newArtwork=false;
    std::thread worker; std::atomic<bool> cancel{false};
    void Join(){if(worker.joinable())worker.join();}
    template<class F> void Update(F f){std::lock_guard<std::mutex> g(lock);f(state);}

    struct Body { std::string bytes; std::size_t limit; Impl *self; };
    static size_t Write(char *data,size_t size,size_t count,void *user){
        auto &body=*static_cast<Body*>(user);const auto n=size*count;
        if(body.self->cancel || n>body.limit-body.bytes.size())return 0;
        body.bytes.append(data,n);return n;
    }
    static int Tick(void *user,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Impl*>(user)->cancel?1:0;}

    static bool Transient(CURLcode result){
        switch(result){
        case CURLE_COULDNT_RESOLVE_HOST:case CURLE_COULDNT_CONNECT:case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:case CURLE_RECV_ERROR:case CURLE_PARTIAL_FILE:case CURLE_GOT_NOTHING:
        case CURLE_HTTP2:case CURLE_HTTP2_STREAM:case CURLE_SSL_CONNECT_ERROR:return true;
        default:return false;
        }
    }
    // Transport success -> true with the body only on HTTP 200 (404 leaves it empty).
    // Slow DNS or a dropped connection gets one more attempt after a short pause.
    bool Fetch(CURL *curl,const std::string &url,std::size_t limit,std::string &bytes,std::string &error){
        curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
        for(int attempt=0;;++attempt){
            Body body{{},limit,this};
            curl_easy_setopt(curl,CURLOPT_WRITEDATA,&body);
            const auto result=curl_easy_perform(curl);long status=0;
            curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
            if(cancel)return false;
            if(result==CURLE_OK){if(status==200)bytes=std::move(body.bytes);return true;}
            if(attempt>=1 || !Transient(result)){error=curl_easy_strerror(result);return false;}
            for(int i=0;i<20 && !cancel;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(cancel)return false;
        }
    }
    static bool Save(const std::string &path,const std::string &bytes){
        const auto part=path+".part";
        FILE *file=std::fopen(part.c_str(),"wb");if(!file)return false;
        bool ok=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size() && std::fflush(file)==0;
        if(ok && fsync(fileno(file))!=0 && errno!=ENOSYS && errno!=ENOTSUP)ok=false;
        if(std::fclose(file)!=0)ok=false;
        if(ok && std::rename(part.c_str(),path.c_str())!=0)ok=false;
        if(!ok)std::remove(part.c_str());
        return ok;
    }

    // One database download per system; writes <titleId>.json for games without metadata.
    void DownloadMetadata(CURL *curl,std::vector<Job> &jobs,const std::string &folder,std::string &fatal){
        const auto locale=GameTdb::LocaleFor(ConsoleLanguage::Tags[ConsoleLanguage::Current()]);
        for(auto system:{GameTdb::System::WiiU,GameTdb::System::Wii}){
            if(cancel || !fatal.empty())return;
            std::set<std::string> wanted;
            for(const auto &job:jobs)
                if(job.system==system && !job.id.empty() && !HasFile(folder,job.stem,{".json",".ini"}))wanted.insert(job.id);
            if(wanted.empty())continue;
            Update([&](Snapshot &s){s.current=system==GameTdb::System::WiiU?"wiiutdb.xml":"wiitdb.xml";});
            std::string zip,error;
            if(!Fetch(curl,GameTdb::DatabaseUrl(system),kMaxDatabase,zip,error)){if(!cancel)fatal=error;return;}
            if(zip.empty()){Update([](Snapshot &s){s.databaseError=true;});continue;}
            std::map<std::string,GameTdb::Metadata> found;
            if(!GameTdb::ParseZippedDatabase(zip,wanted,locale,found,[this]{return bool(cancel);})){
                if(!cancel)Update([](Snapshot &s){s.databaseError=true;});
                continue;
            }
            zip.clear();zip.shrink_to_fit();
            std::size_t saved=0;
            for(const auto &job:jobs){
                if(job.system!=system || !wanted.count(job.id))continue;
                const auto entry=found.find(job.id);
                if(entry==found.end())continue;
                if(Save(folder+"/"+job.stem+".json",GameTdb::MetadataJson(entry->second)))++saved;
            }
            Update([&](Snapshot &s){s.metadata+=saved;if(saved)newArtwork=true;});
        }
    }

    void Run(std::vector<Job> jobs,Targets targets){
        CURL *curl=curl_easy_init();
        if(!curl){Update([](Snapshot &s){s.running=false;s.finished=true;s.error="NETWORK SETUP FAILED";});return;}
        curl_easy_setopt(curl,CURLOPT_CAINFO,UFLOW_GAMETDB_CA);
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
        // The console is IPv4-only and GameTDB has no AAAA record; skipping the
        // IPv6 lookup avoids resolvers that stall on it.
        curl_easy_setopt(curl,CURLOPT_IPRESOLVE,CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,300L);
        curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,512L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,30L);
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_USERAGENT,"uFlow-GameTDB/1");
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Write);
        curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Tick);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,this);
        curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
        MakeDirectories(targets.front);MakeDirectories(targets.full);MakeDirectories(targets.metadata);

        // Resolve IDs first (reads Wii U meta.xml); both phases share them.
        Update([](Snapshot &s){s.current="Reading title information";});
        for(auto &job:jobs){
            if(cancel)break;
            job.id=GameTdb::Upper(GameTdbId(job.game));
            job.stem=Lower(job.game.titleId);
            job.system=job.game.platform==Platform::WiiU?GameTdb::System::WiiU:GameTdb::System::Wii;
        }
        std::string fatal;
        if(!cancel)DownloadMetadata(curl,jobs,targets.metadata,fatal);

        // A transport error fails that game only; several games in a row means
        // the network is gone, so stop instead of timing out on every title.
        constexpr int kMaxConsecutiveErrors=3;
        int consecutiveErrors=0;std::string lastError;
        for(const auto &job:jobs){
            if(cancel || !fatal.empty())break;
            Update([&](Snapshot &s){s.current=job.game.name;});
            bool saved=false,missing=true,failed=false,skipped=true,networkError=false;
            if(!job.id.empty() && !job.stem.empty()) {
                for(auto [art,folder]:{std::pair{GameTdb::Art::Front,targets.front},std::pair{GameTdb::Art::Full,targets.full}}){
                    if(cancel || networkError)break;
                    // A cover already present in any format is never replaced.
                    if(HasFile(folder,job.stem,{".png",".jpg",".jpeg",".tga"}))continue;
                    skipped=false;
                    for(const auto &url:GameTdb::Urls(job.system,art,job.id)){
                        std::string bytes,error;
                        if(!Fetch(curl,url,kMaxImage,bytes,error)){
                            if(!cancel){networkError=true;lastError=error.empty()?"DOWNLOAD FAILED":error;}
                            break;
                        }
                        const char *extension=GameTdb::ImageExtension(bytes);
                        if(!extension)continue; // 404 or not an image: try the next region
                        missing=false;
                        if(Save(folder+"/"+job.stem+"."+extension,bytes))saved=true;
                        else failed=true;
                        break;
                    }
                }
            }
            if(cancel)break;
            if(networkError){
                failed=true;
                if(++consecutiveErrors>=kMaxConsecutiveErrors)fatal=lastError;
            } else if(!skipped)consecutiveErrors=0;
            Update([&](Snapshot &s){
                ++s.done;
                if(saved)++s.saved;
                if(failed)++s.failed;
                else if(skipped && !job.id.empty())++s.skipped;
                else if(!saved && missing)++s.missing;
                if(saved)newArtwork=true;
            });
        }
        curl_easy_cleanup(curl);
        Update([&](Snapshot &s){s.running=false;s.finished=true;s.cancelled=cancel;s.current.clear();
            if(!fatal.empty())s.error="NETWORK ERROR: "+GameTdb::Upper(fatal);});
    }
};

Downloader::Downloader():impl(std::make_unique<Impl>()){curl_global_init(CURL_GLOBAL_DEFAULT);}
Downloader::~Downloader(){Cancel();}
bool Downloader::Start(const std::vector<GameEntry> &games,const Targets &targets){
    if(Poll().running)return false;
    impl->Join();impl->cancel=false;
    std::vector<Impl::Job> jobs;std::set<std::string> seen;
    for(const auto &source:games){
        if(source.titleId.empty() || !source.installPackages.empty() || source.platform==Platform::Files)continue;
        if(!seen.insert(Lower(source.titleId)).second)continue;
        Impl::Job job;auto &g=job.game;
        g.platform=source.platform;g.titleId=source.titleId;g.name=source.name;
        g.absolutePath=source.absolutePath;g.vcSystem=source.vcSystem;g.installed=source.installed;
        jobs.push_back(std::move(job));
    }
    {std::lock_guard<std::mutex> g(impl->lock);impl->state={};impl->state.running=true;impl->state.total=jobs.size();impl->newArtwork=false;}
    impl->worker=std::thread([this,jobs=std::move(jobs),targets]() mutable {impl->Run(std::move(jobs),targets);});
    return true;
}
void Downloader::Cancel(){impl->cancel=true;impl->Join();}
Snapshot Downloader::Poll() const {std::lock_guard<std::mutex> g(impl->lock);return impl->state;}
bool Downloader::TakeNewArtwork(){
    std::lock_guard<std::mutex> g(impl->lock);
    if(!impl->state.finished || !impl->newArtwork)return false;
    impl->newArtwork=false;return true;
}

Targets ChooseTargets(const std::vector<MediaSource> &sources){
    Targets t{std::string(kMediaRoot)+"/covers",std::string(kMediaRoot)+"/fullcovers",std::string(kMediaRoot)+"/metadata"};
    bool front=false,full=false,metadata=false;
    for(const auto &s:sources){
        if(!s.enabled || s.path.empty() || s.library!=-1 || !s.libraryId.empty())continue;
        if(s.type==MediaType::Cover && !front){t.front=s.path;front=true;}
        if(s.type==MediaType::FullCover && !full){t.full=s.path;full=true;}
        if(s.type==MediaType::Metadata && !metadata){t.metadata=s.path;metadata=true;}
    }
    return t;
}
}
