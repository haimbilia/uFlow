#include <cassert>
#include <memory>
#include <cstdio>
static bool servicesAlive=true,sFTPServerEnabled=true,sMochaPathsWereMounted=false;
static int joined=0,unmounted=0,cleared=0;
struct Server {~Server(){assert(servicesAlive);++joined;}};
static std::unique_ptr<Server> server;
void OSReport(const char *,...){}
void Mocha_UnmountFS(const char *){assert(servicesAlive);++unmounted;}
namespace IOAbstraction {void clear(){++cleared;}}
namespace nn::ac {void Initialize(){} void ConnectAsync(){}}
void initLogging(){} void deinitLogging(){}
void wiiu_init(){if(sFTPServerEnabled){server=std::make_unique<Server>();sMochaPathsWereMounted=true;}}
#define ON_APPLICATION_START() void Start()
#define ON_APPLICATION_REQUESTS_EXIT() void RequestExit()
#define ON_APPLICATION_ENDS() void End()
// PRODUCTION_STOP_SERVER
// PRODUCTION_LIFECYCLE
int main(){
    Start();assert(server);
    RequestExit();assert(!server&&!sMochaPathsWereMounted&&joined==1&&unmounted==8);
    servicesAlive=false;End();End();assert(joined==1&&unmounted==8); // no late join/unmount
    servicesAlive=true;Start();assert(server&&sFTPServerEnabled); // FTP restored on next app
    End();assert(!server&&joined==2&&unmounted==16); // fallback for direct exits
    sFTPServerEnabled=false;Start();assert(!server);RequestExit();End();
    assert(!sFTPServerEnabled&&joined==2&&unmounted==16&&cleared==6);
    puts("PASS: actual FTPiiU lifecycle joins/unmounts before teardown, handles repeated cleanup, restarts normally and preserves disabled setting.");
}
