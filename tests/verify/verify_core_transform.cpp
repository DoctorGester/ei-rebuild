// Differential checks for the transform object and camera helpers. Each of these writes only the
// object's own transform / a few camera fields (or an output vector), so the original runs on a
// scratch copy of those bytes and ours on the real object, or the other way round where an input
// may alias the output. Save/Load run the original against a recording file and ours through a
// recording proxy of the real file, and compare the call sequences.
#include "verify.h"

#include "../../src/game/camera.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

constexpr std::uintptr_t kBaseTransformVtable = 0x0073BA38;   // slot 14 = 0040CB80
constexpr std::uintptr_t kCombineRotation = 0x0040CB80;

// Scratch copy of the CTransformObject part of an object (0x54 bytes).
struct TransformCopy {
    alignas(4) unsigned char bytes[sizeof(CTransformObject)];
    CTransformObject* get() { return reinterpret_cast<CTransformObject*>(bytes); }
};

// 00401490: MOV EAX,ECX (2), PUSH ESI (1), MOV ECX,[ESP+8] (4).
CTransformObject*(__fastcall* s_copy)(CTransformObject*, void*, const CTransformObject*);
CTransformObject* __fastcall CopyVerify(CTransformObject* self, void*, const CTransformObject* other) {
    TransformCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    bool retOk = actual.get()->CTransformObject::CopyTransform(*other) == actual.get();
    CTransformObject* expectedRet = s_copy(self, nullptr, other);
    bool same = retOk && expectedRet == self && std::memcmp(self, actual.bytes, sizeof(actual.bytes)) == 0;
    ei::verify::Check("CTransformObject::CopyTransform", true, same, "");
    return expectedRet;
}

// 0040CB80: SUB ESP,0x10 (3), MOV EAX,[ESP+0x14] (4).
void(__fastcall* s_combine)(CTransformObject*, void*, const Quaternion*);
void __fastcall CombineVerify(CTransformObject* self, void*, const Quaternion* parent) {
    TransformCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    actual.get()->CTransformObject::CombineRotation(*parent);
    Quaternion p = *parent;
    s_combine(self, nullptr, parent);
    ei::verify::Check("CTransformObject::CombineRotation", true,
                      std::memcmp(self, actual.bytes, sizeof(actual.bytes)) == 0, "parent (%g %g %g %g)", p.w, p.x,
                      p.y, p.z);
}

// 0040CC30: SUB ESP,0x54 (3), PUSH ESI (1), MOV ESI,ECX (2). It calls slot 14 first, which some
// classes override (00516370, rotation only). The original runs on a scratch copy given the base
// vtable, so it cannot touch the real object; the world position is always compared, the world
// rotation only when the object's own slot 14 is the base one.
void(__fastcall* s_update)(CTransformObject*, void*, const Quaternion*, const Vector3*);
void __fastcall UpdateVerify(CTransformObject* self, void*, const Quaternion* rotation, const Vector3* position) {
    TransformCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    *reinterpret_cast<std::uintptr_t*>(expected.bytes) = kBaseTransformVtable;
    s_update(expected.get(), nullptr, rotation, position);
    bool baseSlot = (*reinterpret_cast<std::uintptr_t* const*>(self))[14] == kCombineRotation;
    self->CTransformObject::UpdateWorld(*rotation, *position);
    const std::size_t from = offsetof(CTransformObject, worldPosition_);
    const std::size_t to = baseSlot ? sizeof(CTransformObject) : from + sizeof(Vector3);
    bool same = std::memcmp(reinterpret_cast<const char*>(self) + from, expected.bytes + from, to - from) == 0 &&
                std::memcmp(&self->localPosition_, expected.bytes + offsetof(CTransformObject, localPosition_),
                            sizeof(Vector3)) == 0;
    ei::verify::Check("CTransformObject::UpdateWorld", true, same, "local (%g %g %g) world (%g %g %g)",
                      self->localPosition_.x, self->localPosition_.y, self->localPosition_.z,
                      self->worldPosition_.x, self->worldPosition_.y, self->worldPosition_.z);
}

// File slots 13 (Read) and 14 (Write), recorded.
struct FileCall {
    int slot;
    std::ptrdiff_t offset;   // from the object
    unsigned size;
};
struct Recording {
    const void* object;
    FileCall calls[4];
    int count;
};
// A stand-in CFile: vtable pointer, then the recording and the real file to forward to (or null).
struct RecordingFile {
    void* const* vtable;
    Recording* rec;
    void* real;
};
void Record(RecordingFile* f, int slot, void* p, unsigned n) {
    Recording& r = *f->rec;
    if (r.count < 4) r.calls[r.count] = {slot, static_cast<char*>(p) - static_cast<const char*>(r.object), n};
    ++r.count;
}
unsigned __fastcall RecordRead(RecordingFile* f, void*, void* p, unsigned n) {
    Record(f, 13, p, n);
    return f->real ? ei::VCall<unsigned>(f->real, 13, p, n) : n;
}
void __fastcall RecordWrite(RecordingFile* f, void*, void* p, unsigned n) {
    Record(f, 14, p, n);
    if (f->real) ei::VCall<void>(f->real, 14, p, n);
}
void* const* RecordingVtable() {
    static void* vtable[16] = {};
    vtable[13] = reinterpret_cast<void*>(&RecordRead);
    vtable[14] = reinterpret_cast<void*>(&RecordWrite);
    return vtable;
}

using FileFn = void(__fastcall*)(CTransformObject*, void*, void*);
void CheckFile(const char* name, FileFn original, void (*ours)(CTransformObject*, void*), CTransformObject* self,
               void* file) {
    // The original against a file that only records (a Read fills nothing, so `self` is unchanged).
    Recording expected{self, {}, 0};
    RecordingFile dry{RecordingVtable(), &expected, nullptr};
    original(self, nullptr, &dry);
    // Ours against the real file, through a recording proxy (called non-virtually: the object's
    // vtable leads back to this wrapper).
    Recording actual{self, {}, 0};
    RecordingFile proxy{RecordingVtable(), &actual, file};
    ours(self, &proxy);
    bool same = expected.count == actual.count &&
                std::memcmp(expected.calls, actual.calls, sizeof(FileCall) * (expected.count < 4 ? expected.count : 4)) == 0;
    ei::verify::Check(name, true, same, "%d vs %d calls", expected.count, actual.count);
}

// 00401510 / 00401540: PUSH ESI (1), MOV ESI,[ESP+8] (4).
FileFn s_save, s_load;
void __fastcall SaveVerify(CTransformObject* self, void*, void* file) {
    CheckFile("CTransformObject::SaveTransform", s_save,
              [](CTransformObject* o, void* f) { o->CTransformObject::SaveTransform(f); }, self, file);
}
void __fastcall LoadVerify(CTransformObject* self, void*, void* file) {
    CheckFile("CTransformObject::LoadTransform", s_load,
              [](CTransformObject* o, void* f) { o->CTransformObject::LoadTransform(f); }, self, file);
}

// 0040E810: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x1C (3).
void(__fastcall* s_ground)(const CCamera*, void*, Vector3*);
void __fastcall GroundVerify(const CCamera* self, void*, Vector3* out) {
    Vector3 actual;
    self->GroundPoint(&actual);
    s_ground(self, nullptr, out);
    ei::verify::Check("CCamera::GroundPoint", true, std::memcmp(out, &actual, sizeof(actual)) == 0,
                      "pos (%g %g %g) rot (%g %g %g %g)", self->localPosition_.x, self->localPosition_.y,
                      self->localPosition_.z, self->localRotation_.w, self->localRotation_.x,
                      self->localRotation_.y, self->localRotation_.z);
}

// Camera setters touch only fields below +0x120: the original runs on a copy of those bytes.
struct CameraCopy {
    alignas(4) unsigned char bytes[0x120];
};
bool SameCamera(const CCamera* self, const CameraCopy& c) { return std::memcmp(self, c.bytes, sizeof(c.bytes)) == 0; }

// 0040F360 / 0040F3C0: PUSH EBP (1), MOV EBP,ESP (2), PUSH ECX (1), MOV EAX,[EBP+8] (3).
using RateFn = void(__fastcall*)(void*, void*, float);
RateFn s_turn, s_tilt;
void __fastcall TurnVerify(CCamera* self, void*, float rate) {
    CameraCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_turn(expected.bytes, nullptr, rate);
    self->SetTurnRate(rate);
    ei::verify::Check("CCamera::SetTurnRate", true, SameCamera(self, expected), "%g", rate);
}
void __fastcall TiltVerify(CCamera* self, void*, float rate) {
    CameraCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_tilt(expected.bytes, nullptr, rate);
    self->SetTiltRate(rate);
    ei::verify::Check("CCamera::SetTiltRate", true, SameCamera(self, expected), "%g", rate);
}

// 0040F420: MOV EAX,[ESP+4] (4), MOV [ECX+0xD0],EAX (6).
void(__fastcall* s_d0)(void*, void*, std::uint32_t);
void __fastcall D0Verify(CCamera* self, void*, std::uint32_t value) {
    CameraCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_d0(expected.bytes, nullptr, value);
    self->SetZoomTarget(value);
    ei::verify::Check("CCamera::SetZoomTarget", true, SameCamera(self, expected), "%08X", value);
}

// 0040D3B0: MOV EAX,[ESP+4] (4), TEST EAX,EAX (2).
void(__fastcall* s_target)(void*, void*, const CEngineObject*);
void __fastcall TargetVerify(CCamera* self, void*, const CEngineObject* target) {
    CameraCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_target(expected.bytes, nullptr, target);
    self->SetTarget(target);
    ei::verify::Check("CCamera::SetTarget", true, SameCamera(self, expected), "id %d", self->targetId_);
}

// 0040D420: MOV EAX,[ESP+4] (4), CMP EAX,-1 (3).
void(__fastcall* s_targetId)(void*, void*, int);
void __fastcall TargetIdVerify(CCamera* self, void*, int id) {
    CameraCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_targetId(expected.bytes, nullptr, id);
    self->SetTargetId(id);
    ei::verify::Check("CCamera::SetTargetId", true, SameCamera(self, expected), "id %d", id);
}

} // namespace

EI_VERIFY(0x00401490, CopyVerify, 7, s_copy);
EI_VERIFY(0x0040CB80, CombineVerify, 7, s_combine);
EI_VERIFY(0x0040CC30, UpdateVerify, 6, s_update);
EI_VERIFY(0x00401510, SaveVerify, 5, s_save);
EI_VERIFY(0x00401540, LoadVerify, 5, s_load);
EI_VERIFY(0x0040E810, GroundVerify, 6, s_ground);
EI_VERIFY(0x0040F360, TurnVerify, 7, s_turn);
EI_VERIFY(0x0040F3C0, TiltVerify, 7, s_tilt);
EI_VERIFY(0x0040F420, D0Verify, 10, s_d0);
EI_VERIFY(0x0040D3B0, TargetVerify, 6, s_target);
EI_VERIFY(0x0040D420, TargetIdVerify, 7, s_targetId);
