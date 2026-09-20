GlassFlow background derived from the user's supplied video:
D:/Projects/WiiU/Media/Wii U Menu - TV Background Loop.mp4
Source SHA256: 0024584A0E2733E1E6AD2F47A5A714A947B6A40FA9E08E3C8CDCAFC82C75BA46
Embedded source metadata credits Imora; no ownership/redistribution claim is made.
Personal build asset; source video remains untouched.

Prepared using tools/prepare-glass-background.sh:
960x540, 30fps, H.264 Constrained Baseline level3.1, YUV420p, no B-frames,
one reference, CRF18, no audio, timestamps start at zero, MP4 faststart.
Source intro/outro fades removed (keep seconds3..598); final second crossfades
into seconds3..4 and repeats at second4 for a continuous, bright loop.
Output594seconds. Repeats through VideoPlayer's EOF loop.
Included inside uFlow.wuhb so standalone and fresh-installer builds both have it.
Existing Background Motion setting freezes playback on a frame when disabled.
Game previews have decoder priority. Settings/details freeze the theme; cover
inspection does not. Missing/bad assets fall back to the procedural background.
