#include "vwii.hpp"
#include "vwii_ticket.hpp"
#include "../vendor/compat/installer.h"
#include "../vendor/compat/d2x_patcher.h"
#include "../vendor/compat/StateUtils.h"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/ios.h>
#include <coreinit/systeminfo.h>
#include <mocha/mocha.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>
#include <malloc.h>

FSAClientHandle fsaClient=-1;
namespace {
Setup::Progress callback;
std::vector<Vwii::Title> prepared;
Vwii::Bytes Digest(const Vwii::Bytes &b){Vwii::Bytes out(20);Setup::Need(mbedtls_sha1_ret(b.data(),b.size(),out.data())==0,"SHA-1 failed");return out;}
struct Aligned {void *data;explicit Aligned(size_t n):data(memalign(64,(n+63)&~size_t(63))){Setup::Need(data!=nullptr,"Not enough memory for console I/O");}~Aligned(){free(data);}};
void Check(FSError error,const std::string &what){Setup::Need(error==FS_ERROR_OK,what+" ("+std::to_string(error)+")");}
void Owner(const std::string &path,uint32_t uid,uint32_t gid){
    Aligned a(sizeof(FSARequest)),b(sizeof(FSAResponse));auto *request=static_cast<FSARequest*>(a.data);
    memset(request,0,sizeof(*request));memset(b.data,0,sizeof(FSAResponse));
    Setup::Need(path.size()<FS_MAX_PATH,"Console path too long");strcpy(request->changeOwner.path,path.c_str());request->changeOwner.owner=uid;request->changeOwner.group=gid;
    Setup::Need(IOS_Ioctl(fsaClient,FSA_COMMAND_CHANGE_OWNER,request,sizeof(*request),b.data,sizeof(FSAResponse))==0,"Cannot set vWii ownership: "+path);
}
void WriteScope(const std::string &path){
    Vwii::CheckNandPath(path);
    if(path==Vwii::UidPath||path==Vwii::StagePath(Vwii::UidPath)||path==Vwii::PreviousPath(Vwii::UidPath)||
       path==Vwii::ParentPath(Vwii::StagePath(Vwii::UidPath))||path==Vwii::ParentPath(Vwii::PreviousPath(Vwii::UidPath)))return;
    for(uint64_t id:{Vwii::HBC,Vwii::WiiFlow,0x1000000f8ULL,0x1000000f9ULL,0x1000000faULL,0x1000000fbULL}){
        auto root=Vwii::TitlePath(id),ticket=Vwii::TicketPath(id);
        if(path==root||path==root+"/data"||path==root+"/content"||path==Vwii::StagePath(root+"/content")||path==Vwii::PreviousPath(root+"/content")||
           path==ticket||path==Vwii::StagePath(ticket)||path==Vwii::PreviousPath(ticket)||
           path==Vwii::ParentPath(Vwii::StagePath(root+"/content"))||path==Vwii::ParentPath(Vwii::PreviousPath(root+"/content"))||
           path==Vwii::ParentPath(Vwii::StagePath(ticket))||path==Vwii::ParentPath(Vwii::PreviousPath(ticket))||
           path=="/vol/slccmpt01/title/"+Vwii::Hex(id>>32,8)||path=="/vol/slccmpt01/ticket/"+Vwii::Hex(id>>32,8))return;
        auto prefix=Vwii::StagePath(root+"/content")+"/";
        if(path.starts_with(prefix)){auto leaf=path.substr(prefix.size());if(leaf=="title.tmd"||(leaf.size()==12&&leaf.ends_with(".app")&&leaf.substr(0,8).find_first_not_of("0123456789abcdef")==std::string::npos))return;}
    }
    throw std::runtime_error("Refusing out-of-scope console write: "+path);
}
Vwii::Bytes ReadFsa(const std::string &path){
    Vwii::CheckNandPath(path);
    FSAFileHandle handle;Check(FSAOpenFileEx(fsaClient,path.c_str(),"r",static_cast<FSMode>(0),FS_OPEN_FLAG_NONE,0,&handle),"Cannot read "+path);
    try{FSStat stat{};Check(FSAGetStatFile(fsaClient,handle,&stat),"Cannot stat "+path);Setup::Need(stat.size<=64*1024*1024,"Oversized console file");
        Vwii::Bytes out(stat.size);Aligned block(65536);size_t pos=0;
        while(pos<out.size()){auto n=std::min<size_t>(65536,out.size()-pos);auto got=FSAReadFile(fsaClient,block.data,1,n,handle,FSA_READ_FLAG_NONE);Setup::Need(got==int(n),"Short console read: "+path);memcpy(out.data()+pos,block.data,n);pos+=n;}
        Check(FSACloseFile(fsaClient,handle),"Cannot close "+path);return out;
    }catch(...){FSACloseFile(fsaClient,handle);throw;}
}
Vwii::Bytes ReadBundle(const std::string &path){std::ifstream f(path,std::ios::binary|std::ios::ate);Setup::Need(bool(f),"Missing bundled console file: "+path);auto n=f.tellg();Setup::Need(n>=0&&n<=64*1024*1024,"Oversized bundle file");Vwii::Bytes out(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size());Setup::Need(bool(f),"Cannot read bundled console file");return out;}
Vwii::Bytes Trusted(const std::string &path,const char *hash){Setup::Need(Setup::HashFile(path,callback,"Checking console package") == hash,"Console package hash mismatch: "+path);return ReadBundle(path);}
void Crypt(bool enc,unsigned char *key,unsigned char *iv,const unsigned char *in,size_t n,unsigned char *out){mbedtls_aes_context c;mbedtls_aes_init(&c);auto r=enc?mbedtls_aes_setkey_enc(&c,key,128):mbedtls_aes_setkey_dec(&c,key,128);if(!r)r=mbedtls_aes_crypt_cbc(&c,enc?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,n,iv,in,out);mbedtls_aes_free(&c);Setup::Need(!r,"Title crypto failed");}
}
extern "C" bool GetCommonKeyFromOTP(uint8_t index,uint8_t out[16]){
    if(index>2)return false;
    WiiUConsoleOTP otp{};const auto status=Mocha_ReadOTP(&otp);
    Setup::Need(status==MOCHA_RESULT_SUCCESS,std::string("Cannot read console OTP: ")+Mocha_GetStatusStr(status)+" ("+std::to_string(status)+")");
    memcpy(out,index==2?otp.wiiUBank.vWiiCommonKey:index==1?otp.wiiCertBank.koreanKey:otp.wiiBank.commonKey,16);
    volatile unsigned char *p=reinterpret_cast<volatile unsigned char*>(&otp);for(size_t i=0;i<sizeof(otp);++i)p[i]=0;return true;
}
extern "C" void sha(const unsigned char *data,unsigned int size,unsigned char *out){Setup::Need(mbedtls_sha1_ret(data,size,out)==0,"Content hash failed");}
extern "C" void aes_cbc_dec(unsigned char *key,unsigned char *iv,unsigned char *in,unsigned int n,unsigned char *out){Crypt(false,key,iv,in,n,out);}
extern "C" void aes_cbc_enc(unsigned char *key,unsigned char *iv,unsigned char *in,unsigned int n,unsigned char *out){Crypt(true,key,iv,in,n,out);}
extern "C" void WUPI_putstr(const char *text){Setup::Tick(callback,"Preparing vWii prerequisites",text,0,0);}
bool State::AppRunning(){Setup::Tick(callback,"Preparing vWii prerequisites","Reading this console's base IOS",0,0);return true;}
bool ReadFileToBuffer(const std::string &path,uint8_t **out,uint32_t *size){
    auto bytes=path.starts_with("fs:")?ReadBundle(path):ReadFsa(path);*out=static_cast<uint8_t*>(memalign(64,(bytes.size()+63)&~size_t(63)));Setup::Need(*out!=nullptr,"Console buffer allocation failed");memcpy(*out,bytes.data(),bytes.size());*size=bytes.size();return true;
}
extern "C" int32_t FindSharedContentIndex(const uint8_t *hash){
    auto map=ReadFsa("/vol/slccmpt01/shared1/content.map");Setup::Need(map.size()%28==0,"Invalid shared-content map");
    for(size_t p=0;p<map.size();p+=28)if(!memcmp(map.data()+p+8,hash,20)){
        std::string name(reinterpret_cast<char*>(map.data()+p),8);Setup::Need(name.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid shared-content name");return std::stoul(name,nullptr,16);
    }return -1;
}
extern "C" int32_t CINS_Install(uint64_t id,const TitleTicket *ticket,uint32_t ticketSize,const TitleTmd *tmd,uint32_t tmdSize,const CINS_Content *contents,uint16_t count){
    // The imported patcher queues packages here; this function NEVER writes NAND.
    Vwii::Title t;t.id=id;t.ticket.assign(reinterpret_cast<const unsigned char*>(ticket),reinterpret_cast<const unsigned char*>(ticket)+ticketSize);
    t.tmd.assign(reinterpret_cast<const unsigned char*>(tmd),reinterpret_cast<const unsigned char*>(tmd)+tmdSize);
    Setup::Need(tmdSize>=0x1e4+size_t(count)*36,"Truncated prepared TMD");
    for(unsigned i=0;i<count;++i){auto cid=Vwii::U32(t.tmd,0x1e4+i*36);auto *data=static_cast<const unsigned char*>(contents[i].data);Setup::Need(contents[i].length<=32*1024*1024,"Oversized prepared content");t.contents[cid]=Vwii::Bytes(data,data+contents[i].length);}
    Vwii::Validate(t,Digest);prepared.push_back(std::move(t));return 0;
}
namespace Vwii {
class Console final:public IO {
    bool mocha=false,mounted=false;
public:
    Console(){Setup::Need(Mocha_InitLibrary()==MOCHA_RESULT_SUCCESS,"Aroma Mocha services unavailable. Reboot into Aroma.");mocha=true;FSAInit();fsaClient=FSAAddClient(nullptr);
        try{Setup::Need(fsaClient>=0,"Cannot open console filesystem");Setup::Need(Mocha_UnlockFSClientEx(fsaClient)==MOCHA_RESULT_SUCCESS,"Cannot access vWii filesystem");
            auto r=FSAMount(fsaClient,"/dev/slccmpt01","/vol/slccmpt01",FSA_MOUNT_FLAG_LOCAL_MOUNT,nullptr,0);if(r==FS_ERROR_OK)mounted=true;else Setup::Need(r==FS_ERROR_ALREADY_EXISTS,"Cannot mount vWii filesystem");
        }catch(...){Close();throw;}}
    void Close(){if(fsaClient>=0){if(mounted)FSAUnmount(fsaClient,"/vol/slccmpt01",FSA_UNMOUNT_FLAG_NONE);FSADelClient(fsaClient);fsaClient=-1;}if(mocha){Mocha_DeInitLibrary();mocha=false;}}
    ~Console(){Close();}
    bool Exists(const std::string &path)override{CheckNandPath(path);FSStat stat{};auto r=FSAGetStat(fsaClient,path.c_str(),&stat);if(r==FS_ERROR_NOT_FOUND)return false;Check(r,"Cannot inspect "+path);return true;}
    Bytes Read(const std::string &path)override{return ReadFsa(path);}
    void Directory(const std::string &path,uint32_t mode,uint32_t uid,uint32_t gid)override{WriteScope(path);if(Exists(path)){FSStat s{};Check(FSAGetStat(fsaClient,path.c_str(),&s),path);Setup::Need(s.flags&FS_STAT_DIRECTORY,"Expected console directory: "+path);return;}Check(FSAMakeDir(fsaClient,path.c_str(),static_cast<FSMode>(mode)),"Cannot create "+path);Owner(path,uid,gid);}
    void Write(const std::string &path,const Bytes &bytes,uint32_t mode,uint32_t uid,uint32_t gid)override{WriteScope(path);Setup::Need(!Exists(path),"Refusing to overwrite console staging: "+path);
        FSAFileHandle handle;Check(FSAOpenFileEx(fsaClient,path.c_str(),"w",static_cast<FSMode>(mode),FS_OPEN_FLAG_NONE,0,&handle),"Cannot stage "+path);Check(FSACloseFile(fsaClient,handle),"Cannot close new file");Owner(path,uid,gid);
        Check(FSAOpenFileEx(fsaClient,path.c_str(),"r+",static_cast<FSMode>(0),FS_OPEN_FLAG_NONE,0,&handle),"Cannot write "+path);
        try{Aligned block(65536);for(size_t pos=0;pos<bytes.size();){auto n=std::min<size_t>(65536,bytes.size()-pos);memcpy(block.data,bytes.data()+pos,n);Setup::Need(FSAWriteFile(fsaClient,block.data,1,n,handle,FSA_WRITE_FLAG_NONE)==int(n),"Short console write");pos+=n;}Check(FSAFlushFile(fsaClient,handle),"Cannot flush file");Check(FSACloseFile(fsaClient,handle),"Cannot close file");}catch(...){FSACloseFile(fsaClient,handle);throw;}
        Check(FSAChangeMode(fsaClient,path.c_str(),static_cast<FSMode>(mode)),"Cannot set console permissions");}
    void Move(const std::string &from,const std::string &to)override{WriteScope(from);WriteScope(to);Setup::Need(!Exists(to),"Console destination exists: "+to);
        FSStat stat{};Check(FSAGetStat(fsaClient,from.c_str(),&stat),"Cannot inspect rename source "+from);
        Setup::Need((stat.flags&FS_STAT_DIRECTORY)||from.substr(from.rfind('/')+1)==to.substr(to.rfind('/')+1),"vWii file moves must preserve the filename");
        Check(FSARename(fsaClient,from.c_str(),to.c_str()),"Cannot publish "+from+" -> "+to);}
    void Flush()override{Check(FSAFlushVolume(fsaClient,"/vol/slccmpt01"),"Cannot flush console storage");}
    uint64_t Free()override{uint64_t n=0;Check(FSAGetFreeSpaceSize(fsaClient,"/vol/slccmpt01",&n),"Cannot check console free space");return n;}
};
Title Forwarder(const Bytes &wad){
    auto align=[](uint64_t n){return (n+63)&~uint64_t(63);};Setup::Need(wad.size()>=64,"Truncated forwarder WAD");
    uint64_t pos=align(U32(wad,0))+align(U32(wad,8))+align(U32(wad,12));
    auto take=[&](uint64_t n){Setup::Need(n<=wad.size()&&pos<=wad.size()-n,"Truncated WAD section");Bytes out(wad.begin()+pos,wad.begin()+pos+n);pos+=align(n);return out;};
    Title t;t.id=WiiFlow;t.ticket=take(U32(wad,16));t.tmd=take(U32(wad,20));
    Setup::Need(t.ticket.size()>=0x2a4&&t.tmd.size()>=0x1e4&&U64(t.tmd,0x18c)==WiiFlow&&U64(t.ticket,0x1dc)==WiiFlow,"Wrong forwarder package");
    unsigned char common[16]{},key[16]{},iv[16]{};Setup::Need(GetCommonKeyFromOTP(ForwarderCommonKeyIndex(t.ticket),common),"Unsupported console common-key index");memcpy(iv,t.ticket.data()+0x1dc,8);Crypt(false,common,iv,t.ticket.data()+0x1bf,16,key);memset(common,0,16);
    auto n=U16(t.tmd,0x1de);Setup::Need(n<=128&&t.tmd.size()==0x1e4+size_t(n)*36,"Invalid WAD content table");
    for(unsigned i=0;i<n;++i){size_t p=0x1e4+i*36;auto size=U64(t.tmd,p+8);Setup::Need(size<=32*1024*1024,"Oversized WAD content");auto encrypted=take(align(size));Bytes data(encrypted.size());memset(iv,0,16);iv[0]=t.tmd[p+4];iv[1]=t.tmd[p+5];Crypt(false,key,iv,encrypted.data(),encrypted.size(),data.data());data.resize(size);t.contents[U32(t.tmd,p)]=std::move(data);}
    memset(key,0,16);Validate(t,Digest);return t;
}
Session PrepareConsole(const std::string &bundle,uint32_t selected,const std::vector<Setup::FileDef> &files,const Setup::Progress &progress){
    Session s;if(!Required(selected))return s;callback=progress;prepared.clear();s.console=std::make_shared<Console>();
    for(const auto &f:files)if(f.path.starts_with("apps/d2x-cios-installer-vwii/"))Setup::Need(Setup::HashFile(bundle+"/payload/"+f.path,callback,"Verifying cIOS modules")==f.hash,"cIOS module hash mismatch");
    const auto root=bundle+"/console/";Title hbc;hbc.id=HBC;
    hbc.ticket=Trusted(root+"title_cetk.bin","42c3c11bb99eec48468e374997400fe062d98f7c59c25074327dfb97bbf52988");
    hbc.tmd=Trusted(root+"title_tmd.bin","3f19297931a2bfc9a7d79788abef67f4d464cfd18c4592898f43b79c8dd9fe74");
    hbc.contents[0]=Trusted(root+"title_00000000.bin","a1488abb67b3d062d791c6944bac528b9e2a32637ec89ef90734f0645886f685");
    hbc.contents[1]=Trusted(root+"title_00000001.bin","b3aba32582af2f3f705bf92f281de0125b27f64b41d77f364e6fa0d3880009ea");Validate(hbc,Digest);prepared.push_back(std::move(hbc));
    prepared.push_back(Forwarder(Trusted(root+"wiiflow-vwii.wad","a569fc78ba42e0bfee0c4043e1028fe599c30e2d8ba81a3b543afdb19a6d74d3")));
    std::vector<std::pair<int,bool>> slots;PrepareD2XBatch(bundle+"/payload/apps/d2x-cios-installer-vwii/d2x-v11-beta3-vWii",slots);
    Setup::Need(slots.size()==4&&std::all_of(slots.begin(),slots.end(),[](auto p){return p.second;})&&prepared.size()==6,"Could not prepare all vWii cIOS slots; no console writes performed");
    s.plan=Prepare(*s.console,std::move(prepared),Digest);callback={};return s;
}
void InstallConsole(Session &s,const std::string &report,const Setup::Progress &progress){
    if(!s.console)return;
    std::ofstream log(report,std::ios::app);Setup::Need(bool(log),"Cannot open setup report before console installation");
    const bool home=OSIsHomeButtonMenuEnabled();OSEnableHomeButtonMenu(false);
    try{Install(*s.console,s.plan,[&](const auto &phase,const auto &path,uint64_t n,uint64_t total,bool cancel){log<<phase<<": "<<path<<'\n';log.flush();Setup::Need(bool(log),"Cannot update console setup report");return !progress||progress(phase,path,n,total,cancel);});
        log<<"VWII VERIFIED: Homebrew Channel, WiiFlow forwarder, cIOS 248/38 249/56 250/57 251/58\n";log.flush();Setup::Need(bool(log),"Cannot save console completion report");
    }catch(const std::exception &e){log<<"VWII STOPPED: "<<e.what()<<'\n';log.flush();OSEnableHomeButtonMenu(home);throw;}
    catch(...){OSEnableHomeButtonMenu(home);throw;}
    OSEnableHomeButtonMenu(home);s.console.reset();
}
}
