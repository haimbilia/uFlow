"""Add a sample-only BC3 format to the pinned SDL Wii U renderer.

Uses SDL's normal texture ownership and draw fences. No private app-side ABI,
GPU copies, context switches, mip uploads or compressed render targets.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'src/render/wiiu'
p = root / 'SDL_render_wiiu.c'
s = p.read_text()
assert '.num_texture_formats = 15,' in s
s = s.replace('.num_texture_formats = 15,', '.num_texture_formats = 16,')
s = s.replace('            SDL_PIXELFORMAT_ARGB2101010,', "            SDL_PIXELFORMAT_ARGB2101010,\n            SDL_FOURCC('D','X','T','5'), /* uFlow: static BC3; preserve default RGBA */")
p.write_text(s)
p = root / 'SDL_rtexture_wiiu.c'
s = p.read_text()
s = '#include "bc3-upload-layout.h"\n' + s
s = s.replace('    BOOL res;', "    const SDL_bool bc3 = texture->format == SDL_FOURCC('D','X','T','5');\n    BOOL res;", 1)
s = s.replace('    /* Setup sampler */', '''    if (bc3 && (texture->access != SDL_TEXTUREACCESS_STATIC || texture->w > 2048 || texture->h > 2048)) {
        SDL_free(tdata);
        return SDL_SetError("BC3 covers are sample-only static textures");
    }
    /* Setup sampler */''', 1)
s = s.replace('    gx2_fmt = WIIU_SDL_GetPixFmt(texture->format);', '''    if (bc3) {
        gx2_fmt.fmt = GX2_SURFACE_FORMAT_UNORM_BC3;
        gx2_fmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R,GX2_SQ_SEL_G,GX2_SQ_SEL_B,GX2_SQ_SEL_A);
    } else gx2_fmt = WIIU_SDL_GetPixFmt(texture->format);''', 1)
s = s.replace('    GX2InitColorBufferRegs(&tdata->cbuf);', '    if (!bc3) GX2InitColorBufferRegs(&tdata->cbuf);', 1)
s = s.replace('tdata->texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;',
    'tdata->texture.surface.tileMode = bc3 ? GX2_TILE_MODE_TILED_1D_THIN1 : GX2_TILE_MODE_LINEAR_ALIGNED;', 1)
s = s.replace('    /* Allocate normal textures from MEM2 */', '''    if (bc3) surface_flags = GX2R_RESOURCE_BIND_TEXTURE |
        GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
    /* Allocate normal textures from MEM2 */''', 1)
s = s.replace('    /* Allocate a colour buffer, using the same backing buffer */', '''    if (bc3) {
        SDL_memset(&tdata->cbuf, 0, sizeof(tdata->cbuf));
        texture->driverdata = tdata;
        return 0;
    }
    /* Allocate a colour buffer, using the same backing buffer */''', 1)
needle='''    /* We write the rules, and we say all textures are streaming */'''
assert needle in s
s = s.replace(needle, '''    if (texture->format == SDL_FOURCC('D','X','T','5')) {
        WIIU_RenderData *data = (WIIU_RenderData *)renderer->driverdata;
        WIIU_TextureData *td = (WIIU_TextureData *)texture->driverdata;
        const unsigned rows = (texture->h + 3) / 4;
        const unsigned row_bytes = ((texture->w + 3) / 4) * 16;
        const unsigned dst_pitch = td->texture.surface.pitch * 16;
        unsigned char *out;
        int copied;
        if (td->texture.surface.tileMode != GX2_TILE_MODE_TILED_1D_THIN1 ||
            rect->x || rect->y || rect->w != texture->w || rect->h != texture->h ||
            pitch < (int)row_bytes || dst_pitch < row_bytes ||
            (rows-1)*dst_pitch + row_bytes > td->texture.surface.imageSize)
            return SDL_SetError("Invalid full BC3 cover upload");
        if (WIIU_TextureInUse(data, td)) WIIU_TextureWaitDone(data, td);
        out = GX2RLockSurfaceEx(&td->texture.surface, 0, 0);
        if (!out) return SDL_SetError("BC3 surface lock failed");
        copied = UFlow_CopyBC3(texture->w, texture->h, pixels, pitch, out, dst_pitch, td->texture.surface.imageSize);
        GX2RUnlockSurfaceEx(&td->texture.surface, 0, 0);
        return copied ? 0 : SDL_SetError("BC3 copy bounds failed");
    }
''' + needle, 1)
s = s.replace('    GX2RDestroySurfaceEx(&tdata->cbuf.surface, 0);', "    if (texture->format != SDL_FOURCC('D','X','T','5')) GX2RDestroySurfaceEx(&tdata->cbuf.surface, 0);", 1)
s += "\nunsigned UFlow_BC3TextureBytes(SDL_Texture *texture) {\n    WIIU_TextureData *td = (WIIU_TextureData *)texture->driverdata;\n    return td->texture.surface.imageSize + td->texture.surface.mipmapSize;\n}\n"
p.write_text(s)
print('Patched SDL: static BC3 allocation, bounded CPU upload, normal ownership/fences')
