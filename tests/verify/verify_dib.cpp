// Verify wrappers for CDib (dib.cpp). A screenshot (Scroll Lock: scenario camera) makes a bitmap
// with the sized constructor (Create, Empty), allocates its bits and deletes it. The empty
// constructor, Size and MakePalette serve the loose .bmp texture overrides, and the runtime class's
// slots MFC serialisation: no scenario reaches those.
#include "verify.h"

#include "../../src/game/dib.h"
#include "../../src/lib/crt.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>

namespace {

const void* DibVtable() { return reinterpret_cast<const void*>(0x00744288); }

// Every field but the vtable is zero (what Empty leaves).
bool IsEmpty(const CDib* dib) {
    CDib zero;
    std::memset(&zero, 0, sizeof(zero));
    zero.vtable = dib->vtable;
    return std::memcmp(dib, &zero, sizeof(zero)) == 0;
}

// The fields of two bitmaps made by Create: the same but for the header's address, the headers
// compared by content (with the masks).
bool SameCreated(const CDib& a, const CDib& b) {
    if (!a.header || !b.header) return false;
    CDib left = a, right = b;
    left.header = right.header = nullptr;
    left.colours = right.colours = nullptr;
    if (std::memcmp(&left, &right, sizeof(CDib)) != 0) return false;
    if (reinterpret_cast<const std::uint8_t*>(a.colours) - reinterpret_cast<const std::uint8_t*>(a.header) !=
        reinterpret_cast<const std::uint8_t*>(b.colours) - reinterpret_cast<const std::uint8_t*>(b.header))
        return false;
    return std::memcmp(a.header, b.header, sizeof(BITMAPINFOHEADER) + a.maskBytes) == 0;
}

// 006D4AA0: PUSH -1; PUSH imm32 (7 bytes). The original on a scratch object, ours on the caller's;
// the 0x3C bytes compared (every field is written).
CDib*(__fastcall* s_construct)(CDib*);

CDib* __fastcall ConstructVerify(CDib* self) {
    CDib scratch;
    std::memset(&scratch, 0xCC, sizeof(scratch));
    s_construct(&scratch);
    CDib* result = self->Construct();
    ei::verify::Check("CDib::Construct", true, result == self && std::memcmp(&scratch, self, sizeof(CDib)) == 0,
                      "the empty bitmaps differ");
    return result;
}
EI_VERIFY(0x006D4AA0, ConstructVerify, 7, s_construct);

// 006D49E0: PUSH -1; PUSH imm32 (7 bytes). Both make an object; compared, the original's freed.
CDib*(__cdecl* s_createObject)();

CDib* __cdecl CreateObjectVerify() {
    // Ours first, so that its object lands where the original's would without the check (SV2f).
    auto* actual = static_cast<CDib*>(gamecrt::operator_new(sizeof(CDib)));
    if (actual) actual->Construct();
    CDib* expected = s_createObject();
    ei::verify::Check("CDib CreateObject", true,
                      expected && actual && std::memcmp(expected, actual, sizeof(CDib)) == 0,
                      "the new bitmaps differ");
    gamecrt::operator_delete(expected);
    return actual;
}
EI_VERIFY(0x006D49E0, CreateObjectVerify, 7, s_createObject);

// 006D4BA0: PUSH EBX; PUSH ESI; MOV ESI,ECX; XOR EBX,EBX (6 bytes). The original, its bitmap copied
// and its header freed (Create drops what the object held without freeing it), ours; compared.
void(__fastcall* s_create)(CDib*, void*, int, int, int, std::uint32_t, std::uint32_t, std::uint32_t);

void __fastcall CreateVerify(CDib* self, void*, int width, int height, int bitCount, std::uint32_t red,
                             std::uint32_t green, std::uint32_t blue) {
    s_create(self, nullptr, width, height, bitCount, red, green, blue);
    CDib expected = *self;
    std::uint8_t header[sizeof(BITMAPINFOHEADER) + 16] = {};
    const bool sized = expected.header && expected.maskBytes >= 0 && expected.maskBytes <= 16;
    if (sized) {
        std::memcpy(header, expected.header, sizeof(BITMAPINFOHEADER) + expected.maskBytes);
        expected.colours = reinterpret_cast<RGBQUAD*>(header + (reinterpret_cast<std::uint8_t*>(expected.colours) -
                                                                reinterpret_cast<std::uint8_t*>(expected.header)));
        gamecrt::operator_delete(expected.header);
        expected.header = reinterpret_cast<BITMAPINFOHEADER*>(header);
    }
    self->Create(width, height, bitCount, red, green, blue);
    ei::verify::Check("CDib::Create", true, sized && SameCreated(expected, *self), "%d x %d, %d bits: size %u / %u",
                      width, height, bitCount, expected.imageSize, self->imageSize);
}
EI_VERIFY(0x006D4BA0, CreateVerify, 6, s_create);

// 006D4B30: PUSH -1; PUSH imm32 (7 bytes). The original on a scratch object (its Create runs
// through the wrapper above), ours on the caller's; compared, the scratch's header freed.
CDib*(__fastcall* s_constructSized)(CDib*, void*, int, int, int, std::uint32_t, std::uint32_t, std::uint32_t);

CDib* __fastcall ConstructSizedVerify(CDib* self, void*, int width, int height, int bitCount, std::uint32_t red,
                                      std::uint32_t green, std::uint32_t blue) {
    CDib scratch;
    std::memset(&scratch, 0xCC, sizeof(scratch));
    s_constructSized(&scratch, nullptr, width, height, bitCount, red, green, blue);
    CDib* result = self->ConstructSized(width, height, bitCount, red, green, blue);
    ei::verify::Check("CDib::ConstructSized", true, result == self && SameCreated(scratch, *self),
                      "%d x %d, %d bits", width, height, bitCount);
    if (scratch.headerOwner == CDib::kNew) gamecrt::operator_delete(scratch.header);
    return result;
}
EI_VERIFY(0x006D4B30, ConstructSizedVerify, 7, s_constructSized);

// 006D69B0: ours only (it frees what the bitmap owns). Checked: everything zero afterwards.
void(__fastcall* s_empty)(CDib*);

void __fastcall EmptyVerify(CDib* self) {
    self->Empty();
    ei::verify::Check("CDib::Empty", true, IsEmpty(self), "fields left set");
}
EI_VERIFY(0x006D69B0, EmptyVerify, 0, s_empty);

// 006D4CD0: ours only. Checked: the class's vtable, everything else zero.
void(__fastcall* s_destruct)(CDib*);

void __fastcall DestructVerify(CDib* self) {
    self->Destruct();
    ei::verify::Check("CDib::Destruct", true, self->vtable == DibVtable() && IsEmpty(self), "fields left set");
}
EI_VERIFY(0x006D4CD0, DestructVerify, 0, s_destruct);

// 006D4B10 (slot 1): ours only (PUSH ESI; MOV ESI,ECX; CALL: no five bytes to steal). The object
// is gone afterwards when bit 0 is set; otherwise checked as the destructor.
CDib*(__fastcall* s_delete)(CDib*, void*, unsigned);

CDib* __fastcall DeleteVerify(CDib* self, void*, unsigned flags) {
    CDib* result = self->Delete(flags);
    const bool ok = result == self && ((flags & 1) != 0 || (self->vtable == DibVtable() && IsEmpty(self)));
    ei::verify::Check("CDib::Delete", true, ok, "flags %u", flags);
    return result;
}
EI_VERIFY(0x006D4B10, DeleteVerify, 0, s_delete);

// 006D4A60 (slot 0): MOV EAX,imm32 (5 bytes). Pure.
const void*(__fastcall* s_runtimeClass)(const CDib*);

const void* __fastcall RuntimeClassVerify(const CDib* self) {
    const void* expected = s_runtimeClass(self);
    ei::verify::Check("CDib::GetRuntimeClass", expected, self->GetRuntimeClass(), "runtime class");
    return expected;
}
EI_VERIFY(0x006D4A60, RuntimeClassVerify, 5, s_runtimeClass);

// 006D5BB0 (slot 2): ours only (it reads or writes the archive's file once).
void(__fastcall* s_serialize)(CDib*, void*, mfc::CArchive*);

void __fastcall SerializeVerify(CDib* self, void*, mfc::CArchive* archive) { self->Serialize(archive); }
EI_VERIFY(0x006D5BB0, SerializeVerify, 0, s_serialize);

// 006D4D10: MOV EAX,[ECX+0x10]; TEST EAX,EAX (5 bytes). Pure.
SIZE*(__fastcall* s_size)(const CDib*, void*, SIZE*);

SIZE* __fastcall SizeVerify(const CDib* self, void*, SIZE* out) {
    SIZE expected = {-1, -1};
    SIZE* expectedResult = s_size(self, nullptr, &expected);
    SIZE* result = self->Size(out);
    ei::verify::Check("CDib::Size", true,
                      expectedResult == &expected && result == out && expected.cx == out->cx && expected.cy == out->cy,
                      "%ld x %ld / %ld x %ld", expected.cx, expected.cy, out->cx, out->cy);
    return result;
}
EI_VERIFY(0x006D4D10, SizeVerify, 5, s_size);

// 006D54D0: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0x24] (6 bytes). The original (it deletes the old
// palette), its palette taken out of the bitmap, ours; the results and the palettes' entries
// compared, the original's palette deleted.
int(__fastcall* s_makePalette)(CDib*);

int __fastcall MakePaletteVerify(CDib* self) {
    const HPALETTE before = self->palette;
    const int expected = s_makePalette(self);
    const HPALETTE expectedPalette = self->palette;
    if (expected) self->palette = nullptr;
    const int actual = self->MakePalette();
    bool same = expected == actual;
    if (same && expected) {
        PALETTEENTRY left[256] = {}, right[256] = {};
        const UINT leftCount = GetPaletteEntries(expectedPalette, 0, 256, left);
        const UINT rightCount = GetPaletteEntries(self->palette, 0, 256, right);
        same = leftCount == rightCount && std::memcmp(left, right, sizeof(left)) == 0;
        DeleteObject(expectedPalette);
    } else if (same) {
        same = self->palette == before;
    }
    ei::verify::Check("CDib::MakePalette", true, same, "%d colours: %d / %d", self->colourCount, expected, actual);
    return actual;
}
EI_VERIFY(0x006D54D0, MakePaletteVerify, 6, s_makePalette);

// 006D6AB0: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0x10] (6 bytes). The original, its block freed and
// the two fields put back, ours; both give a block (or both the old pointer) and mark it owned.
std::uint8_t*(__fastcall* s_allocateBits)(CDib*);

std::uint8_t* __fastcall AllocateBitsVerify(CDib* self) {
    std::uint8_t* const bits = self->bits;
    const int owner = self->bitsOwner;
    std::uint8_t* expected = s_allocateBits(self);
    const int expectedOwner = self->bitsOwner;
    const bool allocated = self->header != nullptr;
    if (allocated) gamecrt::operator_delete(expected);
    self->bits = bits;
    self->bitsOwner = owner;
    std::uint8_t* actual = self->AllocateBits();
    const bool same = expectedOwner == self->bitsOwner && actual == self->bits &&
                      (allocated ? (expected != nullptr) == (actual != nullptr) : expected == actual);
    ei::verify::Check("CDib::AllocateBits", true, same, "header %d: owner %d / %d", allocated, expectedOwner,
                      self->bitsOwner);
    return actual;
}
EI_VERIFY(0x006D6AB0, AllocateBitsVerify, 6, s_allocateBits);

// 006D6D40: PUSH -1; PUSH imm32 (7 bytes). From the screenshot (scenario camera) and the movie
// recorder. Both read the surface (a read lock each; nothing draws in between); the two bitmaps'
// headers and pixels compared, the original's deleted.
CDib*(__fastcall* s_captureRect)(CDXSurface*, const RECT*);

CDib* __fastcall CaptureRectVerify(CDXSurface* self, const RECT* rect) {
    // A holder without a DirectDraw surface (the switch-over, docs/notes/renderer.md section 9;
    // the original cannot read it): ours only, taken from the new renderer's frame (no frame: no
    // bitmap). Checked for the rectangle's size and the screen's depth; the frame's pixels are
    // compared by the harness's "checkshot".
    if (!self->surface) {
        CDib* made = CaptureSurfaceRect(self, rect);
        if (made) {
            const BITMAPINFOHEADER* header = made->header;
            ei::verify::Check("CaptureSurfaceRect (the new renderer's frame)", true,
                              header && header->biWidth == rect->right - rect->left &&
                                  header->biHeight == rect->bottom - rect->top && header->biBitCount == self->bitsPerPixel &&
                                  made->bits,
                              "%ld,%ld-%ld,%ld of a %d-bit holder", rect->left, rect->top, rect->right, rect->bottom,
                              self->bitsPerPixel);
        }
        return made;
    }
    CDib* expected = s_captureRect(self, rect);
    const long expectedResult = self->result;
    CDib* actual = CaptureSurfaceRect(self, rect);
    bool same = expected && actual && expectedResult == self->result && SameCreated(*expected, *actual) &&
                (expected->bits != nullptr) == (actual->bits != nullptr) && expected->bitsOwner == actual->bitsOwner;
    std::size_t bytes = 0;
    if (same && expected->bits) {
        const BITMAPINFOHEADER* header = expected->header;
        const int pixelBytes = header->biBitCount == 16 || header->biBitCount == 24 || header->biBitCount == 32
                                   ? header->biBitCount / 8
                                   : 0;   // other depths: the block is left as allocated
        bytes = static_cast<std::size_t>(header->biWidth) * header->biHeight * pixelBytes;
        same = std::memcmp(expected->bits, actual->bits, bytes) == 0;
    }
    ei::verify::Check("CaptureSurfaceRect", true, same, "%ld,%ld-%ld,%ld of a %d-bit surface: %u bytes", rect->left,
                      rect->top, rect->right, rect->bottom, self->bitsPerPixel, static_cast<unsigned>(bytes));
    if (expected) expected->Delete(1);
    return actual;
}
EI_VERIFY(0x006D6D40, CaptureRectVerify, 7, s_captureRect);

// 006D6FC0: ours only (it calls the capture above through its address). Checked: a bitmap of the
// surface's size.
CDib*(__fastcall* s_capture)(CDXSurface*);

CDib* __fastcall CaptureVerify(CDXSurface* self) {
    CDib* result = CaptureSurface(self);
    ei::verify::Check("CaptureSurface", true,
                      result && result->header && result->header->biWidth == self->width &&
                          result->header->biHeight == self->height,
                      "%d x %d", self->width, self->height);
    return result;
}
EI_VERIFY(0x006D6FC0, CaptureVerify, 0, s_capture);

} // namespace
