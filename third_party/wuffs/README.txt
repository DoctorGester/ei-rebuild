Wuffs (https://github.com/google/wuffs), Apache License 2.0 (LICENSE).
release/c/wuffs-v0.3.c of release v0.3.6 (tag v0.3.6, commit b69ebf4606d8dbf2a67accf6ca2ce88434c5062a; its
WUFFS_VERSION_STRING "0.3.6+3407.20261020"), unmodified.
Used to read the textures' PNG files (src/lib/png.h; docs/notes/mods.md, "Textures as PNG"). wuffs_png.c and
wuffs_png.h are ours: the library built with its PNG modules only (base, adler32, crc32, deflate, zlib, png) and the
one decode call, built as the wuffs library with its warnings off (CMakeLists.txt). Built without /arch:AVX, so with
MSVC it runs its portable code (its x86 filters and checksums need AVX there; it says so at compile time).
