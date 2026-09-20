#include "update_transaction.hpp"
#include "update_trust.hpp"
#include <filesystem>
#include <iostream>
#include <cassert>
namespace fs=std::filesystem;
using namespace UFlowUpdate;
std::string B64(const std::string &s){const char *c="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;unsigned n=0,bits=0;for(unsigned char b:s){n=(n<<8)|b;bits+=8;while(bits>=6){bits-=6;out+=c[(n>>bits)&63];}}if(bits)out+=c[(n<<(6-bits))&63];while(out.size()%4)out+='=';return out;}
std::string Envelope(const std::string &body,const uint8_t *sk){uint8_t sig[64];crypto_ed25519_sign(sig,sk,(const uint8_t*)body.data(),body.size());return "{\"manifest\":\""+B64(body)+"\",\"signature\":\""+B64(std::string((char*)sig,64))+"\"}";}
template<class F>void Fails(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}assert(failed);}
int main(int argc,char **argv){
    if(argc==3){auto r=Parse(ReadSmall(argv[1]),PublicKey,"stable");assert(Setup::Size(argv[2])==r.size&&Setup::HashFile(argv[2],{},"")==r.sha);std::cout<<"PASS: Python-signed release accepted by production C++ verifier: "<<r.version<<"\n";return 0;}
    uint8_t seed[32]={7},sk[64],pk[32];crypto_ed25519_key_pair(sk,pk,seed);
    const std::string binary="new verified uFlow test binary",hash=Setup::Hash(binary);
    std::string body="{\"schema\":1,\"release\":\"2026.09.20-test1\",\"sequence\":2026092001,\"channel\":\"stable\",\"base_url\":\""+std::string(BaseURL)+"\",\"changes\":\"Popup updates\\nRelease notes\",\"components\":[{\"component\":\"uflow\",\"destination\":\"wiiu/apps/uFlow/uFlow.wuhb\",\"size\":"+std::to_string(binary.size())+",\"sha256\":\""+hash+"\",\"url\":\"releases/2026.09.20-test1/files/"+hash+"\"}]}";
    auto envelope=Envelope(body,sk);assert(Parse(envelope,pk,"stable").sha==hash);
    Fails([&]{Parse(envelope,pk,"testing");});uint8_t wrong[32]={0};Fails([&]{Parse(envelope,wrong,"stable");});
    auto altered=envelope;altered[30]=altered[30]=='A'?'B':'A';Fails([&]{Parse(altered,pk,"stable");});
    auto evil=body;evil.replace(evil.find("wiiu/apps/uFlow/uFlow.wuhb"),24,"../../evil");Fails([&]{Parse(Envelope(evil,sk),pk,"stable");});
    Fails([&]{Parse(std::string(1000,'['),pk,"stable");});
    char temp[]="/tmp/uflow-update-XXXXXX";auto root=std::string(mkdtemp(temp));
    for(int interruption=0;interruption<=4;++interruption){
        auto sd=root+"/case"+std::to_string(interruption);fs::create_directories(sd);
        Transaction tx(sd,pk,"stable");Durable(tx.target,"original app");Durable(tx.stage,binary);
        Durable(sd+"/wiiu/apps/uFlow/settings.cfg","user settings");
        try{tx.Commit(envelope,[&](int point){if(point==interruption)throw std::runtime_error("simulated power loss");});}catch(const std::exception &){}
        tx.Recover();assert(!fs::exists(tx.journal));
        assert(tx.Hash(tx.target)==Setup::Hash(interruption==1||interruption==2?"original app":binary));
        assert(ReadSmall(sd+"/wiiu/apps/uFlow/settings.cfg")=="user settings");
    }
    auto sd=root+"/corrupt";fs::create_directories(sd);Transaction tx(sd,pk,"stable");
    Durable(tx.target,"original app");Durable(tx.stage,"corrupt");Fails([&]{tx.Commit(envelope);});
    assert(ReadSmall(tx.target)=="original app"&&!fs::exists(tx.journal));
    fs::remove_all(root);
    std::cout<<"PASS: signatures, tampering, channel/path restrictions, JSON depth, corrupted download, commit and recovery at all four checkpoints, settings preserved\n";
}
