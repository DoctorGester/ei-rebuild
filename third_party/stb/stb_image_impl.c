/* Ours, not stb's: the implementations of stb_image.h (PNG only, from memory), stb_image_write.h (PNG only, to a
   callback, its deflate miniz's) and stb_dxt.h, built as the stb_image library (CMakeLists.txt) without their warnings.
   Used by the PNG textures (src/game/models/texture_png.cpp; docs/notes/mods.md, "Textures as PNG"). */
#include "miniz/miniz.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"

/* stb_image_write's own deflate makes larger files than miniz's. */
static unsigned char* ei_png_deflate(unsigned char* data, int length, int* outLength, int quality) {
    size_t size = 0;
    void* out = tdefl_compress_mem_to_heap(data, (size_t)length, &size,
                                           (int)tdefl_create_comp_flags_from_zip_params(quality, 15, MZ_DEFAULT_STRATEGY));
    *outLength = (int)size;
    return (unsigned char*)out;
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_ZLIB_COMPRESS ei_png_deflate
#include "stb/stb_image_write.h"

#define STB_DXT_IMPLEMENTATION
#include "stb/stb_dxt.h"
