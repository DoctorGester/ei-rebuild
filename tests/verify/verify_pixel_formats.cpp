// Checks for pixel_formats.cpp (A-S11). All side by side: the functions are pure (the original on
// a scratch output first, ours on the real one). Main thread.
//  - Split / ToNative (sampled): every texture the game fills on the CPU and every 8888 .mmp converted to 16
//    bits (texture_blank.cpp, texture_fill.cpp: any zone load); FromNative with them: the escape
//    menu's greyed screen copy and the interface's pixel reads (ui_surface_pixels.cpp: ui_saveload,
//    ui_screens).
//  - FindFormat: new CPU-written and shadow textures (00691D80, 00692010: every new game);
//    ParseMmpHeader: every .mmp texture load (006922A0).
//  - FindDepthFormat: the device's creation (004E6C30, every start).
#include "verify.h"

#include "../../src/game/texture.h"

#include <cstring>

namespace {

// 004EDAA0: PUSH EBX (1), MOV EBX,[ESP+8] (4).
int(__fastcall* s_split)(ChannelLayout*, void*, const void*);
int __fastcall SplitVerify(ChannelLayout* self, void*, const void* format) {
    ChannelLayout expected;
    std::memset(&expected, 0xCC, sizeof(expected));
    int expectedResult = s_split(&expected, nullptr, format);
    int result = self->Split(format);
    bool same = result == expectedResult && std::memcmp(&expected, self, sizeof(expected)) == 0;
    const auto* words = static_cast<const std::uint32_t*>(format);
    ei::verify::Check("ChannelLayout::Split (004EDAA0)", true, same, "result %d/%d masks %08X %08X %08X %08X",
                      expectedResult, result, words ? words[7] : 0, words ? words[4] : 0, words ? words[5] : 0,
                      words ? words[6] : 0);
    return result;
}
EI_VERIFY(0x004EDAA0, SplitVerify, 5, s_split);

// The two conversions run once per pixel when a side-by-side check runs an original that still
// calls them (the greyed screen copy: two calls for each pixel of the screen), so only the first
// calls and then one in 64 are compared.
bool Sample(unsigned& calls) { return calls++ < 4096 || (calls & 63) == 0; }

// 004EDC00: PUSH EBX, PUSH ESI (2), MOV ESI,ECX (2), XOR EDX,EDX (2).
std::uint32_t(__fastcall* s_toNative)(const ChannelLayout*, void*, std::uint32_t);
std::uint32_t __fastcall ToNativeVerify(const ChannelLayout* self, void*, std::uint32_t argb) {
    static unsigned calls;
    if (!Sample(calls)) return self->ToNative(argb);
    std::uint32_t expected = s_toNative(self, nullptr, argb);
    std::uint32_t result = self->ToNative(argb);
    ei::verify::Check("ChannelLayout::ToNative (004EDC00)", expected, result, "%08X bits %u %u %u %u", argb,
                      self->alphaBits, self->redBits, self->greenBits, self->blueBits);
    return result;
}
EI_VERIFY(0x004EDC00, ToNativeVerify, 6, s_toNative);

// 004EDC70: MOV EDX,ECX (2), PUSH EBX, PUSH ESI (2), MOV ESI,[ESP+0xC] (4).
std::uint32_t(__fastcall* s_fromNative)(const ChannelLayout*, void*, std::uint32_t);
std::uint32_t __fastcall FromNativeVerify(const ChannelLayout* self, void*, std::uint32_t pixel) {
    static unsigned calls;
    if (!Sample(calls)) return self->FromNative(pixel);
    std::uint32_t expected = s_fromNative(self, nullptr, pixel);
    std::uint32_t result = self->FromNative(pixel);
    ei::verify::Check("ChannelLayout::FromNative (004EDC70)", expected, result, "%08X bits %u %u %u %u", pixel,
                      self->alphaBits, self->redBits, self->greenBits, self->blueBits);
    return result;
}
EI_VERIFY(0x004EDC70, FromNativeVerify, 8, s_fromNative);

// A lookup that fills `format` only when it finds one: the original into a copy of the caller's
// format, ours into the caller's.
template <typename Original, typename Ours>
int CheckLookup(const char* name, PixelFormat* format, Original original, Ours ours, std::uint32_t key) {
    PixelFormat expected = *format;
    int expectedResult = original(&expected);
    int result = ours(format);
    bool same = result == expectedResult && std::memcmp(&expected, format, sizeof(expected)) == 0;
    ei::verify::Check(name, true, same, "key %X result %d/%d", key, expectedResult, result);
    return result;
}

// 004E7DA0: PUSH ESI (1), MOV ESI,[ECX+0xC0] (6).
int(__fastcall* s_findFormat)(CTextureDevice*, void*, int, PixelFormat*);
int __fastcall FindFormatVerify(CTextureDevice* self, void*, int id, PixelFormat* format) {
    return CheckLookup(
        "CTextureDevice::FindFormat (004E7DA0)", format,
        [&](PixelFormat* out) { return s_findFormat(self, nullptr, id, out); },
        [&](PixelFormat* out) { return self->FindFormat(id, out); }, static_cast<std::uint32_t>(id));
}
EI_VERIFY(0x004E7DA0, FindFormatVerify, 7, s_findFormat);

// 004E7C40: SUB ESP,0x10 (3), PUSH ESI (1), MOV ESI,[ESP+0x18] (4).
int(__fastcall* s_parseMmpHeader)(CTextureDevice*, void*, const void*, PixelFormat*);
int __fastcall ParseMmpHeaderVerify(CTextureDevice* self, void*, const void* data, PixelFormat* format) {
    return CheckLookup(
        "CTextureDevice::ParseMmpHeader (004E7C40)", format,
        [&](PixelFormat* out) { return s_parseMmpHeader(self, nullptr, data, out); },
        [&](PixelFormat* out) { return self->ParseMmpHeader(data, out); },
        static_cast<const MmpHeader*>(data)->format);
}
EI_VERIFY(0x004E7C40, ParseMmpHeaderVerify, 8, s_parseMmpHeader);

// 004E6BC0: PUSH EBX, PUSH EBP, PUSH ESI, PUSH EDI (4), MOV EDI,[ESP+0x14] (4).
int(__fastcall* s_findDepthFormat)(CTextureDevice*, void*, PixelFormat*);
int __fastcall FindDepthFormatVerify(CTextureDevice* self, void*, PixelFormat* format) {
    return CheckLookup(
        "CTextureDevice::FindDepthFormat (004E6BC0)", format,
        [&](PixelFormat* out) { return s_findDepthFormat(self, nullptr, out); },
        [&](PixelFormat* out) { return self->FindDepthFormat(out); }, format->words[1]);
}
EI_VERIFY(0x004E6BC0, FindDepthFormatVerify, 8, s_findDepthFormat);

} // namespace
