"""Packed YUV preview shader support for the pinned Wii U SDL renderer.

Uses ordinary RGBA texture storage, upload fences and ownership; only tagged
video textures select a colour-conversion shader. All other draws are unchanged.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1])/'src/render/wiiu'
def edit(name, old, new):
    p=root/name
    s=p.read_text()
    assert s.count(old)==1, (name,old)
    p.write_text(s.replace(old,new))

edit('SDL_render_wiiu.h','    OSTime timestamp;','    OSTime timestamp;\n    unsigned uflowYuvMode;')
edit('SDL_shaders_wiiu.h','    NUM_SHADERS','    SHADER_YUV601, SHADER_YUV709, SHADER_YUV601_FULL, SHADER_YUV709_FULL,\n    NUM_SHADERS')
edit('SDL_shaders_wiiu.c','#include "shaders/textureShader.inc"',
     '#include "shaders/textureShader.inc"\n'+''.join(f'#include "shaders/video{i}.inc"\n' for i in range(1,5)))
edit('SDL_shaders_wiiu.c','        WiiU_CreateFetchShader(&shaderGroups[SHADER_TEXTURE], attributes, 3);',
     '        WiiU_CreateFetchShader(&shaderGroups[SHADER_TEXTURE], attributes, 3);\n'+''.join(
         f'        WiiU_LoadGFDShaderGroup(&shaderGroups[SHADER_YUV601+{i-1}], 0, video{i}_gsh);\n'
         f'        WiiU_CreateFetchShader(&shaderGroups[SHADER_YUV601+{i-1}], attributes, 3);\n' for i in range(1,5)))
edit('SDL_shaders_wiiu.c','        WiiU_FreeShaderGroup(&shaderGroups[SHADER_TEXTURE]);',
     '        WiiU_FreeShaderGroup(&shaderGroups[SHADER_TEXTURE]);\n'
     '        for(int i=SHADER_YUV601;i<NUM_SHADERS;++i) WiiU_FreeShaderGroup(&shaderGroups[i]);')
edit('SDL_shaders_wiiu.c','    GX2RDestroyBufferEx(&group->vertexShader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);',
     '    if(group->vertexShader) GX2RDestroyBufferEx(&group->vertexShader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);')
edit('SDL_shaders_wiiu.c','    GX2RDestroyBufferEx(&group->pixelShader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);',
     '    if(group->pixelShader) GX2RDestroyBufferEx(&group->pixelShader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);')
edit('SDL_rqueue_wiiu.c','    WiiU_ShaderGroup* shaderGroup = WIIU_SDL_GetShaderGroup(shader);',
     '''    if(cmd->data.draw.texture) {
        WIIU_TextureData *td=(WIIU_TextureData *)cmd->data.draw.texture->driverdata;
        if(td->uflowYuvMode>=1 && td->uflowYuvMode<=4) shader=SHADER_YUV601+td->uflowYuvMode-1;
    }
    WiiU_ShaderGroup* shaderGroup = WIIU_SDL_GetShaderGroup(shader);''')
p=root/'SDL_rtexture_wiiu.c'
p.write_text(p.read_text()+'''
int UFlow_YuvShadersReady(void) {
    for(int i=SHADER_YUV601;i<NUM_SHADERS;++i) {
        WiiU_ShaderGroup *s=WIIU_SDL_GetShaderGroup(i);
        if(!s->pixelShader || !s->vertexShader || !s->fetchShaderProgram) return 0;
    }
    return 1;
}
int UFlow_SetYuvTexture(SDL_Texture *texture, unsigned mode) {
    if(!texture || !texture->driverdata || mode>4 || (mode && !UFlow_YuvShadersReady())) return -1;
    ((WIIU_TextureData *)texture->driverdata)->uflowYuvMode=mode;
    return 0;
}
''')
