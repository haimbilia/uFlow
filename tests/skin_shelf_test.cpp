#include "skin_layout.hpp"
#include <cassert>
#include <iostream>
int main(){
    const auto parse=[](std::string fields,int schema=2){return SkinLayout::Parse("{\"schema\":"+std::to_string(schema)+",\"nodes\":[{\"kind\":\"library\",\"x\":0,\"y\":0,\"width\":850,\"height\":544,\"source\":\"box\",\"mode\":\"shelf\",\"rows\":2,\"columns\":7,\"case-height\":200"+fields+"}]}");};
    auto doc=parse(",\"angles\":[82,12],\"selected-scale\":125");assert(doc.schema==2&&doc.Enhanced());assert(doc.nodes[0].shelfAngles[1]==12);
    const auto reject=[&](std::string fields,int schema=2){bool failed=false;try{parse(fields,schema);}catch(...){failed=true;}assert(failed);};
    reject("",1);reject(",\"angles\":[]");reject(",\"angles\":[181]");reject(",\"angles\":[1.5]");reject(",\"animation-ms\":0");reject(",\"selected-scale\":150");reject(",\"source\":\"logo\"");reject(",\"columns\":30,\"rows\":3");reject(",\"shelf-baselines\":[240]");reject(",\"shelf-slopes\":[0,90]");
    assert(parse(",\"angles\":[95,-180,180],\"selected-angle\":-180").nodes[0].shelfAngles[0]==95);
    reject(",\"selected-angle\":181");
    SkinShelf::Motion m;
    for(int i=0;i<12;++i)m.Advance(2,14,1.f/60,200);
    assert(m.Focus(2)>.999f&&m.Focus(1)==0);
    // A rapid reversal continues the outgoing pose; no reset/jump or queue.
    m.Advance(3,14,1.f/60,200);const auto outgoing=m.Focus(2);assert(outgoing>0&&outgoing<1);
    m.Advance(2,14,1.f/60,200);assert(m.Focus(2)>outgoing);
    for(int i=0;i<20;++i)m.Advance(13,14,1.f/60,200);
    assert(m.Focus(13)>.999f&&m.Focus(2)==0);
    m.Reset();for(float f:m.progress)assert(f==0);
    m.Advance(-1,0,.05f,200);for(float f:m.progress)assert(f==0);
    // Equivalent duration at 30Hz and 60Hz yields the same pose.
    SkinShelf::Motion a,b;for(int i=0;i<3;++i)a.Advance(0,1,1.f/30,200);for(int i=0;i<6;++i)b.Advance(0,1,1.f/60,200);
    assert(std::abs(a.Focus(0)-b.Focus(0))<.0001f);
    std::cout<<"PASS: schema gating, malformed parameters, reversible bounded motion, empty/reset, frame-rate independence\n";
}
