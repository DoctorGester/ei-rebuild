// A texture from loose .bmp files (from CTexture::LoadBmp for a
// textures\<name>.bmp in the game folder, which overrides the archived .mmp: CTextureManager::
// LoadTexture). The .bmp's 24-bit colours and, when there is an 8-bit a_<name>.bmp beside it, its
// grey levels as alpha are drawn into a texture on the GPU (CTexture::TakeBlankHolder,
// render::DrawIntoTexture), which keeps what the texture's format keeps of the alpha. See
// docs/notes/renderer.md (section 7).
#include "texture.h"
#include "game_strings.h"

#include "dib.h"
#include "surface.h"

#include "platform/file.h"
#include "lib/crt.h"
#include "render/render.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

const char* CantLoadText() { return "CTexture::FillSurface.\n Can't Load texture %s.\n"; }        // "CTexture::FillSurface.\n Can't Load texture %s.\n"
const char* AlphaNot8BitText() { return "Alpha-file %s must be 8bit gray instead of %d"; }


// The .bmp's pixels as 0xAARRGGBB, `width` x `height`: `width` rows of `height` pixels read (the original's loop),
// the colours read on as B, G, R bytes without the .bmp's row padding (and in its row order: a bottom-up .bmp comes out
// upside down), the alpha the alpha .bmp's byte at the pixel's index (when given, else 255). Pixels from row `height` /
// column `width` on are read but not kept.
std::vector<std::uint32_t> ReadPixels(int width, int height, const std::uint8_t* colour, const std::uint8_t* alpha) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    int index = 0;   // the colour byte read next
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < height; ++x) {
            std::uint32_t a = alpha ? alpha[index / 3] : 0xFF;
            std::uint32_t argb = (a << 24) | (static_cast<std::uint32_t>(colour[index + 2]) << 16) |
                                 (static_cast<std::uint32_t>(colour[index + 1]) << 8) | colour[index];
            index += 3;
            if (y < height && x < width) out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = argb;
        }
    }
    return out;
}

} // namespace

// Details kept: the loop runs over the width, then the height (a non-square .bmp overran
// the surface's rows; here the rows and pixels past it are left out); a failed open of the alpha
// file is silent. No texture format: the error for a surface that cannot be made (the staging
// surface's: sizes zero) ends the game.
int CTexture::LoadBmpFiles(TextureDevice* device, const char* path, const char* alphaPath) {
    if (flags & TextureFlag::Loaded) return 1;
    platform::File file;
    platform::File alphaFile;
    Dib colour;
    Dib alpha;
    if (!file.Open(path, platform::File::modeRead)) {
        game::DebugLog(CantLoadText(), GetName());
        return 0;
    }
    colour.Read(&file);
    file.Close();
    SIZE size;
    colour.Size(&size);
    const int bmpWidth = size.cx, bmpHeight = size.cy;
    bool withAlpha = false;
    if (alphaFile.Open(alphaPath, platform::File::modeRead)) {
        alpha.Read(&alphaFile);
        alphaFile.Close();
        std::uint16_t bits = alpha.header->bitCount;
        if (bits == 8)
            withAlpha = true;
        else
            game::DebugLog(AlphaNot8BitText(), alphaPath, static_cast<unsigned>(bits));
    }
    height = static_cast<std::uint32_t>(bmpHeight);
    width = static_cast<std::uint32_t>(bmpWidth);
    const std::uint8_t* alphaBits = withAlpha ? alpha.bits.data() : nullptr;

    // Drawn into the texture on the GPU.
    TakeBlankHolder(device, bmpWidth, bmpHeight, true);
    const std::vector<std::uint32_t> pixels = ReadPixels(bmpWidth, bmpHeight, colour.bits.data(), alphaBits);
    const render::TextureImage image{nullptr, pixels.data(), static_cast<std::uint32_t>(pixels.size() * 4), bmpWidth, bmpHeight,
                                     render::ImageLayout::Argb};
    render::DrawIntoTexture(surface, &image, 0, 0, bmpWidth, bmpHeight);
    flags |= TextureFlag::Loaded;
    return 1;
}
