#include "vwii_plan.hpp"
#include <cassert>
#include <iostream>
using namespace Vwii;
// Model the real ISFS limit independently of the production path validator.
void NandLimit(const std::string &path){
    assert(path.starts_with("/vol/slccmpt01/"));
    std::istringstream parts(path.substr(std::string("/vol/slccmpt01/").size()));std::string leaf;
    while(std::getline(parts,leaf,'/')){assert(!leaf.empty()&&leaf.size()<=12);}
}
struct Fake:IO {
    std::map<std::string,Bytes> files;std::set<std::string> dirs;std::vector<std::string> writes;
    int failAt=-1,count=0;uint64_t free=1000000000;
    void Mutation(const std::string &p){NandLimit(p);if(count++==failAt)throw std::runtime_error("injected I/O failure");writes.push_back(p);}
    bool Exists(const std::string &p)override{NandLimit(p);return files.contains(p)||dirs.contains(p);}
    Bytes Read(const std::string &p)override{NandLimit(p);Setup::Need(files.contains(p),"file missing");return files.at(p);}
    void Write(const std::string &p,const Bytes &b,uint32_t,uint32_t,uint32_t)override{Mutation(p);assert(!Exists(p));files[p]=b;}
    void Directory(const std::string &p,uint32_t,uint32_t,uint32_t)override{if(!Exists(p)){Mutation(p);dirs.insert(p);}}
    void Move(const std::string &from,const std::string &to)override{
        if(files.contains(from))Setup::Need(from.substr(from.rfind('/')+1)==to.substr(to.rfind('/')+1),"ISFS invalid file rename");
        Mutation(to);assert(Exists(from)&&!Exists(to));assert(dirs.contains(ParentPath(to)));
        if(files.contains(from)){files[to]=files[from];files.erase(from);return;}
        dirs.erase(from);dirs.insert(to);std::map<std::string,Bytes> next;
        for(auto &[p,b]:files)next[p.starts_with(from+"/")?to+p.substr(from.size()):p]=b;files=std::move(next);
    }
    void Flush()override{}
    uint64_t Free()override{return free;}
};
Bytes Digest(const Bytes &b){auto s=Setup::Hash(std::string(b.begin(),b.end()));return Bytes(s.begin(),s.begin()+20);} // deterministic injectable digest
Title Fixture(uint64_t id){Title t;t.id=id;t.ticket.resize(0x2a4);t.tmd.resize(0x1e4+36);Put32(t.ticket,0,0x10001);Put32(t.tmd,0,0x10001);Put64(t.ticket,0x1dc,id);Put64(t.tmd,0x18c,id);t.tmd[0x1df]=1;t.contents[0]={1,2,3};Put64(t.tmd,0x1ec,3);auto h=Digest(t.contents[0]);std::copy(h.begin(),h.end(),t.tmd.begin()+0x1f4);return t;}
Fake Fresh(){Fake io;io.dirs.insert("/vol/slccmpt01/sys");Bytes uid(12);Put64(uid,0,0x100000002ULL);Put32(uid,8,0x1000);io.files[UidPath]=uid;io.files["/vol/slccmpt01/title/00000001/00000002/content/title.tmd"]={9,8,7};return io;}
template<class F>void Fails(F f){bool caught=false;try{f();}catch(const std::exception &){caught=true;}assert(caught);}
int main(){
    const auto hbcRoot=TitlePath(HBC);
    assert(StagePath(hbcRoot+"/content")==hbcRoot+"/uflow-new/content");
    assert(PreviousPath(hbcRoot+"/content")==hbcRoot+"/uflow-old/content");
    assert(StagePath(TicketPath(HBC))=="/vol/slccmpt01/ticket/00010001/uflow-new/4f484243.tik");
    assert(PreviousPath(TicketPath(HBC))=="/vol/slccmpt01/ticket/00010001/uflow-old/4f484243.tik");
    assert(StagePath(UidPath)=="/vol/slccmpt01/sys/uflow-new/uid.sys");
    assert(PreviousPath(UidPath)=="/vol/slccmpt01/sys/uflow-old/uid.sys");
    auto renameFailure=Fresh();Fails([&]{renameFailure.Move(UidPath,"/vol/slccmpt01/sys/uid.ufo");});assert(renameFailure.writes.empty()&&renameFailure.Exists(UidPath));
    for(const auto &path:{hbcRoot+"/content.uflow-new",hbcRoot+"/content.uflow-old",TicketPath(HBC)+".uflow-new",std::string(UidPath)+".uflow-old",hbcRoot+"/1234567890123",hbcRoot+"/../content",hbcRoot+"/",hbcRoot+"//content"})Fails([&]{CheckNandPath(path);});
    CheckNandPath(hbcRoot+"/123456789012");
    assert(!Required(1));assert(Required(2)&&Required(4)&&Required(1<<12));
    assert(!Allowed(0x100000002ULL)&&!Allowed(0x100000050ULL));Fails([]{TitlePath(0x100000050ULL);});
    auto title=Fixture(HBC);Validate(title,Digest);
    auto corrupt=title;corrupt.contents[0][0]^=1;Fails([&]{Validate(corrupt,Digest);});
    corrupt=title;corrupt.tmd[0x1ea]=0x80;Fails([&]{Validate(corrupt,Digest);});
    auto io=Fresh();auto original=io.files;auto p=Prepare(io,{title,Fixture(WiiFlow),Fixture(0x1000000f9ULL)},Digest);
    assert(io.writes.empty()&&p.titles.size()==3&&p.uidAfter.size()==48);
    Install(io,p,{});assert(Matches(io,title));assert(io.files.at("/vol/slccmpt01/title/00000001/00000002/content/title.tmd")==original.at("/vol/slccmpt01/title/00000001/00000002/content/title.tmd"));
    auto rerun=Prepare(io,{title,Fixture(WiiFlow),Fixture(0x1000000f9ULL)},Digest);assert(rerun.titles.empty()&&rerun.uidBefore==rerun.uidAfter);
    auto count=io.writes.size();Install(io,rerun,{});assert(io.writes.size()==count);
    auto bad=Fresh();bad.free=0;Fails([&]{Prepare(bad,{title},Digest);});assert(bad.writes.empty());
    bad=Fresh();bad.files[UidPath].push_back(1);Fails([&]{Prepare(bad,{title},Digest);});assert(bad.writes.empty());
    bad=Fresh();p=Prepare(bad,{title},Digest);bad.files[UidPath][0]=1;Fails([&]{Install(bad,p,{});});assert(bad.writes.empty());
    for(const auto &path:{StagePath(hbcRoot+"/content"),StagePath(TicketPath(HBC)),StagePath(UidPath),PreviousPath(hbcRoot+"/content"),PreviousPath(TicketPath(HBC)),PreviousPath(UidPath)}){
        bad=Fresh();bad.files[path]={42};Fails([&]{Prepare(bad,{title},Digest);});assert(bad.writes.empty()&&bad.files.at(path)==Bytes{42});
    }
    // Install all six titles through the filename-limited adapter, then update
    // an existing title and verify short-name retained originals and reruns.
    auto all=Fresh();std::vector<Title> six{title,Fixture(WiiFlow)};
    for(uint64_t id=0x1000000f8ULL;id<=0x1000000fbULL;++id)six.push_back(Fixture(id));
    auto allPlan=Prepare(all,six,Digest);assert(all.writes.empty());Install(all,allPlan,{});
    for(const auto &t:six)assert(Matches(all,t));
    auto updated=title;updated.ticket.back()=1;
    auto updatePlan=Prepare(all,{updated},Digest);Install(all,updatePlan,{});
    assert(all.files.at(PreviousPath(TicketPath(HBC)))==title.ticket);
    assert(all.files.at(PreviousPath(hbcRoot+"/content")+"/title.tmd")==title.tmd);
    assert(Matches(all,updated));assert(Prepare(all,{updated},Digest).titles.empty());
    // Reproduce the old build's exact stop: all content/tickets and UID staged,
    // uid.sys still original, nothing published. Recovery is read-only until
    // Install, and copies verified bytes to new same-basename staging paths.
    auto legacy=Fresh();auto legacyPlan=Prepare(legacy,six,Digest);
    legacy.files["/vol/slccmpt01/sys/uid.ufn"]=legacyPlan.uidAfter;
    for(const auto &t:six){const auto root=TitlePath(t.id),stage=LegacySidecarPath(root+"/content",false);
        legacy.dirs.insert(root);legacy.dirs.insert(stage);legacy.files[stage+"/title.tmd"]=t.tmd;
        for(const auto &[cid,data]:t.contents)legacy.files[stage+"/"+Hex(cid,8)+".app"]=data;
        legacy.files[LegacySidecarPath(TicketPath(t.id),false)]=t.ticket;
    }
    const auto legacyOriginal=legacy;
    auto recovery=Prepare(legacy,six,Digest);assert(recovery.resumeLegacy&&legacy.writes.empty());
    Install(legacy,recovery,{}); // same explicit publish path after confirmation
    for(const auto &t:six)assert(Matches(legacy,t));
    for(const auto &[path,data]:legacyOriginal.files)if(path!=UidPath)assert(legacy.files.at(path)==data);
    assert(legacy.files.at(PreviousPath(UidPath))==legacyOriginal.files.at(UidPath));
    assert(Prepare(legacy,six,Digest).titles.empty());
    for(const auto &path:{std::string("/vol/slccmpt01/sys/uid.ufn"),LegacySidecarPath(TicketPath(HBC),false),LegacySidecarPath(hbcRoot+"/content",false)+"/title.tmd",LegacySidecarPath(hbcRoot+"/content",false)+"/00000000.app"}){
        auto broken=legacyOriginal;broken.files.at(path)[0]^=1;Fails([&]{Prepare(broken,six,Digest);});assert(broken.writes.empty());
    }
    auto changed=legacyOriginal;recovery=Prepare(changed,six,Digest);changed.files[LegacySidecarPath(TicketPath(HBC),false)][0]^=1;
    Fails([&]{Install(changed,recovery,{});});assert(changed.writes.empty());
    auto movedUid=legacyOriginal;movedUid.files["/vol/slccmpt01/sys/uid.ufo"]=movedUid.files.at(UidPath);
    Fails([&]{Prepare(movedUid,six,Digest);});assert(movedUid.writes.empty());
    for(const auto &path:{std::string("/vol/slccmpt01/sys/uid.ufn"),LegacySidecarPath(TicketPath(HBC),false),LegacySidecarPath(hbcRoot+"/content",false)+"/00000000.app"}){
        auto missing=legacyOriginal;missing.files.erase(path);Fails([&]{Prepare(missing,six,Digest);});assert(missing.writes.empty());
    }
    auto partial=Fresh();partial.files["/vol/slccmpt01/sys/uid.ufn"]=legacyPlan.uidAfter;
    Fails([&]{Prepare(partial,six,Digest);});assert(partial.writes.empty());
    // Existing title saves are retained, and every injected mutation failure is reported.
    for(int failure=0;failure<18;++failure){bad=Fresh();auto root=TitlePath(HBC);bad.dirs.insert(root+"/data");bad.files[root+"/data/save"]={42};p=Prepare(bad,{title},Digest);bad.failAt=failure;
        try{Install(bad,p,{});}catch(const std::exception &){}assert(bad.files.at(root+"/data/save")==Bytes{42});assert(bad.files.at("/vol/slccmpt01/title/00000001/00000002/content/title.tmd")==Bytes({9,8,7}));
    }
    std::cout<<"PASS: ISFS same-basename moves/12-byte names, six-title install/update, exact legacy-stop recovery and tamper/race rejection, retained originals, no-write preflight, readback/idempotence and injected failures\n";
}
