#pragma once
#include <stddef.h>
#include <string.h>
/* GX2 1D thin microtiles: 8x8 BC3 blocks of 128 bits each.
 * R600 displayable 128-bit element order is y0,x0,x1,x2,y1,y2.
 * Coordinates and pitch are in compressed blocks, not pixels.
 */
static size_t UFlow_BC3TileOffset(size_t x, size_t y, size_t pitch) {
    const size_t tile=(y/8)*(pitch/8)+x/8;
    const size_t element=(y&1)|((x&7)<<1)|((y&6)<<3);
    return tile*1024+element*16;
}
static int UFlow_CopyBC3(unsigned w, unsigned h, const void *src, size_t src_pitch,
                         void *dst, size_t dst_pitch, size_t dst_size) {
    if (!w || !h || w>2048 || h>2048 || !src || !dst) return 0;
    const size_t blocks=(w+3)/4, rows=(h+3)/4, padded_rows=(rows+7)&~(size_t)7;
    if(src_pitch<blocks*16 || dst_pitch%128 || dst_pitch<blocks*16 ||
       dst_pitch>dst_size || padded_rows>dst_size/dst_pitch ||
       src_pitch>((size_t)-1)/rows) return 0;
    for(size_t y=0;y<rows;++y) for(size_t x=0;x<blocks;++x)
        memcpy((unsigned char*)dst+UFlow_BC3TileOffset(x,y,dst_pitch/16),
               (const unsigned char*)src+y*src_pitch+x*16,16);
    return 1;
}
