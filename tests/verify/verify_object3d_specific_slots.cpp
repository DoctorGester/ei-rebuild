// Checks for object3d_specific_slots.cpp (A-U4).
// - ExtraScale (0051AD20) / ServerExtraScale (0051AC30): pure; the original into scratch floats,
//   ours into the caller's, bitwise. Reached by CObject3D::PartWorldPosition (005188B0: effects at
//   unit parts, spells_cast, effects) and the drawing of every client object.
// - RegisterPartMaterials (0051C970): ours only; the cursor ends within the drawn parts. Reached
//   when a client object's figure is built (world_client_maps.cpp's object setup, every zone load).
#include "verify.h"

#include "../../src/game/map_object_shadow.h"
#include "../../src/game/map_object_figures.h"

#include <cstring>

void __fastcall ServerExtraScale(void*, void*, float* x, float* y, float* z);

namespace {
void* s_unusedRegister;

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
} // namespace

// 0051AD20: MOV EDX,[ESP+4]; MOV EAX,[ECX+0xF0] (10 bytes).
static void(__fastcall* s_extraScale)(CObject3DClientSpecific*, void*, float*, float*, float*);
static void __fastcall ExtraScaleVerify(CObject3DClientSpecific* self, void*, float* x, float* y, float* z) {
    float ex, ey, ez;
    s_extraScale(self, nullptr, &ex, &ey, &ez);
    self->ExtraScale(x, y, z);
    bool ok = SameBits(ex, *x) && SameBits(ey, *y) && SameBits(ez, *z);
    ei::verify::Check("CObject3DClientSpecific::ExtraScale", true, ok, "%g %g %g", *x, *y, *z);
}
EI_VERIFY(0x0051AD20, ExtraScaleVerify, 10, s_extraScale);

// 0051AC30: MOV EAX,[ESP+4]; MOV ECX,[ESP+8] (8 bytes).
static void(__fastcall* s_serverExtraScale)(void*, void*, float*, float*, float*);
static void __fastcall ServerExtraScaleVerify(void* self, void*, float* x, float* y, float* z) {
    float ex, ey, ez;
    s_serverExtraScale(self, nullptr, &ex, &ey, &ez);
    ServerExtraScale(self, nullptr, x, y, z);
    bool ok = SameBits(ex, *x) && SameBits(ey, *y) && SameBits(ez, *z);
    ei::verify::Check("CObject3DServer::ExtraScale", true, ok, "%g %g %g", *x, *y, *z);
}
EI_VERIFY(0x0051AC30, ServerExtraScaleVerify, 8, s_serverExtraScale);

static void __fastcall RegisterPartMaterialsVerify(CObject3DClientSpecific* self) {
    int cursorBefore = self->materialCursor;
    self->RegisterPartMaterials();
    bool ok = self->materials.size <= 0 ? self->materialCursor == cursorBefore
                                        : self->materialCursor >= 0 && self->materialCursor <= self->materials.size;
    ei::verify::Check("CObject3DClientSpecific::RegisterPartMaterials", true, ok, "cursor %d of %d",
                      self->materialCursor, self->materials.size);
}
EI_VERIFY(0x0051C970, RegisterPartMaterialsVerify, 0, s_unusedRegister);

// A-U11. Picking (0051C130 / 0051C160 / 0051C1D0; the cursor over the world each frame), the part
// lookups (0051B2E0 / 0051B2D0 / 00516920) and the class slots (0051AD50 / 0051AD60): pure, the
// original and ours both run, results compared. The base half's destructors (0051AD00 / 0051ACE0;
// an object's side half dropped): ours only, the engine object's vtable afterwards.
static int(__fastcall* s_boundsContain)(CObject3DClientSpecific*, void*, const long*);
static int __fastcall BoundsContainVerify(CObject3DClientSpecific* self, void*, const long* point) {
    int expected = s_boundsContain(self, nullptr, point);
    int actual = self->BoundsContainPoint(point);
    ei::verify::Check("CObject3DClientSpecific::BoundsContainPoint", expected, actual, "(%ld, %ld)", point[0], point[1]);
    return expected;
}
EI_VERIFY(0x0051C130, BoundsContainVerify, 7, s_boundsContain);

static int(__fastcall* s_partsContain)(CObject3DClientSpecific*, void*, const long*);
static int __fastcall PartsContainVerify(CObject3DClientSpecific* self, void*, const long* point) {
    int expected = s_partsContain(self, nullptr, point);
    int actual = self->PartsContainPoint(point);
    ei::verify::Check("CObject3DClientSpecific::PartsContainPoint", expected, actual, "(%ld, %ld)", point[0], point[1]);
    return expected;
}
EI_VERIFY(0x0051C160, PartsContainVerify, 7, s_partsContain);

static int(__fastcall* s_partsIntersect)(CObject3DClientSpecific*, void*, const tagRECT*);
static int __fastcall PartsIntersectVerify(CObject3DClientSpecific* self, void*, const tagRECT* rect) {
    int expected = s_partsIntersect(self, nullptr, rect);
    int actual = self->PartsIntersectRect(rect);
    ei::verify::Check("CObject3DClientSpecific::PartsIntersectRect", expected, actual, "");
    return expected;
}
EI_VERIFY(0x0051C1D0, PartsIntersectVerify, 6, s_partsIntersect);

static CFigureMesh*(__fastcall* s_partNamed)(CObject3DClientSpecific*, void*, const char*);
static CFigureMesh* __fastcall PartNamedVerify(CObject3DClientSpecific* self, void*, const char* name) {
    CFigureMesh* expected = s_partNamed(self, nullptr, name);
    CFigureMesh* actual = self->ObjectPartNamed(name);
    ei::verify::Check("CObject3DClientSpecific::ObjectPartNamed", static_cast<void*>(expected), static_cast<void*>(actual),
                      "%s", name);
    return expected;
}
EI_VERIFY(0x0051B2E0, PartNamedVerify, 7, s_partNamed);

static CFigureMesh*(__fastcall* s_partOfType)(CObject3DClientSpecific*, void*, int);
static CFigureMesh* __fastcall PartOfTypeVerify(CObject3DClientSpecific* self, void*, int type) {
    CFigureMesh* expected = s_partOfType(self, nullptr, type);
    CFigureMesh* actual = self->ObjectPartOfType(type);
    ei::verify::Check("CObject3DClientSpecific::ObjectPartOfType", static_cast<void*>(expected), static_cast<void*>(actual),
                      "%d", type);
    return expected;
}
EI_VERIFY(0x0051B2D0, PartOfTypeVerify, 7, s_partOfType);

static CFigureMesh*(__fastcall* s_findPartOfType)(CObject3D*, void*, int);
static CFigureMesh* __fastcall FindPartOfTypeVerify(CObject3D* self, void*, int type) {
    CFigureMesh* expected = s_findPartOfType(self, nullptr, type);
    CFigureMesh* actual = self->FindPartOfType(type);
    ei::verify::Check("CObject3D::FindPartOfType", static_cast<void*>(expected), static_cast<void*>(actual), "%d", type);
    return expected;
}
EI_VERIFY(0x00516920, FindPartOfTypeVerify, 6, s_findPartOfType);

static const char*(__fastcall* s_clientName)(CObject3DClientSpecific*);
static const char* __fastcall ClientNameVerify(CObject3DClientSpecific* self) {
    const char* expected = s_clientName(self);
    ei::verify::Check("CObject3DClientSpecific::GetClassName", 0, std::strcmp(expected, "CObject3DClientSpecific"), "%s",
                      expected);
    return expected;
}
EI_VERIFY(0x0051AD50, ClientNameVerify, 5, s_clientName);

static int(__fastcall* s_clientId)(CObject3DClientSpecific*);
static int __fastcall ClientIdVerify(CObject3DClientSpecific* self) {
    int expected = s_clientId(self);
    ei::verify::Check("CObject3DClientSpecific::GetClassId", expected, 0x37, "%X", expected);
    return expected;
}
EI_VERIFY(0x0051AD60, ClientIdVerify, 5, s_clientId);

void __fastcall SpecificBaseDestruct(CObject3DClientSpecific* self);
CObject3DClientSpecific* __fastcall SpecificBaseDelete(CObject3DClientSpecific* self, void*, unsigned flags);
static void __fastcall BaseDestructVerify(CObject3DClientSpecific* self) {
    SpecificBaseDestruct(self);
    bool ok = !self->object && *reinterpret_cast<const std::uintptr_t*>(self) == CEngineObject::kVtableAddress;
    ei::verify::Check("CObject3DSpecific::Destruct", true, ok, "");
}
EI_VERIFY(0x0051AD00, BaseDestructVerify, 0, s_unusedRegister);
static CObject3DClientSpecific* __fastcall BaseDeleteVerify(CObject3DClientSpecific* self, void*, unsigned flags) {
    CObject3DClientSpecific* result = SpecificBaseDelete(self, nullptr, flags);
    ei::verify::Check("CObject3DSpecific::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x0051ACE0, BaseDeleteVerify, 0, s_unusedRegister);
