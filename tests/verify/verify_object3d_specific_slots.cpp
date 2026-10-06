// Checks for object3d_specific_slots.cpp (A-U4).
// - ExtraScale (0051AD20) / ServerExtraScale (0051AC30): pure; the original into scratch floats,
//   ours into the caller's, bitwise. Reached by CObject3D::PartWorldPosition (005188B0: effects at
//   unit parts, spells_cast, effects) and the drawing of every client object.
// - RegisterPartMaterials (0051C970): ours only; the cursor ends within the drawn parts. Reached
//   when a client object's figure is built (world_client_maps.cpp's object setup, every zone load).
#include "verify.h"

#include "../../src/game/map_object_shadow.h"

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
    int cursorBefore = self->drawnPartCursor;
    self->RegisterPartMaterials();
    bool ok = self->drawnPartCount <= 0 ? self->drawnPartCursor == cursorBefore
                                        : self->drawnPartCursor >= 0 && self->drawnPartCursor <= self->drawnPartCount;
    ei::verify::Check("CObject3DClientSpecific::RegisterPartMaterials", true, ok, "cursor %d of %d",
                      self->drawnPartCursor, self->drawnPartCount);
}
EI_VERIFY(0x0051C970, RegisterPartMaterialsVerify, 0, s_unusedRegister);
