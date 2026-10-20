#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t Uint8;
typedef int GX2SurfaceFormat;
typedef struct {unsigned width,height,depth,dim,format,tileMode,mipLevels,pitch,imageSize;void *image;unsigned alignment;} GX2Surface;
typedef struct {GX2Surface surface;unsigned viewNumMips,viewNumSlices,compMap;} GX2Texture;
typedef struct {int unused;} GX2Sampler;
typedef struct {GX2Texture texture,uflowChroma;GX2Sampler sampler;unsigned uflowYuvMode;void (*uflowRelease)(void *);void *uflowLease;} WIIU_TextureData;
typedef struct {int unused;} WIIU_RenderData;
typedef struct {int hasForeground;} WIIU_VideoData;
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct {void *driverdata;} SDL_Renderer;
typedef struct {int access,w,h,scaleMode,format;void *driverdata;SDL_Renderer *renderer;} SDL_Texture;
typedef struct {void *driverdata;} SDL_VideoDevice;
enum {GX2_SURFACE_DIM_TEXTURE_2D=1,GX2_TILE_MODE_LINEAR_ALIGNED=1,
GX2_SURFACE_FORMAT_UNORM_R8=1,GX2_SURFACE_FORMAT_UNORM_R8_G8=2,
GX2R_RESOURCE_BIND_TEXTURE=1,GX2R_RESOURCE_USAGE_CPU_WRITE=2,
GX2R_RESOURCE_USAGE_GPU_READ=4,GX2R_RESOURCE_USAGE_FORCE_MEM2=8,
SDL_TEXTUREACCESS_TARGET=2,SDL_PIXELFORMAT_NV12=12,SDL_ScaleModeNearest=0,
GX2_TEX_CLAMP_MODE_CLAMP=0,GX2_TEX_XY_FILTER_MODE_POINT=0,GX2_TEX_XY_FILTER_MODE_LINEAR=1};
#define GX2_COMP_MAP(a,b,c,d) 0
#define SDL_calloc calloc
#define SDL_free free
#define SDL_memcpy memcpy
static int live,allocations,failAllocation,failLock,waits,used,ready=1;
enum { DMAE_SWAP_NONE=0 };
static int dmaCopies,dmaWaits,flushes,invalidations;
static void DCFlushRange(void *p,size_t n){assert(p && n);++flushes;}
static void DCInvalidateRange(void *p,size_t n){assert(p && n);++invalidations;}
static uint64_t DMAECopyMem(void *d,void *s,size_t words,int mode){
    assert(mode==DMAE_SWAP_NONE && flushes==2);++dmaCopies;memcpy(d,s,words*4);return 42;
}
static int DMAEWaitDone(uint64_t id){assert(id==42);return ++dmaWaits>=2;}
static WIIU_VideoData vd={1};static SDL_VideoDevice device={&vd};
static int SDL_SetError(const char *s){(void)s;return -1;}
static int SDL_OutOfMemory(void){return -1;}
static int UFlow_YuvShadersReady(void){return ready;}
static SDL_VideoDevice *SDL_GetVideoDevice(void){return &device;}
static void GX2CalcSurfaceSizeAndAlignment(GX2Surface *s){s->pitch=16;s->imageSize=s->pitch*s->format*s->height;s->alignment=16;}
static void GX2InitTextureRegs(GX2Texture *t){(void)t;}
static int GX2RCreateSurface(GX2Surface *s,int flags){
    assert(flags==15);if(++allocations==failAllocation)return 0;
    s->image=malloc(s->imageSize);memset(s->image,0xcd,s->imageSize);++live;return 1;
}
static void GX2RDestroySurfaceEx(GX2Surface *s,int flags){(void)flags;assert(s->image);free(s->image);s->image=NULL;--live;}
static void *GX2RLockSurfaceEx(GX2Surface *s,int a,int b){(void)a;(void)b;return failLock?NULL:s->image;}
static void GX2RUnlockSurfaceEx(GX2Surface *s,int a,int b){(void)s;(void)a;(void)b;}
static void GX2InitSampler(GX2Sampler *s,int a,int b){(void)s;(void)a;(void)b;}
static int WIIU_TextureInUse(WIIU_RenderData *r,WIIU_TextureData *t){(void)r;(void)t;return used;}
static int WIIU_TextureWaitDone(WIIU_RenderData *r,WIIU_TextureData *t){(void)r;(void)t;++waits;return 1;}
enum {GX2_INVALIDATE_MODE_TEXTURE=1};
static int textureInvalidations,renderFlushes,failFlush,releases,releaseAfter;
static void GX2Invalidate(int mode,void *p,size_t bytes){assert(mode==1 && p && bytes);++textureInvalidations;}
static int SDL_RenderFlush(SDL_Renderer *r){assert(r);++renderFlushes;return failFlush?-1:0;}
static void releaseLease(void *p){assert(waits>releaseAfter);releaseAfter=waits;++releases;free(p);}
#include "../tools/video-nv12-backend.c"
int main(void){
    SDL_Texture t={0,8,4,1,SDL_PIXELFORMAT_NV12,NULL};
    WIIU_RenderData rd={0};SDL_Renderer renderer={&rd};SDL_Rect rect={0,0,8,4};
    t.renderer=&renderer;
    Uint8 y[40],uv[24];for(int i=0;i<40;++i)y[i]=i;for(int i=0;i<24;++i)uv[i]=100+i;
    failAllocation=2;assert(UFlow_CreateNV12(&t)<0 && live==0);
    failAllocation=0;assert(UFlow_CreateNV12(&t)==0 && live==2);
    WIIU_TextureData *td=t.driverdata;used=1;
    assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)==0 && waits==1);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<16;++col)
        assert(((Uint8*)td->texture.surface.image)[row*16+col]==(col<8?y[row*10+col]:0xcd));
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<32;++col)
        assert(((Uint8*)td->uflowChroma.surface.image)[row*32+col]==(col<8?uv[row*12+col]:0xcd));
    assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,7,uv,12)<0);
    rect.x=1;assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)<0);rect.x=0;
    failLock=1;assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)<0);failLock=0;
    vd.hasForeground=0;int before=waits;assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)==0 && waits==before);
    GX2RDestroySurfaceEx(&td->texture.surface,0);GX2RDestroySurfaceEx(&td->uflowChroma.surface,0);free(td);assert(live==0);
    t.w=7;assert(UFlow_CreateNV12(&t)<0);t.w=8;t.access=SDL_TEXTUREACCESS_TARGET;assert(UFlow_CreateNV12(&t)<0);
    t.access=0;ready=0;assert(UFlow_CreateNV12(&t)<0 && live==0);
    /* Exact last-row allocation catches DMA overreads of trailing padding. */
    size_t n=3*2048+1920;
    Uint8 *source=malloc(n),*dest=malloc(4*2048);
    for(size_t i=0;i<n;++i)source[i]=(Uint8)i;
    memset(dest,0xcd,4*2048);
    GX2Surface plane={0};plane.pitch=2048;plane.imageSize=4*2048;plane.image=dest;
    assert(UFlow_CopyPlane(&plane,source,2048,1920,4,1)==0);
    assert(dmaCopies==1 && dmaWaits==2 && invalidations==1 && !memcmp(source,dest,n));
    for(size_t i=n;i<4*2048;++i)assert(dest[i]==0xcd);
    assert(UFlow_CopyPlane(&plane,source,2048,1920,0,1)<0);
    /* Unaligned source must retain the row-copy path. */
    assert(UFlow_CopyPlane(&plane,source+1,2048,1919,4,1)==0 && dmaCopies==1);
    free(source);free(dest);
    ready=1;vd.hasForeground=1;
    assert(UFlow_CreateNV12(&t)==0 && live==2);td=t.driverdata;
    Uint8 *lease,*replacement;assert(!posix_memalign((void**)&lease,16,128));
    assert(!posix_memalign((void**)&replacement,16,128));
    void *owned=td->texture.surface.image;
    assert(UFlow_BindNV12(&t,lease,15,64,lease+64,32,64,releaseLease,lease)<0 && !releases);
    assert(UFlow_BindNV12(&t,lease,16,63,lease+64,32,64,releaseLease,lease)<0 && !releases);
    assert(UFlow_BindNV12(&t,lease+1,16,64,lease+64,32,64,releaseLease,lease)<0 && !releases);
    failFlush=1;assert(UFlow_BindNV12(&t,lease,16,64,lease+64,32,64,releaseLease,lease)<0);
    assert(td->texture.surface.image==owned && live==2);failFlush=0;
    assert(UFlow_BindNV12(&t,lease,16,64,lease+64,32,64,releaseLease,lease)==0);
    assert(live==0 && td->texture.surface.image==lease && td->uflowChroma.surface.image==lease+64);
    assert(textureInvalidations==2 && dmaCopies==1 && !releases);
    releaseAfter=waits;
    assert(UFlow_BindNV12(&t,replacement,16,64,replacement+64,32,64,releaseLease,replacement)==0);
    assert(releases==1 && live==0);
    failAllocation=allocations+2;
    assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)<0);
    assert(releases==1 && live==0 && td->texture.surface.image==replacement);
    failAllocation=0;
    assert(UFlow_UpdateNV12(&renderer,&t,&rect,y,10,uv,12)==0 && releases==2 && live==2);
    assert(!posix_memalign((void**)&lease,16,128));
    vd.hasForeground=0;
    assert(UFlow_BindNV12(&t,lease,16,64,lease+64,32,64,releaseLease,lease)<0 && releases==2);
    vd.hasForeground=1;
    assert(UFlow_BindNV12(&t,lease,16,64,lease+64,32,64,releaseLease,lease)==0 && live==0);
    releaseAfter=waits;
    WIIU_TextureWaitDone(&rd,td);UFlow_DestroyNV12(td);free(td);
    assert(live==0 && releases==3);
    puts("PASS: NV12 plane allocation unwind, padded uploads, bounds, fences, lock failure and background suspension");
    puts("PASS: imported decoder buffers, zero copies, alignment/size guards, GPU-fenced lease replacement and copy fallback unwind");
}
