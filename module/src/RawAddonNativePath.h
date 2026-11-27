#pragma once
#include "RawNativePath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <fstream>
#ifdef __WIIU__
#include <coreinit/filesystem_fsa.h>
#endif

// Built before game execution. Lookup never performs I/O, including when
// called from an FS async completion thread with a small stack.
class RawUpdateFileIndex {
    std::vector<std::string> files;
    static std::string Key(std::string value) {
        for (char &c : value) if(c >= 'A' && c <= 'Z') c += 'a'-'A';
        return value;
    }
public:
    void Clear() { std::vector<std::string>().swap(files); }
    size_t Size() const { return files.size(); }
    bool Contains(const std::string &path) const {
        return std::binary_search(files.begin(),files.end(),Key(path));
    }
    bool Save(const std::string &path,const std::string &root) const {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out << "UFLOW-UPDATE-INDEX-1\n" << Key(root) << "\n" << files.size() << "\n";
        for(const auto &file:files) out << file << "\n";
        out.flush(); const bool ok=bool(out); out.close(); return ok && bool(out);
    }
    bool Load(const std::string &path,const std::string &root) {
        Clear();
        std::ifstream in(path,std::ios::binary);
        std::string line;
        if(!std::getline(in,line) || line!="UFLOW-UPDATE-INDEX-1" ||
           !std::getline(in,line) || line!=Key(root) || !std::getline(in,line)) return false;
        unsigned count=0;
        if(line.empty() || line.size()>6) return false;
        for(char c:line) { if(c<'0'||c>'9') return false; count=count*10+(c-'0'); }
        if(count>100000) return false;
        const auto prefix=Key(root)+"/";
        std::vector<std::string> loaded; loaded.reserve(count);
        for(unsigned i=0;i<count;++i) {
            if(!std::getline(in,line) || line.size()>1024 || line.size()<=prefix.size() ||
               line.compare(0,prefix.size(),prefix)!=0 || line!=Key(line)) return false;
            loaded.push_back(line);
        }
        if(std::getline(in,line) || !in.eof() || !std::is_sorted(loaded.begin(),loaded.end())) return false;
        files.swap(loaded); return true;
    }
#ifdef __WIIU__
    bool BuildNative(const std::string &root,const std::string &relativeRoot) {
        Clear();
        const auto client=FSAAddClient(nullptr);
        if(client<0) return false;
        std::vector<std::string> pending{""};
        bool ok=true;
        while(ok && !pending.empty()) {
            const auto suffix=pending.back(); pending.pop_back();
            FSADirectoryHandle dir;
            if(FSAOpenDir(client,(root+suffix).c_str(),&dir)<0) { ok=false; break; }
            for(;;) {
                FSADirectoryEntry entry{};
                const auto result=FSAReadDir(client,dir,&entry);
                if(result==FS_ERROR_END_OF_DIR) break;
                if(result<0) { ok=false; break; }
                if(!strcmp(entry.name,".") || !strcmp(entry.name,"..")) continue;
                const auto child=suffix+"/"+entry.name;
                if(entry.info.flags & FS_STAT_DIRECTORY) pending.push_back(child);
                else files.push_back(Key(relativeRoot+child));
            }
            if(FSACloseDir(client,dir)<0) ok=false;
        }
        FSADelClient(client);
        if(!ok) { Clear(); return false; }
        std::sort(files.begin(),files.end()); return true;
    }
#endif
    bool Build(const std::string &absoluteRoot,const std::string &relativeRoot) {
#ifdef __WIIU__
        if(absoluteRoot.compare(0,4,"fs:/")==0)
            return BuildNative(absoluteRoot.substr(3),relativeRoot);
#endif
        Clear();
        std::vector<std::string> pending{""};
        while(!pending.empty()) {
            const auto suffix=pending.back(); pending.pop_back();
            DIR *dir=opendir((absoluteRoot+suffix).c_str());
            if(!dir) { Clear(); return false; }
            bool ok=true;
            for(;;) {
                errno=0; auto *entry=readdir(dir);
                if(!entry) {
#ifdef __WIIU__
                    if(errno && errno!=ENOENT) ok=false;
#else
                    if(errno) ok=false;
#endif
                    break;
                }
                if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
                const auto child=suffix+"/"+entry->d_name;
                struct stat info{};
                if(stat((absoluteRoot+child).c_str(),&info)) { ok=false; break; }
                if(S_ISDIR(info.st_mode)) pending.push_back(child);
                else if(S_ISREG(info.st_mode)) files.push_back(Key(relativeRoot+child));
            }
            closedir(dir);
            if(!ok) { Clear(); return false; }
        }
        std::sort(files.begin(),files.end());
        return true;
    }
};

template<class Info, class Exists>
inline bool RawAddonNativeOpenPath(char *out,size_t capacity,const Info &info,
                                  const char *path,const char *mode,Exists exists) {
    if(!path || !mode)return false;
    if(info.active && (!strcmp(mode,"r") || !strcmp(mode,"rb"))) {
        if(info.updateContentPath[0] && !strncmp(path,"/vol/content/",13)) {
            const std::string candidate=std::string(info.updateContentPath)+"/"+(path+13);
            if(exists(candidate))return RawNativeReadPath(out,capacity,true,path,mode,info.updateContentPath,info.codePath);
        }
        const auto n=strlen(info.dlcVolume);
        if(n && !strncmp(path,info.dlcVolume,n) && path[n]=='/') {
            const std::string content="/vol/content/"+std::string(path+n+1);
            return RawNativeReadPath(out,capacity,true,content.c_str(),mode,info.dlcPath,info.codePath);
        }
    }
    return RawNativeOpenPath(out,capacity,info.active,path,mode,info.contentPath,info.codePath,info.savePath);
}
