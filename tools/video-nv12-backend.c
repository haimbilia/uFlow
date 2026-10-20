/* uFlow native NV12 planes. Included by SDL_rtexture_wiiu.c. */
static int UFlow_CreatePlane(GX2Texture *t, unsigned w, unsigned h, GX2SurfaceFormat format)
{
    t->surface.width=w; t->surface.height=h; t->surface.depth=1;
    t->surface.dim=GX2_SURFACE_DIM_TEXTURE_2D; t->surface.format=format;
    t->surface.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED; t->surface.mipLevels=1;
    t->viewNumMips=1; t->viewNumSlices=1;
    t->compMap=GX2_COMP_MAP(GX2_SQ_SEL_R,GX2_SQ_SEL_G,GX2_SQ_SEL_0,GX2_SQ_SEL_1);
    GX2CalcSurfaceSizeAndAlignment(&t->surface); GX2InitTextureRegs(t);
    return GX2RCreateSurface(&t->surface,GX2R_RESOURCE_BIND_TEXTURE |
        GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_FORCE_MEM2);
}
static int UFlow_CreateNV12(SDL_Texture *texture)
{
    WIIU_TextureData *td;
    if(texture->access==SDL_TEXTUREACCESS_TARGET || (texture->w&1) || (texture->h&1) ||
       texture->w>1920 || texture->h>1088 || !UFlow_YuvShadersReady())
        return SDL_SetError("Invalid NV12 preview texture");
    td=SDL_calloc(1,sizeof(*td));
    if(!td)return SDL_OutOfMemory();
    if(!UFlow_CreatePlane(&td->texture,texture->w,texture->h,GX2_SURFACE_FORMAT_UNORM_R8)) {
        SDL_free(td);return SDL_OutOfMemory();
    }
    if(!UFlow_CreatePlane(&td->uflowChroma,texture->w/2,texture->h/2,GX2_SURFACE_FORMAT_UNORM_R8_G8)) {
        GX2RDestroySurfaceEx(&td->texture.surface,0);SDL_free(td);return SDL_OutOfMemory();
    }
    GX2InitSampler(&td->sampler,GX2_TEX_CLAMP_MODE_CLAMP,
        texture->scaleMode==SDL_ScaleModeNearest?GX2_TEX_XY_FILTER_MODE_POINT:GX2_TEX_XY_FILTER_MODE_LINEAR);
    td->uflowYuvMode=2;
    texture->driverdata=td;return 0;
}
static int UFlow_CopyPlane(GX2Surface *surface,const Uint8 *source,int pitch,int rowBytes,int rows,int bytesPerPixel)
{
    Uint8 *dst;
    unsigned dstPitch=surface->pitch*bytesPerPixel;
    if(rows<=0 || rowBytes<=0 || pitch<rowBytes || dstPitch<(unsigned)rowBytes ||
       (size_t)(rows-1)*dstPitch+rowBytes>surface->imageSize)return SDL_SetError("NV12 plane bounds");
    dst=GX2RLockSurfaceEx(surface,0,0);
    if(!dst)return SDL_SetError("NV12 plane lock failed");
    /* Native decoder and GX2 planes share an aligned pitch. Transfer their
       padding too, but never read beyond the last row's actual pixels. */
    size_t bytes=(size_t)(rows-1)*dstPitch+rowBytes;
    if((unsigned)pitch==dstPitch && bytes>5120 && !(bytes&3) &&
       !((uintptr_t)source&7) && !((uintptr_t)dst&7)) {
        DCFlushRange((void*)source,bytes);
        DCFlushRange(dst,bytes);
        uint64_t transfer=DMAECopyMem(dst,(void*)source,bytes>>2,DMAE_SWAP_NONE);
        while(!DMAEWaitDone(transfer)) {}
        DCInvalidateRange(dst,bytes);
    } else {
        for(int row=0;row<rows;++row) SDL_memcpy(dst+row*dstPitch,source+(size_t)row*pitch,rowBytes);
    }
    GX2RUnlockSurfaceEx(surface,0,0);return 0;
}
/* Caller has already waited for this texture's final GPU use. A borrowed
   decoder frame is released instead of freeing its plane pointers with GX2R. */
static void UFlow_DestroyNV12(WIIU_TextureData *td)
{
    if(td->uflowRelease) {
        td->uflowRelease(td->uflowLease);
        td->uflowRelease=NULL;td->uflowLease=NULL;
    } else {
        GX2RDestroySurfaceEx(&td->texture.surface,0);
        GX2RDestroySurfaceEx(&td->uflowChroma.surface,0);
    }
}
int UFlow_BindNV12(SDL_Texture *texture,const Uint8 *y,int yp,size_t yBytes,
                  const Uint8 *uv,int uvp,size_t uvBytes,void (*release)(void *),void *lease)
{
    WIIU_TextureData *td;
    WIIU_RenderData *rd;
    WIIU_VideoData *vd=SDL_GetVideoDevice()->driverdata;
    if(!texture || texture->format!=SDL_PIXELFORMAT_NV12 || !texture->driverdata ||
       !y || !uv || !release || !lease || !vd->hasForeground)return -1;
    td=texture->driverdata;rd=texture->renderer->driverdata;
    /* Only import exactly compatible linear surfaces. GX2 may sample padded
       rows, so require its complete imageSize, not just the visible pixels. */
    if(yp<=0 || uvp<=0 || (unsigned)yp!=td->texture.surface.pitch ||
       (unsigned)uvp!=td->uflowChroma.surface.pitch*2 ||
       yBytes<td->texture.surface.imageSize || uvBytes<td->uflowChroma.surface.imageSize ||
       !td->texture.surface.alignment || !td->uflowChroma.surface.alignment ||
       (uintptr_t)y%td->texture.surface.alignment ||
       (uintptr_t)uv%td->uflowChroma.surface.alignment)return -1;
    if(SDL_RenderFlush(texture->renderer)<0)return -1;
    if(WIIU_TextureInUse(rd,td))WIIU_TextureWaitDone(rd,td);
    {
        GX2Texture luma=td->texture,chroma=td->uflowChroma;
        UFlow_DestroyNV12(td);
        td->texture=luma;td->uflowChroma=chroma;
    }
    td->texture.surface.image=(void*)y;
    td->uflowChroma.surface.image=(void*)uv;
    td->uflowRelease=release;td->uflowLease=lease;
    /* h264.rpl produced these in main memory, and the adapter invalidated CPU
       caches. Invalidate texture caches without another CPU flush/copy. */
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,(void*)y,td->texture.surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,(void*)uv,td->uflowChroma.surface.imageSize);
    return 0;
}
int UFlow_UpdateNV12(SDL_Renderer *renderer,SDL_Texture *texture,const SDL_Rect *rect,
                     const Uint8 *y,int yp,const Uint8 *uv,int uvp)
{
    WIIU_TextureData *td=texture->driverdata;
    WIIU_RenderData *rd=renderer->driverdata;
    WIIU_VideoData *vd=SDL_GetVideoDevice()->driverdata;
    if(texture->format!=SDL_PIXELFORMAT_NV12 || !y || !uv ||
       rect->x || rect->y || rect->w!=texture->w || rect->h!=texture->h)
        return SDL_SetError("NV12 requires a complete frame");
    if(!vd->hasForeground)return 0;
    if(WIIU_TextureInUse(rd,td))WIIU_TextureWaitDone(rd,td);
    if(td->uflowRelease) {
        /* A format/alignment change can fall back to copies. Allocate both
           replacements before relinquishing the previous valid frame. */
        GX2Texture luma={0},chroma={0};
        if(!UFlow_CreatePlane(&luma,texture->w,texture->h,GX2_SURFACE_FORMAT_UNORM_R8))return SDL_OutOfMemory();
        if(!UFlow_CreatePlane(&chroma,texture->w/2,texture->h/2,GX2_SURFACE_FORMAT_UNORM_R8_G8)) {
            GX2RDestroySurfaceEx(&luma.surface,0);return SDL_OutOfMemory();
        }
        UFlow_DestroyNV12(td);td->texture=luma;td->uflowChroma=chroma;
    }
    if(UFlow_CopyPlane(&td->texture.surface,y,yp,texture->w,texture->h,1)<0)return -1;
    return UFlow_CopyPlane(&td->uflowChroma.surface,uv,uvp,texture->w,texture->h/2,2);
}
