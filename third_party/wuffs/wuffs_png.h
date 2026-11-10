/* Ours, not Wuffs': the calls of Wuffs' PNG and deflate decoders that wuffs_png.c makes for src/lib/png.h. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The buffer for the decoded pixels, width x height x 4 bytes (R, G, B, A); null when there is none. */
typedef unsigned char* (*ei_png_allocate)(void* context, unsigned width, unsigned height);

/* `png` decoded as 8-bit RGBA with straight alpha into the buffer `allocate` gives (a 16-bit sample's high byte; a
   palette's or tRNS's alpha; no gamma). Null when it was, else the reason it was not. */
const char* ei_png_decode(const unsigned char* png, size_t size, ei_png_allocate allocate, void* context);

/* `size` bytes of raw deflate data inflated into `out`: 1 when they make `outSize` bytes at least (those written),
   else 0. */
int ei_inflate(const unsigned char* data, size_t size, unsigned char* out, size_t outSize);

#ifdef __cplusplus
}
#endif
