// The camera object itself: construction and destruction, its small virtual slots, Init, and saving / loading its state with the world. The transform base's destructor
// is tagged here too.
// See camera.h and docs/notes/core.md.
#include "camera.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "world.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {


// The default pitch (degrees as radians: pi / 180 * 45) and the default field of view
// (pi * 2/7), computed at run time as the original does.
float DefaultPitch() { return static_cast<float>(CAM_DEGREE * CAM_FORTY_FIVE); }
float DefaultAngle() { return static_cast<float>(CAM_PI * CAM_TWO_SEVENTHS); }


} // namespace

// 0040CB70 (deleting 0040CB50): CTransformObject's destructor, which is CEngineObject's.
EI_REWRITTEN_SPECIAL(0x0040CB70, CTransformObject::~CTransformObject);
EI_REWRITTEN_SPECIAL(0x0040CB50, CTransformObject::~CTransformObject);   // deleting

// The constructor. Identity rotations, eight saved views (distance 30, pitch 45 degrees),
// the shake list and view stack, no camera file, the focus at (10, 10, 2) two above the ground,
// response rates 2/15 (pan) and 1/3, no target.
CCamera::CCamera() {
    ClearFields();
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    viewRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    shakes_.Construct();
    for (CameraView& view : views_) {
        view.distance = 30.0f;
        std::memcpy(&view.rotation, &camera::kIdentityRotation, sizeof(Quaternion));
        view.pitch = DefaultPitch();
    }
    viewStack_.Construct(0);
    keys_.data = nullptr;
    keys_.growBy = 0;
    keys_.capacity = 0;
    keys_.size = 0;
    std::memcpy(&offset98_, &camera::kZeroVector, sizeof(Vector3));
    std::memcpy(&offsetA4_, &camera::kZeroVector, sizeof(Vector3));
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
    zoomTarget_ = 0;
    focusHeight_ = 2.0f;
    focus_.z = 2.0f;
    std::memcpy(&viewRotation_, &camera::kIdentityRotation, sizeof(Quaternion));
    targetId_ = -1;
    nearDistance_ = 1.0f;
    farDistance_ = 2000.0f;
    fieldOfView_ = DefaultAngle();
}
EI_REWRITTEN_SPECIAL(0x0040C8B0, CCamera::CCamera);

// The destructor: no target, the camera path keys freed, the view stack and shake list
// destroyed (the file name, a member, goes after them: the original freed it before the view stack).
CCamera::~CCamera() {
    targetId_ = -1;
    if (keys_.data) lib::FreeAligned32(keys_.data);   // block start kept before the keys
    viewStack_.Destroy();   // the nodes, then the map
    shakes_.Destroy();      // the nodes, then the sentinel
}
EI_REWRITTEN_SPECIAL(0x0040CE60, CCamera::~CCamera);
EI_REWRITTEN_SPECIAL(0x0040CE10, CCamera::~CCamera);

// The camera, zero at the start: built by the static initialiser 0040C880 in the list's order and destroyed by the
// exit function it registers; both ported on the name and registered at their addresses
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CCamera> g_camera = /*0079B2A8*/ {};
static_assert(sizeof(CCamera) == 0x308);
CCamera* TheCamera() { return &*g_camera; }

namespace {
void __cdecl DestroyCamera() { g_camera.Destroy(); }
void __cdecl ConstructCamera() {
    g_camera.Construct();
    libcrt::AtExit(DestroyCamera);
}
} // namespace

EI_REPLACE(0x0040C880, ConstructCamera);
EI_REPLACE(0x0040C8A0, DestroyCamera);

const char* CCamera::GetClassName() { return "CCamera"; }
EI_REWRITTEN(0x0040CDF0, CCamera::GetClassName);
int CCamera::GetClassId() { return 0x43; }
EI_REWRITTEN(0x0040CE00, CCamera::GetClassId);
EI_REWRITTEN(0x0040E900, CCamera::RunTick);

int CCamera::Create() {
    targetId_ = -1;
    nearDistance_ = 1.0f;
    farDistance_ = 2000.0f;
    fieldOfView_ = DefaultAngle();
    return 1;
}
EI_REWRITTEN(0x0040D190, CCamera::Create);
void CCamera::Destroy() { targetId_ = -1; }
EI_REWRITTEN(0x0040D1D0, CCamera::Destroy);

// Init(settings, near, far, fov). Takes the views and the start distance and focus from
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
EI_REWRITTEN(0x0040D0A0, CCamera::Init);

// The near distance, far distance and field of view (as dwords).
void CCamera::GetRange(std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) const {
    std::memcpy(a, &nearDistance_, 4);
    std::memcpy(b, &farDistance_, 4);
    std::memcpy(c, &fieldOfView_, 4);
}
EI_REWRITTEN(0x0040D380, CCamera::GetRange);

// The point the camera is placed by: its own position when playing a file or placed
// freely, else the focus.
const Vector3* __fastcall CameraAnchor(const CCamera* self) {
    return (self->flags_ & CCamera::kPlaced) ? &self->localPosition_ : &self->focus_;
}
EI_REWRITTEN(0x0040FB20, CameraAnchor);

namespace {

const char kCurrentRecord[] = ".\\camera\\camera.cam";
const char kNumberedRecords[] = ".\\camera\\camera0*.cam";
const char kNumberedRecord[] = ".\\camera\\camera%.4d.cam";
const char kRecordFolder[] = ".\\camera\\";

} // namespace

// Starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from its title past the fourth character ("ra0005": always 0, so the
// rename goes to camera0001.cam; kept as the original has it). Sets bit 0x10 of 007AF5C0 either
// way. Returns 1 (file name kept at +0x2EC, flag 0x10 set) when the file can be created, else 0.
int CCamera::OpenFile(const char* name) {
    mfc::CFileFind find;
    mfc::CFile file;
    if (find.FindFile(kCurrentRecord, 0) == 1) {
        int last = 0;
        if (find.FindFile(kNumberedRecords, 0)) {
            int more;
            do {
                more = find.FindNext();
                std::string title = find.GetTitle().GetString();
                {
                    std::string tail = str::Right(title, 4);
                    title = tail;
                }
                int number = std::atoi(title.c_str());
                if (number > last) last = number;
            } while (more);
        }
        std::string path;
        path = str::Format(kNumberedRecord, last + 1);
        gameapi::MoveFileA(kCurrentRecord, path.c_str());
    }
    TheClientWorld()->flags_ |= 0x10;
    bool failed;
    {
        std::string folder = kRecordFolder;
        std::string full = folder + name;
        failed = file.Open(full.c_str(), 0x1001, nullptr) == 0;   // modeCreate | modeWrite
    }
    if (failed) return 0;
    {
        std::string folder = kRecordFolder;
        std::string full = folder + name;
        fileName_ = full;
    }
    flags_ |= CCamera::kRecording;
    file.Close();
    return 1;
}
EI_REWRITTEN(0x0040E910, CCamera::OpenFile);

// While recording (flag 0x10), appends a frame {time, -, local position, local rotation}
// to the file named at +0x2EC. Returns 1 when written, 0 when not recording or the file does not
// open.
int CCamera::RecordFrame(std::uint32_t time) {
    CameraKey record;
    mfc::CFile file;
    if (!(flags_ & CCamera::kRecording)) return 0;
    if (!file.Open(fileName_, 0x2001, nullptr)) return 0;   // modeNoTruncate | modeWrite
    file.Virtuals()->Seek(0, 2);   // Seek(0, end), through slot 10
    std::memcpy(&record.time, &time, sizeof(record.time));
    record.reserved = 0;
    std::memcpy(&record.position, &localPosition_, sizeof(record.position));
    std::memcpy(&record.rotation, &localRotation_, sizeof(record.rotation));
    file.Write(&record, sizeof(record));
    file.Close();
    return 1;
}
EI_REWRITTEN(0x0040EB60, CCamera::RecordFrame);

// Closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int __fastcall CameraCloseFile(CCamera* self) {
    self->flags_ &= ~CCamera::kRecording;
    self->fileName_.Empty();
    return 1;
}
EI_REWRITTEN(0x0040ECB0, CameraCloseFile);

// Loads the camera state saved by Save. Saves before version 0x6F hold all eight
// views, later ones views 4..7. A followed target is kept when one is saved (its offsets reset).
void CCamera::Load(CStream* stream) {
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
        std::memcpy(&offsetA4_, &camera::kZeroVector, sizeof(Vector3));
        std::memcpy(&offset98_, &camera::kZeroVector, sizeof(Vector3));
    }
}
EI_REWRITTEN(0x00410AB0, CCamera::Load);

// Saves the camera state (focus, rotation, pitch, distance, the rates, flags, target and
// views 4..7).
void CCamera::Save(CStream* stream) {
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
EI_REWRITTEN(0x00410C10, CCamera::Save);
