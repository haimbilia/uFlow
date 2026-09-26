#!/bin/sh
set -eu
# Run inside visual-preview.Dockerfile image, project mounted at /project.
mkdir -p /tmp/uflow-preview
printf 'extern unsigned char glass_ttf[];\nextern unsigned int glass_ttf_size;\n' > /tmp/uflow-preview/glass_ttf.h
cd /project/app/data
{ printf 'unsigned char glass_ttf[] = {';
  od -An -v -tu1 glass.ttf | awk '{for(i=1;i<=NF;i++) printf "%s,", $i}';
  printf '};\nunsigned int glass_ttf_size = sizeof(glass_ttf);\n';
} > /tmp/uflow-preview/font.cpp
cd /project
g++ -std=c++20 -O2 ${UFLOW_PREVIEW_CXXFLAGS:-} -DUFLOW_HOST_PREVIEW -Iapp/source -Itests/stubs -I/tmp/uflow-preview \
    app/source/main.cpp app/source/media.cpp app/source/music.cpp app/source/video.cpp app/source/library.cpp app/source/settings.cpp app/source/raw_launch.cpp app/source/vc_inject.cpp /tmp/uflow-preview/font.cpp \
    $(pkg-config --cflags --libs sdl2 SDL2_ttf SDL2_mixer libpng libavformat libavcodec libavutil libswscale libswresample) -lturbojpeg -lz -o /tmp/uflow-preview/render
cd /tmp/uflow-preview
for screen in ${UFLOW_PREVIEW_SCREENS:-library coverfront coverback details details25 details50 details75 settings sources sourceedit media mediaedit mediaguide directories audio missing launchers launcheredit launchervc}; do
    /tmp/uflow-preview/render "$screen" "/output/$screen.png"
done
/tmp/uflow-preview/render hintchecks /output/control-hints
/tmp/uflow-preview/render launchoptionchecks /output/launch-options
/tmp/uflow-preview/render layoutchecks /output/cover-layouts
/tmp/uflow-preview/render pagingchecks /output/unused.png
if [ -n "${UFLOW_PREVIEW_CJK_FONT:-}" ]; then /tmp/uflow-preview/render japanesechecks /output/japanese-title; fi
if [ -n "${UFLOW_PREVIEW_GPU:-}" ]; then
    for check in ${UFLOW_PREVIEW_CHECKS:-checks}; do
        /tmp/uflow-preview/render "$check" /output/unused.png
    done
fi
