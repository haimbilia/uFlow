#pragma once
#include "../../app/source/case_profiles.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace Setup {
constexpr uint32_t Core=1u<<31, All=(1u<<17)-1;
// Optional RetroArch systems: one-press through retroarch-sysargs-v1 modules.
constexpr int PlayStation=13,SnesRetroArch=14,NesRetroArch=15,GbaRetroArch=16;
struct Component {const char *id,*name;};
inline constexpr Component components[]={
    {"wiiu","Wii U - installed and raw games"},{"wii","Wii - WiiFlow / USB Loader GX"},
    {"gamecube","GameCube - Nintendont"},{"ds","Nintendo DS - Virtual Console"},
    {"nes","NES - Virtual Console"},{"snes","SNES - Virtual Console"},{"gba","Game Boy Advance - Virtual Console"},
    {"n64","Nintendo 64 - Virtual Console"},{"pce","TurboGrafx-16 / PC Engine - Virtual Console"},{"msx","MSX - Virtual Console"},{"noods","NooDS - alternative DS emulator"},
    {"startup","Start-on-boot support (initially OFF)"},{"vwii-tools","vWii console setup (automatic for Wii / GC)"},
    {"ps1","PlayStation - RetroArch (PCSX ReARMed)"},{"snes-retroarch","SNES - RetroArch (Snes9x)"},
    {"nes-retroarch","NES - RetroArch (Nestopia UE)"},{"gba-retroarch","Game Boy Advance - RetroArch (mGBA)"}};
inline constexpr int ComponentCount=static_cast<int>(sizeof(components)/sizeof(components[0]));
inline bool On(uint32_t bits,int i){return (bits&(1u<<i))!=0;}
struct FileDef {std::string path,hash;uint64_t size;uint32_t mask;char mode;}; // r=replace, k=keep, b=base
struct Item {FileDef file;std::string before,text;bool generated=false,write=false;};
struct Plan {std::vector<Item> items;std::vector<std::string> dirs;uint64_t bytes=0;size_t preserved=0;uint32_t selected=0;};
using Progress=std::function<bool(const std::string &,const std::string &,uint64_t,uint64_t,bool)>;
inline void Need(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
inline void Tick(const Progress &p,const std::string &phase,const std::string &path,uint64_t n,uint64_t total,bool cancel=true){
    if(p&&!p(phase,path,n,total,cancel))throw std::runtime_error("Stopped. No automatic restore. Re-run setup if publishing had started.");
}
// Same streaming SHA-256 algorithm used by uFlow's injection verifier.
struct SHA {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char block[64]{};uint64_t count=0;size_t used=0;
    static uint32_t R(uint32_t x,int n){return(x>>n)|(x<<(32-n));}
    void Compress(){
        static const uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|(uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;++i){uint32_t t=hh+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));hh=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
        uint32_t v[]={a,b,c,d,e,f,g,hh};for(int i=0;i<8;++i)h[i]+=v[i];
    }
    void Add(const char *p,size_t n){count+=n;while(n){auto take=std::min(n,sizeof(block)-used);std::memcpy(block+used,p,take);used+=take;p+=take;n-=take;if(used==64){Compress();used=0;}}}
    std::string End(){uint64_t bits=count*8;char p=char(0x80);Add(&p,1);p=0;while(used!=56)Add(&p,1);for(int i=7;i>=0;--i){p=char(bits>>(i*8));Add(&p,1);}std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto x:h)s<<std::setw(8)<<x;return s.str();}
};
inline std::string Hash(const std::string &text){SHA s;s.Add(text.data(),text.size());return s.End();}
inline bool Safe(const std::string &path){
    if(path.empty()||path.size()>700||path.front()=='/'||path.back()=='/'||path.find_first_of("\\:\t\r\n")!=std::string::npos)return false;
    std::istringstream in(path);std::string part;
    while(std::getline(in,part,'/'))if(part.empty()||part=="."||part==".."||part.find('\0')!=std::string::npos)return false;
    return true;
}
inline bool Allowed(const std::string &p){
    if(p=="wiiu/apps/WiiFlow.wuhb")return true;
    if(p=="wiiu/apps/retroarch.wuhb")return true;
    for(const char *core:{"pcsx_rearmed","snes9x","nestopia","mgba"})
        if(p==std::string("retroarch/cores/")+core+"_libretro.rpx"||p==std::string("retroarch/cores/info/")+core+"_libretro.info")return true;
    for(const char *prefix:{"wiiu/apps/uFlow/","wiiu/apps/noods/","wiiu/vc-bases/","apps/wiiflow/","apps/usbloader_gx/","apps/nintendont/","apps/d2x-cios-installer-vwii/","apps/wiiflow_channel_installer/","wiiflow/"})if(p.rfind(prefix,0)==0)return true;
    return p=="wiiu/apps/compat_installer.wuhb"||p=="wiiu/environments/aroma/modules/RPXLoadingModule.wms"||p=="wiiu/environments/aroma/modules/PluginBackend.wms"||
        p=="wiiu/environments/aroma/modules/uFlowRawKernel.wms"||p=="wiiu/environments/aroma/modules/setup/00_mocha.rpx"||p=="wiiu/environments/aroma/modules/setup/99_autoboot.rpx"||
        p=="wiiu/environments/aroma/plugins/regionfree.wps"||p=="wiiu/environments/aroma/plugins/ftpiiu.wps";
}
inline std::string Path(const std::string &root,const std::string &rel){
    Need(Safe(rel),"Unsafe installer path");std::string p=root;struct stat st{};
    Need(lstat(root.c_str(),&st)==0&&S_ISDIR(st.st_mode),"Root missing or linked: "+root);
    std::istringstream in(rel);std::string part;
    while(std::getline(in,part,'/')){p+="/"+part;if(lstat(p.c_str(),&st)==0){Need(!S_ISLNK(st.st_mode),"Link refused: "+p);if(in.peek()!=EOF)Need(S_ISDIR(st.st_mode),"Parent is a file: "+p);}}
    return p;
}
inline bool Exists(const std::string &p){struct stat s{};return lstat(p.c_str(),&s)==0;}
inline bool File(const std::string &p){struct stat s{};return lstat(p.c_str(),&s)==0&&S_ISREG(s.st_mode);}
inline uint64_t Size(const std::string &p){struct stat s{};Need(lstat(p.c_str(),&s)==0&&S_ISREG(s.st_mode)&&s.st_size>=0,"Not a regular file: "+p);return s.st_size;}
inline void Directory(const std::string &root,const std::string &rel){
    Path(root,rel);std::string p=root;std::istringstream in(rel);std::string part;struct stat s{};
    while(std::getline(in,part,'/')){p+="/"+part;if(lstat(p.c_str(),&s)!=0)Need(mkdir(p.c_str(),0777)==0,"Cannot create: "+p);else Need(S_ISDIR(s.st_mode),"Not a directory: "+p);}
}
inline void Parents(const std::string &root,const std::string &rel){auto slash=rel.find_last_of('/');if(slash!=std::string::npos)Directory(root,rel.substr(0,slash));}
inline std::string HashFile(const std::string &path,const Progress &p,const std::string &phase,bool cancel=true){
    uint64_t total=Size(path),n=0;std::ifstream f(path,std::ios::binary);Need(bool(f),"Cannot read: "+path);SHA hash;std::array<char,65536>b{};
    while(f){f.read(b.data(),b.size());auto got=f.gcount();hash.Add(b.data(),got);n+=got;Tick(p,phase,path,n,total,cancel);}Need(f.eof()&&n==total,"Read failed: "+path);return hash.End();
}
inline void Write(const std::string &path,const std::string &text){std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(text.data(),text.size());f.flush();bool ok=bool(f);f.close();Need(ok&&!f.fail(),"Write failed: "+path);}
inline std::vector<std::pair<std::string,std::string>> Configs(uint32_t bits,std::vector<std::string> &dirs,const std::vector<FileDef> &files={}){
    std::string libraries,content,media;
    auto add=[&](int i,const std::string &parser,const std::string &ext,const std::string &folder,const std::string &launcher){
        std::string id=components[i].id;
        const char *names[]={"Wii U","Wii","GameCube","Nintendo DS","NES","SNES","GBA","N64","PC Engine","MSX"};
        const auto size=CaseProfiles::For(id,launcher,ext);
        libraries+=id+"\t"+names[i]+"\t"+parser+"\twiiflow\t"+ext+"\t1\t"+std::to_string(i)+"\t"+launcher+"\t"+std::to_string(size.width)+"\t"+std::to_string(size.height)+"\t"+std::to_string(size.thickness)+"\t1\n";
        content+=parser+"\t1\t3\tfs:/vol/external01/"+folder+"\twiiflow\t"+id+"\t"+launcher+"\n";dirs.push_back(folder);
        for(const char *kind:{"front","back","spine","fullcovers","backgrounds","logos","previews","metadata"}){
            std::string path="wiiu/apps/uFlow/media/"+id+"/"+kind;dirs.push_back(path);
            const bool hasDefault=std::any_of(files.begin(),files.end(),[&](const auto &f){return f.path==path+"/Default.png" && (f.mask&(bits|Core));});
            media+=std::string(kind)+"\tid:"+id+"\t1\t1\tfs:/vol/external01/"+path+"\t1000\t1\t70\t"+(hasDefault?"Default.png":"")+"\t100\n";
        }
    };
    if(On(bits,0))add(0,"wiiu",".rpx","wiiu/raw-games","native");
    if(On(bits,1))add(1,"wii",".wbfs","wbfs","wiiflow");
    if(On(bits,2))add(2,"gamecube",".iso","games","nintendont");
    const char *exts[]={".nds,.srl",".nes,.unf,.unif,.zip",".sfc,.smc",".gba",".z64,.v64,.n64",".pce",".rom,.mx1,.mx2"};
    // NES, SNES and GBA play through a VC injector or RetroArch. Either one
    // creates the content type; RetroArch is its default when selected.
    const int retroArch[]={-1,NesRetroArch,SnesRetroArch,GbaRetroArch,-1,-1,-1};
    for(int i=3;i<=9;++i){
        const bool viaRetroArch=retroArch[i-3]>=0&&On(bits,retroArch[i-3]);
        if(On(bits,i)||(i==3&&On(bits,10))||viaRetroArch)
            add(i,"files",exts[i-3],std::string("wiiu/roms/")+components[i].id,
                viaRetroArch?std::string(components[retroArch[i-3]].id):i==3&&!On(bits,3)?"noods":std::string(components[i].id)+"-vc");
    }
    if(On(bits,PlayStation)) {
        // CD jewel case, launched one-press through the retroarch-ps1 module.
        const std::string id=components[PlayStation].id,folder="wiiu/roms/ps1";
        libraries+=id+"\tPlayStation\tfiles\twiiflow\t.cue,.chd,.pbp\t1\t"+std::to_string(PlayStation)+"\tps1\t142\t125\t10\t1\n";
        content+="files\t1\t3\tfs:/vol/external01/"+folder+"\twiiflow\t"+id+"\tps1\n";dirs.push_back(folder);
        for(const char *kind:{"front","back","spine","fullcovers","backgrounds","logos","previews","metadata"}){
            std::string path="wiiu/apps/uFlow/media/"+id+"/"+kind;dirs.push_back(path);
            media+=std::string(kind)+"\tid:"+id+"\t1\t1\tfs:/vol/external01/"+path+"\t1000\t1\t70\t\t100\n";
        }
    }
    std::string settings="preload_builtin_covers=1\nshow_installed="+std::to_string(On(bits,0))+"\nshow_raw_wiiu="+std::to_string(On(bits,0))+"\nshow_wii="+std::to_string(On(bits,1))+"\nshow_gamecube="+std::to_string(On(bits,2))+"\n";
    return {{"libraries.cfg",libraries},{"content_sources.cfg",content},{"media_sources.cfg",media},{"uflow.cfg",settings}};
}
inline Plan Prepare(const std::string &sd,const std::string &bundle,const std::vector<FileDef> &files,uint32_t selected,const Progress &progress){
    Need(!(selected&~All),"Invalid component selection");Plan plan;plan.selected=selected;std::set<std::string> seen;
    for(const char *required:{"wiiu/environments/aroma/root.rpx","wiiu/environments/aroma/modules/setup/10_wums_loader.rpx","wiiu/environments/aroma/modules/ContentRedirectionModule.wms","wiiu/environments/aroma/modules/WUHBUtilsModule.wms"})Need(File(Path(sd,required)),"Aroma prerequisite missing");
    for(const auto &f:files){
        Need(Safe(f.path)&&Allowed(f.path)&&seen.insert(f.path).second,"Unsafe or duplicate package entry");
        // Repair an existing FTP service, never introduce one or change its settings.
        if(f.path=="wiiu/environments/aroma/plugins/ftpiiu.wps"&&!File(Path(sd,f.path)))continue;
        // The integrated backend reads its modules from WUHB content. Do not
        // install redundant standalone setup apps onto a fresh user's SD.
        if(f.path=="wiiu/apps/compat_installer.wuhb" || f.path.starts_with("apps/d2x-cios-installer-vwii/") ||
           f.path.starts_with("apps/wiiflow_channel_installer/"))continue;
        if(!(f.mask&(selected|Core)))continue;
        Need(f.size<0xffffffffULL&&f.hash.size()==64&&(f.mode=='r'||f.mode=='k'||f.mode=='b'),"Invalid package metadata");
        auto source=Path(bundle,"payload/"+f.path);Need(Size(source)==f.size,"Incomplete installer content: "+f.path);
        Need(HashFile(source,progress,"Checking bundled files")==f.hash,"Damaged installer content: "+f.path);
        plan.items.push_back({f,"","",false,false});
    }
    for(const auto &[name,text]:Configs(selected,plan.dirs,files))plan.items.push_back({{"wiiu/apps/uFlow/"+name,Hash(text),text.size(),Core,'k'},"",text,true,false});
    for(auto &item:plan.items){
        const auto dest=Path(sd,item.file.path);
        if(Exists(dest)){
            item.before=HashFile(dest,progress,"Checking existing files");
            if(item.before==item.file.hash)continue;
            Need(item.file.mode!='b',"Different existing VC base; kept untouched: "+item.file.path);
            if(item.file.mode=='k'){++plan.preserved;continue;}
        }
        item.write=true;plan.bytes+=item.file.size;
    }
    for(const auto &dir:plan.dirs){auto p=Path(sd,dir);if(Exists(p)){struct stat s{};Need(lstat(p.c_str(),&s)==0&&S_ISDIR(s.st_mode),"Folder destination occupied: "+dir);}}
    return plan;
}
inline std::string Install(const std::string &sd,const std::string &bundle,const Plan &plan,const std::string &run,const Progress &progress){
    Need(Safe(run)&&run.find('/')==std::string::npos,"Invalid run ID");const std::string work=".uflow-setup/"+run;
    Need(!Exists(Path(sd,work)),"Installation run already exists");Directory(sd,work);
    const std::string report=Path(sd,work+"/report.txt");Write(report,"STAGING - SD only. NAND prerequisites not verified.\n");
    auto append=[&](const std::string &s){std::ofstream f(report,std::ios::app);f<<s<<'\n';f.flush();Need(bool(f),"Cannot write install report");};
    try{
        for(const auto &item:plan.items)if(item.write){
            auto rel=work+"/new/"+item.file.path;Parents(sd,rel);auto dest=Path(sd,rel);Need(!Exists(dest),"Staging file already exists");
            if(item.generated)Write(dest,item.text);
            else{
                std::ifstream in(Path(bundle,"payload/"+item.file.path),std::ios::binary);Need(bool(in),"Cannot open bundled file");
                std::ofstream out(dest,std::ios::binary);Need(bool(out),"Cannot stage file");std::array<char,65536>b{};uint64_t n=0;
                while(in){in.read(b.data(),b.size());auto got=in.gcount();out.write(b.data(),got);Need(bool(out),"Copy failed: "+item.file.path);n+=got;Tick(progress,"Copying selected files",item.file.path,n,item.file.size);}
                Need(in.eof()&&n==item.file.size,"Source read failed");out.flush();bool ok=bool(out);out.close();Need(ok&&!out.fail(),"Cannot flush staged file");
            }
            Need(HashFile(dest,progress,"Verifying staged files")==item.file.hash,"Staged copy failed verification");
        }
        for(const auto &item:plan.items){const auto dest=Path(sd,item.file.path);auto hash=Exists(dest)?HashFile(dest,progress,"Rechecking destinations"):"";Need(hash==item.before,"Destination changed: "+item.file.path);}
        Tick(progress,"Publishing - do not power off","All selected files staged and verified",0,0,false);append("PUBLISHING - no automatic rollback");
        for(const auto &dir:plan.dirs)Directory(sd,dir);
        for(const auto &item:plan.items)if(item.write){
            auto dest=Path(sd,item.file.path);Parents(sd,item.file.path);
            if(!item.before.empty()){
                Need(HashFile(dest,progress,"Backing up original",false)==item.before,"Destination changed before backup");
                auto rel=work+"/backup/"+item.file.path;Parents(sd,rel);auto backup=Path(sd,rel);Need(!Exists(backup),"Backup already exists");
                append("BACKUP\t"+item.file.path+"\t"+item.before);
                Need(std::rename(dest.c_str(),backup.c_str())==0,"Cannot preserve original: "+item.file.path);
                Need(HashFile(backup,progress,"Verifying backup",false)==item.before,"Backup verification failed");
            }else Need(!Exists(dest),"Destination appeared during install");
            auto staged=Path(sd,work+"/new/"+item.file.path);
            Need(std::rename(staged.c_str(),dest.c_str())==0,"Cannot publish: "+item.file.path);
            Need(HashFile(dest,progress,"Verifying installed files",false)==item.file.hash,"Published file verification failed");
            append("VERIFIED\t"+item.file.path+"\t"+item.file.hash);
        }
        append("COMPLETE - SD files verified. Full reboot required. NAND setup not verified.");
    }catch(const std::exception &e){try{append(std::string("STOPPED: ")+e.what());}catch(...){}throw;}
    return report;
}
}
