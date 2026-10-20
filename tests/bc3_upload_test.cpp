#include "../tools/bc3-upload-layout.h"
#include <cassert>
#include <vector>
#include <cstdio>
#ifdef UFLOW_ADDRLIB_REFERENCE
#include "r600/r600addrlib.h"
#endif
int main() {
#ifdef UFLOW_ADDRLIB_REFERENCE
    R600AddrLib reference(nullptr);
#endif
    for(unsigned w: {1u,3u,4u,5u,127u,513u,1024u,2048u})
    for(unsigned h: {1u,3u,4u,5u,129u,768u,2048u}) {
        size_t row=((w+3)/4)*16, rows=(h+3)/4;
        for(unsigned pad: {0u,128u,256u}) {
            size_t pitch=((row+127)/128)*128+pad;
            size_t size=pitch*((rows+7)/8)*8;
            std::vector<unsigned char> src(row*rows), dst(size+64,0xa5), written(size);
            for(size_t i=0;i<src.size();++i)src[i]=i*37;
            assert(UFlow_CopyBC3(w,h,src.data(),row,dst.data(),pitch,size));
            for(size_t y=0;y<rows;++y) {
                for(size_t x=0;x<row/16;++x) {
                    size_t offset=UFlow_BC3TileOffset(x,y,pitch/16);
#ifdef UFLOW_ADDRLIB_REFERENCE
                    uint32_t bit=0;
                    assert(offset==reference.ComputeSurfaceAddrFromCoordMicroTiled(
                        x,y,0,128,pitch/16,(rows+7)/8*8,ADDR_TM_1D_TILED_THIN1,false,0,0,&bit));
                    assert(bit==0);
#endif
                    assert(!memcmp(src.data()+y*row+x*16,dst.data()+offset,16));
                    for(size_t b=0;b<16;++b){assert(!written[offset+b]);written[offset+b]=1;}
                }
            }
            for(size_t i=0;i<dst.size();++i)if(i>=size || !written[i])assert(dst[i]==0xa5);
            assert(!UFlow_CopyBC3(w,h,src.data(),row-1,dst.data(),pitch,size));
            assert(!UFlow_CopyBC3(w,h,src.data(),row,dst.data(),pitch,size-1));
            assert(!UFlow_CopyBC3(w,h,src.data(),row,dst.data(),pitch+16,size));
        }
    }
    unsigned char a[16]{},b[16]{};
    assert(!UFlow_CopyBC3(0,1,a,16,b,16,16));
    assert(!UFlow_CopyBC3(2049,1,a,16,b,16,16));
    assert(!UFlow_CopyBC3(1,1,nullptr,16,b,16,16));
    assert(UFlow_BC3TileOffset(0,1,16)==16);
    assert(UFlow_BC3TileOffset(1,0,16)==32);
    assert(UFlow_BC3TileOffset(8,0,16)==1024);
    assert(UFlow_BC3TileOffset(0,8,16)==2048);
    puts("PASS: BC3 microtiles, odd dimensions, padded rows, unique writes, bounds and sentinels");
}
