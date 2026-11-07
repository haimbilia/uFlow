FROM uflow-builder:video-import
COPY tools/patch-sdl-handoff.py /tmp/patch-sdl-handoff.py
RUN python3 /tmp/patch-sdl-handoff.py /tmp/SDL2-2.32.10 && \
    cd /tmp/SDL2-2.32.10 && \
    $DEVKITPPC/bin/powerpc-eabi-gcc -c -O2 -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ \
      -I$DEVKITPRO/portlibs/wiiu/include/SDL2 -I. -Iinclude -I$DEVKITPRO/wut/include \
      src/video/wiiu/SDL_wiiuvideo.c -o /tmp/SDL_wiiuvideo.o && \
    $DEVKITPPC/bin/powerpc-eabi-ar r $DEVKITPRO/portlibs/wiiu/lib/libSDL2.a /tmp/SDL_wiiuvideo.o
