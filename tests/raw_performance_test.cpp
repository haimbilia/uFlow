#include "RawPerformanceTrace.h"
#include "RawReadRange.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
using FSStatus=int;using FSFileHandle=int;using FSErrorFlag=int;
struct FSClient{};struct FSCmdBlock{};struct FSAsyncData{void *callback,*queue,*context;};
struct {bool active=true;}gLooseTitleInfo;
RawPerformanceTrace sRawPerformance;
uint64_t OSGetTime(){static uint64_t t=0;return t+=20000;}
uint64_t OSTicksToMicroseconds(uint64_t t){return t;}
uint32_t PerformanceMs(){return 100;}
#define DECL_FUNCTION(ret,name,...) ret name(__VA_ARGS__)
FSClient c;FSCmdBlock b;uint8_t data[8]={};FSAsyncData a{&c,&b,data};int calls=0,expected=-17;
int real_Raw_FSReadFileAsync(FSClient *cp,FSCmdBlock *bp,uint8_t *p,uint32_t size,uint32_t count,int h,uint32_t flags,int mask,FSAsyncData *async){
 assert(cp==&c&&bp==&b&&p==data&&size==2&&count==4&&h==9&&flags==3&&mask==7&&async==&a);
 ++calls;return expected;
}
int real_Raw_FSReadFileWithPosAsync(FSClient *cp,FSCmdBlock *bp,uint8_t *p,uint32_t size,uint32_t count,uint32_t pos,int h,uint32_t flags,int mask,FSAsyncData *async){assert(pos==123);return real_Raw_FSReadFileAsync(cp,bp,p,size,count,h,flags,mask,async);}
// PRODUCTION_READ_HOOKS
int main(){
 const char *path="/tmp/raw-performance-test.log";std::remove(path);
 sRawPerformance.Begin(true);
 assert(Raw_FSReadFileAsync(&c,&b,data,2,4,9,3,7,&a)==expected);
 expected=0;assert(Raw_FSReadFileWithPosAsync(&c,&b,data,2,4,123,9,3,7,&a)==0);
 assert(calls==2&&sRawPerformance.asyncSubmits==2&&a.callback==&c&&a.queue==&b&&a.context==data&&data[0]==0);
 gLooseTitleInfo.active=false;Raw_FSReadFileAsync(&c,&b,data,2,4,9,3,7,&a);assert(sRawPerformance.asyncSubmits==2);
 assert(!std::ifstream(path));
 sRawPerformance.Record("frame-gap",42,123456,0,0,0);
 sRawPerformance.frameCalls=12;
 for(int i=0;i<2050;++i)sRawPerformance.Record("test",i,1,9,0,8,"update/file");
 sRawPerformance.StopAndWrite(path);
 std::ifstream f(path);std::string log{std::istreambuf_iterator<char>(f),{}};
 assert(log.find("overwritten=5")!=std::string::npos&&log.find("END raw-performance-v1")!=std::string::npos);
 assert(log.find("FRAME_PROBE calls=12 guard_drops=0; PAUSES retained=1 overwritten=0")!=std::string::npos);
 assert(log.find("PAUSE\t42\tframe-gap\t123456")!=std::string::npos);
 const auto before=log.size();sRawPerformance.StopAndWrite(path);
 std::ifstream again(path);std::string log2{std::istreambuf_iterator<char>(again),{}};assert(log2.size()==before);
 sRawPerformance.Begin(true);assert(sRawPerformance.frameCalls==0);
 for(int i=0;i<260;++i)sRawPerformance.Record("frame-gap",i,50001,0,0,0);
 sRawPerformance.StopAndWrite(path);
 std::ifstream third(path);std::string log3{std::istreambuf_iterator<char>(third),{}};
 assert(log3.find("PAUSES retained=256 overwritten=4")!=std::string::npos);
 std::remove(path);
}
