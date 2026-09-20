# Video decoder build

uFlow uses a decoding-only build of [GaryOderNichts/FFmpeg-wiiu](https://github.com/GaryOderNichts/FFmpeg-wiiu)
at commit `24997bdb3e5a3bc666f05e1497b0c102390f9ae0` (FFmpeg LGPL 2.1 or later).
No GPL codecs, encoders, remote protocols or external codec packages are enabled.
`tools/ffmpeg/h264_wiiu.c` replaces that revision's experimental adapter with
padded framebuffer allocation, packet-size limits, error cleanup, callback-driven
frame emission and timestamps. Hardware-specific behavior requires console tests.

From the project root, using the existing base builder:

```powershell
docker build -f tools/video-ffmpeg.Dockerfile -t uflow-builder:video .
docker run --rm --mount "type=bind,source=$($PWD.Path),target=/project" uflow-builder:video make -C /project/app
```

`build.ps1` also builds this decoder image before the app. Keep app/build/*.o,
the Makefile and sources to rebuild/relink against a modified FFmpeg library.
The builder retains the exact dependency source under `/tmp/uflow-ffmpeg`, and
its license notices under `/opt/uflow-ffmpeg-license`. The public source revision
and complete local adapter are identified above; do not distribute a binary
without the corresponding dependency notices and source/relink materials.

## Verification

Build the host image with `tools/visual-preview.Dockerfile`, tag
`uflow-visual-preview:video`. Mount the project read-only at `/project` and run
`sh /project/tools/test-video.sh`. It generates short H.264/AAC MP4, MPEG-4/PCM AVI
and silent MJPEG AVI clips in a disposable directory. Checks cover decoded and
presented frames, consumed audio samples, looping, cancellation, suspended-stop
cleanup, missing files and idle-timer behavior. `UFLOW_VIDEO_SAMPLE` can point to
an additional read-only mounted video for testing, without changing the original.
Set `UFLOW_VIDEO_SAMPLE_SECONDS=43` to check a full loop of the supplied 39s Bolt
sample. The optional sample must be shorter than the requested duration for that
extended test's loop assertion. The LGPL notice is in `FFmpeg-LICENSE.txt` here.

`tools/test-library.sh` covers old/new media settings, source association, AVI/MP4
names, scope, depth and invalid values. The host renderer's `videochecks` command
checks dynamic settings rows, touch scrolling, both playback layers and suppression
by Settings, Details, input and navigation. Mount the sample at
`/fixture/Disney-Bolt.mp4`; screenshots use the supplied output prefix.

The optional `UFLOW_PREVIEW_CHECKS` list chooses GPU regression checks in the
visual-preview script. Its default remains `checks`; that older settings fixture
has a known stale first-row assumption (Manage Content Types vs directory).
Use `fadechecks detailschecks launcherchecks` for the current additional checks.

## Console performance diagnosis

The player logs `video-perf` deltas to uflow.log every 5 seconds during the first
30 seconds of each preview, plus a stop summary. `hw` confirms hardware selection;
`decoded`, `shown`, `converted`, `late` are frame counts. `read_ms`, `decode_ms`,
`convert_ms`, `upload_ms` are accumulated stage times during the `dt` interval;
decode time includes video and audio send/receive calls. They overlap between UI
and worker threads and should not be added as if they were serial wall time.
Capture 15 seconds of playback before returning to FTP. Host frame rates cannot
establish Wii U hardware performance. GPU uploads use a three-texture ring;
swscale context and color tables are reused unless stream properties change.

Decode and conversion run on separate workers with 256KiB stacks. A bounded
three-frame queue plus one conversion slot retain decoder frames without copying
their pixels. Only the producer moves/unrefs AVFrame references; the converter
reads a leased image and returns the slot without touching FFmpeg's buffer pool.
Final release happens after both workers have exited. This is important because
the pinned FFmpeg build's pool mutexes are disabled along with pthread support.
Frames may be dropped only when another decoded frame is available; a wall-clock
lateness threshold can no longer discard every future frame. NV12 and planar
4:2:0 at output size use table-based BT.601/709 conversion (full/limited range),
with chroma reused across 2x2 blocks; scaling/other formats retain swscale.
The host suite checks colors against independent floating-point formulas and
injects 26ms decode plus 60ms conversion delays to reproduce the console timing
imbalance, requiring continued presentation instead of starvation.

The next console log measured ~20 presented frames/sec and ~47ms per color
conversion even with the parallel pipeline. For even-sized 4:2:0 images larger
than 960x540, conversion now box-filters each 2x2 luma block and converts its
shared chroma directly to one RGBA pixel. Thus a 1280x720 source uses a 640x360
texture, with SDL's linear GPU scaling for full-screen display. No source file is
changed; aspect ratio is preserved. Smaller 4:2:0 sources retain their resolution;
other formats use swscale capped at 640x360. This deliberately trades preview
detail for a smaller CPU conversion/upload budget. Color tests cover the fused
downsample, varying luma/chroma, padded strides and odd output widths.

Playback holds video and audio until three converted frames are buffered, with
a 200ms fallback for tiny clips or EOF/error tails. A one-frame fixture verifies
this cannot deadlock short clips. Additional metrics: `output` is texture size;
`max_gap_ms` is the session's largest interval between uploads (loop startup
excluded); `gaps_over50` counts >50ms presentation intervals in this report;
`audio_missing` counts requested audio frames unavailable in the ring buffer;
`lag_ms` compares the current video timestamp to the wall clock. These measurements
are needed to verify console pacing, not just aggregate frame counts.

## Decode order versus display order

Bolt's first MP4 packet presentation times, in 1/30s units, are
`0,2,1,6,4,3,5,10,8,7,9,14,12,11,13`; packet DTS starts at -2 and increases.
It contains B pictures and must not be shown in packet/decode order.
The adapter uses immediate `OUTPUT_PER_FRAME=1`; Cemu's implementation models
this mode as `IVD_DECODE_FRAME_OUT` rather than `IVD_DISPLAY_FRAME_OUT`:
[Cemu H264 backend](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/h264_avc/H264DecBackendAVC.cpp).
FFmpeg's best-effort timestamp heuristic can replace non-monotonic hardware PTS
with monotonic packet DTS, mislabeling those pictures. The app now retains the
hardware frame's actual PTS and sorts retained AVFrames before cadence filtering
and conversion. Packet DTS is the safe output watermark. Missing-DTS frames use
the stream's declared reorder depth; EOF drains the tail. The reorder queue is
hard-bounded to 18 entries and owned entirely by the decoder thread.

`reordered` counts backward steps in incoming hardware PTS (before sorting);
`pts_fixed` counts hardware PTS differing from FFmpeg's best-effort timestamp.
`handoff_ms` measures reference handoff instead of the removed full image copy.
`cores` gives sampled CPU masks for decoder/converter (bits 0,1,2 correspond to
cores 0,1,2); no affinity or scheduling priority change has been made.

The host suite checks the exact Bolt packet ordering against monotonic expected
frame identities, DTS/missing-DTS release, capacity, EOF and loop reset. Set
`UFLOW_VIDEO_TEST_REORDER=1` to route real software-decoded fixtures through the
same reorder/lease path. `UFLOW_VIDEO_TEST_CXXFLAGS='-fsanitize=address,undefined
-fno-omit-frame-pointer'` adds memory/UB checks. Injected slow conversion also
compares leased image bytes before/after waiting while the producer runs ahead.
