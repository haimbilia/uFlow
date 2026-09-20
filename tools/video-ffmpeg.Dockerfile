# A pinned, decoding-only FFmpeg build. No network protocols, encoders or GPL codecs.
FROM uflow-builder:dev
ARG FFMPEG_REV=24997bdb3e5a3bc666f05e1497b0c102390f9ae0
RUN git clone https://github.com/GaryOderNichts/FFmpeg-wiiu.git /tmp/uflow-ffmpeg && \
    cd /tmp/uflow-ffmpeg && git checkout --detach ${FFMPEG_REV}
COPY tools/ffmpeg/h264_wiiu.c /tmp/uflow-ffmpeg/libavcodec/h264_wiiu.c
RUN cd /tmp/uflow-ffmpeg && \
    ./configure --prefix=$DEVKITPRO/portlibs/ppc --enable-cross-compile \
      --cross-prefix=$DEVKITPPC/bin/powerpc-eabi- --arch=ppc --cpu=750 --target-os=none \
      --extra-cflags="-D__WIIU__ -O2 -mcpu=750 -meabi -mhard-float -I$DEVKITPRO/wut/include" \
      --extra-ldflags="-mcpu=750 -meabi -mhard-float -specs=$DEVKITPRO/wut/share/wut.specs -L$DEVKITPRO/wut/lib -lwut" \
      --disable-shared --enable-static --disable-runtime-cpudetect --disable-programs --disable-doc \
      --disable-everything --disable-network --disable-avdevice --disable-avfilter --disable-postproc \
      --disable-pthreads --disable-w32threads --disable-os2threads --disable-debug \
      --disable-bzlib --disable-iconv --disable-lzma --disable-securetransport --disable-xlib --disable-zlib \
      --enable-decoder=h264,h264_wiiu,mpeg4,mjpeg,aac,mp3,pcm_s16le,pcm_s16be,pcm_u8 \
      --enable-parser=h264,mpeg4video,mjpeg,aac,mpegaudio --enable-demuxer=mov,avi \
      --enable-bsf=h264_mp4toannexb --enable-protocol=file --enable-swscale --enable-swresample && \
    make -j4 && make install && \
    mkdir -p /opt/uflow-ffmpeg-license && \
    cp COPYING.LGPLv2.1 LICENSE.md /opt/uflow-ffmpeg-license/
