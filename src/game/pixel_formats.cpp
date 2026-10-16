// Pixel format helpers of the renderer's device object (A-S11): a DDPIXELFORMAT split into
// channels and the pixel conversions through it (004EDAA0 / 004EDC00 / 004EDC70), and the device's
// format lookups (004E7C40 / 004E7DA0 its texture formats). Pure: none of them touches DirectDraw;
// the list they read is filled once at device creation (004E6EE0). (The depth format lookup
// 004E6BC0 went with its only caller, the depth buffer 004E6C30, in R11b: config/dead_code.csv.)
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// DDPIXELFORMAT, the fields read here (as dwords: PixelFormat::words).
constexpr int kFormatFlags = 1;          // +0x04 dwFlags
constexpr int kFormatRed = 4;            // +0x10 dwRBitMask
constexpr int kFormatGreen = 5;          // +0x14
constexpr int kFormatBlue = 6;           // +0x18
constexpr int kFormatAlpha = 7;          // +0x1C
constexpr std::uint32_t kFormatRgb = 0x40;          // DDPF_RGB

constexpr std::uint32_t kMmpDxtMask = 0x00FFFFFF;
constexpr std::uint32_t kMmpDxt = 'D' | ('X' << 8) | ('T' << 16);   // "DXT" (DXT1, DXT3, ...)

// One channel mask: the shift of its lowest set bit and the length of the run of set bits from
// there (a zero mask: 0 and 0). False when the run is longer than 8 bits.
bool SplitMask(std::uint32_t mask, std::uint32_t* bits, std::uint32_t* shift) {
    *bits = 0;
    *shift = 0;
    if (mask == 0) return true;
    std::uint32_t bit = 1;
    while (!(mask & bit)) {
        bit <<= 1;
        ++*shift;
    }
    while (mask & bit) {
        bit <<= 1;
        ++*bits;
    }
    return *bits <= 8;
}

// SHR / SHL by CL: the count is taken modulo 32.
std::uint32_t Shr(std::uint32_t value, std::uint32_t count) { return value >> (count & 31); }
std::uint32_t Shl(std::uint32_t value, std::uint32_t count) { return value << (count & 31); }

} // namespace

int ChannelLayout::Split(const void* format) {
    std::memset(this, 0, sizeof(*this));
    if (!format) return 0;
    const auto* words = static_cast<const std::uint32_t*>(format);
    if (!(words[kFormatFlags] & kFormatRgb)) {
        game::DebugLog("Format have no RGB information.\n");
        return 0;
    }
    std::uint32_t bits, shift;
    // Each channel is stored only when it passes, so a failure leaves it and the later ones zero.
    if (!SplitMask(words[kFormatAlpha], &bits, &shift)) return 0;
    alphaMask = words[kFormatAlpha];
    alphaBits = bits;
    alphaShift = shift;
    if (!SplitMask(words[kFormatRed], &bits, &shift)) return 0;
    redMask = words[kFormatRed];
    redBits = bits;
    redShift = shift;
    if (!SplitMask(words[kFormatGreen], &bits, &shift)) return 0;
    greenMask = words[kFormatGreen];
    greenBits = bits;
    greenShift = shift;
    if (!SplitMask(words[kFormatBlue], &bits, &shift)) return 0;
    blueMask = words[kFormatBlue];
    blueBits = bits;
    blueShift = shift;
    return 1;
}
EI_REPLACE_METHOD(0x004EDAA0, ChannelLayout::Split);

std::uint32_t ChannelLayout::ToNative(std::uint32_t argb) const {
    auto channel = [](std::uint32_t value, std::uint32_t bits, std::uint32_t shift) {
        return Shl(Shr(value, 8 - bits), shift);
    };
    return channel(argb >> 24, alphaBits, alphaShift) | channel((argb >> 8) & 0xFF, greenBits, greenShift) |
           channel((argb >> 16) & 0xFF, redBits, redShift) | channel(argb & 0xFF, blueBits, blueShift);
}
EI_REPLACE_METHOD(0x004EDC00, ChannelLayout::ToNative);

std::uint32_t ChannelLayout::FromNative(std::uint32_t pixel) const {
    auto channel = [pixel](std::uint32_t mask, std::uint32_t bits, std::uint32_t shift) {
        return Shl(Shr(pixel & mask, shift), 8 - bits);
    };
    // Each step shifts everything so far up a byte and ORs the next channel in (a channel wider
    // than 8 bits would spill into its neighbour; Split refuses those).
    std::uint32_t argb = channel(alphaMask, alphaBits, alphaShift);
    argb = (argb << 8) | channel(redMask, redBits, redShift);
    argb = (argb << 8) | channel(greenMask, greenBits, greenShift);
    argb = (argb << 8) | channel(blueMask, blueBits, blueShift);
    return argb;
}
EI_REPLACE_METHOD(0x004EDC70, ChannelLayout::FromNative);

int CTextureDevice::FindFormat(int id, PixelFormat* format) {
    auto* node = formats_.Find(id);
    if (!node) return 0;
    *format = *node->value;
    return 1;
}
EI_REPLACE_METHOD(0x004E7DA0, CTextureDevice::FindFormat);

int CTextureDevice::ParseMmpHeader(const void* data, PixelFormat* format) {
    const auto* header = static_cast<const MmpHeader*>(data);
    if (auto* node = formats_.Find(static_cast<int>(header->format))) {
        *format = *node->value;
        return 1;
    }
    if ((header->format & kMmpDxtMask) != kMmpDxt) return 0;
    // A compressed file the device has no format for: the 16 or 32-bit format its channel counts
    // name (the loader decodes into it), keyed by red, green, blue and alpha bits.
    int id = 0;
    std::uint32_t red = header->redBits, green = header->greenBits, blue = header->blueBits, alpha = header->alphaKind;
    if (red == 5 && green == 6 && blue == 5 && alpha == 0) id = 0x5650;
    else if (red == 5 && green == 5 && blue == 5 && alpha == 0) id = 0x5550;
    else if (red == 5 && green == 5 && blue == 5 && alpha == 1) id = 0x5551;
    else if (red == 4 && green == 4 && blue == 4 && alpha == 4) id = 0x4444;
    else if (red == 8 && green == 8 && blue == 8 && alpha == 8) id = 0x8888;
    auto* node = formats_.Find(id);
    if (!node) return 0;
    *format = *node->value;
    return 1;
}
EI_REPLACE_METHOD(0x004E7C40, CTextureDevice::ParseMmpHeader);


