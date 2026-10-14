// Checks for the 3D object's figure code (map_object_figures.cpp).
//
// ComputeBounds writes only the object's bounds (and zeroes the pending parts' positions, which
// it reads only after zeroing): ours, the bounds saved and restored, the original; bytes and EAX
// compared. The others build figures, take and drop references and allocate: ours only, with
// what must hold afterwards.
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/texture.h"
#include "../../src/render/render.h"

#include <bit>
#include <cstring>
#include <vector>

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
    bool same = !(self->scale.x < wanted[0] || self->scale.x > wanted[0]) &&
                !(self->scale.y < wanted[1] || self->scale.y > wanted[1]) &&
                !(self->scale.z < wanted[2] || self->scale.z > wanted[2]);
    float old[3];
    std::memcpy(old, &self->scale, sizeof(old));
    std::uintptr_t result = self->SetFigureScale(wanted);
    bool ok = same ? std::memcmp(old, &self->scale, sizeof(old)) == 0 && result == 0
                   : std::memcmp(wanted, &self->scale, sizeof(wanted)) == 0 &&
                         (self->parts.size == 0 && self->pendingParts.size == 0 ||
                          ((self->boundsFlags & 3) == 3 && result == Bits(self->center.z)));
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

// 005164B0. Ours only (references the part): the part is in the list afterwards.
static int __fastcall AddPartVerify(CObject3D* self, void*, CFigureMesh* part) {
    int result = self->AddPart(part);
    bool found = false;
    for (int k = 0; k < self->parts.size && !found; ++k) found = self->parts.data[k] == part;
    ei::verify::Check("CObject3D::AddPart", true, found, "object %d", self->id_);
    return result;
}
EI_VERIFY(0x005164B0, AddPartVerify, 0, s_unused);

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

// --- The shadow (map_object_shadow.cpp) ---------------------------------------------------------

namespace {
CTextureManager* TextureManager() { return *reinterpret_cast<CTextureManager**>(0x007AF744); }

// The pixels of a texture whose holder has only the new renderer's memory (the switch-over), or
// empty.
std::vector<std::uint8_t> MemoryOf(const CEngineObject* object) {
    const auto* texture = static_cast<const CTexture*>(object);
    const CDXSurface* holder = texture ? texture->surface_ : nullptr;
    const render::SurfaceMemory* memory = holder && !holder->surface ? render::SurfaceMemoryOf(holder) : nullptr;
    if (!memory) return {};
    const auto* bits = static_cast<const std::uint8_t*>(memory->bits);
    return std::vector<std::uint8_t>(bits, bits + static_cast<std::size_t>(memory->pitch) * memory->height);
}
} // namespace

// 0051CE60. Ours only (renders into textures and draws): returns 1 (the failure paths end in a
// fatal error). In the switch-over with "cpuwindows" (render_present, render_present_cpuwindows)
// the shadow's render surface and texture are memory-only (texture_shadow.cpp): when the call
// rendered the shadow (the render surface's memory changed), the texture's memory equals it.
static int __fastcall DrawShadowVerify(CObject3DClientSpecific* self, void*, CSceneRenderer* context) {
    CTextureManager* manager = TextureManager();
    std::vector<std::uint8_t> before = MemoryOf(manager ? manager->shadowRenderSurface_ : nullptr);
    int result = self->DrawShadow(context);
    ei::verify::Check("CObject3DClientSpecific::DrawShadow", 1, result, "flags %X", self->flags_);
    std::vector<std::uint8_t> rendered = MemoryOf(manager ? manager->shadowRenderSurface_ : nullptr);
    if (!rendered.empty() && rendered != before) {
        std::vector<std::uint8_t> texture = MemoryOf(self->texture);
        ei::verify::Check("CObject3DClientSpecific::DrawShadow copy", true, texture == rendered,
                          "texture %p: %u bytes, render surface %u bytes", static_cast<const void*>(self->texture),
                          static_cast<unsigned>(texture.size()), static_cast<unsigned>(rendered.size()));
    }
    return result;
}
EI_VERIFY(0x0051CE60, DrawShadowVerify, 0, s_unused);

// 0051B7E0. Ours only (draws): returns 1 and the scene's three matrix stacks end as deep as they
// started.
static int __fastcall DrawVerify(CObject3DClientSpecific* self, void*, CSceneRenderer* context, void* dirty) {
    auto depth = [&](int index) { return context->pipeline_->stacks_[index].count; };
    int before[3] = {depth(1), depth(2), depth(3)};
    int result = self->Draw(context, dirty);
    bool ok = result == 1 && depth(1) == before[0] && depth(2) == before[1] && depth(3) == before[2];
    ei::verify::Check("CObject3DClientSpecific::Draw", true, ok, "result %d, stacks %d %d %d -> %d %d %d", result,
                      before[0], before[1], before[2], depth(1), depth(2), depth(3));
    return result;
}
EI_VERIFY(0x0051B7E0, DrawVerify, 0, s_unused);
