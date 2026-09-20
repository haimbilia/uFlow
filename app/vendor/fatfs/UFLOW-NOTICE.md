FatFs source from https://github.com/abbrev/fatfs at
ab992fcf2926b8a1ee3f0a8f9cefb6584174100d. Copyright and redistribution
conditions are retained in ff.c, ff.h and ffunicode.c (ChaN).

uFlow configuration: read-only FAT32, long UTF-8 names, stack LFN buffers,
single volume. All FatFs operations are serialized by the USB devoptab mutex.
Sector I/O uses libmocha raw reads with an aligned bounce buffer; there is no
raw-write implementation. No format, repair, or system mount replacement occurs.
USB mapping initially targets one FAT32 disk, first supported partition.
