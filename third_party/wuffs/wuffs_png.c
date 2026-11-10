/* Ours, not Wuffs': Wuffs' PNG decoder (wuffs-v0.3.c with its PNG modules only) built as the wuffs library
   (CMakeLists.txt) without its warnings, and the two calls the game makes of it (wuffs_png.h). Used by the PNG reader
   (src/lib/png.h; docs/notes/mods.md, "Textures as PNG"). */
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__ZLIB
#include "wuffs/wuffs-v0.3.c"

#include "wuffs/wuffs_png.h"

#include <stdlib.h>

const char* ei_png_decode(const unsigned char* png, size_t size, int sixteen, ei_png_allocate allocate, void* context) {
    wuffs_png__decoder* decoder = (wuffs_png__decoder*)malloc(sizeof__wuffs_png__decoder());
    if (!decoder) return "out of memory";
    const char* error = NULL;
    unsigned char* work = NULL;
    wuffs_base__status status = wuffs_png__decoder__initialize(decoder, sizeof__wuffs_png__decoder(), WUFFS_VERSION,
                                                               WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED);
    /* The chunks' CRCs and the zlib stream's Adler-32 are not checked. */
    wuffs_png__decoder__set_quirk_enabled(decoder, WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, true);
    wuffs_base__io_buffer source = wuffs_base__ptr_u8__reader((uint8_t*)png, size, true);
    wuffs_base__image_config config;
    if (!status.repr) status = wuffs_png__decoder__decode_image_config(decoder, &config, &source);
    if (!status.repr) {
        const uint32_t width = wuffs_base__pixel_config__width(&config.pixcfg);
        const uint32_t height = wuffs_base__pixel_config__height(&config.pixcfg);
        const uint64_t workBytes = wuffs_png__decoder__workbuf_len(decoder).max_incl;
        unsigned char* pixels = width && height ? allocate(context, width, height) : NULL;
        work = workBytes && workBytes <= SIZE_MAX ? (unsigned char*)malloc((size_t)workBytes) : NULL;
        if (!pixels || (workBytes && !work)) {
            error = !pixels ? "no pixels for its size" : "out of memory";
        } else {
            const size_t pixelBytes = sixteen ? 8 : 4;
            wuffs_base__pixel_config__set(&config.pixcfg,
                                          sixteen ? WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE
                                                  : WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
                                          WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, width, height);
            wuffs_base__pixel_buffer buffer;
            status = wuffs_base__pixel_buffer__set_from_slice(&buffer, &config.pixcfg,
                                                              wuffs_base__make_slice_u8(pixels, (size_t)width * height * pixelBytes));
            if (!status.repr)
                status = wuffs_png__decoder__decode_frame(decoder, &buffer, &source, WUFFS_BASE__PIXEL_BLEND__SRC,
                                                          wuffs_base__make_slice_u8(work, (size_t)workBytes), NULL);
            /* Wuffs' swizzler gives 16-bit samples as BGRA only: blue and red change places. */
            if (!status.repr && sixteen) {
                uint16_t* p = (uint16_t*)pixels;
                for (size_t i = 0, n = (size_t)width * height; i < n; ++i, p += 4) {
                    const uint16_t blue = p[0];
                    p[0] = p[2];
                    p[2] = blue;
                }
            }
        }
    }
    if (!error && status.repr) error = wuffs_base__status__message(&status);
    free(work);
    free(decoder);
    return error;
}

int ei_inflate(const unsigned char* data, size_t size, unsigned char* out, size_t outSize) {
    wuffs_deflate__decoder* decoder = (wuffs_deflate__decoder*)malloc(sizeof__wuffs_deflate__decoder());
    if (!decoder) return 0;
    uint8_t work[WUFFS_DEFLATE__DECODER_WORKBUF_LEN_MAX_INCL_WORST_CASE + 1];
    wuffs_base__io_buffer source = wuffs_base__ptr_u8__reader((uint8_t*)data, size, true);
    wuffs_base__io_buffer target = wuffs_base__ptr_u8__writer(out, outSize);
    wuffs_base__status status = wuffs_deflate__decoder__initialize(decoder, sizeof__wuffs_deflate__decoder(), WUFFS_VERSION,
                                                                   WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED);
    /* A segment ends after a full flush, not on a final block: its bytes are all there when the output is full. */
    if (!status.repr) wuffs_deflate__decoder__transform_io(decoder, &target, &source, wuffs_base__make_slice_u8(work, sizeof work));
    free(decoder);
    return !status.repr && target.meta.wi == outSize;
}
