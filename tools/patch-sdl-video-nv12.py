"""Native sample-only NV12 textures, extending the packed video shader backend."""
from pathlib import Path
import sys
root=Path(sys.argv[1])/'src/render/wiiu'
def edit(name,old,new):
    p=root/name;s=p.read_text();assert s.count(old)==1,(name,old);p.write_text(s.replace(old,new))
edit('SDL_render_wiiu.h','    unsigned uflowYuvMode;', '    unsigned uflowYuvMode;\n    GX2Texture uflowChroma;\n    void (*uflowRelease)(void *);\n    void *uflowLease;')
edit('SDL_render_wiiu.h','/* Ask texture driver', '''int UFlow_YuvShadersReady(void);
int UFlow_UpdateNV12(SDL_Renderer *,SDL_Texture *,const SDL_Rect *,const Uint8 *,int,const Uint8 *,int);
/* Ask texture driver''')
# SDL_RendererInfo has exactly 16 slots. Replace the unused 10-bit format;
# its software fallback remains available, rather than overflowing the list.
edit('SDL_render_wiiu.c','            SDL_PIXELFORMAT_ARGB2101010,','            SDL_PIXELFORMAT_NV12,')
edit('SDL_render_wiiu.c','    renderer->UpdateTexture = WIIU_SDL_UpdateTexture;',
     '    renderer->UpdateTexture = WIIU_SDL_UpdateTexture;\n    renderer->UpdateTextureNV = UFlow_UpdateNV12;')
edit('SDL_rtexture_wiiu.c','int WIIU_SDL_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)\n{',
     '#include "video-nv12-backend.c"\nint WIIU_SDL_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)\n{\n'
     '    if(texture->format==SDL_PIXELFORMAT_NV12)return UFlow_CreateNV12(texture);')
edit('SDL_rtexture_wiiu.c','    /* We write the rules, and we say all textures are streaming */',
     '''    if(texture->format==SDL_PIXELFORMAT_NV12)
        return UFlow_UpdateNV12(renderer,texture,rect,pixels,pitch,(const Uint8 *)pixels+(size_t)pitch*texture->h,pitch);
    /* We write the rules, and we say all textures are streaming */''')
edit('SDL_rtexture_wiiu.c',"if (texture->format != SDL_FOURCC('D','X','T','5')) GX2RDestroySurfaceEx(&tdata->cbuf.surface, 0);",
     "if (texture->format != SDL_FOURCC('D','X','T','5') && texture->format != SDL_PIXELFORMAT_NV12) GX2RDestroySurfaceEx(&tdata->cbuf.surface, 0);\n"
     "    if(texture->format==SDL_PIXELFORMAT_NV12) UFlow_DestroyNV12(tdata);")
edit('SDL_rtexture_wiiu.c','    GX2RDestroySurfaceEx(&tdata->texture.surface, 0);\n\n    SDL_free(tdata);',
     '    if(texture->format!=SDL_PIXELFORMAT_NV12) GX2RDestroySurfaceEx(&tdata->texture.surface, 0);\n\n    SDL_free(tdata);')
edit('SDL_shaders_wiiu.h','    NUM_SHADERS','    SHADER_NV601, SHADER_NV709, SHADER_NV601_FULL, SHADER_NV709_FULL,\n    NUM_SHADERS')
edit('SDL_shaders_wiiu.c','#include "shaders/video4.inc"',
     '#include "shaders/video4.inc"\n'+''.join(f'#include "shaders/nv{i}.inc"\n' for i in range(1,5)))
edit('SDL_shaders_wiiu.c','        WiiU_CreateFetchShader(&shaderGroups[SHADER_TEXTURE], attributes, 3);',
     '        WiiU_CreateFetchShader(&shaderGroups[SHADER_TEXTURE], attributes, 3);\n'+''.join(
         f'        WiiU_LoadGFDShaderGroup(&shaderGroups[SHADER_NV601+{i-1}], 0, nv{i}_gsh);\n'
         f'        WiiU_CreateFetchShader(&shaderGroups[SHADER_NV601+{i-1}], attributes, 3);\n' for i in range(1,5)))
edit('SDL_rqueue_wiiu.c','shader=SHADER_YUV601+td->uflowYuvMode-1;',
     'shader=(cmd->data.draw.texture->format==SDL_PIXELFORMAT_NV12?SHADER_NV601:SHADER_YUV601)+td->uflowYuvMode-1;')
edit('SDL_rqueue_wiiu.c','            GX2SetPixelSampler(&tdata->sampler, location);',
     '''            GX2SetPixelSampler(&tdata->sampler, location);
            if(texture->format==SDL_PIXELFORMAT_NV12) {
                GX2SetPixelTexture(&tdata->uflowChroma,1);
                GX2SetPixelSampler(&tdata->sampler,1);
            }''')
