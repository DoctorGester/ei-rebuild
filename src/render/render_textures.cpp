// Game textures in the new renderer: a CTexture's pixels are read once from its DirectDraw surface
// (the managed texture the old path loaded, CTexture +0x5C -> +0x10) with its mip levels,
// converted to RGBA8 (any RGB mask layout, or DXT1-5) and kept as a sokol image; levels the surface
// lacks are box-filtered. Later the
// textures will come from the .mmp files directly; reading the surface keeps the first steps small.
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ddraw.h>

#include <cstring>
#include <unordered_map>

namespace render::detail {

namespace {

struct Texture {
    const void* surface = nullptr;   // the IDirectDrawSurface7 it was read from
    sg_image image{};
    sg_view view{};
    DWORD uniqueness = 0;            // the surface's uniqueness value when it was read (0: unknown)
    std::uint32_t checkedFrame = 0xFFFFFFFFu;
    std::uint32_t usedFrame = 0;
    bool keyed = false;              // window surfaces: the colour key it was converted with
    std::uint32_t key = 0;
    int width = 0, height = 0;
};

std::unordered_map<const void*, Texture> g_textures;   // by surface holder: textures with mip levels
std::unordered_map<const void*, Texture> g_windows;    // by surface holder: window surfaces
std::vector<sg_view> g_retiredViews;                   // replaced during the frame: destroyed at Present
std::vector<sg_image> g_retiredImages;
std::unordered_map<std::uint32_t, sg_sampler> g_samplers;
Texture g_white;
sg_sampler g_linear{};
int g_failures = 0;
bool g_uniquenessLogged = false;
constexpr std::uint32_t kUnusedFrames = 600;   // entries unused this long are dropped

// One channel of an RGB mask layout: mask, shift of its lowest bit, width.
struct Channel {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
};
Channel MakeChannel(std::uint32_t mask) {
    Channel c;
    c.mask = mask;
    if (!mask) return c;
    while (!((mask >> c.shift) & 1)) ++c.shift;
    while (c.shift + c.bits < 32 && ((mask >> (c.shift + c.bits)) & 1)) ++c.bits;
    return c;
}
std::uint8_t Expand(std::uint32_t pixel, const Channel& c, std::uint8_t missing) {
    if (!c.bits) return missing;
    std::uint32_t v = (pixel & c.mask) >> c.shift;
    if (c.bits >= 8) return static_cast<std::uint8_t>(v >> (c.bits - 8));
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += c.bits) out |= (v << (8 - c.bits)) >> filled;
    return static_cast<std::uint8_t>(out);
}

// A 5:6:5 colour as RGBA8.
void Colour565(std::uint16_t c, std::uint8_t* out) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    out[3] = 255;
}

// One DXT colour block (8 bytes) into a 4x4 RGBA8 tile. `alphaBlock` (DXT1 only) allows the
// three-colour mode with transparent black.
void ColourBlock(const std::uint8_t* block, std::uint8_t tile[16][4], bool dxt1) {
    std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
    std::uint16_t c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
    std::uint8_t palette[4][4];
    Colour565(c0, palette[0]);
    Colour565(c1, palette[1]);
    for (int ch = 0; ch < 3; ++ch) {
        if (!dxt1 || c0 > c1) {
            palette[2][ch] = static_cast<std::uint8_t>((2 * palette[0][ch] + palette[1][ch]) / 3);
            palette[3][ch] = static_cast<std::uint8_t>((palette[0][ch] + 2 * palette[1][ch]) / 3);
        } else {
            palette[2][ch] = static_cast<std::uint8_t>((palette[0][ch] + palette[1][ch]) / 2);
            palette[3][ch] = 0;
        }
    }
    palette[2][3] = 255;
    palette[3][3] = (!dxt1 || c0 > c1) ? 255 : 0;
    std::uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<std::uint32_t>(block[7]) << 24);
    for (int i = 0; i < 16; ++i) std::memcpy(tile[i], palette[(bits >> (2 * i)) & 3], 4);
}

// DXT1..5 (FourCC) into RGBA8.
bool DecodeDxt(const DDSURFACEDESC2& d, std::uint32_t fourcc, std::vector<std::uint8_t>& out) {
    int kind = fourcc == MAKEFOURCC('D', 'X', 'T', '1') ? 1
             : (fourcc == MAKEFOURCC('D', 'X', 'T', '2') || fourcc == MAKEFOURCC('D', 'X', 'T', '3')) ? 3
             : (fourcc == MAKEFOURCC('D', 'X', 'T', '4') || fourcc == MAKEFOURCC('D', 'X', 'T', '5')) ? 5 : 0;
    if (!kind) return false;
    int width = static_cast<int>(d.dwWidth), height = static_cast<int>(d.dwHeight);
    int blockBytes = kind == 1 ? 8 : 16;
    int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
    // A compressed surface's lock reports either the size of the whole level (DDSD_LINEARSIZE) or
    // the bytes per row of blocks.
    int rowPitch = (d.dwFlags & DDSD_LINEARSIZE) || d.lPitch <= 0 ? blocksX * blockBytes : static_cast<int>(d.lPitch);
    out.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const auto* base = static_cast<const std::uint8_t*>(d.lpSurface);
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* block = base + static_cast<std::size_t>(by) * rowPitch + static_cast<std::size_t>(bx) * blockBytes;
            std::uint8_t tile[16][4];
            ColourBlock(kind == 1 ? block : block + 8, tile, kind == 1);
            if (kind == 3) {
                for (int i = 0; i < 16; ++i) {
                    int a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                    tile[i][3] = static_cast<std::uint8_t>(a * 17);
                }
            } else if (kind == 5) {
                int a0 = block[0], a1 = block[1];
                int alpha[8] = {a0, a1};
                for (int k = 2; k < 8; ++k) {
                    if (a0 > a1) alpha[k] = ((8 - k) * a0 + (k - 1) * a1) / 7;
                    else if (k < 6) alpha[k] = ((6 - k) * a0 + (k - 1) * a1) / 5;
                    else alpha[k] = k == 6 ? 0 : 255;
                }
                std::uint64_t bits = 0;
                for (int k = 0; k < 6; ++k) bits |= static_cast<std::uint64_t>(block[2 + k]) << (8 * k);
                for (int i = 0; i < 16; ++i) tile[i][3] = static_cast<std::uint8_t>(alpha[(bits >> (3 * i)) & 7]);
            }
            for (int i = 0; i < 16; ++i) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < width && y < height) std::memcpy(&out[(static_cast<std::size_t>(y) * width + x) * 4], tile[i], 4);
            }
        }
    }
    return true;
}

// The locked level as RGBA8 rows. With `keyed`, pixels whose colour bits equal `key`'s (a DirectDraw
// source colour key) come out transparent.
bool Convert(const DDSURFACEDESC2& d, std::vector<std::uint8_t>& out, bool keyed = false, std::uint32_t key = 0) {
    const DDPIXELFORMAT& pf = d.ddpfPixelFormat;
    if (pf.dwFlags & DDPF_FOURCC) return !keyed && DecodeDxt(d, pf.dwFourCC, out);
    int bytes = static_cast<int>(pf.dwRGBBitCount / 8);
    if (!(pf.dwFlags & DDPF_RGB) || bytes < 2 || bytes > 4) return false;
    Channel r = MakeChannel(pf.dwRBitMask), g = MakeChannel(pf.dwGBitMask), b = MakeChannel(pf.dwBBitMask);
    Channel a = (pf.dwFlags & DDPF_ALPHAPIXELS) ? MakeChannel(pf.dwRGBAlphaBitMask) : Channel{};
    std::uint32_t colourBits = pf.dwRBitMask | pf.dwGBitMask | pf.dwBBitMask;
    std::uint32_t keyBits = key & colourBits;
    int width = static_cast<int>(d.dwWidth), height = static_cast<int>(d.dwHeight);
    out.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(d.lpSurface) + static_cast<std::size_t>(y) * d.lPitch;
        std::uint8_t* o = &out[static_cast<std::size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, row + x * bytes, static_cast<std::size_t>(bytes));
            o[x * 4 + 0] = Expand(pixel, r, 0);
            o[x * 4 + 1] = Expand(pixel, g, 0);
            o[x * 4 + 2] = Expand(pixel, b, 0);
            o[x * 4 + 3] = keyed ? ((pixel & colourBits) == keyBits ? 0 : 255) : Expand(pixel, a, 255);
        }
    }
    return true;
}

// An RGBA8 image from `levels` (level 0 first, as read from the game's surface); levels the surface
// did not have are box-filtered from the last one, down to 1x1.
sg_image MakeImage(int width, int height, std::vector<std::vector<std::uint8_t>> levels, const char* label) {
    int w = width, h = height;
    for (std::size_t i = 1; i < levels.size(); ++i) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    while ((w > 1 || h > 1) && levels.size() < SG_MAX_MIPMAPS) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        const std::vector<std::uint8_t>& src = levels.back();
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
                for (int c = 0; c < 4; ++c) {
                    int sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                              src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                    dst[(static_cast<std::size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    sg_image_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.num_mipmaps = static_cast<int>(levels.size());
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    for (std::size_t i = 0; i < levels.size(); ++i) desc.data.mip_levels[i] = sg_range{levels[i].data(), levels[i].size()};
    desc.label = label;
    return sg_make_image(&desc);
}

sg_view MakeTextureView(sg_image image) {
    sg_view_desc v{};
    v.texture.image = image;
    return sg_make_view(&v);
}

const Texture& White() {
    if (!g_white.image.id) {
        g_white.image = MakeImage(1, 1, {std::vector<std::uint8_t>(4, 255)}, "ei-white");
        g_white.view = MakeTextureView(g_white.image);
    }
    return g_white;
}

// Reads one level of a surface into RGBA8. `hr` gets the Lock result.
bool ReadLevel(IDirectDrawSurface7* dd, int* width, int* height, std::vector<std::uint8_t>& pixels, DDSURFACEDESC2& desc,
               HRESULT& hr) {
    desc = DDSURFACEDESC2{};
    desc.dwSize = sizeof(desc);
    hr = dd->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    bool ok = SUCCEEDED(hr) && desc.lpSurface && desc.dwWidth && desc.dwHeight;
    if (ok) {
        ok = Convert(desc, pixels);
        *width = static_cast<int>(desc.dwWidth);
        *height = static_cast<int>(desc.dwHeight);
    }
    if (SUCCEEDED(hr)) dd->Unlock(nullptr);
    return ok;
}

// Reads `surface` (an IDirectDrawSurface7) and the mip levels attached to it (the game loads the
// .mmp file's own mipmaps) into RGBA8 levels.
bool ReadSurface(void* surface, int* width, int* height, std::vector<std::vector<std::uint8_t>>& levels) {
    auto* dd = static_cast<IDirectDrawSurface7*>(surface);
    DDSURFACEDESC2 desc{};
    HRESULT hr = S_OK;
    std::vector<std::uint8_t> pixels;
    LockGameDevice();
    bool ok = ReadLevel(dd, width, height, pixels, desc, hr);
    if (ok) {
        levels.push_back(std::move(pixels));
        int w = *width, h = *height;
        IDirectDrawSurface7* level = dd;
        level->AddRef();
        while ((w > 1 || h > 1) && levels.size() < SG_MAX_MIPMAPS) {
            DDSCAPS2 caps{};
            caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
            IDirectDrawSurface7* next = nullptr;
            if (FAILED(level->GetAttachedSurface(&caps, &next)) || !next) break;
            level->Release();
            level = next;
            int lw = 0, lh = 0;
            DDSURFACEDESC2 ld{};
            HRESULT lhr = S_OK;
            std::vector<std::uint8_t> lp;
            int ew = w > 1 ? w / 2 : 1, eh = h > 1 ? h / 2 : 1;
            if (!ReadLevel(level, &lw, &lh, lp, ld, lhr) || lw != ew || lh != eh) break;
            levels.push_back(std::move(lp));
            w = ew;
            h = eh;
        }
        level->Release();
    }
    UnlockGameDevice();
    if (!ok && g_failures++ < 10)
        ei::Log("render: cannot read texture surface %p (Lock %08lX, format flags %08lX, FourCC %08lX, %lu bits)", surface,
                static_cast<unsigned long>(hr), static_cast<unsigned long>(desc.ddpfPixelFormat.dwFlags),
                static_cast<unsigned long>(desc.ddpfPixelFormat.dwFourCC),
                static_cast<unsigned long>(desc.ddpfPixelFormat.dwRGBBitCount));
    return ok;
}

// Re-reads are needed when the surface's contents changed since `t` was read: its uniqueness value
// (IDirectDrawSurface7::GetUniquenessValue, changed by every Lock / GetDC / Blt) differs. When the
// value is not available, `unknownChanged` decides.
bool Changed(const Texture& t, IDirectDrawSurface7* dd, DWORD* now, bool unknownChanged) {
    DWORD value = 0;
    LockGameDevice();
    HRESULT hr = dd->GetUniquenessValue(&value);
    UnlockGameDevice();
    if (!g_uniquenessLogged) {
        g_uniquenessLogged = true;
        ei::Log("render: surface uniqueness values %s (%08lX, %lu)", SUCCEEDED(hr) && value ? "available" : "not available",
                static_cast<unsigned long>(hr), static_cast<unsigned long>(value));
    }
    *now = SUCCEEDED(hr) ? value : 0;
    if (*now == 0) return unknownChanged;
    return t.uniqueness != *now;
}

void Retire(Texture& t) {
    if (t.image.id) {
        g_retiredViews.push_back(t.view);
        g_retiredImages.push_back(t.image);
    }
    t.image = sg_image{};
    t.view = sg_view{};
}

// Reads a texture holder's surface (with mip levels) into `t`.
void ReadTexture(Texture& t, void* surface) {
    int width = 0, height = 0;
    std::vector<std::vector<std::uint8_t>> levels;
    t.surface = surface;
    if (ReadSurface(surface, &width, &height, levels)) {
        t.image = MakeImage(width, height, std::move(levels), "ei-game-texture");
        t.view = MakeTextureView(t.image);
    }
    if (!t.view.id || sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        if (t.view.id) sg_destroy_view(t.view);
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
        t.view = sg_view{};
    }
}

// Reads a window surface (one level; keyed) into `t`.
void ReadWindow(Texture& t, void* surface, bool keyed, std::uint32_t key) {
    auto* dd = static_cast<IDirectDrawSurface7*>(surface);
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    std::vector<std::uint8_t> pixels;
    LockGameDevice();
    HRESULT hr = dd->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    bool ok = SUCCEEDED(hr) && desc.lpSurface && desc.dwWidth && desc.dwHeight && Convert(desc, pixels, keyed, key);
    if (SUCCEEDED(hr)) dd->Unlock(nullptr);
    UnlockGameDevice();
    t.surface = surface;
    t.keyed = keyed;
    t.key = key;
    if (!ok) {
        if (g_failures++ < 10)
            ei::Log("render: cannot read window surface %p (Lock %08lX, %lu bits)", surface, static_cast<unsigned long>(hr),
                    static_cast<unsigned long>(desc.ddpfPixelFormat.dwRGBBitCount));
        return;
    }
    t.width = static_cast<int>(desc.dwWidth);
    t.height = static_cast<int>(desc.dwHeight);
    sg_image_desc d{};
    d.width = t.width;
    d.height = t.height;
    d.num_mipmaps = 1;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = sg_range{pixels.data(), pixels.size()};
    d.label = "ei-window-surface";
    t.image = sg_make_image(&d);
    t.view = MakeTextureView(t.image);
    if (sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(t.view);
        sg_destroy_image(t.image);
        t.image = sg_image{};
        t.view = sg_view{};
    }
}

void* SurfaceOf(const void* holder) {
    // A CDXSurface: +0x10 its IDirectDrawSurface7.
    return holder ? *reinterpret_cast<void* const*>(static_cast<const char*>(holder) + 0x10) : nullptr;
}

} // namespace

sg_view SurfaceView(const void* holder) {
    void* surface = SurfaceOf(holder);
    if (!surface) return White().view;
    std::uint32_t frame = FrameNumber();
    Texture& t = g_textures[holder];
    t.usedFrame = frame;
    if (t.surface == surface && t.checkedFrame == frame) return t.view.id ? t.view : White().view;
    // Textures are checked once a frame; without uniqueness values they are read once.
    DWORD now = 0;
    bool changed = Changed(t, static_cast<IDirectDrawSurface7*>(surface), &now, false);
    bool reread = t.surface != surface || changed;
    t.checkedFrame = frame;
    if (reread) {
        Retire(t);
        ReadTexture(t, surface);
        t.uniqueness = now;
    }
    return t.view.id ? t.view : White().view;
}

void ReadTextureFrom(const void* holder, const void* source) {
    void* surface = SurfaceOf(holder);
    void* from = SurfaceOf(source);
    if (!surface || !from) return;
    Texture& t = g_textures[holder];
    t.usedFrame = FrameNumber();
    Retire(t);
    ReadTexture(t, from);
    // From now on the entry stands for the holder's surface as it is now.
    t.surface = surface;
    t.checkedFrame = FrameNumber();
    DWORD now = 0;
    Changed(t, static_cast<IDirectDrawSurface7*>(surface), &now, false);
    t.uniqueness = now;
}

sg_view TextureView(const void* texture) {
    // CTexture +0x5C: the counted surface holder (CDXSurface).
    if (!texture) return White().view;
    return SurfaceView(*reinterpret_cast<const void* const*>(static_cast<const char*>(texture) + 0x5C));
}

sg_view WindowSurfaceView(const void* holder, bool keyed, std::uint32_t key, int* width, int* height) {
    *width = *height = 0;
    void* surface = SurfaceOf(holder);
    if (!surface) return sg_view{};
    std::uint32_t frame = FrameNumber();
    Texture& t = g_windows[holder];
    t.usedFrame = frame;
    // Window surfaces are checked at every blit (the game may redraw one between two blits of a
    // frame); without uniqueness values they are read once a frame.
    DWORD now = 0;
    bool changed = Changed(t, static_cast<IDirectDrawSurface7*>(surface), &now, t.checkedFrame != frame);
    bool reread = t.surface != surface || !t.view.id || t.keyed != keyed || (keyed && t.key != key) || changed;
    t.checkedFrame = frame;
    if (reread) {
        Retire(t);
        ReadWindow(t, surface, keyed, key);
        t.uniqueness = now;
    }
    *width = t.width;
    *height = t.height;
    return t.view;
}

sg_sampler LinearSampler() {
    if (!g_linear.id) {
        sg_sampler_desc d{};
        d.min_filter = SG_FILTER_LINEAR;
        d.mag_filter = SG_FILTER_LINEAR;
        d.mipmap_filter = SG_FILTER_NEAREST;   // the game's D3DTFP_POINT
        d.wrap_u = SG_WRAP_REPEAT;
        d.wrap_v = SG_WRAP_REPEAT;
        d.label = "ei-linear";
        g_linear = sg_make_sampler(&d);
    }
    return g_linear;
}

namespace {
sg_wrap Wrap(std::uint8_t address) {
    switch (address) {   // D3DTEXTUREADDRESS
    case 2: return SG_WRAP_MIRRORED_REPEAT;
    case 3: return SG_WRAP_CLAMP_TO_EDGE;
    case 4: return SG_WRAP_CLAMP_TO_BORDER;
    default: return SG_WRAP_REPEAT;
    }
}
} // namespace

sg_sampler StageSampler(const StageState& s) {
    std::uint32_t key = static_cast<std::uint32_t>(s.magFilter & 7) | (static_cast<std::uint32_t>(s.minFilter & 7) << 3) |
                        (static_cast<std::uint32_t>(s.mipFilter & 7) << 6) | (static_cast<std::uint32_t>(s.addressU & 7) << 9) |
                        (static_cast<std::uint32_t>(s.addressV & 7) << 12);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    sg_sampler_desc d{};
    d.mag_filter = s.magFilter == 1 ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;   // D3DTFG_POINT
    d.min_filter = s.minFilter == 1 ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;   // D3DTFN_POINT
    d.mipmap_filter = s.mipFilter == 3 ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    if (s.mipFilter <= 1) d.max_lod = 0.0f;   // D3DTFP_NONE: level 0 only
    d.wrap_u = Wrap(s.addressU);
    d.wrap_v = Wrap(s.addressV);
    d.border_color = SG_BORDERCOLOR_TRANSPARENT_BLACK;
    d.label = "ei-stage-sampler";
    sg_sampler sampler = sg_make_sampler(&d);
    g_samplers.emplace(key, sampler);
    return sampler;
}

void RetireTextures() {
    for (sg_view v : g_retiredViews) sg_destroy_view(v);
    for (sg_image i : g_retiredImages) sg_destroy_image(i);
    g_retiredViews.clear();
    g_retiredImages.clear();
    std::uint32_t frame = FrameNumber();
    for (auto* map : {&g_textures, &g_windows}) {
        for (auto it = map->begin(); it != map->end();) {
            if (frame - it->second.usedFrame > kUnusedFrames) {
                if (it->second.image.id) {
                    sg_destroy_view(it->second.view);
                    sg_destroy_image(it->second.image);
                }
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}

void DestroyTextures() {
    RetireTextures();
    for (auto* map : {&g_textures, &g_windows}) {
        for (auto& entry : *map) {
            if (entry.second.image.id) {
                sg_destroy_view(entry.second.view);
                sg_destroy_image(entry.second.image);
            }
        }
        map->clear();
    }
    if (g_white.image.id) {
        sg_destroy_view(g_white.view);
        sg_destroy_image(g_white.image);
    }
    g_white = Texture{};
    if (g_linear.id) sg_destroy_sampler(g_linear);
    g_linear = sg_sampler{};
    for (auto& s : g_samplers) sg_destroy_sampler(s.second);
    g_samplers.clear();
}

} // namespace render::detail
