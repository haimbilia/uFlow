#!/bin/sh
set -eu
# Run inside visual-preview.Dockerfile image, project mounted at /project.
mkdir -p /tmp/uflow-preview
for face in glass switch; do
  printf 'extern unsigned char %s_ttf[];\nextern unsigned int %s_ttf_size;\n' "$face" "$face" > /tmp/uflow-preview/${face}_ttf.h
  { printf 'unsigned char %s_ttf[] = {' "$face";
    od -An -v -tu1 /project/app/data/${face}.ttf | awk '{for(i=1;i<=NF;i++) printf "%s,", $i}';
    printf '};\nunsigned int %s_ttf_size = sizeof(%s_ttf);\n' "$face" "$face";
  } > /tmp/uflow-preview/${face}-font.cpp
done
cat /tmp/uflow-preview/glass-font.cpp /tmp/uflow-preview/switch-font.cpp > /tmp/uflow-preview/font.cpp
cd /project
g++ -std=c++20 -O2 ${UFLOW_PREVIEW_CXXFLAGS:-} -DUFLOW_HOST_PREVIEW -Iapp/source -Itests/stubs -I/tmp/uflow-preview \
    app/source/main.cpp app/source/diagnostics.cpp app/source/title_install.cpp app/source/media.cpp app/source/music.cpp app/source/video.cpp app/source/library.cpp app/source/settings.cpp app/source/raw_launch.cpp app/source/vc_inject.cpp /tmp/uflow-preview/font.cpp \
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
