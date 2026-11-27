#pragma once
#include <cstring>
using FSADirectoryHandle=int;
constexpr int FS_ERROR_END_OF_DIR=-2, FS_STAT_DIRECTORY=0x80000000;
struct FSADirectoryEntry { struct { unsigned flags; } info; char name[256]; };
inline int opened=0,closed=0,deleted=0,reads=0, failAt=0;
inline int FSAAddClient(void*) { return 7; }
inline int FSADelClient(int) { ++deleted; return 0; }
inline int FSAOpenDir(int,const char *p,int *d) { ++opened; *d=std::strstr(p,"/sub")?1:0; return 0; }
inline int FSACloseDir(int,int) { ++closed; return 0; }
inline int rootNext=0,subNext=0;
inline int FSAReadDir(int,int d,FSADirectoryEntry *e) {
 ++reads; if(failAt==reads) return -5;
 int &i=d?subNext:rootNext;
 if(d) { if(i++==0) { std::strcpy(e->name,"Movie.bin"); return 0; } }
 else { if(i++==0) {std::strcpy(e->name,"sub");e->info.flags=FS_STAT_DIRECTORY;return 0;} }
 return FS_ERROR_END_OF_DIR;
}
