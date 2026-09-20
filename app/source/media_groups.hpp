#pragma once
#include "library.hpp"
#include <algorithm>
#include <map>
#include <set>

inline std::string MediaGroupKey(const MediaSource &source) {
    if(!source.libraryId.empty()) return source.libraryId;
    if(source.library>=0 && source.library<=static_cast<int>(Platform::Files))
        return DefaultLibraryId(static_cast<Platform>(source.library));
    return {}; // Shared directories have their own All Libraries group.
}

struct MediaDirectoryRow {
    enum Kind { Header, Directory, Add, Scan, Display, Help } kind=Header;
    std::string key, label;
    int source=-1, directories=0;
    bool expanded=false;
};

inline std::vector<MediaDirectoryRow> MediaDirectoryRows(const std::vector<MediaSource> &sources,
        const std::vector<LibraryDefinition> &libraries, const std::set<std::string> &expanded) {
    std::map<std::string,std::vector<int>> groups;
    std::vector<std::string> order;
    for(int i=0;i<static_cast<int>(sources.size());++i) {
        if(sources[i].path.empty()) continue;
        const auto key=MediaGroupKey(sources[i]);
        if(!groups.count(key)) order.push_back(key);
        groups[key].push_back(i);
    }
    std::vector<MediaDirectoryRow> rows;
    const auto append=[&](const std::string &key,const std::string &label) {
        const auto found=groups.find(key);
        if(found==groups.end()) return;
        const bool open=expanded.count(key)!=0;
        rows.push_back({MediaDirectoryRow::Header,key,label,-1,static_cast<int>(found->second.size()),open});
        if(open) for(int index:found->second)
            rows.push_back({MediaDirectoryRow::Directory,key,{},index});
        groups.erase(found);
    };
    append({},"ALL LIBRARIES");
    for(const auto &library:libraries) append(library.id,library.name);
    // Keep saved directories accessible even if a content-type definition is absent.
    for(const auto &key:order) {
        std::string label=key;
        for(int p=0;p<=static_cast<int>(Platform::Files);++p)
            if(key==DefaultLibraryId(static_cast<Platform>(p))) label=MediaLibraryName(p);
        append(key,label);
    }
    for(auto kind:{MediaDirectoryRow::Add,MediaDirectoryRow::Scan,MediaDirectoryRow::Display,MediaDirectoryRow::Help})
        rows.push_back({kind,{},{}});
    return rows;
}
