// The camera object itself: construction and destruction, its small vtable slots (vtable
// 0073B9E0), Init, and saving / loading its state with the world. The transform base's destructor
// and the engine object's inlined constructor live here too.
// See camera.h and docs/notes/core.md.
#include "camera.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"   // ei::SetVtable
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>

#pragma float_control(precise, on)

namespace {

using camera::Store;

// The default pitch (degrees as radians: pi / 180 * 45) and the default field of view
// (pi * 2/7), computed at run time as the original does.
float DefaultPitch() { return Store((CAM_PI * CAM_PER_180) * CAM_FORTY_FIVE); }
float DefaultAngle() { return Store(CAM_PI * CAM_TWO_SEVENTHS); }


} // namespace

// 0040CB20: the engine object's constructor body (inlined in most classes): vtable 0073B8E0,
// id -1, everything else zero.
CEngineObject* __fastcall EngineObjectConstruct(CEngineObject* self) {
    ei::SetVtable(self, CEngineObject::kVtableAddress);
    self->refCount_ = 0;
    self->id_ = -1;
    self->prototypeId_ = 0;
    self->type_ = 0;
    self->flags_ = 0;
    self->owner_ = nullptr;
    return self;
}
EI_REPLACE(0x0040CB20, EngineObjectConstruct);

// 0040CB70: CTransformObject's destructor body; 0040CB50 its deleting destructor (slot 0).
void __fastcall TransformDestruct(CTransformObject* self) {
    ei::SetVtable(self, CTransformObject::kVtableAddress);
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
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    viewRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    std::uint8_t alloc = 0;
    ShakeListConstruct(&shakes_, nullptr, &alloc);
    for (CameraView& view : views_) {
        view.distance = 30.0f;
        std::memcpy(&view.rotation, &camera::IdentityRotation(), sizeof(Quaternion));
        view.pitch = DefaultPitch();
    }
    ViewStackConstruct(&viewStack_, nullptr, &alloc, 0);
    new (&fileName_) mfc::CString;   // the empty string
    keys_ = nullptr;
    unknown_2FC = 0;
    unknown_2F8 = 0;
    keyCount_ = 0;
    ei::SetVtable(this, kVtableAddress);
    std::memcpy(&offset98_, &camera::ZeroVector(), sizeof(Vector3));
    std::memcpy(&offsetA4_, &camera::ZeroVector(), sizeof(Vector3));
    panResponse_ = 2.0f / 15.0f;
    turnResponse_ = 1.0f / 3.0f;
    tiltResponse_ = 1.0f / 3.0f;
    zoomResponse_ = 1.0f / 3.0f;
    turnRate_ = 0.0f;
    turnTarget_ = 0.0f;
    tiltRate_ = 0.0f;
    focus_.x = 10.0f;
    tiltTarget_ = 0.0f;
    zoomRate_ = 0.0f;
    focus_.y = 10.0f;
    unknownD0_ = 0;
    focusHeight_ = 2.0f;
    focus_.z = 2.0f;
    std::memcpy(&viewRotation_, &camera::IdentityRotation(), sizeof(Quaternion));
    targetId_ = -1;
    nearDistance_ = 1.0f;
    farDistance_ = 2000.0f;
    fieldOfView_ = DefaultAngle();
    return this;
}
EI_REPLACE_METHOD(0x0040C8B0, CCamera::Construct);

// 0040CE60: the destructor body: no target, the camera path keys and file name freed, the view
// stack and shake list destroyed, then the transform base.
void CCamera::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    targetId_ = -1;
    if (keys_) gamecrt::operator_delete(reinterpret_cast<void**>(keys_)[-1]);   // block start kept before the keys
    fileName_.~CString();
    ViewStackDestroyRange(viewStack_.start, viewStack_.finish);
    if (viewStack_.map) {
        void** endNode = viewStack_.finish.node + 1;
        for (void** node = viewStack_.start.node; node < endNode; ++node)
            ViewStackFreeNode(&viewStack_.mapSize, nullptr, *node, 0xE);
        ViewStackFreeMap(viewStack_.map, static_cast<int>(viewStack_.mapSize));
    }
    for (CameraShake* node = shakes_->next; node != shakes_;) {
        CameraShake* next = node->next;
        ShakeNodeFree(node, 1);
        node = next;
    }
    shakes_->next = shakes_;
    shakes_->prev = shakes_;
    ShakeNodeFree(shakes_, 1);
    ei::SetVtable(this, CTransformObject::kVtableAddress);
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
const char* __fastcall CameraClassName(const CCamera*) { return "CCamera"; }
int __fastcall CameraClassId(const CCamera*) { return 0x43; }
int __fastcall CameraReturnsOne(CCamera*) { return 1; }
int __fastcall CameraCreate(CCamera* self) {
    self->targetId_ = -1;
    self->nearDistance_ = 1.0f;
    self->farDistance_ = 2000.0f;
    self->fieldOfView_ = DefaultAngle();
    return 1;
}
void __fastcall CameraDestroy(CCamera* self) { self->targetId_ = -1; }
EI_REPLACE(0x0040CDF0, CameraClassName);
EI_REPLACE(0x0040CE00, CameraClassId);
EI_REPLACE(0x0040E900, CameraReturnsOne);
EI_REPLACE(0x0040D190, CameraCreate);
EI_REPLACE(0x0040D1D0, CameraDestroy);

// 0040D0A0: Init(settings, near, far, fov). Takes the views and the start distance and focus from
// the settings, keeps the three distances / angle (as the dword bit patterns of floats) at
// +0x2E0..+0x2E8, switches to free placement, and resets views 6 and 7 to the start distance at 45
// degrees (the pitch too). Returns 1.
int CCamera::Init(CameraSettings* settings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits) {
    ViewsFromSettings(settings);
    settings_ = settings;
    distance_ = settings->startDistance;
    focus_.x = settings->startFocusX;
    focus_.y = settings->startFocusY;
    focus_.z = settings->startFocusZ;
    std::memcpy(&nearDistance_, &nearBits, 4);
    std::memcpy(&farDistance_, &farBits, 4);
    std::memcpy(&fieldOfView_, &fovBits, 4);
    flags_ |= kFree;
    std::memcpy(&views_[7].distance, &settings->startDistance, 4);
    pitch_ = DefaultPitch();
    views_[7].pitch = pitch_;
    std::memcpy(&views_[6].distance, &settings->startDistance, 4);
    pitch_ = DefaultPitch();
    views_[6].pitch = pitch_;
    return 1;
}
EI_REPLACE_METHOD(0x0040D0A0, CCamera::Init);

// 0040D380: the near distance, far distance and field of view (as dwords).
void __fastcall CameraGetRange(const CCamera* self, void*, std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) {
    std::memcpy(a, &self->nearDistance_, 4);
    std::memcpy(b, &self->farDistance_, 4);
    std::memcpy(c, &self->fieldOfView_, 4);
}
EI_REPLACE(0x0040D380, CameraGetRange);

// 0040FB20: the point the camera is placed by: its own position when playing a file or placed
// freely, else the focus.
const Vector3* __fastcall CameraAnchor(const CCamera* self) {
    return (self->flags_ & CCamera::kPlaced) ? &self->localPosition_ : &self->focus_;
}
EI_REPLACE(0x0040FB20, CameraAnchor);

namespace {

const char kCurrentRecord[] = ".\\camera\\camera.cam";
const char kNumberedRecords[] = ".\\camera\\camera0*.cam";
const char kNumberedRecord[] = ".\\camera\\camera%.4d.cam";
const char kRecordFolder[] = ".\\camera\\";

} // namespace

// 0040E910: starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from its title past the fourth character ("ra0005": always 0, so the
// rename goes to camera0001.cam; kept as the original has it). Sets bit 0x10 of 007AF5C0 either
// way. Returns 1 (file name kept at +0x2EC, flag 0x10 set) when the file can be created, else 0.
int __fastcall CameraOpenFile(CCamera* self, void*, const char* name) {
    mfc::CFileFind find;
    mfc::CFile file;
    if (find.FindFile(kCurrentRecord, 0) == 1) {
        int last = 0;
        if (find.FindFile(kNumberedRecords, 0)) {
            int more;
            do {
                more = CFileFindNext(&find);
                mfc::CString title;
                CFileFindTitle(&find, nullptr, &title);
                {
                    mfc::CString tail = title.Right(4);
                    title = tail;
                }
                int number = GameAtoi(title);
                if (number > last) last = number;
            } while (more);
        }
        mfc::CString path;
        path.Format(kNumberedRecord, last + 1);
        gameapi::MoveFileA(kCurrentRecord, path);
    }
    camera::GlobalFlags() |= 0x10;
    bool failed;
    {
        mfc::CString folder(kRecordFolder);
        mfc::CString full = folder + name;
        failed = file.Open(full, 0x1001, nullptr) == 0;   // modeCreate | modeWrite
    }
    if (failed) return 0;
    {
        mfc::CString folder(kRecordFolder);
        mfc::CString full = folder + name;
        self->fileName_ = full;
    }
    self->flags_ |= CCamera::kRecording;
    file.Close();
    return 1;
}
EI_REPLACE(0x0040E910, CameraOpenFile);

// 0040EB60: while recording (flag 0x10), appends a frame {time, -, local position, local rotation}
// to the file named at +0x2EC. Returns 1 when written, 0 when not recording or the file does not
// open.
int __fastcall CameraRecordFrame(CCamera* self, void*, std::uint32_t time) {
    CameraKey record;
    mfc::CFile file;
    if (!(self->flags_ & CCamera::kRecording)) return 0;
    if (!file.Open(self->fileName_, 0x2001, nullptr)) return 0;   // modeNoTruncate | modeWrite
    reinterpret_cast<mfc::CFileVirtuals*>(&file)->Seek(0, 2);   // Seek(0, end), through slot 10
    std::memcpy(&record.time, &time, sizeof(record.time));
    record.unknown_04 = 0;
    std::memcpy(&record.position, &self->localPosition_, sizeof(record.position));
    std::memcpy(&record.rotation, &self->localRotation_, sizeof(record.rotation));
    CFileWrite(&file, nullptr, &record, sizeof(record));
    file.Close();
    return 1;
}
EI_REPLACE(0x0040EB60, CameraRecordFrame);

// 0040ECB0: closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int __fastcall CameraCloseFile(CCamera* self) {
    self->flags_ &= ~CCamera::kRecording;
    self->fileName_.Empty();
    return 1;
}
EI_REPLACE(0x0040ECB0, CameraCloseFile);

// 00412120: memory for `count` camera records (36 bytes each; the played-back path at +0x2F0,
// sized by 0040ECD0): one record more is allocated and the start aligned to 32 bytes, with the
// block's address kept in the dword before it (0040ECD0 frees through it).
void* __fastcall CameraRecordsAllocate(int count) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count * 9 + 9) * 4));
    auto* start = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    std::memcpy(start - 4, &block, 4);
    return start;
}
EI_REPLACE(0x00412120, CameraRecordsAllocate);

// 00410AB0: loads the camera state saved by SaveState. Saves before version 0x6F hold all eight
// views, later ones views 4..7. A followed target is kept when one is saved (its offsets reset).
void CCamera::LoadState(CStream* stream) {
    stream->Read(&focus_, 12);
    stream->Read(&focusHeight_, 4);
    stream->Read(&viewRotation_, 16);
    stream->Read(&pitch_, 4);
    stream->Read(&distance_, 4);
    stream->Read(&offset98_, 0x1C);
    stream->Read(&turnRate_, 12);
    stream->Read(&tiltRate_, 12);
    stream->Read(&zoomRate_, 12);
    std::uint32_t savedFlags = 0;
    stream->Read(&savedFlags, 4);
    int target = 0;
    stream->Read(&target, 4);
    if (static_cast<int>(stream->version_) < 0x6F) stream->Read(views_, 0xC0);
    else stream->Read(&views_[4], 0x60);
    if (static_cast<std::uint8_t>(savedFlags) & kFollowing) {
        if (target != -1) flags_ |= kFollowing;
        else flags_ &= ~kFollowing;
    }
    targetId_ = target;
    if (target != -1) {
        std::memcpy(&offsetA4_, &camera::ZeroVector(), sizeof(Vector3));
        std::memcpy(&offset98_, &camera::ZeroVector(), sizeof(Vector3));
    }
}
EI_REPLACE_METHOD(0x00410AB0, CCamera::LoadState);

// 00410C10: saves the camera state (focus, rotation, pitch, distance, the rates, flags, target and
// views 4..7).
void CCamera::SaveState(CStream* stream) {
    stream->Write(&focus_, 12);
    stream->Write(&focusHeight_, 4);
    stream->Write(&viewRotation_, 16);
    stream->Write(&pitch_, 4);
    stream->Write(&distance_, 4);
    stream->Write(&offset98_, 0x1C);
    stream->Write(&turnRate_, 12);
    stream->Write(&tiltRate_, 12);
    stream->Write(&zoomRate_, 12);
    stream->Write(&flags_, 4);
    stream->Write(&targetId_, 4);
    stream->Write(&views_[4], 0x60);
}
EI_REPLACE_METHOD(0x00410C10, CCamera::SaveState);
