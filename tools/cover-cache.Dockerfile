FROM uflow-builder:video
# Pinned renderer sources for the installed wiiu-sdl2 2.32.10-2.
ADD https://libsdl.org/release/SDL2-2.32.10.tar.gz /tmp/SDL2.tar.gz
ADD https://raw.githubusercontent.com/devkitPro/wut-packages/master/SDL2/SDL2-2.32.10.patch /tmp/SDL2.patch
RUN echo '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165  /tmp/SDL2.tar.gz' | sha256sum -c - && \
    echo 'fe9d639eb1aaad1dd4b148b43946e32154d1b6f2d35839d3b5a5afab660f979c  /tmp/SDL2.patch' | sha256sum -c - && \
    tar -xzf /tmp/SDL2.tar.gz -C /tmp && cd /tmp/SDL2-2.32.10 && patch -p1 < /tmp/SDL2.patch
COPY tools/bc3-upload-layout.h /tmp/SDL2-2.32.10/src/render/wiiu/bc3-upload-layout.h
COPY tools/patch-sdl-cover-format.py /tmp/patch-sdl-cover-format.py
RUN python3 /tmp/patch-sdl-cover-format.py /tmp/SDL2-2.32.10 && \
    test "$(dkp-pacman -Q wiiu-sdl2)" = 'wiiu-sdl2 2.32.10-2' && \
    cd /tmp/SDL2-2.32.10 && \
    for unit in SDL_render_wiiu SDL_rtexture_wiiu; do \
      $DEVKITPPC/bin/powerpc-eabi-gcc -c -O2 -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ \
        -I$DEVKITPRO/portlibs/wiiu/include/SDL2 -I. -Iinclude -I$DEVKITPRO/wut/include \
        src/render/wiiu/$unit.c -o /tmp/$unit.o || exit 1; \
      $DEVKITPPC/bin/powerpc-eabi-ar r $DEVKITPRO/portlibs/wiiu/lib/libSDL2.a /tmp/$unit.o || exit 1; \
    done
