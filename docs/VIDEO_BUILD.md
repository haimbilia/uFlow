# Cover cache GPU safety (2026-10-02)

The native BC3 importer is currently disabled after a console GPU hang. The app
uses worker-side cached-mip expansion and public SDL texture uploads on every
platform. The ordinary `uflow-builder:video` image builds this path; the old
`covers` image/helper is experimental and is no longer linked into the app.
Existing disk caches remain compatible. Console retest is required after build.

# Video decoder build

uFlow uses a decoding-only build of [GaryOderNichts/FFmpeg-wiiu](https://github.com/GaryOderNichts/FFmpeg-wiiu)
at commit `24997bdb3e5a3bc666f05e1497b0c102390f9ae0` (FFmpeg LGPL 2.1 or later).
No GPL codecs, encoders, remote protocols or external codec packages are enabled.
`tools/ffmpeg/h264_wiiu.c` replaces that revision's experimental adapter with
padded framebuffer allocation, packet-size limits, error cleanup, callback-driven
frame emission and timestamps. Hardware-specific behavior requires console tests.

Intermittent decoder-open follow-up (2026-09-28): analysis of the console's
H264DECInitParam maps status 0x01060000 to UVDCheckSegmentViolation. Its physical
address check includes the one-past-end address and rejects a buffer crossing
a 256 MiB segment. Workspace, frame and packet allocations now validate with
H264DECCheckMemSegmentation. A rejected 1024-byte-aligned allocation is freed,
then retried once with power-of-two alignment strictly larger than its size.
The retry is validated too; virtual alignment alone is not assumed sufficient.
Allocation sizes and retained-frame ownership remain unchanged. Fault-injection
tests cover first-attempt rejection, bounded retry, allocation failure and
retained frames under ASan/UBSan. Console validation is still required; this
does not establish a fix for the separately observed mapped-heap lock stall.

The September 28 playback candidate decodes directly into 1024-byte-aligned,
reference-counted NV12 buffers, with 256-byte pitch and 16-line height padding.
It removes the callback's full-image copy. A retained frame cannot return to the
pool until its last reader releases it; pool teardown also permits outstanding
frames to survive. Only the producer manipulates references, as before. Fresh
allocations are flushed before hardware writes and outputs invalidated before
CPU reads. Every allocated image passes the platform's memory-segmentation check.
When packet timestamps fall behind the playback clock, only packets positively
identified as non-reference pictures by h264.rpl may be skipped; references and
presentation reordering remain intact. Skipped packets also advance the reorder
watermark. No source videos are rewritten by the player.

Run `tools/test-h264-adapter.ps1` for ASan/UBSan tests against real FFmpeg buffer
ownership and stubbed console calls: padded layouts, retained frames, pool reuse,
close/flush lifetime, allocation/init/decode errors and safe skipping. The normal
video suite remains required. `native_ms` separates H264DECExecute wall time from
the broader `decode_ms`; `skip_nonref` counts safe skips. These counters measure
the candidate on the console; host success alone does not establish playback speed.

From the project root, using the existing base builder:

```powershell
docker build -f tools/video-ffmpeg.Dockerfile -t uflow-builder:video .
docker build -f tools/cover-cache.Dockerfile -t uflow-builder:covers .
docker build -f tools/video-colour.Dockerfile -t uflow-builder:video-colour .
docker build -f tools/video-nv12.Dockerfile -t uflow-builder:video-nv12 .
docker run --rm --mount "type=bind,source=$($PWD.Path),target=/project" uflow-builder:video-nv12 make -C /project/app
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
checks dynamic settings rows, touch scrolling, all three preview modes and suppression
by Settings, Details, input and navigation. Mount the sample at
`/fixture/Disney-Bolt.mp4`; screenshots use the supplied output prefix.

Glass Flow's Showcase metadata panel uses Settings' selected-category lens:
clear glass material, soft shadow and a cyan wash, without an opaque underlay.
Light lettering uses the existing subtle video shadow instead of the scan
popup's multi-pass white halo. The existing 192x108 blur targets and lens scratch
sample the live video; no additional texture or GPU readback is introduced.
Surface, shadow and lettering follow the same reveal/reversal animation.
Pixel Flow keeps its existing panel. Run videochecks, detailschecks and scanchecks
to inspect both skins and intermediate transition frames.

Showcase (mode 2) is the default for new preview sources. Modes 0/1 retain their
saved Background/Foreground meanings. Showcase uses the same decoder and case
geometry as the existing player/details screen: a complete turn into the lower
left, then a compact theme-colored metadata panel. Entry and dismissal use the
configured animation speed. Dismissal cancels audio/decoding immediately and
retains only the last GPU texture for the short fade, releasing it at completion;
menus, selection changes and system-background release discard it immediately.
The host video test checks frame retention across asynchronous worker cleanup;
`videochecks` captures both themes at quarter-transition poses and checks reversal,
details entry and navigation interruption. The native installer generates mode 2
for each selected system without replacing saved media settings on reinstall.

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
# Preview audio read-ahead

The packet reader overlaps local storage reads with video/audio decoding. It
owns demux until joined; seek/close remain on the decoder worker after the join.
The queue is capped at 96 packets and normally 512 KiB, with one oversized packet
(up to the existing 8 MiB packet limit) allowed only into an empty queue; the
reader may hold one pending packet. Decoded-image and PCM queues are unchanged.
Playback waits for 200 ms of sound when possible, with a 400 ms startup fallback.
`tools/test-video.sh` compares identical injected 10 ms packet reads and 22 ms
video decodes in serial and parallel modes and checks audio underruns, bounds,
looping, suspension, nonblocking cancellation and cleanup. Host success still
needs a console retest using the original preview files.
