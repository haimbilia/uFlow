#include <cassert>
#include <cstdint>
#include <string>
using FSStatus=int;using FSFileHandle=int;using FSErrorFlag=int;using FSAFilePosition=uint32_t;
struct FSClient{};struct FSCmdBlock{};struct FSStat{int size;};
using OSTime=uint64_t;
uint64_t OSGetTime(){return 123;}
#define DECL_FUNCTION(ret,name,...) ret name(__VA_ARGS__)
FSClient c;FSCmdBlock b;FSStat stat{41};int wanted=0,calls=0,traces=0;const char *path="/vol/content/Model/a";
void PerformanceOperation(const char *kind,OSTime t,int result,int handle=0,const char *p=""){
 assert(t==123 && result==wanted);++traces;
 if(std::string(kind)=="stat-path")assert(p==path && handle==0);else assert(handle==91);
}
int real_Raw_FSGetStat(FSClient *cp,FSCmdBlock *bp,const char *p,FSStat *s,int mask){assert(cp==&c&&bp==&b&&p==path&&s==&stat&&mask==7);++calls;return wanted;}
int real_Raw_FSCloseFile(FSClient *cp,FSCmdBlock *bp,int h,int mask){assert(cp==&c&&bp==&b&&h==91&&mask==7);++calls;return wanted;}
int real_Raw_FSSetPosFile(FSClient *cp,FSCmdBlock *bp,int h,uint32_t pos,int mask){assert(pos==0xfffffff0);return real_Raw_FSCloseFile(cp,bp,h,mask);}
// PRODUCTION_STALL_HOOKS
int main(){for(int value:{0,-1,-17}){wanted=value;assert(Raw_FSGetStat(&c,&b,path,&stat,7)==value);assert(Raw_FSCloseFile(&c,&b,91,7)==value);assert(Raw_FSSetPosFile(&c,&b,91,0xfffffff0,7)==value);}assert(calls==9&&traces==9&&stat.size==41);}
