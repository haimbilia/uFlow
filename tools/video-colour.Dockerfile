FROM uflow-builder:covers
COPY tools/video-shaders/video*.inc /tmp/SDL2-2.32.10/src/render/wiiu/shaders/
COPY tools/patch-sdl-video-colour.py /tmp/patch-sdl-video-colour.py
RUN python3 /tmp/patch-sdl-video-colour.py /tmp/SDL2-2.32.10 && \
    cd /tmp/SDL2-2.32.10 && \
    for unit in SDL_render_wiiu SDL_rtexture_wiiu SDL_rqueue_wiiu SDL_rpresent_wiiu SDL_shaders_wiiu; do \
      $DEVKITPPC/bin/powerpc-eabi-gcc -c -O2 -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ \
        -I$DEVKITPRO/portlibs/wiiu/include/SDL2 -I. -Iinclude -I$DEVKITPRO/wut/include \
        src/render/wiiu/$unit.c -o /tmp/$unit.o || exit 1; \
      $DEVKITPPC/bin/powerpc-eabi-ar r $DEVKITPRO/portlibs/wiiu/lib/libSDL2.a /tmp/$unit.o || exit 1; \
    done
