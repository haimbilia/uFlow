#pragma once
#include "installer.hpp"
#include <map>

namespace Vwii {
using Bytes=std::vector<unsigned char>;
constexpr uint64_t HBC=0x000100014f484243ULL, WiiFlow=0x0001000144574641ULL;
inline uint16_t U16(const Bytes &b,size_t p){Setup::Need(p+2<=b.size(),"Truncated vWii data");return (b[p]<<8)|b[p+1];}
inline uint32_t U32(const Bytes &b,size_t p){return (uint32_t(U16(b,p))<<16)|U16(b,p+2);}
inline uint64_t U64(const Bytes &b,size_t p){return (uint64_t(U32(b,p))<<32)|U32(b,p+4);}
inline void Put32(Bytes &b,size_t p,uint32_t v){for(int i=3;i>=0;--i){b.at(p+i)=v;v>>=8;}}
inline void Put64(Bytes &b,size_t p,uint64_t v){Put32(b,p,v>>32);Put32(b,p+4,v);}
inline std::string Hex(uint64_t id,int width=16){std::ostringstream s;s<<std::hex<<std::setw(width)<<std::setfill('0')<<id;return s.str();}
inline bool Allowed(uint64_t id){return id==HBC||id==WiiFlow||(id>=0x1000000f8ULL&&id<=0x1000000fbULL);}
inline bool Required(uint32_t bits){return Setup::On(bits,1)||Setup::On(bits,2)||Setup::On(bits,12);}
inline std::string TitlePath(uint64_t id){Setup::Need(Allowed(id),"Unapproved vWii title");return "/vol/slccmpt01/title/"+Hex(id>>32,8)+"/"+Hex(uint32_t(id),8);}
inline std::string TicketPath(uint64_t id){Setup::Need(Allowed(id),"Unapproved vWii ticket");return "/vol/slccmpt01/ticket/"+Hex(id>>32,8)+"/"+Hex(uint32_t(id),8)+".tik";}
// ISFS (vWii NAND) allows at most 12 bytes per path component.
inline void CheckNandPath(const std::string &path){
    const std::string mount="/vol/slccmpt01/";
    Setup::Need(path.starts_with(mount)&&path.size()>mount.size(),"Invalid vWii path: "+path);
    for(size_t pos=mount.size();pos<path.size();){
        const auto end=path.find('/',pos);const auto leaf=path.substr(pos,end==std::string::npos?end:end-pos);
        Setup::Need(!leaf.empty()&&leaf.size()<=12&&leaf!="."&&leaf!=".."&&leaf.find('\0')==std::string::npos,"Invalid vWii path component: "+path);
        if(end==std::string::npos)break;
        pos=end+1;Setup::Need(pos<path.size(),"Invalid trailing slash in vWii path");
    }
}
inline std::string LegacySidecarPath(const std::string &path,bool old){
    CheckNandPath(path);const auto slash=path.rfind('/'),dot=path.rfind('.');
    const auto result=path.substr(0,dot!=std::string::npos&&dot>slash?dot:path.size())+(old?".ufo":".ufn");
    CheckNandPath(result);return result;
}
inline std::string ParentPath(const std::string &path){return path.substr(0,path.rfind('/'));}
// ISFS rename preserves a FILE's basename. Stage/retain in separate folders,
// never rename uid.sys to uid.ufo or 4f484243.tik to 4f484243.ufn.
inline std::string SidecarPath(const std::string &path,bool old){
    CheckNandPath(path);const auto slash=path.rfind('/');
    auto out=path.substr(0,slash)+(old?"/uflow-old":"/uflow-new")+path.substr(slash);
    CheckNandPath(out);return out;
}
inline std::string StagePath(const std::string &path){return SidecarPath(path,false);}
inline std::string PreviousPath(const std::string &path){return SidecarPath(path,true);}
struct Title {uint64_t id=0;Bytes ticket,tmd;std::map<uint32_t,Bytes> contents;};
// SHA-1 is the Wii TMD format's content digest, not a download trust decision.
using Sha1=std::function<Bytes(const Bytes &)>;
inline void Validate(const Title &t,const Sha1 &sha){
    Setup::Need(Allowed(t.id),"Refusing an unapproved vWii title");
    Setup::Need(t.ticket.size()>=0x2a4&&t.tmd.size()>=0x1e4,"Incomplete vWii title metadata");
    Setup::Need(U32(t.ticket,0)==0x10001&&U32(t.tmd,0)==0x10001,"Unsupported title signature format");
    Setup::Need(U64(t.ticket,0x1dc)==t.id&&U64(t.tmd,0x18c)==t.id,"vWii title ID mismatch");
    const auto n=U16(t.tmd,0x1de);Setup::Need(n>0&&n<=128&&t.contents.size()==n&&t.tmd.size()==0x1e4+size_t(n)*36,"Invalid vWii content table");
    std::set<uint16_t> indices;bool boot=false;uint64_t total=0;
    for(unsigned i=0;i<n;++i){size_t p=0x1e4+i*36;auto cid=U32(t.tmd,p);auto size=U64(t.tmd,p+8);
        Setup::Need(!(U16(t.tmd,p+6)&0x8000),"Shared NAND content writes are forbidden");
        Setup::Need(indices.insert(U16(t.tmd,p+4)).second,"Duplicate content index");
        boot|=U16(t.tmd,p+4)==U16(t.tmd,0x1e0);
        auto it=t.contents.find(cid);Setup::Need(it!=t.contents.end()&&it->second.size()==size&&size<=32*1024*1024,"Missing/oversized title content");
        auto digest=sha(it->second);Setup::Need(digest.size()==20&&std::equal(digest.begin(),digest.end(),t.tmd.begin()+p+16),"vWii content hash mismatch");total+=size;
    }
    Setup::Need(boot&&total<=64*1024*1024,"Invalid title boot index or total size");
}
struct IO {
    virtual ~IO()=default;
    virtual bool Exists(const std::string &)=0;
    virtual Bytes Read(const std::string &)=0;
    virtual void Write(const std::string &,const Bytes &,uint32_t mode,uint32_t uid,uint32_t gid)=0;
    virtual void Directory(const std::string &,uint32_t mode,uint32_t uid,uint32_t gid)=0;
    virtual void Move(const std::string &,const std::string &)=0;
    virtual void Flush()=0;
    virtual uint64_t Free()=0;
};
struct Plan {std::vector<Title> titles;Bytes uidBefore,uidAfter;std::map<uint64_t,uint32_t> uids;uint64_t bytes=0;bool resumeLegacy=false;};
inline constexpr const char *UidPath="/vol/slccmpt01/sys/uid.sys";
inline bool Matches(IO &io,const Title &t){
    const auto root=TitlePath(t.id)+"/content";
    if(!io.Exists(root+"/title.tmd")||!io.Exists(TicketPath(t.id))||io.Read(root+"/title.tmd")!=t.tmd||io.Read(TicketPath(t.id))!=t.ticket)return false;
    for(const auto &[id,data]:t.contents)if(!io.Exists(root+"/"+Hex(id,8)+".app")||io.Read(root+"/"+Hex(id,8)+".app")!=data)return false;
    return true;
}
inline bool LegacyStaged(IO &io,const Title &t){
    const auto content=LegacySidecarPath(TitlePath(t.id)+"/content",false),ticket=LegacySidecarPath(TicketPath(t.id),false);
    if(!io.Exists(content)&&!io.Exists(ticket))return false;
    Setup::Need(io.Exists(content)&&io.Exists(ticket),"Incomplete legacy staging; inspection required");
    Setup::Need(io.Read(ticket)==t.ticket&&io.Read(content+"/title.tmd")==t.tmd,"Legacy staging metadata mismatch; inspection required");
    for(const auto &[cid,data]:t.contents)Setup::Need(io.Read(content+"/"+Hex(cid,8)+".app")==data,"Legacy staging content mismatch; inspection required");
    return true;
}
inline Plan Prepare(IO &io,std::vector<Title> titles,const Sha1 &sha){
    Plan p;p.uidBefore=io.Read(UidPath);p.uidAfter=p.uidBefore;
    Setup::Need(!p.uidBefore.empty()&&p.uidBefore.size()%12==0&&p.uidBefore.size()<1024*1024,"Invalid vWii UID table; no console writes performed");
    uint32_t maxUid=0x1000;std::set<uint32_t> used;bool menu=false;
    for(size_t i=0;i<p.uidBefore.size();i+=12){auto id=U64(p.uidBefore,i);auto uid=U32(p.uidBefore,i+8);
        Setup::Need(uid>=0x1000&&uid<0xfffffffe&&used.insert(uid).second&&!p.uids.contains(id),"Invalid/duplicate vWii UID");
        p.uids[id]=uid;maxUid=std::max(maxUid,uid);menu|=id==0x100000002ULL&&uid==0x1000;
    }
    Setup::Need(menu,"vWii System Menu UID is missing");std::set<uint64_t> ids,legacy;
    for(auto &t:titles){Validate(t,sha);Setup::Need(ids.insert(t.id).second,"Duplicate setup title");
        const auto root=TitlePath(t.id),ticket=TicketPath(t.id);
        const bool matched=Matches(io,t);
        for(const auto &path:{StagePath(root+"/content"),StagePath(ticket)})
            Setup::Need(!io.Exists(path),"Previous console installation needs recovery: "+path);
        for(const auto &path:{LegacySidecarPath(root+"/content",true),LegacySidecarPath(ticket,true)})
            Setup::Need(!io.Exists(path),"Unexpected legacy retained original; inspection required: "+path);
        if(LegacyStaged(io,t))legacy.insert(t.id);
        if(!p.uids.contains(t.id)){p.uids[t.id]=++maxUid;auto pos=p.uidAfter.size();p.uidAfter.resize(pos+12);Put64(p.uidAfter,pos,t.id);Put32(p.uidAfter,pos+8,maxUid);}
        if(matched)continue;
        for(const auto &path:{PreviousPath(root+"/content"),PreviousPath(ticket)})Setup::Need(!io.Exists(path),"Retained previous title needs recovery: "+path);
        p.bytes+=t.ticket.size()+t.tmd.size();for(auto &[id,data]:t.contents)p.bytes+=data.size();p.titles.push_back(std::move(t));
    }
    Setup::Need(!io.Exists(StagePath(UidPath)),"Previous UID publication needs recovery");
    if(p.uidAfter!=p.uidBefore)Setup::Need(!io.Exists(PreviousPath(UidPath)),"Retained UID publication needs recovery");
    Setup::Need(!io.Exists(LegacySidecarPath(UidPath,true)),"Unexpected legacy UID original; inspection required");
    const auto legacyUid=LegacySidecarPath(UidPath,false);
    if(!legacy.empty()||io.Exists(legacyUid)){
        Setup::Need(!legacy.empty()&&io.Exists(legacyUid)&&io.Read(legacyUid)==p.uidAfter,"Legacy UID staging differs; inspection required");
        for(const auto &t:p.titles)Setup::Need(legacy.contains(t.id),"Legacy transaction is incomplete; inspection required");
        p.resumeLegacy=!p.titles.empty();
        if(p.resumeLegacy)Setup::Need(p.uidBefore!=p.uidAfter,"Unexpected legacy publication state; inspection required");
    }
    Setup::Need(io.Free()>=p.bytes+8*1024*1024,"Insufficient free vWii console storage");return p;
}
inline void CheckedWrite(IO &io,const std::string &path,const Bytes &data,uint32_t mode=0x660,uint32_t uid=0,uint32_t gid=0){
    io.Write(path,data,mode,uid,gid);Setup::Need(io.Read(path)==data,"Console write verification failed: "+path);
}
inline void Publish(IO &io,const std::string &stage,const std::string &dest){
    if(io.Exists(dest)){io.Directory(ParentPath(PreviousPath(dest)),0x660,0,0);io.Move(dest,PreviousPath(dest));}
    io.Move(stage,dest);io.Flush();
}
// Called only after the explicit console-storage confirmation. No whole-system
// repair, shared content-map changes, region change, uninstaller or rollback.
inline void Install(IO &io,const Plan &p,const Setup::Progress &progress){
    Setup::Need(io.Read(UidPath)==p.uidBefore,"Console UID table changed since preflight");
    if(p.resumeLegacy){
        Setup::Need(io.Read(LegacySidecarPath(UidPath,false))==p.uidAfter,"Legacy UID staging changed since preflight");
        for(const auto &t:p.titles)Setup::Need(LegacyStaged(io,t),"Legacy staging changed since preflight");
    }
    for(size_t i=0;i<p.titles.size();++i){const auto &t=p.titles[i];auto root=TitlePath(t.id),stage=StagePath(root+"/content"),ticket=TicketPath(t.id);
        Setup::Tick(progress,"Preparing console files",Hex(t.id),i,p.titles.size(),false);
        io.Directory("/vol/slccmpt01/title/"+Hex(t.id>>32,8),0x664,0,0);io.Directory(root,0x664,0,0);
        io.Directory(ParentPath(stage),0x660,0,0);
        io.Directory(stage,0x660,0,0);io.Directory("/vol/slccmpt01/ticket/"+Hex(t.id>>32,8),0,0,0);
        io.Directory(ParentPath(StagePath(ticket)),0x660,0,0);
        CheckedWrite(io,stage+"/title.tmd",t.tmd);
        for(const auto &[cid,data]:t.contents){Setup::Tick(progress,"Verifying console staging",Hex(t.id)+" / "+Hex(cid,8),i,p.titles.size(),false);CheckedWrite(io,stage+"/"+Hex(cid,8)+".app",data);}
        CheckedWrite(io,StagePath(ticket),t.ticket);
    }
    if(p.uidAfter!=p.uidBefore){io.Directory(ParentPath(StagePath(UidPath)),0x660,0,0);CheckedWrite(io,StagePath(UidPath),p.uidAfter);Publish(io,StagePath(UidPath),UidPath);}
    for(size_t i=0;i<p.titles.size();++i){const auto &t=p.titles[i];auto root=TitlePath(t.id),ticket=TicketPath(t.id);
        Setup::Tick(progress,"Installing vWii prerequisites",Hex(t.id),i,p.titles.size(),false);
        io.Directory(root+"/data",0x600,p.uids.at(t.id),U16(t.tmd,0x198));
        Publish(io,StagePath(root+"/content"),root+"/content");Publish(io,StagePath(ticket),ticket);
        Setup::Need(Matches(io,t),"Installed title verification failed: "+Hex(t.id));
    }
    io.Flush();Setup::Need(io.Read(UidPath)==p.uidAfter,"Published UID verification failed");
}
}
