#pragma once
#include "update_protocol.hpp"
#include <cerrno>
#include <unistd.h>
namespace UFlowUpdate {
inline void Durable(const std::string &path,const std::string &text){
    FILE *f=std::fopen(path.c_str(),"wb");Setup::Need(f!=nullptr,"Cannot write update state");
    bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size()&&std::fflush(f)==0;
    if(ok&&fsync(fileno(f))!=0&&errno!=ENOSYS&&errno!=ENOTSUP)ok=false;
    if(std::fclose(f)!=0)ok=false;Setup::Need(ok,"Cannot flush update state");
}
inline std::string ReadSmall(const std::string &path){
    if(!Setup::File(path))return {};
    Setup::Need(Setup::Size(path)<=33000,"Invalid update state size");
    std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});
}
struct Transaction {
    std::string root,target,recovery,stage,journal,receipt;
    const uint8_t *key;std::string channel;
    Transaction(const std::string &sd,const uint8_t *pk,const std::string &ch):root(sd),key(pk),channel(ch){
        Setup::Directory(root,"wiiu/apps/uFlow/cache/update");
        target=Setup::Path(root,"wiiu/apps/uFlow/uFlow.wuhb");
        recovery=Setup::Path(root,"wiiu/apps/uFlow-Recovery.wuhb");
        stage=Setup::Path(root,"wiiu/apps/uFlow/cache/update/download.wuhb");
        journal=Setup::Path(root,"wiiu/apps/uFlow/cache/update/pending");
        receipt=Setup::Path(root,"wiiu/apps/uFlow/cache/update/installed");
    }
    std::string Hash(const std::string &path){return Setup::HashFile(path,{},"");}
    void Move(const std::string &a,const std::string &b){Setup::Need(std::rename(a.c_str(),b.c_str())==0,"Cannot replace uFlow file");}
    void Remove(const std::string &p){Setup::Need(std::remove(p.c_str())==0||errno==ENOENT,"Cannot finish update cleanup");}
    void Record(const std::string &envelope){Durable(receipt+".tmp",envelope);Remove(receipt);Move(receipt+".tmp",receipt);}
    bool Recover(){
        if(!Setup::File(journal))return false;
        const auto saved=ReadSmall(journal);Setup::Need(saved.size()>65&&saved[64]=='\n',"Invalid update journal");
        const auto old=saved.substr(0,64),body=saved.substr(65);auto release=Parse(body,key,channel);
        if(Setup::File(target)){
            auto hash=Hash(target);
            if(hash==release.sha){Record(body);Remove(recovery);Remove(stage);Remove(journal);return true;}
            Setup::Need(hash==old,"uFlow changed outside the updater; recovery retained");
        }else{
            Setup::Need(Setup::File(recovery)&&Hash(recovery)==old,"Recovery file is unavailable");Move(recovery,target);
        }
        Remove(stage);Remove(journal);return false;
    }
    void Commit(const std::string &envelope,const std::function<void(int)> &checkpoint={}){
        auto release=Parse(envelope,key,channel);
        Setup::Need(Setup::Size(stage)==release.size&&Hash(stage)==release.sha,"Downloaded update failed verification");
        Setup::Need(!Setup::Exists(recovery)&&!Setup::Exists(journal),"An earlier update needs recovery");
        const auto old=Hash(target);Durable(journal,old+"\n"+envelope);if(checkpoint)checkpoint(1);
        Move(target,recovery);if(checkpoint)checkpoint(2);
        try {Move(stage,target);}catch(...){Move(recovery,target);throw;}
        if(checkpoint)checkpoint(3);
        Setup::Need(Hash(target)==release.sha,"Installed update failed verification");
        Record(envelope);if(checkpoint)checkpoint(4);
        // Retain a runnable recovery WUHB until the next startup confirms commit.
    }
};
}
