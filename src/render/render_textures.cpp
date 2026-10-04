// Game textures in the new renderer: a CTexture's pixels are read once from its DirectDraw surface
// (the managed texture the old path loaded, CTexture +0x5C -> +0x10), converted to RGBA8 (any RGB
// mask layout, or DXT1-5), given a box-filtered mip chain and kept as a sokol image. Later the
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
};

std::unordered_map<const void*, Texture> g_textures;
Texture g_white;
sg_sampler g_linear{};
int g_failures = 0;

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

// The locked level as RGBA8 rows.
bool Convert(const DDSURFACEDESC2& d, std::vector<std::uint8_t>& out) {
    const DDPIXELFORMAT& pf = d.ddpfPixelFormat;
    if (pf.dwFlags & DDPF_FOURCC) return DecodeDxt(d, pf.dwFourCC, out);
    int bytes = static_cast<int>(pf.dwRGBBitCount / 8);
    if (!(pf.dwFlags & DDPF_RGB) || bytes < 2 || bytes > 4) return false;
    Channel r = MakeChannel(pf.dwRBitMask), g = MakeChannel(pf.dwGBitMask), b = MakeChannel(pf.dwBBitMask);
    Channel a = (pf.dwFlags & DDPF_ALPHAPIXELS) ? MakeChannel(pf.dwRGBAlphaBitMask) : Channel{};
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
            o[x * 4 + 3] = Expand(pixel, a, 255);
        }
    }
    return true;
}

sg_image MakeImage(int width, int height, const std::vector<std::uint8_t>& level0, const char* label) {
    // Box-filtered mip chain down to 1x1.
    std::vector<std::vector<std::uint8_t>> levels;
    levels.push_back(level0);
    int w = width, h = height;
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
        std::vector<std::uint8_t> pixel(4, 255);
        g_white.image = MakeImage(1, 1, pixel, "ei-white");
        g_white.view = MakeTextureView(g_white.image);
    }
    return g_white;
}

// Reads `surface` (an IDirectDrawSurface7) into RGBA8.
bool ReadSurface(void* surface, int* width, int* height, std::vector<std::uint8_t>& pixels) {
    auto* dd = static_cast<IDirectDrawSurface7*>(surface);
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    LockGameDevice();
    HRESULT hr = dd->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    bool ok = SUCCEEDED(hr) && desc.lpSurface && desc.dwWidth && desc.dwHeight;
    if (ok) {
        ok = Convert(desc, pixels);
        *width = static_cast<int>(desc.dwWidth);
        *height = static_cast<int>(desc.dwHeight);
    }
    if (SUCCEEDED(hr)) dd->Unlock(nullptr);
    UnlockGameDevice();
    if (!ok && g_failures++ < 10)
        ei::Log("render: cannot read texture surface %p (Lock %08lX, format flags %08lX, FourCC %08lX, %lu bits)", surface,
                static_cast<unsigned long>(hr), static_cast<unsigned long>(desc.ddpfPixelFormat.dwFlags),
                static_cast<unsigned long>(desc.ddpfPixelFormat.dwFourCC),
                static_cast<unsigned long>(desc.ddpfPixelFormat.dwRGBBitCount));
    return ok;
}

} // namespace

sg_view TextureView(const void* texture) {
    if (!texture) return White().view;
    // CTexture +0x5C: the counted surface holder (CDXSurface), +0x10 its IDirectDrawSurface7.
    const char* holder = *reinterpret_cast<const char* const*>(static_cast<const char*>(texture) + 0x5C);
    void* surface = holder ? *reinterpret_cast<void* const*>(holder + 0x10) : nullptr;
    if (!surface) return White().view;
    auto it = g_textures.find(texture);
    if (it != g_textures.end()) {
        if (it->second.surface == surface) return it->second.view;
        sg_destroy_view(it->second.view);
        sg_destroy_image(it->second.image);
        g_textures.erase(it);
    }
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    Texture t;
    t.surface = surface;
    if (ReadSurface(surface, &width, &height, pixels)) {
        t.image = MakeImage(width, height, pixels, "ei-game-texture");
        t.view = MakeTextureView(t.image);
    }
    if (!t.view.id || sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        t.image = sg_image{};
        t.view = White().view;
    }
    return g_textures.emplace(texture, t).first->second.view;
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

void DestroyTextures() {
    for (auto& entry : g_textures) {
        if (entry.second.image.id) {
            sg_destroy_view(entry.second.view);
            sg_destroy_image(entry.second.image);
        }
    }
    g_textures.clear();
    if (g_white.image.id) {
        sg_destroy_view(g_white.view);
        sg_destroy_image(g_white.image);
    }
    g_white = Texture{};
    if (g_linear.id) sg_destroy_sampler(g_linear);
    g_linear = sg_sampler{};
}

} // namespace render::detail
