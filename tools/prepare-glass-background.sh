#!/bin/sh
set -eu
# Run in uflow-visual-preview:video, /project writable and /media read-only.
# Use a NEW output path when replacing an asset, then review before publishing.
input=${1:-/media/Wii U Menu - TV Background Loop.mp4}
output=${2:-/project/app/content/skins/glass/background.mp4}
ffmpeg -v error -i "$input" -an \
    -filter_complex ' [0:v]trim=start=3:end=598,setpts=PTS-STARTPTS,fps=30,scale=960:540:flags=lanczos,split=2[a][b];[a]trim=start=1,setpts=PTS-STARTPTS[body];[b]trim=duration=1,setpts=PTS-STARTPTS[head];[body][head]xfade=transition=fade:duration=1:offset=593,format=yuv420p[out]' -map '[out]' \
    -c:v libx264 -preset fast -crf 18 -profile:v baseline -level:v 3.1 \
    -pix_fmt yuv420p -bf 0 -refs 1 -g 60 -movflags +faststart -map_metadata -1 -n "$output"
ffmpeg -v error -i "$output" -f null -
