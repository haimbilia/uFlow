FROM uflow-builder:video-colour
COPY tools/ffmpeg/h264_wiiu.c /tmp/uflow-ffmpeg/libavcodec/h264_wiiu.c
COPY tools/ffmpeg/wiiu-buffer-pool.h /tmp/uflow-ffmpeg/libavutil/wiiu-buffer-pool.h
COPY tools/patch-ffmpeg-buffer-pool.py /tmp/patch-ffmpeg-buffer-pool.py
RUN python3 /tmp/patch-ffmpeg-buffer-pool.py /tmp/uflow-ffmpeg && \
    make -C /tmp/uflow-ffmpeg -j4 && make -C /tmp/uflow-ffmpeg install
COPY tools/video-shaders/nv*.inc /tmp/SDL2-2.32.10/src/render/wiiu/shaders/
COPY tools/video-nv12-backend.c /tmp/SDL2-2.32.10/src/render/wiiu/video-nv12-backend.c
COPY tools/patch-sdl-video-nv12.py /tmp/patch-sdl-video-nv12.py
RUN python3 /tmp/patch-sdl-video-nv12.py /tmp/SDL2-2.32.10 && \
    cd /tmp/SDL2-2.32.10 && \
    for unit in SDL_render_wiiu SDL_rtexture_wiiu SDL_rqueue_wiiu SDL_rpresent_wiiu SDL_shaders_wiiu; do \
      $DEVKITPPC/bin/powerpc-eabi-gcc -c -O2 -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ \
        -I$DEVKITPRO/portlibs/wiiu/include/SDL2 -I. -Iinclude -I$DEVKITPRO/wut/include \
        src/render/wiiu/$unit.c -o /tmp/$unit.o || exit 1; \
      $DEVKITPPC/bin/powerpc-eabi-ar r $DEVKITPRO/portlibs/wiiu/lib/libSDL2.a /tmp/$unit.o || exit 1; \
    done
