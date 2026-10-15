// Checks for camera_object.cpp.
//  - The constructor: ours on the real object, then the original on a scratch copy of the memory
//    (our memory);
//    BuiltCompare (the shake list sentinel and the view stack blocks pair up); the scratch camera is
//    then destroyed with our destructor body.
//  - The small slots, Init, GetRange and Anchor: side by side (scratch copies of the camera).
//  - The engine object constructor: side by side on a copy of its 0x1C bytes.
//  - The destructors, CloseFile, OpenFile, RecordFrame, LoadState / SaveState: ours only (the file
//    functions touch the disk).
//  - CameraRecordsAllocate: ours only (it allocates); the start is 32-byte aligned and the dword
//    before it points at most 0x23 bytes back.
#include "net_verify.h"

#include "../../src/game/camera.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

CEngineObject* __fastcall EngineObjectConstruct(CEngineObject* self);
void __fastcall TransformDestruct(CTransformObject* self);
const Vector3* __fastcall CameraAnchor(const CCamera* self);
int __fastcall CameraCloseFile(CCamera* self);
int __fastcall CameraCreate(CCamera* self);
void __fastcall CameraDestroy(CCamera* self);
void* __fastcall CameraRecordsAllocate(int count);

namespace {

void* s_unused;
using CameraBytes = unsigned char[sizeof(CCamera)];

bool SameBytes(const void* a, const void* b, std::size_t n) { return std::memcmp(a, b, n) == 0; }

// 0040CB20: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX],imm32 (6).
CEngineObject*(__fastcall* s_engineCtor)(CEngineObject*);
CEngineObject* __fastcall EngineCtorVerify(CEngineObject* self) {
    alignas(4) unsigned char scratch[sizeof(CEngineObject)];
    std::memcpy(scratch, self, sizeof(scratch));
    s_engineCtor(reinterpret_cast<CEngineObject*>(scratch));
    CEngineObject* result = EngineObjectConstruct(self);
    ei::verify::Check("EngineObjectConstruct", true, result == self && SameBytes(scratch, self, sizeof(scratch)), "");
    return result;
}

void __fastcall TransformDestructVerify(CTransformObject* self) {
    TransformDestruct(self);
    ei::verify::Check("TransformDestruct", true, true, "");
}
CTransformObject* __fastcall TransformDeleteVerify(CTransformObject* self, void*, unsigned flags) {
    ei::verify::Check("TransformDeletingDestructor", true, true, "");
    return self->DeleteScalar(flags);
}

// 0040C8B0: PUSH -1 (2), PUSH imm32 (5).
CCamera*(__fastcall* s_cameraCtor)(CCamera*);
CCamera* __fastcall CameraCtorVerify(CCamera* self) {
    // Ours first and the scratch in our memory, so that ours' blocks land where the original's would
    // without the check (the game reads the heap's address order, SV2f).
    std::vector<std::uint8_t> scratchBytes(sizeof(CCamera));
    std::uint8_t* scratch = scratchBytes.data();
    std::memcpy(scratch, self, sizeof(CCamera));
    CCamera* actual = self->Construct();
    CCamera* expected = s_cameraCtor(reinterpret_cast<CCamera*>(scratch));
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), sizeof(CCamera)};
    cmp.Range(0, sizeof(CCamera));
    ei::verify::Check("CCamera::Construct", true,
                      cmp.ok && expected == reinterpret_cast<CCamera*>(scratch) && actual == self, "%s", cmp.detail);
    reinterpret_cast<CCamera*>(scratch)->Destruct();
    return actual;
}

void __fastcall CameraDestructVerify(CCamera* self) {
    self->Destruct();
    ei::verify::Check("CCamera::Destruct", true, true, "");
}
CCamera* __fastcall CameraDeleteVerify(CCamera* self, void*, unsigned flags) {
    ei::verify::Check("CameraDeletingDestructor", true, true, "");
    return self->DeleteScalar(flags);
}

// Slots: MOV EAX,imm32 (5).
std::uintptr_t(__fastcall* s_className)(const CCamera*);
std::uintptr_t(__fastcall* s_classId)(const CCamera*);
std::uintptr_t(__fastcall* s_returnsOne)(CCamera*);
std::uintptr_t __fastcall ClassNameVerify(const CCamera* self) {
    std::uintptr_t expected = s_className(self);
    ei::verify::Check("CameraClassName", expected, std::uintptr_t{0x00783CF8}, "");
    return 0x00783CF8;
}
std::uintptr_t __fastcall ClassIdVerify(const CCamera* self) {
    std::uintptr_t expected = s_classId(self);
    ei::verify::Check("CameraClassId", expected, std::uintptr_t{0x43}, "");
    return 0x43;
}
std::uintptr_t __fastcall ReturnsOneVerify(CCamera* self) {
    std::uintptr_t expected = s_returnsOne(self);
    ei::verify::Check("CameraReturnsOne", expected, std::uintptr_t{1}, "");
    return 1;
}

// 0040D190 / 0040D1D0: MOV [ECX+0x11C],-1 (10). Side by side on a copy.
int(__fastcall* s_create)(CCamera*);
void(__fastcall* s_destroy)(CCamera*);
int __fastcall CreateVerify(CCamera* self) {
    CameraBytes scratch;
    std::memcpy(scratch, self, sizeof(scratch));
    int expected = s_create(reinterpret_cast<CCamera*>(scratch));
    int actual = CameraCreate(self);
    ei::verify::Check("CameraCreate", true, expected == actual && SameBytes(scratch, self, sizeof(scratch)), "");
    return actual;
}
void __fastcall DestroyVerify(CCamera* self) {
    CameraBytes scratch;
    std::memcpy(scratch, self, sizeof(scratch));
    s_destroy(reinterpret_cast<CCamera*>(scratch));
    CameraDestroy(self);
    ei::verify::Check("CameraDestroy", true, SameBytes(scratch, self, sizeof(scratch)), "");
}

// 0040D0A0: SUB ESP,0xC (3), PUSH ESI (1), PUSH EDI (1). Side by side on a copy.
int(__fastcall* s_init)(CCamera*, void*, CameraSettings*, std::uint32_t, std::uint32_t, std::uint32_t);
int __fastcall InitVerify(CCamera* self, void*, CameraSettings* settings, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    CameraBytes scratch;
    std::memcpy(scratch, self, sizeof(scratch));
    int expected = s_init(reinterpret_cast<CCamera*>(scratch), nullptr, settings, a, b, c);
    int actual = self->Init(settings, a, b, c);
    ei::verify::Check("CCamera::Init", true, expected == actual && SameBytes(scratch, self, sizeof(scratch)), "");
    return actual;
}

// 0040D380: MOV EDX,[ESP+4] (4), MOV EAX,[ECX+0x2E0] (6).
void(__fastcall* s_range)(const CCamera*, void*, std::uint32_t*, std::uint32_t*, std::uint32_t*);
void __fastcall RangeVerify(const CCamera* self, void*, std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) {
    std::uint32_t ea, eb, ec;
    s_range(self, nullptr, &ea, &eb, &ec);
    self->GetRange(a, b, c);
    ei::verify::Check("CameraGetRange", true, ea == *a && eb == *b && ec == *c, "");
}

// 0040FB20: TEST byte [ECX+8],0x60 (4), LEA EAX,[ECX+0x1C] (3).
const Vector3*(__fastcall* s_anchor)(const CCamera*);
const Vector3* __fastcall AnchorVerify(const CCamera* self) {
    const Vector3* expected = s_anchor(self);
    const Vector3* actual = CameraAnchor(self);
    ei::verify::Check("CameraAnchor", true, expected == actual, "");
    return actual;
}

int __fastcall CloseFileVerify(CCamera* self) {
    int result = CameraCloseFile(self);
    ei::verify::Check("CameraCloseFile", true, result == 1 && !(self->flags_ & 0x10), "");
    return result;
}
int __fastcall OpenFileVerify(CCamera* self, void*, const char* name) {
    int result = self->OpenFile(name);
    ei::verify::Check("CameraOpenFile", true, result == 0 || (result == 1 && (self->flags_ & 0x10)), "");
    return result;
}
int __fastcall RecordFrameVerify(CCamera* self, void*, std::uint32_t time) {
    bool recording = (self->flags_ & 0x10) != 0;
    int result = self->RecordFrame(time);
    ei::verify::Check("CameraRecordFrame", true, recording || result == 0, "");
    return result;
}
void __fastcall LoadStateVerify(CCamera* self, void*, CStream* stream) {
    self->LoadState(stream);
    ei::verify::Check("CCamera::LoadState", true, true, "");
}
void __fastcall SaveStateVerify(CCamera* self, void*, CStream* stream) {
    self->SaveState(stream);
    ei::verify::Check("CCamera::SaveState", true, true, "");
}

void* __fastcall RecordsAllocateVerify(int count) {
    void* start = CameraRecordsAllocate(count);
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(start);
    std::uintptr_t block;
    std::memcpy(&block, static_cast<std::uint8_t*>(start) - 4, 4);
    ei::verify::Check("CameraRecordsAllocate", true, (a & 0x1F) == 0 && block <= a - 4 && a - block <= 0x23, "%d", count);
    return start;
}
} // namespace

EI_VERIFY(0x0040CB20, EngineCtorVerify, 10, s_engineCtor);
EI_VERIFY(0x0040CB70, TransformDestructVerify, 0, s_unused);
EI_VERIFY(0x0040CB50, TransformDeleteVerify, 0, s_unused);
EI_VERIFY(0x0040C8B0, CameraCtorVerify, 7, s_cameraCtor);
EI_VERIFY(0x0040CE60, CameraDestructVerify, 0, s_unused);
EI_VERIFY(0x0040CE10, CameraDeleteVerify, 0, s_unused);
EI_VERIFY(0x0040CDF0, ClassNameVerify, 5, s_className);
EI_VERIFY(0x0040CE00, ClassIdVerify, 5, s_classId);
EI_VERIFY(0x0040E900, ReturnsOneVerify, 5, s_returnsOne);
EI_VERIFY(0x0040D190, CreateVerify, 10, s_create);
EI_VERIFY(0x0040D1D0, DestroyVerify, 10, s_destroy);
EI_VERIFY(0x0040D0A0, InitVerify, 5, s_init);
EI_VERIFY(0x0040D380, RangeVerify, 10, s_range);
EI_VERIFY(0x0040FB20, AnchorVerify, 7, s_anchor);
EI_VERIFY(0x0040ECB0, CloseFileVerify, 0, s_unused);
EI_VERIFY(0x0040E910, OpenFileVerify, 0, s_unused);
EI_VERIFY(0x0040EB60, RecordFrameVerify, 0, s_unused);
EI_VERIFY(0x00410AB0, LoadStateVerify, 0, s_unused);
EI_VERIFY(0x00410C10, SaveStateVerify, 0, s_unused);
EI_VERIFY(0x00412120, RecordsAllocateVerify, 0, s_unused);
