# Preview GPU colour conversion

Packed texture channels are Y, U, V, 255. The CPU applies the same half-size
2x2 luma filter as before; the GPU performs the colour matrix, clamping and SDL
colour/opacity modulation. Four fixed matrices cover limited/full-range BT.601
and BT.709. Other pixel formats keep the existing CPU conversion fallback.

The vertex shader comes from SDL's Wii U renderer (GaryOderNichts, zlib licence).
Pixel shaders and generation/test scripts are uFlow additions. Generated `.inc`
files are checked in so normal app builds do not require a shader assembler.

Regenerate with `python tools/video-shaders/generate.py PATH/latte-assembler.exe`,
using the [Decaf assembler](https://github.com/decaf-emu/latte-assembler/releases/tag/v0.1).
Then run `python tools/video-shaders/test.py` and rebuild the
`tools/video-colour.Dockerfile` image before relinking the app.

The GPU path uses SDL's existing triple-buffered texture ownership and draw
fences. Textures retain their colour mode through fades and are recreated when
the mode changes. Shader allocation failure falls back to the CPU path.
The native NV12 backend bypasses CPU packing entirely for hardware-decoded
H.264. It holds an AVFrame reference until both planes have been copied into
fenced Y8/UV88 GPU textures, then immediately releases that reference. NV12
textures use the source resolution; packed textures remain a fallback for
planar software decoding. The native shaders sample luma and chroma separately.
Build `tools/video-nv12.Dockerfile` after the colour builder. Plane upload bounds,
allocation failure, fencing and background suspension have host stub tests in
`tests/video_nv12_backend_test.c`; frame lifetime is covered by the decoder suite
with a raw NV12 fixture and `UFLOW_VIDEO_TEST_NV12=1`.

Host tests validate packing and matrix calculations; console diagnostics expose
`gpu_colour`, `converted`, `convert_us`, `upload_us`, lag and audio underruns.
