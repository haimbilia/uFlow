#include "vwii_ticket.hpp"
#include <cassert>
#include <iostream>
using namespace Vwii;
template<class F>void Fails(F f){bool caught=false;try{f();}catch(const std::exception &){caught=true;}assert(caught);}
int main(int argc,char **argv){
    assert(argc==2);
    assert(Setup::HashFile(argv[1],{},"")=="a569fc78ba42e0bfee0c4043e1028fe599c30e2d8ba81a3b543afdb19a6d74d3");
    std::ifstream stream(argv[1],std::ios::binary);
    Bytes wad((std::istreambuf_iterator<char>(stream)),{});
    auto align=[](uint64_t n){return (n+63)&~uint64_t(63);};
    const auto offset=align(U32(wad,0))+align(U32(wad,8))+align(U32(wad,12));
    const auto size=U32(wad,16);assert(offset+size<=wad.size());
    Bytes ticket(wad.begin()+offset,wad.begin()+offset+size),original=ticket;
    assert(ticket.at(0x1f1)==0x49);
    assert(ForwarderCommonKeyIndex(ticket)==0&&ticket==original);
    for(unsigned i=0;i<3;++i){ticket[0x1f1]=i;assert(ForwarderCommonKeyIndex(ticket)==i);}
    for(unsigned i=3;i<256;++i)if(i!=0x49){ticket[0x1f1]=i;Fails([&]{ForwarderCommonKeyIndex(ticket);});}
    ticket=original;Put64(ticket,0x1dc,HBC);Fails([&]{ForwarderCommonKeyIndex(ticket);});
    ticket=original;Put32(ticket,0,0x10002);Fails([&]{ForwarderCommonKeyIndex(ticket);});
    ticket.resize(0x1f1);Fails([&]{ForwarderCommonKeyIndex(ticket);});
    std::cout<<"PASS: real pinned forwarder legacy key routing, standard indices, rejection, unchanged ticket\n";
}
