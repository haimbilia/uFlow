/* uFlow BC3/mipmap import bridge for devkitPro SDL2 2.32.10-2.
 * Compiled against the exact package's private headers, never guessed layouts.
 * SDL retains ownership and its normal in-flight GPU destruction fences.
 */
#include "src/render/wiiu/SDL_render_wiiu.h"
#include <gx2r/surface.h>
#include <gx2r/resource.h>
#include <gx2/sampler.h>
#include <gx2/event.h>
#include <string.h>
#include "SDL_version.h"

SDL_Texture *UFlow_CreateCoverTexture(SDL_Renderer *renderer, unsigned width,
    unsigned height, unsigned levels, const unsigned char *blocks, unsigned bytes,
    unsigned *allocated)
{
    SDL_version version;
    SDL_GetVersion(&version);
    if (version.major!=2 || version.minor!=32 || version.patch!=10 ||
        !renderer || SDL_strcasecmp(renderer->info.name,"WiiU GX2")!=0 || !blocks ||
        !width || !height || width>2048 || height>2048 || !levels || levels>12)
        return NULL;
    // Finish SDL's queued commands before issuing native GPU work. Surface
    // copies use GX2's internal context, so restore SDL's context afterwards.
    if(SDL_RenderFlush(renderer)!=0)return NULL;
    GX2Surface result={0}, staging[12]={{0}};
    GX2RResourceFlags flags=GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_FORCE_MEM2;
    result.dim=GX2_SURFACE_DIM_TEXTURE_2D;result.width=width;result.height=height;
    result.depth=1;result.mipLevels=levels;result.format=GX2_SURFACE_FORMAT_UNORM_BC3;
    result.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&result);
    if(!GX2RCreateSurface(&result,flags))return NULL;
    unsigned used=0,w=width,h=height,created=0;int ok=1;
    for(unsigned level=0;level<levels;++level){
        unsigned row=((w+3)/4)*16,rows=(h+3)/4,length=row*rows;
        if(length>bytes-used){ok=0;break;}
        GX2Surface *s=&staging[level];
        s->dim=GX2_SURFACE_DIM_TEXTURE_2D;s->width=w;s->height=h;s->depth=1;
        s->mipLevels=1;s->format=result.format;s->tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
        GX2CalcSurfaceSizeAndAlignment(s);
        if(!GX2RCreateSurface(s,flags)){ok=0;break;}++created;
        unsigned char *dst=GX2RLockSurfaceEx(s,0,0);
        // BC3 surfaces report pitch in 4x4 blocks (16 bytes each).
        unsigned pitch=s->pitch*16;
        if(!dst || pitch<row || (rows-1)*pitch+row>s->imageSize){
            if(dst)GX2RUnlockSurfaceEx(s,0,0);ok=0;break;
        }
        for(unsigned y=0;y<rows;++y)memcpy(dst+y*pitch,blocks+used+y*row,row);
        GX2RUnlockSurfaceEx(s,0,0);
        GX2CopySurface(s,0,0,&result,level,0);
        used+=length;w=w>1?w/2:1;h=h>1?h/2:1;
    }
    // Upload staging remains alive until every queued copy has completed.
    GX2DrawDone();
    GX2SetContextState(((WIIU_RenderData *)renderer->driverdata)->ctx);
    for(unsigned i=0;i<created;++i)GX2RDestroySurfaceEx(&staging[i],0);
    if(!ok || used!=bytes){GX2RDestroySurfaceEx(&result,0);return NULL;}
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,1);
    if(!texture || texture->native){if(texture)SDL_DestroyTexture(texture);GX2RDestroySurfaceEx(&result,0);return NULL;}
    WIIU_TextureData *data=texture->driverdata;
    // Keep the tiny original color-buffer allocation as an owned dummy. The
    // compressed texture is sampled only; it is never used as a render target.
    data->cbuf.surface=data->texture.surface;
    data->texture.surface=result;
    data->texture.viewFirstMip=0;data->texture.viewNumMips=levels;
    data->texture.viewNumSlices=1;
    data->texture.compMap=GX2_COMP_MAP(GX2_SQ_SEL_R,GX2_SQ_SEL_G,GX2_SQ_SEL_B,GX2_SQ_SEL_A);
    GX2InitTextureRegs(&data->texture);
    GX2InitSampler(&data->sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&data->sampler,GX2_TEX_XY_FILTER_MODE_LINEAR,GX2_TEX_XY_FILTER_MODE_LINEAR,GX2_TEX_ANISO_RATIO_2_TO_1);
    GX2InitSamplerZMFilter(&data->sampler,GX2_TEX_Z_FILTER_MODE_POINT,GX2_TEX_MIP_FILTER_MODE_LINEAR);
    GX2InitSamplerLOD(&data->sampler,0.f,(float)(levels-1),0.f);
    texture->w=width;texture->h=height;texture->scaleMode=SDL_ScaleModeLinear;
    if(allocated)*allocated=result.imageSize+result.mipmapSize+data->cbuf.surface.imageSize;
    return texture;
}
