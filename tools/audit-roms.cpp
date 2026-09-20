// Read-only host audit. No normalization is written back to source ROMs.
#include "vc_formats.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>
namespace fs = std::filesystem;
using namespace VCFormat;
std::string Sha1(const Bytes &b) {
    auto *ctx=av_sha_alloc(); Need(ctx!=nullptr,"SHA allocation failed");
    unsigned char digest[20]; av_sha_init(ctx,160);
    av_sha_update(ctx,reinterpret_cast<const unsigned char*>(b.data()),b.size());
    av_sha_final(ctx,digest);av_free(ctx);
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(auto c:digest)out<<std::setw(2)<<unsigned(c);return out.str();
}
int main(int argc,char **argv) {
    if(argc!=2){std::cerr<<"Usage: audit-roms DIRECTORY\n";return 2;}
    unsigned good=0,bad=0;
    for(auto it=fs::recursive_directory_iterator(argv[1]);it!=fs::recursive_directory_iterator();++it) {
        if(it->is_directory()&&it->path().extension()==".uflow"){it.disable_recursion_pending();continue;}
        if(!it->is_regular_file())continue;
        const auto path=it->path().string(),system=SystemFor(path);
        try {
            Need(!system.empty(),"UNRECOGNIZED ROM FORMAT");
            auto original=Read(path),rom=Cartridge(path,system);
            std::ostringstream extra;
            if(system=="ds") {
                Need(rom.size()>=512&&rom[0x12]==0,"REQUIRES STANDARD DS ROM");
                for(size_t p:{0x20,0x30}) {
                    auto off=Number(rom,p),len=Number(rom,p+12);
                    Need(off>=512&&len>0&&uint64_t(off)+len<=rom.size(),"INVALID DS EXECUTABLE RANGE");
                }
                for(size_t p:{0x40,0x48,0x50,0x58}) {
                    auto off=Number(rom,p),len=Number(rom,p+4);
                    Need(uint64_t(off)+len<=rom.size(),"INVALID DS TABLE RANGE");
                }
                auto fat=Number(rom,0x48),len=Number(rom,0x4c);
                Need(len%8==0,"INVALID DS FILE TABLE LENGTH");
                for(size_t p=fat;p<fat+len;p+=8)
                    Need(Number(rom,p)<=Number(rom,p+4)&&Number(rom,p+4)<=rom.size(),"DS FILE OUTSIDE ROM");
                extra<<" DS executables/tables/"<<len/8<<" file ranges checked";
            } else if(system=="nes") {
                extra<<" mapper="<<((uint8_t(rom[6])>>4)|(uint8_t(rom[7])&240))
                     <<" payload_sha1="<<Sha1(rom.substr(16))
                     <<" database_match="<<!NesRom::Header(rom.substr(16)).empty();
            } else if(system=="snes") {
                auto header=SnesHeader(rom);uint32_t sum=0;for(unsigned char c:rom)sum+=c;
                extra<<" header_checksum="<<Number(rom,header+30,2)<<" byte_sum="<<(sum&65535)
                     <<" checksum_match="<<((sum&65535)==Number(rom,header+30,2));
            }
            std::cout<<"PASS "<<std::quoted(path)<<" system="<<system<<" bytes="<<original.size()
                     <<" normalized_bytes="<<rom.size()<<" normalized_changed="<<(rom!=original)
                     <<" source_sha1="<<Sha1(original)<<extra.str()<<"\n";++good;
        }catch(const std::exception &e){std::cout<<"FAIL "<<std::quoted(path)<<" "<<e.what()<<"\n";++bad;}
    }
    std::cout<<"TOTAL "<<good<<" accepted, "<<bad<<" rejected. Structural checks are not an emulator compatibility guarantee.\n";
    return bad?1:0;
}
