// The camera object itself: construction and destruction, its small vtable slots (vtable
// 0073B9E0), Init, and saving / loading its state with the world. The transform base's destructor
// and the engine object's inlined constructor live here too.
// See core_transform.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "core_transform.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
double GameDouble(std::uintptr_t a) { return *reinterpret_cast<const volatile double*>(a); }

constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;
constexpr std::uintptr_t kTransformVtable = 0x0073BA38;
constexpr std::uintptr_t kCameraVtable = 0x0073B9E0;

void SetVtable(void* object, std::uintptr_t v) { std::memcpy(object, &v, 4); }
std::uint8_t* Bytes(void* p) { return static_cast<std::uint8_t*>(p); }
void Put32(void* object, std::size_t offset, std::uint32_t v) { std::memcpy(Bytes(object) + offset, &v, 4); }
std::uint32_t Get32(const void* object, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(object) + offset, 4);
    return v;
}
void Copy16(void* to, std::uintptr_t from) { std::memcpy(to, reinterpret_cast<const void*>(from), 16); }
void Copy12(void* to, std::uintptr_t from) { std::memcpy(to, reinterpret_cast<const void*>(from), 12); }
constexpr std::uintptr_t kIdentityRotation = 0x0079B5B8;
constexpr std::uintptr_t kZeroVector = 0x0079B5C8;

// The default pitch (degrees as radians: pi / 180 * 45) and the default field value at +0x2E8
// (pi * 2/7), computed at run time as the original does.
float DefaultPitch() { return Store((GameDouble(0x0073B9A0) * GameDouble(0x0073BA30)) * GameDouble(0x0073BA28)); }
float DefaultAngle() { return Store(GameDouble(0x0073B9A0) * GameDouble(0x0073B9D8)); }

// The camera's containers (STLport, library): the shake list and the deque of saved view stack
// entries (0x24-byte elements, 14 per node).
__declspec(naked) void __fastcall ShakeListConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00411660)
__declspec(naked) void __fastcall ShakeNodeFree(void* /*node*/, int /*n*/) EI_ORIGINAL(0x00411B30)
__declspec(naked) void __fastcall ViewStackConstruct(void* /*deque*/, void*, const void* /*alloc*/, int /*n*/) EI_ORIGINAL(0x00411AB0)
struct DequeIterator {
    void* cur;
    void* first;
    void* last;
    void** node;
};
__declspec(naked) void __stdcall ViewStackDestroyRange(DequeIterator /*first*/, DequeIterator /*last*/) EI_ORIGINAL(0x00411DF0)
__declspec(naked) void __fastcall ViewStackFreeNode(void* /*alloc*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00411E20)
__declspec(naked) void __fastcall ViewStackFreeMap(void** /*map*/, int /*size*/) EI_ORIGINAL(0x00411EA0)

// The world stream (slot 1 Read, slot 2 Write; +8 the save version).
void Read(void* stream, void* to, unsigned size) { ei::VCall<int>(stream, 1, to, size); }
void Write(void* stream, const void* from, unsigned size) { ei::VCall<int>(stream, 2, from, size); }

} // namespace

// 0040CB20: the engine object's constructor body (inlined in most classes): vtable 0073B8E0,
// id -1, everything else zero.
CEngineObject* __fastcall EngineObjectConstruct(CEngineObject* self) {
    SetVtable(self, kEngineObjectVtable);
    self->refCount_ = 0;
    self->id_ = -1;
    self->unknown_18 = 0;
    self->type_ = 0;
    self->flags_ = 0;
    self->owner_ = nullptr;
    return self;
}
EI_REPLACE(0x0040CB20, EngineObjectConstruct);

// 0040CB70: CTransformObject's destructor body; 0040CB50 its deleting destructor (slot 0).
void __fastcall TransformDestruct(CTransformObject* self) {
    SetVtable(self, kTransformVtable);
    self->DestructBase();
}
EI_REPLACE(0x0040CB70, TransformDestruct);
CTransformObject* __fastcall TransformDeletingDestructor(CTransformObject* self, void*, unsigned flags) {
    TransformDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0040CB50, TransformDeletingDestructor);

// 0040C8B0: the constructor. Identity rotations, eight saved views (distance 30, pitch 45 degrees),
// the shake list and view stack, no camera file, the focus at (10, 10, 2) two above the ground,
// response rates 2/15 (pan) and 1/3, no target.
CCamera* CCamera::Construct() {
    EngineObjectConstruct(this);
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    SetVtable(this, kTransformVtable);
    viewRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    std::uint8_t alloc = 0;
    ShakeListConstruct(&shakes_, nullptr, &alloc);
    for (CameraView& view : views_) {
        Put32(&view.distance, 0, 0x41F00000);   // 30.0f
        Copy16(&view.rotation, kIdentityRotation);
        view.pitch = DefaultPitch();
    }
    ViewStackConstruct(Bytes(this) + 0x1E0, nullptr, &alloc, 0);
    Put32(this, 0x2EC, *reinterpret_cast<const std::uint32_t*>(0x00796E48));   // empty CString
    Put32(this, 0x2F0, 0);
    Put32(this, 0x2FC, 0);
    Put32(this, 0x2F8, 0);
    Put32(this, 0x2F4, 0);
    SetVtable(this, kCameraVtable);
    Copy12(&offset98_, kZeroVector);
    Copy12(&offsetA4_, kZeroVector);
    Put32(&panResponse_, 0, 0x3E088889);
    Put32(&turnResponse_, 0, 0x3EAAAAAB);
    Put32(&tiltResponse_, 0, 0x3EAAAAAB);
    Put32(&zoomResponse_, 0, 0x3EAAAAAB);
    turnRate_ = 0.0f;
    turnTarget_ = 0.0f;
    tiltRate_ = 0.0f;
    Put32(&focus_.x, 0, 0x41200000);   // 10
    tiltTarget_ = 0.0f;
    zoomRate_ = 0.0f;
    Put32(&focus_.y, 0, 0x41200000);
    unknownD0_ = 0;
    Put32(&focusHeight_, 0, 0x40000000);   // 2
    Put32(&focus_.z, 0, 0x40000000);
    Copy16(&viewRotation_, kIdentityRotation);
    targetId_ = -1;
    Put32(this, 0x2E0, 0x3F800000);   // 1
    Put32(this, 0x2E4, 0x44FA0000);   // 2000
    float angle = DefaultAngle();
    std::memcpy(Bytes(this) + 0x2E8, &angle, 4);
    return this;
}
EI_REPLACE_METHOD(0x0040C8B0, CCamera::Construct);

// 0040CE60: the destructor body: no target, the camera path keys and file name freed, the view
// stack and shake list destroyed, then the transform base.
void CCamera::Destruct() {
    SetVtable(this, kCameraVtable);
    targetId_ = -1;
    if (keys_) gamecrt::operator_delete(reinterpret_cast<void**>(keys_)[-1]);   // block start kept before the keys
    reinterpret_cast<mfc::CString*>(Bytes(this) + 0x2EC)->~CString();
    std::uint8_t* stack = Bytes(this) + 0x1E0;
    DequeIterator first, last;
    std::memcpy(&last, stack + 0x10, sizeof(last));
    std::memcpy(&first, stack, sizeof(first));
    ViewStackDestroyRange(first, last);
    if (Get32(stack, 0x20)) {
        void** endNode = reinterpret_cast<void**>(Get32(stack, 0x1C)) + 1;
        for (void** node = reinterpret_cast<void**>(Get32(stack, 0x0C)); node < endNode; ++node)
            ViewStackFreeNode(stack + 0x24, nullptr, *node, 0xE);
        ViewStackFreeMap(reinterpret_cast<void**>(Get32(stack, 0x20)), static_cast<int>(Get32(stack, 0x24)));
    }
    auto** sentinel = reinterpret_cast<CameraShake**>(&shakes_);
    for (CameraShake* node = (*sentinel)->next; node != *sentinel;) {
        CameraShake* next = node->next;
        ShakeNodeFree(node, 1);
        node = next;
    }
    (*sentinel)->next = *sentinel;
    (*sentinel)->prev = *sentinel;
    ShakeNodeFree(*sentinel, 1);
    SetVtable(this, kTransformVtable);
    DestructBase();
}
EI_REPLACE_METHOD(0x0040CE60, CCamera::Destruct);

// 0040CE10 (slot 0): the deleting destructor.
CCamera* __fastcall CameraDeletingDestructor(CCamera* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0040CE10, CameraDeletingDestructor);

// Slots 11 / 12 (class name "CCamera", class id 0x43), slot 2 Create, slot 3 Destroy and the slot
// that only returns 1 (0040E900).
const char* __fastcall CameraClassName(const CCamera*) { return reinterpret_cast<const char*>(0x00783CF8); }
int __fastcall CameraClassId(const CCamera*) { return 0x43; }
int __fastcall CameraReturnsOne(CCamera*) { return 1; }
int __fastcall CameraCreate(CCamera* self) {
    self->targetId_ = -1;
    Put32(self, 0x2E0, 0x3F800000);
    Put32(self, 0x2E4, 0x44FA0000);
    float angle = DefaultAngle();
    std::memcpy(Bytes(self) + 0x2E8, &angle, 4);
    return 1;
}
void __fastcall CameraDestroy(CCamera* self) { self->targetId_ = -1; }
EI_REPLACE(0x0040CDF0, CameraClassName);
EI_REPLACE(0x0040CE00, CameraClassId);
EI_REPLACE(0x0040E900, CameraReturnsOne);
EI_REPLACE(0x0040D190, CameraCreate);
EI_REPLACE(0x0040D1D0, CameraDestroy);

// 0040D0A0: Init(settings, a, b, c). Takes the views and the start distance and focus from the
// settings, keeps a / b / c at +0x2E0..+0x2E8, switches to free placement, and resets views 6 and 7
// to the start distance at 45 degrees (the pitch too). Returns 1.
int CCamera::Init(CameraSettings* settings, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    ViewsFromSettings(settings);
    settings_ = settings;
    const std::uint8_t* s = reinterpret_cast<const std::uint8_t*>(settings);
    distance_ = *reinterpret_cast<const float*>(s + 0x120);
    focus_.x = *reinterpret_cast<const float*>(s + 0x108);
    focus_.y = *reinterpret_cast<const float*>(s + 0x10C);
    focus_.z = *reinterpret_cast<const float*>(s + 0x104);
    Put32(this, 0x2E0, a);
    Put32(this, 0x2E4, b);
    Put32(this, 0x2E8, c);
    flags_ |= kFree;
    std::memcpy(&views_[7].distance, s + 0x120, 4);
    pitch_ = DefaultPitch();
    views_[7].pitch = pitch_;
    std::memcpy(&views_[6].distance, s + 0x120, 4);
    pitch_ = DefaultPitch();
    views_[6].pitch = pitch_;
    return 1;
}
namespace {
int __fastcall InitHook(CCamera* self, void*, CameraSettings* settings, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    return self->Init(settings, a, b, c);
}
} // namespace
EI_REPLACE(0x0040D0A0, InitHook);

// 0040D380: the three values at +0x2E0..+0x2E8.
void __fastcall CameraGetRange(const CCamera* self, void*, std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) {
    *a = Get32(self, 0x2E0);
    *b = Get32(self, 0x2E4);
    *c = Get32(self, 0x2E8);
}
EI_REPLACE(0x0040D380, CameraGetRange);

// 0040FB20: the point the camera is placed by: its own position when playing a file or placed
// freely, else the focus.
const Vector3* __fastcall CameraAnchor(const CCamera* self) {
    return (self->flags_ & 0x60) ? &self->localPosition_ : &self->focus_;
}
EI_REPLACE(0x0040FB20, CameraAnchor);

// 0040ECB0: closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int __fastcall CameraCloseFile(CCamera* self) {
    self->flags_ &= ~0x10u;
    reinterpret_cast<mfc::CString*>(Bytes(self) + 0x2EC)->Empty();
    return 1;
}
EI_REPLACE(0x0040ECB0, CameraCloseFile);

// 00410AB0: loads the camera state saved by SaveState. Saves before version 0x6F hold all eight
// views, later ones views 4..7. A followed target is kept when one is saved (its offsets reset).
void CCamera::LoadState(void* stream) {
    Read(stream, &focus_, 12);
    Read(stream, &focusHeight_, 4);
    Read(stream, &viewRotation_, 16);
    Read(stream, &pitch_, 4);
    Read(stream, &distance_, 4);
    Read(stream, &offset98_, 0x1C);
    Read(stream, &turnRate_, 12);
    Read(stream, &tiltRate_, 12);
    Read(stream, &zoomRate_, 12);
    std::uint32_t savedFlags = 0;
    Read(stream, &savedFlags, 4);
    int target = 0;
    Read(stream, &target, 4);
    if (static_cast<int>(Get32(stream, 8)) < 0x6F) Read(stream, views_, 0xC0);
    else Read(stream, &views_[4], 0x60);
    if (static_cast<std::uint8_t>(savedFlags) & kFollowing) {
        if (target != -1) flags_ |= kFollowing;
        else flags_ &= ~kFollowing;
    }
    targetId_ = target;
    if (target != -1) {
        Copy12(&offsetA4_, kZeroVector);
        Copy12(&offset98_, kZeroVector);
    }
}

// 00410C10: saves the camera state (focus, rotation, pitch, distance, the rates, flags, target and
// views 4..7).
void CCamera::SaveState(void* stream) {
    std::uint32_t flags = flags_;
    Write(stream, &focus_, 12);
    std::uint32_t value = Get32(this, 0xEC);   // dword copies of focusHeight_, pitch_, distance_
    Write(stream, &value, 4);
    Write(stream, &viewRotation_, 16);
    value = Get32(this, 0x104);
    Write(stream, &value, 4);
    value = Get32(this, 0xF0);
    Write(stream, &value, 4);
    Write(stream, &offset98_, 0x1C);
    Write(stream, &turnRate_, 12);
    Write(stream, &tiltRate_, 12);
    Write(stream, &zoomRate_, 12);
    Write(stream, &flags, 4);
    int target = targetId_;
    Write(stream, &target, 4);
    Write(stream, &views_[4], 0x60);
}
namespace {
void __fastcall LoadStateHook(CCamera* self, void*, void* stream) { self->LoadState(stream); }
void __fastcall SaveStateHook(CCamera* self, void*, void* stream) { self->SaveState(stream); }
} // namespace
EI_REPLACE(0x00410AB0, LoadStateHook);
EI_REPLACE(0x00410C10, SaveStateHook);
