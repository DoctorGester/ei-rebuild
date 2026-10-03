// Checks for the 3D object's figure code (map_object_figures.cpp).
//
// ComputeBounds writes only the object's bounds (and zeroes the pending parts' positions, which
// it reads only after zeroing): ours, the bounds saved and restored, the original; bytes and EAX
// compared. The others build figures, take and drop references and allocate: ours only, with
// what must hold afterwards.
#include "verify.h"

#include "../../src/game/map_object_figures.h"

#include <bit>
#include <cstring>

namespace {
void* s_unused;

constexpr int kBoundsStart = 0x124;
constexpr int kBoundsSize = 0x150 - 0x124;

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }
} // namespace

// 00518310: PUSH EBP; MOV EBP,ESP; SUB ESP,0x70 (6 bytes).
static std::uint32_t(__fastcall* s_computeBounds)(CObject3D*);
static std::uint32_t __fastcall ComputeBoundsVerify(CObject3D* self) {
    unsigned char* bounds = reinterpret_cast<unsigned char*>(self) + kBoundsStart;
    unsigned char before[kBoundsSize];
    unsigned char ours[kBoundsSize];
    std::memcpy(before, bounds, kBoundsSize);
    std::uint32_t actual = self->ComputeBounds();
    std::memcpy(ours, bounds, kBoundsSize);
    std::memcpy(bounds, before, kBoundsSize);
    std::uint32_t expected = s_computeBounds(self);
    int diff = -1;
    for (int i = 0; i < kBoundsSize && diff < 0; ++i)
        if (ours[i] != bounds[i]) diff = kBoundsStart + i;
    ei::verify::Check("CObject3D::ComputeBounds", true, diff < 0 && expected == actual,
                      "object %d (%d parts, %d pending): first difference at +0x%X, EAX %08X vs %08X", self->id_,
                      self->parts.size, self->pendingParts.size, diff, expected, actual);
    return expected;
}
EI_VERIFY(0x00518310, ComputeBoundsVerify, 6, s_computeBounds);

// 00517700. Ours only (rebuilds the figure). The scale ends as given; after a change with parts
// the bounds are valid.
static std::uintptr_t __fastcall SetFigureScaleVerify(CObject3D* self, void*, const float* scale) {
    float wanted[3];
    std::memcpy(wanted, scale, sizeof(wanted));
    bool same = !(self->scale[0] < wanted[0] || self->scale[0] > wanted[0]) &&
                !(self->scale[1] < wanted[1] || self->scale[1] > wanted[1]) &&
                !(self->scale[2] < wanted[2] || self->scale[2] > wanted[2]);
    float old[3];
    std::memcpy(old, self->scale, sizeof(old));
    std::uintptr_t result = self->SetFigureScale(wanted);
    bool ok = same ? std::memcmp(old, self->scale, sizeof(old)) == 0 && result == 0
                   : std::memcmp(wanted, self->scale, sizeof(wanted)) == 0 &&
                         (self->parts.size == 0 && self->pendingParts.size == 0 ||
                          ((self->boundsFlags & 3) == 3 && result == Bits(self->center[2])));
    ei::verify::Check("CObject3D::SetFigureScale", true, ok, "object %d", self->id_);
    return result;
}
EI_VERIFY(0x00517700, SetFigureScaleVerify, 0, s_unused);

// 00517500. Ours only (builds parts). Afterwards the figures are loaded, nothing is pending, and
// every part that was pending is in the part list.
static int __fastcall LoadFiguresVerify(CObject3D* self) {
    constexpr int kMaxPending = 64;
    void* pending[kMaxPending];
    bool loaded = (self->flags_ & 0x80) != 0;
    int count = self->pendingParts.size;
    bool checked = !loaded && count >= 0 && count <= kMaxPending;
    if (checked && count) std::memcpy(pending, self->pendingParts.data, count * sizeof(void*));
    int result = self->LoadFigures();
    bool ok = result == 1 && (self->flags_ & 0x80);
    if (checked) {
        ok = ok && self->pendingParts.size == 0 && self->pendingParts.data == nullptr;
        for (int i = 0; i < count && ok; ++i) {
            bool found = false;
            for (int k = 0; k < self->parts.size && !found; ++k) found = self->parts.data[k] == pending[i];
            ok = found;
        }
    }
    ei::verify::Check("CObject3D::LoadFigures", true, ok, "object %d (%d pending)", self->id_, count);
    return result;
}
EI_VERIFY(0x00517500, LoadFiguresVerify, 0, s_unused);

// 00519E80. Ours only (allocates): the value is stored and the size covers it.
static void __fastcall SetAtGrowVerify(AlignedPtrArray* self, void*, int index, void* value) {
    self->SetAtGrow(index, value);
    bool ok = self->size > index && self->capacity >= self->size && self->data[index] == value &&
              (reinterpret_cast<std::uintptr_t>(self->data) & 0x1F) == 0;
    ei::verify::Check("AlignedPtrArray::SetAtGrow", true, ok, "index %d size %d", index, self->size);
}
EI_VERIFY(0x00519E80, SetAtGrowVerify, 0, s_unused);

// 00519D70. Ours only (allocates / frees): the size is as asked, within the capacity; no storage
// for size 0.
static void __fastcall SetSizeVerify(AlignedPtrArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->capacity >= size && (size != 0 || (!self->data && self->capacity == 0)) &&
              (growBy == -1 || self->growBy == growBy);
    ei::verify::Check("AlignedPtrArray::SetSize", true, ok, "size %d", size);
}
EI_VERIFY(0x00519D70, SetSizeVerify, 0, s_unused);
