// Checks for filling texture surfaces from .mmp data and the .bmp loader (texture_fill.cpp).
//
// The three fills run side by side: the original fills the surface's levels, they are read back and
// overwritten with a pattern, ours fills them again and they are read back and compared, with the
// results. Each fill keeps a reference on every attached level it moves to (the original's leak);
// the read-back of ours' run drops the extra one. Every scenario loads .mmp textures with DirectDraw
// textures (frames_basic, render_scene, items); not in the switch-over with "cpuwindows", where
// CreateFromMmp has no staging surface.
#include "verify.h"

#include "../../src/game/texture.h"

#include <windows.h>
#include <ddraw.h>

#include <cstring>
#include <vector>

namespace {

using LevelBytes = std::uint32_t (*)(const MmpHeader*, std::uint32_t width, std::uint32_t height);

std::uint32_t FileLevelBytes(const MmpHeader* header, std::uint32_t width, std::uint32_t height) {
    return (header->bitsPerPixel * height * width) >> 3;
}
std::uint32_t SixteenBitLevelBytes(const MmpHeader*, std::uint32_t width, std::uint32_t height) {
    return height * width * 2;
}

// Walks the levels a fill writes (from `mip` while the file has levels, as long as the chain has
// attached levels) and appends `bytes` of each to `out`; `pattern` then overwrites them. Each
// attached level loses `extraReleases` references besides the walk's own. Returns the levels read.
int ReadLevels(CDXSurface* holder, const MmpHeader* header, int mip, LevelBytes bytes, std::vector<std::uint8_t>* out,
               bool pattern, int extraReleases) {
    auto* root = static_cast<IDirectDrawSurface7*>(holder->surface);
    IDirectDrawSurface7* surface = root;
    std::uint32_t width = header->width >> (mip & 31);
    std::uint32_t height = header->height >> (mip & 31);
    int levels = 0;
    for (int level = mip; level < static_cast<int>(header->mipCount); ++level) {
        DDSURFACEDESC2 desc;
        std::memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK) {
            std::uint32_t n = bytes(header, width, height);
            const auto* memory = static_cast<std::uint8_t*>(desc.lpSurface);
            out->insert(out->end(), memory, memory + n);
            if (pattern) std::memset(desc.lpSurface, 0xA5, n);
            surface->Unlock(nullptr);
        }
        ++levels;
        DDSCAPS2 caps;
        std::memset(&caps, 0, sizeof(caps));
        caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
        IDirectDrawSurface7* next = nullptr;
        if (surface->GetAttachedSurface(&caps, &next) != DD_OK) break;
        if (surface != root) surface->Release();
        for (int i = 0; i < extraReleases; ++i) next->Release();
        surface = next;
        width >>= 1;
        height >>= 1;
    }
    if (surface != root) surface->Release();
    return levels;
}

// The levels in `holder`'s chain (the root and its attached mip levels).
int ChainLevels(CDXSurface* holder) {
    auto* root = static_cast<IDirectDrawSurface7*>(holder->surface);
    IDirectDrawSurface7* surface = root;
    int levels = 1;
    for (;;) {
        DDSCAPS2 caps;
        std::memset(&caps, 0, sizeof(caps));
        caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
        IDirectDrawSurface7* next = nullptr;
        if (surface->GetAttachedSurface(&caps, &next) != DD_OK) break;
        if (surface != root) surface->Release();
        surface = next;
        ++levels;
    }
    if (surface != root) surface->Release();
    return levels;
}

using Fill = int(__fastcall*)(CTexture*, void*, CDXSurface*, const MmpHeader*, int);

template <typename Ours>
int CompareFill(const char* name, Fill original, Ours ours, CTexture* self, CDXSurface* surface,
                const MmpHeader* header, int mip, LevelBytes bytes) {
    int expected = original(self, nullptr, surface, header, mip);
    std::vector<std::uint8_t> before, after;
    ReadLevels(surface, header, mip, bytes, &before, true, 0);
    int actual = ours();
    ReadLevels(surface, header, mip, bytes, &after, false, 1);
    std::size_t first = 0;
    while (first < before.size() && first < after.size() && before[first] == after[first]) ++first;
    bool same = before.size() == after.size() && first == before.size();
    ei::verify::Check(name, true, same, "format %08X %ux%u mip %d -> %u bits: %zu bytes, first difference at %zu",
                      header->format, header->width, header->height, mip, self->format_.words[3], before.size(),
                      first);
    ei::verify::Check(name, expected, actual, "result, format %08X %ux%u mip %d", header->format, header->width,
                      header->height, mip);
    return expected;
}

} // namespace

// 00690970: SUB ESP,0xA0 (6 bytes).
static Fill s_copyToSurface;
static int __fastcall CopyToSurfaceVerify(CTexture* self, void*, CDXSurface* surface, const MmpHeader* header,
                                          int mip) {
    return CompareFill("CTexture::CopyToSurface", s_copyToSurface,
                       [&] { return self->CopyToSurface(surface, header, mip); }, self, surface, header, mip,
                       FileLevelBytes);
}
EI_VERIFY(0x00690970, CopyToSurfaceVerify, 6, s_copyToSurface);

// 00690B30: SUB ESP,0xDC (6 bytes). 8:8:8:8 files on a device without that texture format.
static Fill s_convertToSurface;
static int __fastcall ConvertToSurfaceVerify(CTexture* self, void*, CDXSurface* surface, const MmpHeader* header,
                                             int mip) {
    return CompareFill("CTexture::ConvertToSurface", s_convertToSurface,
                       [&] { return self->ConvertToSurface(surface, header, mip); }, self, surface, header, mip,
                       SixteenBitLevelBytes);
}
EI_VERIFY(0x00690B30, ConvertToSurfaceVerify, 6, s_convertToSurface);

// 00690D30: SUB ESP,0xA0 (6 bytes). DXT files the device cannot take as they are.
static int(__fastcall* s_unpackToSurface)(CTexture*, void*, CDXSurface*, const MmpHeader*, int, int);
static int __fastcall UnpackToSurfaceVerify(CTexture* self, void*, CDXSurface* surface, const MmpHeader* header,
                                            int mip, int kind) {
    int expected = s_unpackToSurface(self, nullptr, surface, header, mip, kind);
    std::vector<std::uint8_t> before, after;
    ReadLevels(surface, header, mip, SixteenBitLevelBytes, &before, true, 0);
    int actual = self->UnpackToSurface(surface, header, mip, kind);
    ReadLevels(surface, header, mip, SixteenBitLevelBytes, &after, false, 1);
    ei::verify::Check("CTexture::UnpackToSurface", true, before == after, "format %08X %ux%u mip %d mode %d",
                      header->format, header->width, header->height, mip, kind);
    ei::verify::Check("CTexture::UnpackToSurface", expected, actual, "result, format %08X mode %d", header->format,
                      kind);
    return expected;
}
EI_VERIFY(0x00690D30, UnpackToSurfaceVerify, 6, s_unpackToSurface);

// 0068FA10 only picks the fill (whose wrappers compare them): ours only. The result is the bytes
// MmpHeader::FilledBytes gives for the surface's chain.
static void* s_fillSurface;
static int __fastcall FillSurfaceVerify(CTexture* self, void*, CDXSurface* surface, const void* data, int mip) {
    int result = self->FillSurface(surface, data, mip);
    const auto* header = static_cast<const MmpHeader*>(data);
    unsigned levels = static_cast<unsigned>(ChainLevels(surface));
    ei::verify::Check("CTexture::FillSurface", header->FilledBytes(self->format_, static_cast<unsigned>(mip), levels),
                      static_cast<std::uint32_t>(result), "format %08X -> %08X / %u bits, mip %d, %u levels",
                      header->format, self->format_.words[2], self->format_.words[3], mip, levels);
    return result;
}
EI_VERIFY(0x0068FA10, FillSurfaceVerify, 0, s_fillSurface);

// 00690260 reads files (LoadBmpFiles): ours only. A texture already loaded under `path` stays as it
// was (1); otherwise 1 means loaded, 0 not. No scenario has a loose textures\<name>.bmp: not reached.
static void* s_loadBmp;
static int __fastcall LoadBmpVerify(CTexture* self, void*, CTextureDevice* device, const char* path) {
    bool wasLoaded = (self->flags_ & kTextureLoaded) != 0;
    int result = self->LoadBmp(device, path);
    bool loaded = (self->flags_ & kTextureLoaded) != 0;
    bool ok = wasLoaded ? loaded && (result == 0 || result == 1) : (result == 1) == loaded && result <= 1 && result >= 0;
    ei::verify::Check("CTexture::LoadBmp", true, ok, "\"%s\" result %d", path, result);
    return result;
}
EI_VERIFY(0x00690260, LoadBmpVerify, 0, s_loadBmp);
