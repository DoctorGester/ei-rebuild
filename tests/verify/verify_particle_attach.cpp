// Checks for the particle source attachment (particle_attach.cpp). Scenarios: particles and
// spells_cast (spell modifiers carry sources attached to the hero: Attach, AttachmentPoint,
// PartPoint every tick; the per-type callbacks ask AttachmentOffset).
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/map_object_figures.h"

namespace {

void CheckPoint(const char* name, const Vector3& expected, const Vector3& actual) {
    ei::verify::CheckNear(name, expected.x, actual.x, "x");
    ei::verify::CheckNear(name, expected.y, actual.y, "y");
    ei::verify::CheckNear(name, expected.z, actual.z, "z");
}

} // namespace

// 00484B40: MOV EAX,[ESP+0xC]; SUB ESP,0xC (7 bytes). Against the original (it only reads).
using PartPointFn = Vector3*(__fastcall*)(CParticleSource*, void*, Vector3*, CObject3D*, const char*);
static PartPointFn s_partPoint;
static Vector3* __fastcall PartPointVerify(CParticleSource* self, void*, Vector3* out, CObject3D* object,
                                           const char* prefix) {
    Vector3 expected;
    s_partPoint(self, nullptr, &expected, object, prefix);
    Vector3* result = self->PartPoint(out, object, prefix);
    ei::verify::Check("PartPoint result", out, result, "%p");
    CheckPoint("PartPoint", expected, *out);
    return result;
}
EI_VERIFY(0x00484B40, PartPointVerify, 7, s_partPoint);

// 00484C00: SUB ESP,0xC; PUSH ESI; MOV ESI,[ESP+0x18] (8 bytes). Against the original, except
// for a unit's effect point (attachPoint 0): 00532860 can read uninitialised stack (unit.h).
using AttachmentPointFn = Vector3*(__fastcall*)(CParticleSource*, void*, Vector3*, void*);
static AttachmentPointFn s_attachmentPoint;
static Vector3* __fastcall AttachmentPointVerify(CParticleSource* self, void*, Vector3* out, void* object) {
    bool compare = !(object && (self->flags_ & 0x10) && self->attachPoint == 0);
    Vector3 expected = {};
    if (compare) s_attachmentPoint(self, nullptr, &expected, object);
    Vector3* result = self->AttachmentPoint(out, object);
    ei::verify::Check("AttachmentPoint result", out, result, "%p");
    if (compare) CheckPoint("AttachmentPoint", expected, *out);
    return result;
}
EI_VERIFY(0x00484C00, AttachmentPointVerify, 8, s_attachmentPoint);

// 00485280: SUB ESP,0xC; PUSH ESI; MOV ESI,[ECX+0x94] (10 bytes). Original first (its bounds
// update is repeated by ours with the same result), offset and radius compared.
using AttachmentOffsetFn = Vector3*(__fastcall*)(CParticleSource*, void*, Vector3*, float*);
static AttachmentOffsetFn s_attachmentOffset;
static Vector3* __fastcall AttachmentOffsetVerify(CParticleSource* self, void*, Vector3* out, float* radius) {
    Vector3 expected;
    float expectedRadius = 0.0f;
    s_attachmentOffset(self, nullptr, &expected, &expectedRadius);
    Vector3* result = self->AttachmentOffset(out, radius);
    ei::verify::Check("AttachmentOffset result", out, result, "%p");
    CheckPoint("AttachmentOffset", expected, *out);
    ei::verify::CheckNear("AttachmentOffset radius", expectedRadius, *radius, "radius");
    return result;
}
EI_VERIFY(0x00485280, AttachmentOffsetVerify, 10, s_attachmentOffset);

// 004849F0 (ours only: it changes the objects' source lists). Afterwards the source follows
// `object`, is in its list, and its emission point is the original's attachment point (or the
// local position without an object).
static void (__fastcall* s_attach)(CParticleSource*, void*, void*);
static void __fastcall AttachVerify(CParticleSource* self, void*, void* object) {
    self->Attach(object);
    ei::verify::Check("Attach attachedTo", object, self->attachedTo, "%p");
    if (!object) {
        CheckPoint("Attach origin (position)", self->position, self->origin);
        return;
    }
    auto* o = static_cast<CObject3D*>(object);
    bool listed = o->effectSources.Find(reinterpret_cast<CEngineObject*>(self)) != nullptr;
    ei::verify::Check("Attach listed", true, listed, "in the object's sources");
    if (!(self->flags_ & 0x10) || self->attachPoint != 0) {
        Vector3 expected;
        s_attachmentPoint(self, nullptr, &expected, object);
        CheckPoint("Attach origin", expected, self->origin);
    }
}
EI_VERIFY(0x004849F0, AttachVerify, 0, s_attach);

// 00485380 (ours only): no particles left, emission off, detached, origin = position.
static void (__fastcall* s_stopAndClear)(CParticleSource*, void*);
static void __fastcall StopAndClearVerify(CParticleSource* self, void*) {
    self->StopAndClear();
    ei::verify::Check("StopAndClear count", 0, self->particles.count, "%d");
    ei::verify::Check("StopAndClear head", static_cast<void*>(nullptr), static_cast<void*>(self->particles.head), "%p");
    ei::verify::Check("StopAndClear flags", 0u, self->flags_ & 5u, "%u");
    ei::verify::Check("StopAndClear attachedTo", static_cast<void*>(nullptr), self->attachedTo, "%p");
    CheckPoint("StopAndClear origin", self->position, self->origin);
}
EI_VERIFY(0x00485380, StopAndClearVerify, 0, s_stopAndClear);
