#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// 8-bit 4:2:0 to RGBA without swscale's generic scaling/conversion pipeline.
// Reuse chroma terms for a 2x2 block; tables cover BT.601/709 and both ranges.
class VideoColor420 {
    std::array<int,256> y_{}, rv_{}, gu_{}, gv_{}, bu_{};
    // All 8-bit BT.601/709 results fall inside [-289, 548]. A small
    // saturation table avoids three data-dependent clamps per output pixel.
    std::array<std::uint8_t,1024> saturated_{};
    bool ready_=false, full_=false, bt709_=false;
    void Pixel(std::uint8_t *out, std::uint8_t y, int r, int g, int b) const {
        const int luma=y_[y];
        out[0]=saturated_[((luma+r)>>8)+384];
        out[1]=saturated_[((luma+g)>>8)+384];
        out[2]=saturated_[((luma+b)>>8)+384];
        out[3]=255;
    }
public:
    // Pack Y,U,V,A for the GPU matrix shader. The half-size path uses the
    // same 2x2 luma box filter and shared chroma as ConvertHalf.
    static void Pack(const std::uint8_t *y,int ys,const std::uint8_t *u,int us,
                     const std::uint8_t *v,int vs,bool nv12,int width,int height,
                     bool half,std::uint8_t *out,int stride) {
        for(int row=0;row<height;row+=2) {
            const auto *y0=y+row*ys,*y1=y0+ys;
            const auto *uu=u+(row/2)*us,*vv=nv12?uu+1:v+(row/2)*vs;
            for(int col=0;col<width;col+=2) {
                const int at=nv12?col:col/2;
                if(half) {
                    auto *p=out+(row/2)*stride+(col/2)*4;
                    p[0]=(y0[col]+y0[col+1]+y1[col]+y1[col+1]+2)/4;
                    p[1]=uu[at];p[2]=vv[at];p[3]=255;
                } else {
                    auto *p=out+row*stride+col*4;
                    for(int r=0;r<2;++r) for(int c=0;c<2;++c) {
                        auto *pixel=p+r*stride+c*4;
                        pixel[0]=(r?y1:y0)[col+c];pixel[1]=uu[at];pixel[2]=vv[at];pixel[3]=255;
                    }
                }
            }
        }
    }
    static bool NeedsHalfSize(int width, int height) { return width>960 || height>540; }
    void Configure(bool full, bool bt709) {
        if(ready_ && full_==full && bt709_==bt709) return;
        if(!ready_) for(int i=0;i<1024;++i)
            saturated_[i]=static_cast<std::uint8_t>(std::clamp(i-384,0,255));
        ready_=true; full_=full; bt709_=bt709;
        const int cy=full?256:298, offset=full?0:16;
        const int cr=full?(bt709?403:359):(bt709?459:409);
        const int cb=full?(bt709?475:454):(bt709?541:516);
        const int cgu=full?(bt709?-48:-88):(bt709?-55:-100);
        const int cgv=full?(bt709?-120:-183):(bt709?-136:-208);
        for(int i=0;i<256;++i) {
            y_[i]=cy*(i-offset)+128; rv_[i]=cr*(i-128);
            gu_[i]=cgu*(i-128); gv_[i]=cgv*(i-128); bu_[i]=cb*(i-128);
        }
    }
    // Even dimensions; planar U/V or interleaved NV12 U/V. Strides include padding.
    void Convert(const std::uint8_t *y, int ys, const std::uint8_t *u, int us,
                 const std::uint8_t *v, int vs, bool nv12, int width, int height,
                 std::uint8_t *out, int stride) const {
        for(int row=0;row<height;row+=2) {
            const auto *y0=y+row*ys, *y1=y0+ys;
            const auto *uu=u+(row/2)*us, *vv=nv12?uu+1:v+(row/2)*vs;
            auto *out0=out+row*stride, *out1=out0+stride;
            for(int col=0;col<width;col+=2) {
                const int at=nv12?col:col/2;
                const int r=rv_[vv[at]], g=gu_[uu[at]]+gv_[vv[at]], b=bu_[uu[at]];
                Pixel(out0+col*4,y0[col],r,g,b); Pixel(out0+(col+1)*4,y0[col+1],r,g,b);
                Pixel(out1+col*4,y1[col],r,g,b); Pixel(out1+(col+1)*4,y1[col+1],r,g,b);
            }
        }
    }
    // Box-filter each 2x2 luma block before converting its shared chroma sample.
    // Four times fewer RGB pixels, with no full-resolution RGBA intermediate.
    void ConvertHalf(const std::uint8_t *y, int ys, const std::uint8_t *u, int us,
                     const std::uint8_t *v, int vs, bool nv12, int width, int height,
                     std::uint8_t *out, int stride) const {
        for(int row=0;row<height;row+=2) {
            const auto *y0=y+row*ys, *y1=y0+ys;
            const auto *uu=u+(row/2)*us, *vv=nv12?uu+1:v+(row/2)*vs;
            auto *destination=out+(row/2)*stride;
            for(int col=0;col<width;col+=2) {
                const int at=nv12?col:col/2;
                const auto luma=static_cast<std::uint8_t>((y0[col]+y0[col+1]+y1[col]+y1[col+1]+2)/4);
                Pixel(destination+(col/2)*4,luma,rv_[vv[at]],gu_[uu[at]]+gv_[vv[at]],bu_[uu[at]]);
            }
        }
    }
};
