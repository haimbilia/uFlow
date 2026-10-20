# Menu sound effects

uFlow includes four small WAV effects extracted from the owner's Wii U Menu dump. Settings -> General -> Audio provides Menu Sound Effects (default On) and Effects Volume (default 35%). Music volume remains independent.

The samples are decoded once on load and share the existing 48 kHz SDL_mixer device with music/video. Only one effect channel plays at a time; navigation has a 65 ms repeat limit. HOME/background release and game launch release the device reference, and returning restores it.

Bundled files are in app/content/sounds. An optional SD sounds directory under wiiu/apps/uFlow can override them. source.json records the original archive hash, sequence cue, bank instrument and sample hash. These are the original instrument samples; uFlow does not reproduce Nintendo's sequence envelopes or multi-note sequencing.

To reproduce the four files from the same owner-provided archive:

    python tools/extract-menu-sounds.py path/to/cafe_barista_men.bfsar output --manifest app/content/sounds/source.json --decoder path/to/vgmstream-cli.exe

The exporter refuses a different archive hash rather than guessing indices. Without --manifest it extracts every validated embedded FWAV for inspection.
