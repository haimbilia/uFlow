$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'cc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer /project/tests/h264_adapter_test.c $(pkg-config --cflags --libs libavcodec libavutil) -o /tmp/h264-adapter-test && /tmp/h264-adapter-test'
if($LASTEXITCODE){throw 'H.264 adapter tests failed'}
