#include "vc_inject.hpp"
#include "../../launcher-modules/sdk/rom_request.hpp"
#include "raw_launch.hpp"
#include "vc_formats.hpp"
#include "vc_package.hpp"
#include "n64_profile.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <zlib.h>

namespace VCInject {
namespace {
const std::string sd="fs:/vol/external01/";
const std::string cache="wiiu/apps/uFlow/vc-cache/ds/";
std::string Cache(const std::string &system){return "wiiu/apps/uFlow/vc-cache/"+system+"/";}
bool ID(const std::string &s){return !s.empty()&&s.size()<65&&std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';});}
bool Dir(const std::string &p){struct stat s{};return stat(p.c_str(),&s)==0&&S_ISDIR(s.st_mode);}
bool Mkdir(const std::string &p){return Dir(p)||mkdir(p.c_str(),0777)==0;}
bool Parents(const std::string &relative){if(!UFlowRom::RelativePath(relative))return false;std::string p=sd;std::istringstream in(relative);std::string part;while(std::getline(in,part,'/')){p+=part;if(!Mkdir(p))return false;p+='/';}return true;}
std::string Read(const std::string &p,size_t limit=1048576){std::ifstream f(p,std::ios::binary);if(!f)return {};std::string s(limit+1,'\0');f.read(&s[0],s.size());s.resize(f.gcount());return s.size()>limit?std::string():s;}
bool Write(const std::string &p,const std::string &s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(s.data(),s.size());f.flush();bool ok=bool(f);f.close();return ok&&!f.fail();}
uint32_t Ror(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
// Streaming SHA-256: identity stays stable across paths/base changes; no ROM-sized allocation.
struct SHA {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char block[64]{};uint64_t count=0;size_t used=0;
    void Compress(){
        static const uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(Ror(w[i-15],7)^Ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(Ror(w[i-2],17)^Ror(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;++i){uint32_t t=hh+(Ror(e,6)^Ror(e,11)^Ror(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(Ror(a,2)^Ror(a,13)^Ror(a,22))+((a&b)^(a&c)^(b&c));hh=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        uint32_t v[]={a,b,c,d,e,f,g,hh};for(int i=0;i<8;++i)h[i]+=v[i];
    }
    void Add(const char *p,size_t n){count+=n;while(n){const auto take=std::min(n,sizeof(block)-used);std::memcpy(block+used,p,take);used+=take;p+=take;n-=take;if(used==64){Compress();used=0;}}}
    std::string End(){const uint64_t bits=count*8;char pad=char(0x80);Add(&pad,1);pad=0;while(used!=56)Add(&pad,1);for(int i=7;i>=0;--i){pad=char(bits>>(i*8));Add(&pad,1);}std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};
struct Failure {std::string text;};
#ifdef UFLOW_VC_TEST_IO
uint64_t verificationReadBytes=0;
#endif
void Require(bool ok,const std::string &s){if(!ok)throw Failure{s};}
void Tick(const Progress &p,const std::string &s,uint64_t n=0,uint64_t total=0){if(p&&!p(s,n,total))throw Failure{"INJECTION CANCELLED - ORIGINAL FILES UNCHANGED"};}
std::string HashFile(const std::string &p,const Progress &progress){std::ifstream f(p,std::ios::binary);Require(bool(f),"CANNOT READ "+p);SHA hash;std::array<char,65536>b;uint64_t n=0;while(f){f.read(b.data(),b.size());const auto got=f.gcount();
#ifdef UFLOW_VC_TEST_IO
    verificationReadBytes+=got;
#endif
    hash.Add(b.data(),got);n+=got;Tick(progress,"VERIFYING INPUT",n);}Require(f.eof(),"INPUT READ ERROR");return hash.End();}
uint16_t U16(const std::string &s,size_t i){return static_cast<unsigned char>(s.at(i))|(static_cast<unsigned char>(s.at(i+1))<<8);}
uint32_t U32(const std::string &s,size_t i){return uint32_t(U16(s,i))|(uint32_t(U16(s,i+2))<<16);}
void W16(std::ostream &s,uint16_t x){s.put(x&255);s.put(x>>8);}
void W32(std::ostream &s,uint32_t x){W16(s,x&65535);W16(s,x>>16);}
std::string ZipName(const std::string &path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);Require(bool(f),"BASE MISSING CONTENT/0010/ROM.ZIP");
    const auto size=f.tellg();Require(size>=22,"INVALID BASE ROM ZIP");const auto tail=std::min<std::streamoff>(size,65557);
    std::string s(size_t(tail),'\0');f.seekg(size-tail);f.read(&s[0],s.size());Require(bool(f),"CANNOT READ BASE ZIP");
    size_t e=s.size()-22;for(;;){if(U32(s,e)==0x06054b50&&e+22+U16(s,e+20)==s.size())break;Require(e>0,"ZIP DIRECTORY MISSING");--e;}
    Require(U16(s,e+4)==0&&U16(s,e+6)==0&&U16(s,e+8)==1&&U16(s,e+10)==1,"DS BASE ZIP MUST CONTAIN ONE ROM");
    uint32_t offset=U32(s,e+16),length=U32(s,e+12);Require(length>=46&&uint64_t(offset)+length<=uint64_t(size),"INVALID ZIP DIRECTORY RANGE");
    f.seekg(offset);s.assign(46,'\0');f.read(&s[0],46);Require(bool(f)&&U32(s,0)==0x02014b50,"INVALID ZIP DIRECTORY");
    Require(!(U16(s,8)&1)&&(U16(s,10)==0||U16(s,10)==8),"UNSUPPORTED BASE ZIP COMPRESSION");
    const auto n=U16(s,28);Require(n>0&&n<128&&46u+n<=length,"INVALID BASE ROM NAME");std::string name(n,'\0');f.read(&name[0],n);
    Require(bool(f)&&name.find("WUP")==0&&name.find_first_of("/\\:")==std::string::npos&&UFlowRom::RelativePath(name),"UNSUPPORTED DS BASE ROM NAME");return name;
}
struct File {std::string relative;uint64_t size;};
void Walk(const std::string &root,const std::string &sub,std::vector<File> &files,int depth=0){
    Require(depth<24&&files.size()<20000,"BASE DIRECTORY TOO COMPLEX");DIR *d=opendir((root+sub).c_str());Require(d,"CANNOT READ BASE DIRECTORY");
    std::vector<std::string> names;while(auto *e=readdir(d)){std::string n=e->d_name;if(n!="."&&n!="..")names.push_back(n);}closedir(d);std::sort(names.begin(),names.end());
    for(const auto &n:names){const std::string rel=sub+n;Require(UFlowRom::RelativePath(rel),"INVALID BASE PATH");struct stat st{};
        Require(lstat((root+rel).c_str(),&st)==0,"CANNOT STAT BASE FILE");
        if(S_ISDIR(st.st_mode))Walk(root,rel+"/",files,depth+1);else{Require(S_ISREG(st.st_mode)&&st.st_size>=0,"BASE CONTAINS SPECIAL FILE / LINK");files.push_back({rel,uint64_t(st.st_size)});}}
}
struct WorkDirectory {
    std::string path;bool committed=false;
    ~WorkDirectory(){
        if(committed||path.rfind(sd+"wiiu/apps/uFlow/vc-cache/",0)!=0||path.find(".work-")==std::string::npos)return;
        // Only this invocation's exclusively-created work directory is eligible.
        try{std::vector<File> files;Walk(path+"/","",files);std::set<std::string> dirs;
            for(const auto &f:files){std::remove((path+"/"+f.relative).c_str());auto p=f.relative.find_last_of('/');while(p!=std::string::npos){dirs.insert(f.relative.substr(0,p));if(!p)break;p=f.relative.find_last_of('/',p-1);}}
            for(auto it=dirs.rbegin();it!=dirs.rend();++it)rmdir((path+"/"+*it).c_str());
            for(const char *p:{"content/0010","content","code","meta"})rmdir((path+"/"+p).c_str());
            rmdir(path.c_str());
        }catch(...){} // preserve incomplete work if the card itself is unreadable
    }
};
std::string Seal(const std::string &root,const Progress &p,const std::map<std::string,std::string> &expected={}){
    std::vector<File> files;Walk(root+"/","",files);std::string seal;uint64_t total=0,done=0;size_t matched=0;
    for(const auto &f:files)if(f.relative!="uflow.complete"&&f.relative!="uflow.sha256")total+=f.size;
    Tick(p,"VERIFYING INJECTION",0,total);
    for(const auto &f:files)if(f.relative!="uflow.complete"&&f.relative!="uflow.sha256"){
        const auto hash=HashFile(root+"/"+f.relative,[&](const auto &,auto n,auto){Tick(p,"VERIFYING INJECTION",done+n,total);return true;});
        const auto prior=expected.find(f.relative);
        Require(prior==expected.end()||prior->second==hash,"CACHE COPY CORRUPTED - ORIGINAL FILES UNCHANGED");
        if(prior!=expected.end())++matched;
        seal+=hash+" "+f.relative+"\n";done+=f.size;
    }
    Require(matched==expected.size(),"CACHE FILE MISSING");
    return seal;
}
std::string Xml(const std::string &s,const std::string &tag){auto p=s.find("<"+tag);if(p==std::string::npos)return {};p=s.find('>',p);auto e=s.find("</"+tag+">",p);return p==std::string::npos||e==std::string::npos?"":s.substr(p+1,e-p-1);}
bool Valid(const Settings &s){return VCSystems::Find(s.system)&&UFlowRom::RelativePath(s.base)&&s.base.rfind("wiiu/apps/uFlow/vc-cache",0)!=0&&
    (s.compression==0||s.compression==1||s.compression==6)&&(s.brightness==-1||(s.brightness>=0&&s.brightness<=100))&&
    s.upscale>=-1&&s.upscale<=1&&s.cacheMiB>=512&&s.cacheMiB<=8192;}
std::string Identity(const Settings &s){return s.base+"\n"+std::to_string(s.compression)+"\n"+std::to_string(s.brightness)+"\n"+std::to_string(s.upscale);}
void PatchNumber(std::string &json,const std::string &name,int number){if(number<0)return;const std::regex expression("(\""+name+"\"\\s*:\\s*)([0-9]+(?:\\.[0-9]+)?)");
    const auto begin=std::sregex_iterator(json.begin(),json.end(),expression);Require(begin!=std::sregex_iterator(),"DS CONFIG MISSING "+name);auto next=begin;++next;Require(next==std::sregex_iterator(),"AMBIGUOUS DS CONFIG FIELD");json.replace(begin->position(2),begin->length(2),std::to_string(number));}
std::string Copy(const std::string &src,const std::string &dst,const Progress &p){
    std::ifstream in(src,std::ios::binary);std::ofstream out(dst,std::ios::binary|std::ios::trunc);
    Require(bool(in)&&bool(out),"CANNOT COPY BASE - CHECK SD SPACE");
    std::array<char,65536>b;SHA hash;uint64_t done=0;
    while(in){in.read(b.data(),b.size());const auto n=in.gcount();
#ifdef UFLOW_VC_TEST_IO
        verificationReadBytes+=n;
#endif
        hash.Add(b.data(),n);out.write(b.data(),n);done+=n;
        Require(bool(out),"CACHE WRITE ERROR / SD FULL");Tick(p,"COPYING VC BASE",done);}
    Require(in.eof(),"BASE READ ERROR");out.flush();Require(bool(out),"CACHE FLUSH ERROR");out.close();Require(!out.fail(),"CACHE CLOSE ERROR");return hash.End();
}
void ZipROM(const std::string &rom,const std::string &dest,const std::string &name,int level,const Progress &p){
    std::ifstream in(rom,std::ios::binary);std::ofstream out(dest,std::ios::binary|std::ios::trunc);Require(bool(in)&&bool(out),"CANNOT CREATE INJECTED ROM ZIP");
    const uint16_t method=level?8:0;W32(out,0x04034b50);W16(out,20);W16(out,0);W16(out,method);W16(out,0);W16(out,0);W32(out,0);W32(out,0);W32(out,0);W16(out,name.size());W16(out,0);out<<name;
    z_stream z{};Require(!level||deflateInit2(&z,level,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK,"ZIP INITIALIZATION FAILED");
    struct End {z_stream &z;bool on;~End(){if(on)deflateEnd(&z);}}end{z,bool(level)};
    std::array<char,65536> src,dst;uint32_t crc=crc32(0,nullptr,0),size=0,packed=0;
    do{in.read(src.data(),src.size());const auto n=in.gcount();Require(n>=0,"ROM READ ERROR");crc=crc32(crc,reinterpret_cast<const Bytef*>(src.data()),n);size+=n;
        if(!level){out.write(src.data(),n);packed+=n;}else{
            z.next_in=reinterpret_cast<Bytef*>(src.data());z.avail_in=n;int status;
            do{z.next_out=reinterpret_cast<Bytef*>(dst.data());z.avail_out=dst.size();status=deflate(&z,in.eof()?Z_FINISH:Z_NO_FLUSH);Require(status==Z_OK||status==Z_STREAM_END,"ZIP COMPRESSION FAILED");auto have=dst.size()-z.avail_out;out.write(dst.data(),have);packed+=have;}while(z.avail_in||!z.avail_out||(in.eof()&&status!=Z_STREAM_END));
        }Require(bool(out),"ZIP WRITE ERROR / SD FULL");Tick(p,"INJECTING DS ROM",size);
    }while(in);Require(in.eof(),"ROM READ FAILED");
    const uint32_t central=30+name.size()+packed;
    W32(out,0x02014b50);W16(out,20);W16(out,20);W16(out,0);W16(out,method);W16(out,0);W16(out,0);W32(out,crc);W32(out,packed);W32(out,size);W16(out,name.size());W16(out,0);W16(out,0);W16(out,0);W16(out,0);W32(out,0);W32(out,0);out<<name;
    W32(out,0x06054b50);W16(out,0);W16(out,0);W16(out,1);W16(out,1);W32(out,46+name.size());W32(out,central);W16(out,0);
    out.seekp(14);W32(out,crc);W32(out,packed);W32(out,size);out.flush();Require(bool(out),"ZIP FINALIZATION FAILED");out.close();Require(!out.fail(),"ZIP CLOSE FAILED");
}
}
Settings Load(const std::string &id){
    Settings s;if(!ID(id))return s;
    for(const auto &system:VCSystems::all)if(id==std::string(system.id)+"-vc")s.system=system.id;
    std::istringstream in(Read(sd+"wiiu/apps/uFlow/launchers/"+id+".cfg",4096));std::string line;
    try{while(std::getline(in,line)){auto eq=line.find('=');if(eq==std::string::npos)continue;auto key=line.substr(0,eq),v=line.substr(eq+1);if(!v.empty()&&v.back()=='\r')v.pop_back();
        if(key=="base")s.base=v;else if(key=="system")s.system=v;else if(key=="reuse")s.reuse=v=="1";else if(key=="compression")s.compression=std::stoi(v);else if(key=="brightness")s.brightness=std::stoi(v);else if(key=="upscale")s.upscale=std::stoi(v);else if(key=="cacheMiB")s.cacheMiB=std::stoi(v);
    }}catch(...){s.base.clear();}return s;
}
bool Save(const std::string &id,const Settings &s){if(!ID(id)||!Valid(s)||!Parents("wiiu/apps/uFlow/launchers"))return false;const auto path=sd+"wiiu/apps/uFlow/launchers/"+id+".cfg";const auto body="base="+s.base+"\nsystem="+s.system+"\nreuse="+(s.reuse?"1":"0")+"\ncompression="+std::to_string(s.compression)+"\nbrightness="+std::to_string(s.brightness)+"\nupscale="+std::to_string(s.upscale)+"\ncacheMiB="+std::to_string(s.cacheMiB)+"\n";return Write(path+".tmp",body)&&std::rename((path+".tmp").c_str(),path.c_str())==0;}
namespace {
struct BasePlan {std::string rpx,target;};
BasePlan Inspect(const Settings &s){
    Require(Valid(s),"SELECT A VALID DECRYPTED VC BASE FOLDER");const auto root=sd+s.base+"/";
    for(const char *p:{"code/app.xml","code/cos.xml","meta/meta.xml"})Require(!Read(root+p).empty(),std::string("BASE MISSING ")+p);
    std::vector<File> files;Walk(root,"code/",files);BasePlan plan;
    for(const auto &f:files)if(f.relative.size()>4&&f.relative.substr(f.relative.size()-4)==".rpx"){
        Require(plan.rpx.empty(),"BASE HAS MULTIPLE RPX FILES");plan.rpx=f.relative;
    }Require(!plan.rpx.empty(),"BASE RPX MISSING");
    if(s.system=="ds"){plan.target="content/0010/rom.zip";ZipName(root+plan.target);}
    else if(s.system=="nes"||s.system=="snes"){
        plan.target=plan.rpx;VCFormat::Rpx r(VCFormat::Read(root+plan.rpx));VCFormat::RomSlot(r,s.system);
    }else if(s.system=="n64"){
        files.clear();Walk(root,"content/rom/",files);Require(files.size()==1,"N64 BASE MUST CONTAIN ONE ROM");plan.target=files[0].relative;
        std::ifstream f(root+plan.target,std::ios::binary);std::string magic(4,'\0');f.read(&magic[0],4);
        Require(bool(f)&&VCFormat::Number(magic,0,4,true)==0x80371240,"INVALID N64 BASE ROM");
    }else if(s.system=="gba"){
        plan.target="content/alldata.psb.m";VCFormat::GbaArchive archive(VCFormat::Read(root+plan.target,4*1024*1024));
        struct stat info{};Require(stat((root+"content/alldata.bin").c_str(),&info)==0&&uint64_t(archive.offset)+archive.length<=uint64_t(info.st_size),"INVALID GBA ARCHIVE ROM RANGE");
    }else{
        plan.target=s.system=="pce"?"content/pceemu/pce.pkg":"content/msx/msx.pkg";
        VCFormat::PackageRom(VCFormat::Read(root+plan.target),s.system);
    }return plan;
}
void Apply(const Settings &s,const BasePlan &plan,const std::string &stage,const std::string &rom,const Progress &progress){
    Tick(progress,"BUILDING "+s.system+" VC CARTRIDGE");const auto target=stage+"/"+plan.target;
    if(s.system=="n64")VCFormat::Write(target,rom);
    else if(s.system=="gba"){
        VCFormat::GbaArchive archive(VCFormat::Read(target,4*1024*1024));auto packed=VCFormat::EncodeMdf(rom,archive.romName);
        const auto binPath=stage+"/content/alldata.bin";
        std::ofstream bin(binPath,std::ios::binary|std::ios::app);Require(bool(bin),"CANNOT OPEN GBA OUTPUT ARCHIVE");auto end=bin.tellp();
        Require(end>=0&&uint64_t(end)+packed.size()+2048<512*1024*1024,"GBA OUTPUT ARCHIVE TOO LARGE");
        const auto off=(uint32_t(end)+2047)&~uint32_t(2047);std::string padding(off-uint32_t(end),'\0');bin.write(padding.data(),padding.size());bin.write(packed.data(),packed.size());
        bin.flush();Require(bool(bin),"GBA ARCHIVE WRITE FAILED");bin.close();Require(!bin.fail(),"GBA ARCHIVE CLOSE FAILED");
        VCFormat::Write(target,archive.Update(off,packed.size()));
    }else{
        auto bytes=VCFormat::Read(target);
        if(s.system=="nes"||s.system=="snes")VCFormat::InjectRpx(bytes,rom,s.system);
        else VCFormat::InjectPackage(bytes,rom,s.system);
        VCFormat::Write(target,bytes);
    }Tick(progress,"VC CARTRIDGE WRITTEN");
}
}
std::string CheckBase(const Settings &s){try{Inspect(s);return {};}catch(const Failure &f){return f.text;}catch(const std::exception &e){return e.what();}catch(...){return "INVALID VC BASE";}}
std::string Prepare(GameEntry &game,const Settings &s,Progress progress){try{
    Require(UFlowRom::File(game.absolutePath),"ORIGINAL ROM REQUIRED TO INJECT / RE-INJECT");
    Tick(progress,"CHECKING VC BASE");
    const auto plan=Inspect(s);std::string error;const auto rom=game.absolutePath;
    Require(!UFlowRom::RelativeROM(rom).empty(),"VC INJECTION REQUIRES AN SD ROM");
    Require(VCSystems::File(rom)==s.system,"ROM DOES NOT MATCH INJECTION MODULE SYSTEM");
    std::string cartridge;
    struct stat st{};Require(stat(rom.c_str(),&st)==0&&st.st_size>0&&st.st_size<=512ll*1024*1024,"INVALID VC ROM SIZE");
    if(s.system=="ds"){
    std::ifstream input(rom,std::ios::binary);std::string header(512,'\0');input.read(&header[0],512);Require(bool(input)&&header[0x12]==0,"REQUIRES A STANDARD DS ROM, NOT DSI");
    auto le=[&](int p){return U32(header,p);};
    Require(le(0x20)>=512&&le(0x2c)>0&&uint64_t(le(0x20))+le(0x2c)<=uint64_t(st.st_size)&&le(0x30)>=512&&le(0x3c)>0&&uint64_t(le(0x30))+le(0x3c)<=uint64_t(st.st_size),"DS ROM HEADER / EXECUTABLE RANGE INVALID");
    }
    const auto romHash=HashFile(rom,progress);SHA build;const auto identity=(s.system=="ds"?std::string("UFLOW-DS-INJECT-1\n"):"UFLOW-VC-INJECT-1\n"+s.system+"\n")+romHash+"\n"+Identity(s);build.Add(identity.data(),identity.size());
    if(s.system!="ds")cartridge=VCFormat::Cartridge(rom,s.system);
    if(s.system=="nes"||s.system=="snes"){
        const std::string version="UFLOW-RPX-LOADER-ORDER-1";build.Add(version.data(),version.size());
    }
    if(s.system=="nes"){
        // Check capacity BEFORE copying the base, and bind cache generations to
        // normalized bytes so repaired headers cannot reuse an old bad build.
        VCFormat::Rpx base(VCFormat::Read(sd+s.base+"/"+plan.target));
        Require(cartridge.size()<=VCFormat::RomSlot(base,"nes").length,"NES ROM TOO LARGE FOR SELECTED VC BASE - SELECT A LARGER NES BASE");
        const std::string version="UFLOW-NES-NORMALIZE-1";build.Add(version.data(),version.size());build.Add(cartridge.data(),cartridge.size());
    }
    std::string ini,iniSource;bool iniSidecar=false;
    if(s.system=="n64"){
        iniSidecar=UFlowRom::File(rom+".ini");
        if(iniSidecar){iniSource="sidecar";ini=VCFormat::Read(rom+".ini",128*1024);Require(!ini.empty()&&ini.find('\0')==std::string::npos,"INVALID N64 INI SIDECAR");}
        else{
            const auto configs=sd+s.base+"/content/config/";std::vector<std::string> names;
            if(DIR *dir=opendir(configs.c_str())){while(auto *entry=readdir(dir))names.emplace_back(entry->d_name);closedir(dir);}
            const auto selected=N64Profile::Select(cartridge,names,[&](const std::string &name){return VCFormat::Read(configs+name,128*1024);});
            iniSource=selected.source;ini=selected.text;
        }
        const auto profileKey=std::string(N64Profile::Version)+"\n"+iniSource+"\n"+ini;
        build.Add(profileKey.data(),profileKey.size());
        Tick(progress,"N64 PROFILE: "+iniSource);
    }
    const auto cache=Cache(s.system);
    const auto root=sd+s.base+"/";std::vector<File> files;for(const char *part:{"code/","content/","meta/"})Walk(root,part,files);
    uint64_t bytes=compressBound(static_cast<uLong>(st.st_size))+65536,baseBytes=0,done=0;std::string rpx;
    std::map<std::string,std::string> baseHashes;
    for(const auto &f:files)if(f.relative!="content/0010/rom.zip")baseBytes+=f.size;
    for(const auto &f:files){Require(f.size<2ull*1024*1024*1024,"BASE FILE TOO LARGE");if(f.relative=="content/0010/rom.zip")continue;bytes+=f.size;
        const auto hash=HashFile(root+f.relative,[&](const auto &,auto n,auto){Tick(progress,"CHECKING BASE FILES",done+n,baseBytes);return true;});done+=f.size;
        baseHashes[f.relative]=hash;build.Add(f.relative.data(),f.relative.size());build.Add(hash.data(),hash.size());
        if(f.relative.rfind("code/",0)==0&&f.relative.size()>4&&f.relative.substr(f.relative.size()-4)==".rpx"){Require(rpx.empty(),"BASE HAS MULTIPLE RPX FILES");rpx=f.relative;}}
    Require(!rpx.empty(),"BASE RPX MISSING");const auto name=s.system=="ds"?ZipName(root+"content/0010/rom.zip"):std::string();build.Add(name.data(),name.size());const auto key=build.End();
    const auto rel=cache+key;std::string output=sd+rel;
    GameEntry packageSource,packageTarget;
    if(s.reuse&&VCPackage::Load(VCPackage::Path(game),packageSource,packageTarget)&&Read(packageTarget.absolutePath+"/uflow.complete",256)==key)output=packageTarget.absolutePath;
    const auto marker=Read(output+"/uflow.complete",256);
    const bool reuse=s.reuse&&marker==key;
    if(reuse){const auto seal=Read(output+"/uflow.sha256",4*1024*1024);Require(!seal.empty()&&seal==Seal(output,progress),"INJECTION CACHE DAMAGED - SELECT REBUILD EACH LAUNCH");}
    if(!reuse){
        Require(Parents(cache.substr(0,cache.size()-1)),"CANNOT CREATE CACHE DIRECTORY");std::vector<File> existing;Walk(sd+cache,"",existing);uint64_t used=0;for(const auto &f:existing)used+=f.size;
        Require(used+bytes<=uint64_t(s.cacheMiB)*1024*1024,"VC CACHE LIMIT REACHED - INCREASE LIMIT OR REMOVE OLD VC CACHE FOLDERS (NOT SAVES)");
        // New private directory on every failed/rebuild attempt; never overwrite a completed cache.
        int serial=0;std::string stage;do{stage=output+".work-"+std::to_string(serial++);}while(Dir(stage)&&serial<10000);
        Require(mkdir(stage.c_str(),0777)==0,"CANNOT CREATE INJECTION WORK DIRECTORY");WorkDirectory work{stage};
        done=0;
        for(const auto &f:files){if(f.relative=="content/0010/rom.zip")continue;std::string parent=stage;std::istringstream parts(f.relative);std::string part;while(std::getline(parts,part,'/')){if(parts.eof())break;parent+="/"+part;Require(Mkdir(parent),"CANNOT CREATE CACHE SUBDIRECTORY");}
            const auto copied=Copy(root+f.relative,stage+"/"+f.relative,[&](const auto &,auto n,auto){Tick(progress,"COPYING VC BASE",done+n,baseBytes);return true;});done+=f.size;
            Require(copied==baseHashes.at(f.relative),"BASE CHANGED DURING INJECTION");}
        if(s.system=="ds"){Require(Mkdir(stage+"/content/0010"),"CANNOT CREATE DS CONTENT DIRECTORY");ZipROM(rom,stage+"/content/0010/rom.zip",name,s.compression,progress);}
        // Detect source changes while copying/compressing, before exposing any result.
        Require(HashFile(rom,progress)==romHash,"ROM CHANGED DURING INJECTION");
        // Only files used by a transformation need pre-transform readback.
        // All unchanged files are checked once during final manifest creation.
        auto unchanged=baseHashes;std::set<std::string> modified{plan.target};
        if(s.system=="gba")modified.insert("content/alldata.bin");
        if(s.system=="ds"&&(s.brightness>=0||s.upscale>=0))modified.insert("content/0010/configuration_cafe.json");
        if(s.system=="n64")modified.insert("content/config/"+plan.target.substr(plan.target.find_last_of('/')+1)+".ini");
        for(const auto &path:modified){const auto hash=baseHashes.find(path);if(hash!=baseHashes.end()){
            Require(HashFile(stage+"/"+path,progress)==hash->second,"CACHE COPY CORRUPTED");unchanged.erase(path);}}
        if(s.system!="ds")Apply(s,plan,stage,cartridge,progress);
        if(s.system=="n64"){
            if(iniSidecar)Require(VCFormat::Read(rom+".ini",128*1024)==ini,"N64 INI CHANGED DURING INJECTION");
            else if(iniSource!="generic")Require(VCFormat::Read(stage+"/content/config/"+iniSource,128*1024)==ini,"N64 BASE PROFILE CHANGED DURING INJECTION");
            Require(Mkdir(stage+"/content/config"),"CANNOT CREATE N64 CONFIG DIRECTORY");
            VCFormat::Write(stage+"/content/config/"+plan.target.substr(plan.target.find_last_of('/')+1)+".ini",ini);
            VCFormat::Write(stage+"/"+N64Profile::Marker,std::string(N64Profile::Version)+"\n"+iniSource+"\n");
        }
        if(s.system=="ds"&&(s.brightness>=0||s.upscale>=0)){auto json=Read(stage+"/content/0010/configuration_cafe.json");Require(!json.empty(),"BASE CONFIGURATION_CAFE.JSON MISSING");PatchNumber(json,"Brightness",s.brightness);PatchNumber(json,"PixelArtUpscaler",s.upscale);Require(Write(stage+"/content/0010/configuration_cafe.json",json),"CANNOT WRITE DISPLAY CONFIG");}
        std::vector<File> stagedFiles;Walk(stage+"/","",stagedFiles);uint64_t actualBytes=0;
        for(const auto &f:stagedFiles)actualBytes+=f.size;
        Require(used+actualBytes+65536<=uint64_t(s.cacheMiB)*1024*1024,"VC CACHE LIMIT REACHED AFTER COMPRESSION - INCREASE LIMIT");
        Require(Write(stage+"/uflow.sha256",Seal(stage,progress,unchanged)),"CANNOT WRITE CACHE VERIFICATION MANIFEST");
        Require(Write(stage+"/uflow.complete",key),"CANNOT COMMIT CACHE MARKER");
        if(!Dir(output))Require(std::rename(stage.c_str(),output.c_str())==0,"CANNOT COMMIT INJECTION DIRECTORY");else output=stage; // force rebuild keeps old cache intact
        work.committed=true;
    }
    GameEntry result=game;result.platform=Platform::WiiU;result.installed=false;result.installedTitleId=0;
    result.privateInjectionStage=!reuse;
    result.absolutePath=output;const auto relative=output.substr(sd.size());result.titleId=Xml(Read(output+"/code/app.xml"),"title_id");
    result.rpxRelative=relative+"/"+rpx;result.codeRelative=relative+"/code";result.contentRelative=relative+"/content";
    result.saveRelative="wiiu/vc-saves/"+s.system+"/"+romHash+"/"+result.titleId;
    Require(result.rpxRelative.size()<0x280&&result.contentRelative.size()<0x280,"CACHE PATH TOO LONG");
    error=ValidateRawGame(result);Require(error.empty(),error);Tick(progress,"INJECTION READY");game=std::move(result);return {};
}catch(const Failure &f){return f.text;}catch(const std::exception &e){return e.what();}}
namespace {
std::string SavedRecord(const GameEntry &rom) {
    SHA h;h.Add(rom.absolutePath.data(),rom.absolutePath.size());
    const auto system=VCSystems::File(rom.absolutePath);return system.empty()?std::string():sd+"wiiu/apps/uFlow/injections/"+system+"/"+h.End()+".cfg";
}
bool ReadSaved(const GameEntry &rom,GameEntry &result) {
    const auto system=VCSystems::File(rom.absolutePath);if(system.empty()||rom.platform!=Platform::Files)return false;
    GameEntry identity;
    const auto profileCurrent=[&]{return system!="n64"||Read(result.absolutePath+"/"+N64Profile::Marker,512).rfind(std::string(N64Profile::Version)+"\n",0)==0;};
    if(VCPackage::Load(VCPackage::Path(rom),identity,result))return identity.absolutePath==rom.absolutePath&&profileCurrent();
    // A published package takes precedence over an older legacy cache reference.
    if(UFlowRom::File(VCPackage::Path(rom)+"/game.cfg"))return false;
    auto record=Read(SavedRecord(rom),4096);
    if(record.empty())record=Read(SavedRecord(rom)+".bak",4096);
    std::istringstream in(record);std::string version,path;
    result=rom;std::getline(in,version);std::getline(in,path);
    std::getline(in,result.absolutePath);std::getline(in,result.rpxRelative);
    std::getline(in,result.saveRelative);std::getline(in,result.titleId);
    const auto relative=UFlowRom::RelativeROM(result.absolutePath);
    if(version!=(system=="ds"?"UFLOW-SAVED-DS-1":"UFLOW-SAVED-VC-1")||path!=rom.absolutePath||relative.rfind(Cache(system),0)!=0||
       !UFlowRom::RelativePath(relative)||!UFlowRom::RelativePath(result.rpxRelative)||
       result.rpxRelative.rfind(relative+"/code/",0)!=0||
       !UFlowRom::RelativePath(result.saveRelative)||result.saveRelative.rfind("wiiu/vc-saves/"+system+"/",0)!=0||
       result.titleId.size()!=16||result.titleId.rfind("00050000",0)!=0)return false;
    if(Read(result.absolutePath+"/uflow.complete",256).size()!=64)return false;
    result.platform=Platform::WiiU;result.installed=false;result.installedTitleId=0;
    result.privateInjectionStage=false;
    result.codeRelative=relative+"/code";result.contentRelative=relative+"/content";
    return ValidateRawGame(result).empty()&&profileCurrent();
}
}
bool HasSaved(const GameEntry &rom) {GameEntry saved;return ReadSaved(rom,saved);}
std::string SavePrepared(const GameEntry &rom,GameEntry &prepared,Progress progress) {try {
    const auto system=VCSystems::File(rom.absolutePath);
    const auto folder=VCPackage::Path(rom);
    Require(!system.empty()&&rom.platform==Platform::Files&&!UFlowRom::RelativeROM(rom.absolutePath).empty()&&ValidateRawGame(prepared).empty(),"INVALID PREPARED VC OUTPUT");
    GameEntry identity,current;
    if(VCPackage::Load(folder,identity,current)&&current.absolutePath==prepared.absolutePath)return {};
    Require(prepared.absolutePath.rfind(sd+Cache(system),0)==0,"INVALID PREPARED VC CACHE PATH");
    const auto owner="UFLOW-ROM-PACKAGE-1\n"+rom.absolutePath.substr(rom.absolutePath.find_last_of('/')+1)+"\n";
    Tick(progress,"SAVING BESIDE ROM");
    if(mkdir(folder.c_str(),0777)==0){
        if(!Write(folder+"/uflow.package",owner)){
            std::remove((folder+"/uflow.package").c_str());rmdir(folder.c_str());
            Require(false,"CANNOT MARK INJECTION FOLDER - CHECK SD SPACE");
        }
    }else{
        Require(VCPackage::Directory(folder),"CANNOT CREATE INJECTION FOLDER - CHECK SD SPACE / ROM NAME LENGTH");
        Require(Read(folder+"/uflow.package")==owner,"INJECTION FOLDER ALREADY EXISTS - ORIGINAL FILES UNCHANGED");
    }
    const auto key=Read(prepared.absolutePath+"/uflow.complete",256);
    Require(key.size()==64&&key.find_first_not_of("0123456789abcdef")==std::string::npos,"INVALID PREPARED INJECTION MARKER");
    std::string destination=folder+"/game-"+key;struct stat info{};int serial=0;
    while(lstat(destination.c_str(),&info)==0&&serial<10000)destination=folder+"/game-"+key+"-"+std::to_string(++serial);
    Require(serial<10000,"TOO MANY INJECTION VERSIONS");
    GameEntry next=prepared;const auto oldRelative=UFlowRom::RelativeROM(prepared.absolutePath);
    Require(prepared.rpxRelative.rfind(oldRelative+"/code/",0)==0,"INVALID PREPARED EXECUTABLE PATH");
    next.absolutePath=destination;const auto relative=UFlowRom::RelativeROM(destination);
    next.rpxRelative=relative+prepared.rpxRelative.substr(oldRelative.size());next.codeRelative=relative+"/code";next.contentRelative=relative+"/content";
    Require(next.rpxRelative.size()<0x280&&next.contentRelative.size()<0x280,"INJECTION PATH TOO LONG - SHORTEN ROM NAME");
    const auto body=VCPackage::Record(rom,next);Require(!body.empty(),"INVALID INJECTION RECORD");
    // Only this preparation's fresh output may be moved. Reused caches can be
    // referenced by OTHER ROM paths too, so a same-ROM registry lookup is insufficient.
    const bool migrating=!prepared.privateInjectionStage;
    std::string publish=prepared.absolutePath;WorkDirectory copied{"",true};
    if(migrating){
        int n=0;do{publish=prepared.absolutePath+".work-publish-"+std::to_string(n++);}while(lstat(publish.c_str(),&info)==0&&n<10000);
        Require(mkdir(publish.c_str(),0777)==0,"CANNOT CREATE MIGRATION WORK DIRECTORY");copied.path=publish;copied.committed=false;
        std::vector<File> files;Walk(prepared.absolutePath+"/","",files);uint64_t total=0,done=0;for(const auto &f:files)total+=f.size;
        for(const auto &f:files){
            const auto parent=f.relative.find_last_of('/');
            if(parent!=std::string::npos)Require(Parents(UFlowRom::RelativeROM(publish)+"/"+f.relative.substr(0,parent)),"CANNOT CREATE MIGRATION DIRECTORY");
            Copy(prepared.absolutePath+"/"+f.relative,publish+"/"+f.relative,[&](const auto &,auto bytes,auto){Tick(progress,"COPYING LEGACY INJECTION",done+bytes,total);return true;});done+=f.size;
        }
        Require(Seal(publish,progress)==Read(prepared.absolutePath+"/uflow.sha256",4*1024*1024),"MIGRATION COPY CORRUPTED - OLD INJECTION UNCHANGED");
    }
    Tick(progress,"SAVING INJECTION RECORD");
    Require(std::rename(publish.c_str(),destination.c_str())==0,"CANNOT MOVE INJECTION BESIDE ROM");copied.committed=true;
    Require(ValidateRawGame(next).empty(),"PUBLISHED INJECTION INVALID");
    const auto target=folder+"/game.cfg",temp=target+".tmp";
    Require(Write(temp,body)&&SavedFile::Commit(temp,target),"CANNOT COMMIT INJECTION RECORD - PREVIOUS VERSION KEPT");
    next.privateInjectionStage=false;prepared=std::move(next);return {};
}catch(const Failure &f){return f.text;}catch(const std::exception &e){return e.what();}}
std::string LaunchSaved(GameEntry &rom,Progress progress) {
    try {
        GameEntry saved;Require(ReadSaved(rom,saved),"SAVED INJECTION MISSING - USE INJECT ROM");
        // ReadSaved checks the record, completion marker, executable and required
        // structure. Hash all content only during preparation/migration, not Play.
        if(saved.absolutePath.rfind(sd+Cache(VCSystems::File(rom.absolutePath)),0)==0){
            const auto error=SavePrepared(rom,saved,progress);Require(error.empty(),error);
        }
        Tick(progress,"SAVED INJECTION READY");rom=std::move(saved);return {};
    }catch(const Failure &f){return f.text;}catch(...){return "CANNOT READ SAVED INJECTION";}
}
std::string LaunchOrPrepare(GameEntry &rom,const Settings &settings,Progress progress) {
    if(rom.platform!=Platform::Files||!VCSystems::Find(settings.system)||VCSystems::File(rom.absolutePath)!=settings.system)
        return "ROM DOES NOT MATCH VC INJECTION SYSTEM";
    if(HasSaved(rom))return LaunchSaved(rom,progress);
    auto candidate=rom;auto build=settings;
    // A missing saved result can leave an incomplete cache at its old key.
    // Build privately rather than trusting or overwriting that old directory.
    build.reuse=false;
    auto error=Prepare(candidate,build,progress);if(!error.empty())return error;
    error=SavePrepared(rom,candidate,progress);if(!error.empty())return error;
    rom=std::move(candidate);return {};
}
}
