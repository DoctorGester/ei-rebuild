/* Ours, not Wuffs': the calls of Wuffs' PNG and deflate decoders that wuffs_png.c makes for src/lib/png.h. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The buffer for the decoded pixels, width x height x 4 samples (R, G, B, A) of 8 bits, or of 16 bits (native
   order) when `sixteen` was set; null when there is none. */
typedef unsigned char* (*ei_png_allocate)(void* context, unsigned width, unsigned height);

/* `png` decoded as RGBA with straight alpha into the buffer `allocate` gives: 8-bit (a 16-bit sample's high byte) or,
   with `sixteen`, 16-bit (an 8-bit sample v as v * 257); a palette's or tRNS's alpha; no gamma. Null when it was,
   else the reason it was not. */
const char* ei_png_decode(const unsigned char* png, size_t size, int sixteen, ei_png_allocate allocate, void* context);

/* `size` bytes of raw deflate data inflated into `out`: 1 when they make `outSize` bytes at least (those written),
   else 0. */
int ei_inflate(const unsigned char* data, size_t size, unsigned char* out, size_t outSize);

#ifdef __cplusplus
}
#endif
