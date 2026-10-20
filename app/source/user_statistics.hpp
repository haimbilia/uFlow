#pragma once
#include "library_sort.hpp"
namespace UserStatistics {
struct Entry {uint64_t launches=0;unsigned rating=0;};
class Store {
    std::map<std::string,Entry> entries_;
    std::string path_;
public:
    void Load(const std::string &path){path_=path;entries_.clear();auto in=SavedFile::Open(path);std::string key;Entry value;while(in>>std::quoted(key)>>value.launches>>value.rating){value.rating=std::min(value.rating,5u);entries_[key]=value;}}
    Entry Get(const GameEntry &game)const{auto it=entries_.find(LibrarySort::Key(game));return it==entries_.end()?Entry{}:it->second;}
    bool Save(){const auto temp=path_+".tmp";std::ofstream out(temp);for(const auto &[key,e]:entries_)out<<std::quoted(key)<<' '<<e.launches<<' '<<e.rating<<'\n';out.close();return bool(out)&&SavedFile::Commit(temp,path_);}
    bool Launch(const GameEntry &game){auto &e=entries_[LibrarySort::Key(game)];if(e.launches<UINT64_MAX)++e.launches;return Save();}
    bool Rate(const GameEntry &game){auto &e=entries_[LibrarySort::Key(game)];e.rating=(e.rating+1)%6;return Save();}
};
}
