#include "gc_return_stub.hpp"
#include <array>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
    std::array<uint8_t,0x1024> original{};
    const uint8_t header[] = {0x48,0,0,0x10,'S','T','U','B','H','A','X','X'};
    const uint8_t call[] = {0x3c,0x60,0,1,0x60,0x63,0,8,0x3c,0x80,0x57,0x49,
                           0x60,0x84,0x49,0x48,0x4b,0xff,0xfe,0x99};
    std::memcpy(original.data(),header,sizeof(header));
    std::memcpy(original.data()+0xf30,call,sizeof(call));
    if(argc==2) {
        FILE *file=std::fopen(argv[1],"rb");
        assert(file);
        assert(std::fread(original.data(),1,original.size(),file)==original.size());
        assert(std::fgetc(file)==EOF);
        std::fclose(file);
    }
    auto stub=original;
    assert(!PatchGameCubeReturnStub(nullptr,stub.size()));
    assert(!PatchGameCubeReturnStub(stub.data(),stub.size()-1));
    assert(stub==original);
    assert(PatchGameCubeReturnStub(stub.data(),stub.size()));
    const auto half=[&](size_t at) { return uint32_t(stub[at])<<8 | stub[at+1]; };
    assert((uint64_t(half(0xf32))<<48 | uint64_t(half(0xf36))<<32 |
            uint64_t(half(0xf3a))<<16 | half(0xf3e))==0x0001000248435641ULL);
    for(size_t i=0;i<stub.size();++i)
        if(i!=0xf37 && i!=0xf3a && i!=0xf3b && i!=0xf3e && i!=0xf3f)
            assert(stub[i]==original[i]);
    // Unknown instruction/header must fail before writing anything.
    for(size_t at : {size_t(4),size_t(0xf30),size_t(0xf40)}) {
        stub=original; stub[at]^=1;
        const auto before=stub;
        assert(!PatchGameCubeReturnStub(stub.data(),stub.size()));
        assert(stub==before);
    }
    puts("PASS: GC return stub full HCVA target, exact guards, unchanged fallback/code");
}
