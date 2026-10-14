// A texture from loose .bmp files (0068FBF0, from CTexture::LoadBmp 00690260 for a
// textures\<name>.bmp in the game folder, which overrides the archived .mmp: CTextureManager::
// LoadTexture). The .bmp's 24-bit colours and, when there is an 8-bit a_<name>.bmp beside it, its
// grey levels as alpha are converted to the texture's 16-bit format, written into a system-memory
// surface and loaded into a managed texture.
//
// In the switch-over ("present" with "cpuwindows", ticket R8) the texture has no DirectDraw
// texture: its holder has only the new renderer's memory (NewMemoryTextureHolder), the pixels are
// written there and the new renderer reads them. Outside the switch-over (or without memory) as
// the original. See docs/notes/renderer.md (section 7).
#include "texture.h"
#include "game_strings.h"

#include "device.h"

#include "dib.h"
#include "dx_surface.h"
#include "originals_engine.h"
#include "originals_render.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
const char* CantLoadText() { return game::Text(0x00795BB4); }        // "CTexture::FillSurface.\n Can't Load texture %s.\n"
const char* AlphaNot8BitText() { return game::Text(0x00795B84); }    // "Alpha-file %s must be 8bit gray instead of %d"
const char* CantLockStagingText() { return game::Text(0x00795B48); }     // "...Can't lock system memory surface\n"
const char* CantUnlockStagingText() { return game::Text(0x00795B0C); }   // "...Can't unlock system memory surface\n"
const char* CantLockText() { return game::Text(0x0078E30C); }        // "Can't lock surface.\n"
const char* CantUnlockText() { return game::Text(0x0078E2F4); }      // "Can't unlock surface.\n"

constexpr std::uint32_t kLockWriteWait = 0x21;   // DDLOCK_WAIT | DDLOCK_WRITEONLY

// DDSURFACEDESC2 (0x7C bytes) as dwords.
enum DescIndex { kSize = 0, kFlags = 1, kHeight = 2, kWidth = 3, kPitch = 4, kSurface = 9, kPixelFormat = 18,
                 kCaps = 26, kCaps2 = 27, kDescWords = 31 };
constexpr std::uint32_t kDescFlags = 0x1007;              // CAPS | HEIGHT | WIDTH | PIXELFORMAT
constexpr std::uint32_t kCapsStaging = 0x1800;            // TEXTURE | SYSTEMMEMORY
constexpr std::uint32_t kCapsTexture = 0x11000, kCaps2Texture = 0x20000;   // TEXTURE | ALLOCONLOAD; TEXTUREMANAGE

// The game's .bmp reader (CDib, dib.h): Read takes an open file (a short or non-"BM" file: a message
// box, nothing read), Size gives 0 x 0 without a header.
using GameBitmap = CDib;

// A GameBitmap constructed and destroyed with the scope (the original's locals).
class Bitmap {
public:
    Bitmap() { dib_.Construct(); }
    ~Bitmap() { dib_.Destruct(); }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    GameBitmap& operator*() { return dib_; }
    GameBitmap* operator->() { return &dib_; }

private:
    GameBitmap dib_;
};

// The .bmp's pixels as 0068FBF0 writes them into `out` (`pitch` bytes a row): `rows` rows of
// `perRow` 16-bit pixels, the colours read on as B, G, R bytes without the .bmp's row padding (and
// in its row order: a bottom-up .bmp comes out upside down), the alpha the alpha .bmp's byte at the
// pixel's index (when given, else 255), converted to the texture's format (004EDC00). Pixels from
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

// A texture holder from 004EFA60 for `desc`, counted once, or null when its allocation fails.
CDXSurface* NewCountedSurface(CTextureDevice* device, const std::uint32_t* desc) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    auto* holder = memory ? static_cast<CDXSurface*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc)) : nullptr;
    if (holder) ++holder->refCount;
    return holder;
}

void DescribeTexture(std::uint32_t (&desc)[kDescWords], const PixelFormat& format, std::uint32_t width,
                     std::uint32_t height, std::uint32_t caps, std::uint32_t caps2) {
    std::memset(desc, 0, sizeof(desc));
    desc[kSize] = sizeof(desc);
    desc[kFlags] = kDescFlags;
    desc[kHeight] = height;
    desc[kWidth] = width;
    std::memcpy(&desc[kPixelFormat], &format, sizeof(format));
    desc[kCaps] = caps;
    desc[kCaps2] = caps2;
}

} // namespace

// Original details kept: the loop runs over the width, then the height (a non-square .bmp overruns
// the surface's rows; in the new renderer's memory the rows and pixels past it are left out); a
// failed open of the alpha file is silent; a failed Lock whose report says go on writes through the
// description it left (null), and one that says stop returns with the device lock held.
int CTexture::LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath) {
    if (flags_ & kTextureLoaded) return 1;
    ChannelLayout layout;
    layout.Split(&format_);
    mfc::CFile file;
    mfc::CFile alphaFile;
    Bitmap colour;
    Bitmap alpha;
    if (!file.Open(path, mfc::CFile::modeRead, nullptr)) {
        game::DebugLog(CantLoadText(), GetName());
        return 0;
    }
    colour->Read(&file);
    file.Close();
    SIZE size;
    colour->Size(&size);
    const int width = size.cx, height = size.cy;
    bool withAlpha = false;
    if (alphaFile.Open(alphaPath, mfc::CFile::modeRead, nullptr)) {
        alpha->Read(&alphaFile);
        alphaFile.Close();
        WORD bits = alpha->header->biBitCount;
        if (bits == 8)
            withAlpha = true;
        else
            game::DebugLog(AlphaNot8BitText(), alphaPath, static_cast<unsigned>(bits));
    }
    height_ = static_cast<std::uint32_t>(height);
    width_ = static_cast<std::uint32_t>(width);
    const std::uint8_t* alphaBits = withAlpha ? alpha->bits : nullptr;

    // In the switch-over: the new renderer's memory is the texture.
    if (CDXSurface* memory = NewMemoryTextureHolder(device, width, height, format_.words)) {
        CDXSurface* previous = surface_;
        surface_ = memory;
        ++memory->refCount;
        if (previous) previous->Release();
        std::uint32_t desc[kDescWords];
        ClientPixels(memory, desc);
        gameapi::EnterCriticalSection(DeviceLock());
        WritePixels(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[kSurface])),
                    static_cast<int>(desc[kPitch]), width, height, static_cast<int>(desc[kHeight]),
                    static_cast<int>(desc[kWidth]), colour->bits, alphaBits, layout);
        gameapi::LeaveCriticalSection(DeviceLock());
        flags_ |= kTextureLoaded;
        return 1;
    }

    std::uint32_t desc[kDescWords];
    DescribeTexture(desc, format_, width_, height_, kCapsStaging, 0);
    CDXSurface* staging = NewCountedSurface(device, desc);
    DescribeTexture(desc, format_, width_, height_, kCapsTexture, kCaps2Texture);
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    auto* texture = memory ? static_cast<CDXSurface*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc)) : nullptr;
    CDXSurface* previous = surface_;
    surface_ = texture;
    if (texture) ++texture->refCount;
    if (previous) previous->Release();   // 004E6BA0

    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, sizeof(desc));
    desc[kSize] = sizeof(desc);
    long hr = device::Surface(staging)->Lock(nullptr, reinterpret_cast<DDSURFACEDESC2*>(desc), kLockWriteWait,
                                              nullptr);
    staging->result = hr;
    if (hr != 0 && !FatalDirectX(hr, CantLockText())) {
        game::DebugLog(CantLockStagingText());
        staging->Release();
        return 0;
    }
    WritePixels(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[kSurface])),
                static_cast<int>(desc[kPitch]), width, height, INT_MAX, INT_MAX, colour->bits, alphaBits, layout);
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = device::Surface(staging)->Unlock(nullptr);
    staging->result = hr;
    if (hr != 0 && !FatalDirectX(hr, CantUnlockText())) {
        game::DebugLog(CantUnlockStagingText());
        staging->Release();
        return 0;
    }
    gameapi::EnterCriticalSection(DeviceLock());
    device->lastLoadResult_ = device::D3D(device)->Load(device::Surface(surface_), nullptr, device::Surface(staging),
                                                        nullptr, 0);
    gameapi::LeaveCriticalSection(DeviceLock());
    flags_ |= kTextureLoaded;
    staging->Release();
    return 1;
}
EI_REPLACE_METHOD(0x0068FBF0, CTexture::LoadBmpFiles);
