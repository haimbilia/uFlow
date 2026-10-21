FatFs source from https://github.com/abbrev/fatfs at
ab992fcf2926b8a1ee3f0a8f9cefb6584174100d. Copyright and redistribution
conditions are retained in ff.c, ff.h and ffunicode.c (ChaN).

uFlow configuration: read-only FAT32, long UTF-8 names, stack LFN buffers,
single volume. All FatFs operations are serialized by the USB devoptab mutex.
Sector I/O uses libmocha raw reads with an aligned bounce buffer; there is no
raw-write implementation. No format, repair, or system mount replacement occurs.
USB mapping initially targets one FAT32 disk, first supported partition.

uFlow discovery accepts the UStealth 55 AB signature at sector zero as well as
55 AA, for partitioned drives and FAT32 volumes beginning at sector zero. Only
the local signature comparison changes; sector buffers, raw identity hashes and
the disk's hidden state remain unchanged. Other partition boot sectors retain
the normal signature requirement. The app and RPXLoadingModule share this code.
