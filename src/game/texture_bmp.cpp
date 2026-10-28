// A texture from loose .bmp files (from CTexture::LoadBmp for a
// textures\<name>.bmp in the game folder, which overrides the archived .mmp: CTextureManager::
// LoadTexture). The .bmp's 24-bit colours and, when there is an 8-bit a_<name>.bmp beside it, its
// grey levels as alpha are converted to the texture's 16-bit format (the pixels go
// into the holder's memory).
// The texture has no DirectDraw texture: its holder has
// only the new renderer's memory (NewMemoryTextureHolder), the pixels are written there and the new
// renderer reads them. See docs/notes/renderer.md (section 7).
#include "texture.h"
#include "game_strings.h"

#include "dib.h"
#include "dx_surface.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include "game_singletons.h"

#include <cstring>

namespace {

const char* CantLoadText() { return "CTexture::FillSurface.\n Can't Load texture %s.\n"; }        // "CTexture::FillSurface.\n Can't Load texture %s.\n"
const char* AlphaNot8BitText() { return "Alpha-file %s must be 8bit gray instead of %d"; }


// The game's .bmp reader (CDib, dib.h): Read takes an open file (a short or non-"BM" file: a message
// box, nothing read), Size gives 0 x 0 without a header.

// The .bmp's pixels as written into `out` (`pitch` bytes a row): `rows` rows of
// `perRow` 16-bit pixels, the colours read on as B, G, R bytes without the .bmp's row padding (and
// in its row order: a bottom-up .bmp comes out upside down), the alpha the alpha .bmp's byte at the
// pixel's index (when given, else 255), converted to the texture's format. Pixels from
// row `maxRows` / column `maxPerRow` on are read but not written.
void WritePixels(std::uint8_t* out, int pitch, int rows, int perRow, int maxRows, int maxPerRow,
                 const std::uint8_t* colour, const std::uint8_t* alpha, const ChannelLayout& layout) {
    int index = 0;   // the colour byte read next
    for (int y = 0; y < rows; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(out + static_cast<std::ptrdiff_t>(y) * pitch);
        for (int x = 0; x < perRow; ++x) {
            std::uint32_t a = alpha ? alpha[index / 3] : 0xFF;
            std::uint32_t argb = (a << 24) | (static_cast<std::uint32_t>(colour[index + 2]) << 16) |
                                 (static_cast<std::uint32_t>(colour[index + 1]) << 8) | colour[index];
            index += 3;
            if (y < maxRows && x < maxPerRow) row[x] = static_cast<std::uint16_t>(layout.ToNative(argb));
        }
    }
}

} // namespace

// Details kept: the loop runs over the width, then the height (a non-square .bmp overran
// the surface's rows; in the new renderer's memory the rows and pixels past it are left out); a
// failed open of the alpha file is silent. No memory for the texture: the error for a
// surface that cannot be made (the staging surface's: sizes zero) ends the game.
int CTexture::LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath) {
    if (flags_ & kTextureLoaded) return 1;
    ChannelLayout layout;
    layout.Split(&format_);
    lib::File file;
    lib::File alphaFile;
    CDib colour;
    CDib alpha;
    if (!file.Open(path, lib::File::modeRead)) {
        game::DebugLog(CantLoadText(), GetName());
        return 0;
    }
    colour.Read(&file);
    file.Close();
    SIZE size;
    colour.Size(&size);
    const int width = size.cx, height = size.cy;
    bool withAlpha = false;
    if (alphaFile.Open(alphaPath, lib::File::modeRead)) {
        alpha.Read(&alphaFile);
        alphaFile.Close();
        WORD bits = alpha.header->biBitCount;
        if (bits == 8)
            withAlpha = true;
        else
            game::DebugLog(AlphaNot8BitText(), alphaPath, static_cast<unsigned>(bits));
    }
    height_ = static_cast<std::uint32_t>(height);
    width_ = static_cast<std::uint32_t>(width);
    const std::uint8_t* alphaBits = withAlpha ? alpha.bits : nullptr;

    // The new renderer's memory is the texture.
    CDXSurface* memory = NewMemoryTextureHolder(device, width, height, format_.words);
    if (!memory) ThrowSurfaceNotMade(device, 0, 0);
    CDXSurface* previous = surface_;
    surface_ = memory;
    ++memory->refs_;
    if (previous) previous->Release();
    SurfacePixels pixels;
    DirectPixels(memory, &pixels);
    gameapi::EnterCriticalSection(&g_deviceLock);
    WritePixels(pixels.bits, pixels.pitch, width, height, pixels.height, pixels.width, colour.bits, alphaBits, layout);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    flags_ |= kTextureLoaded;
    return 1;
}
