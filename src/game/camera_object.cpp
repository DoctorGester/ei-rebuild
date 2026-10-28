// The camera object itself: construction and destruction, its small virtual slots, Init, and saving / loading
// its state with the world. See camera.h and docs/notes/core.md.
#include "camera.h"

#include "core_streams.h"

#include "lib/file_io.h"
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
// computed at run time as the game does.
float DefaultPitch() { return static_cast<float>(CAM_DEGREE * CAM_FORTY_FIVE); }
float DefaultAngle() { return static_cast<float>(CAM_PI * CAM_TWO_SEVENTHS); }


} // namespace

CCamera::CCamera() {
    ClearFields();
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    viewRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    for (CameraView& view : views_) {
        view.distance = 30.0f;
        std::memcpy(&view.rotation, &camera::kIdentityRotation, sizeof(Quaternion));
        view.pitch = DefaultPitch();
    }
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

CCamera::~CCamera() {
    targetId_ = -1;
}

// The camera, zero at the start: built by its static initialiser and destroyed by the exit function it registers.
constinit lib::StaticObject<CCamera> g_camera = /*0079B2A8*/ {};
CCamera* TheCamera() { return &*g_camera; }

namespace {
void DestroyCamera() { g_camera.Destroy(); }
} // namespace

void ConstructCamera() {
    g_camera.Construct();
    std::atexit(DestroyCamera);
}

const char* CCamera::GetClassName() { return "CCamera"; }
int CCamera::GetClassId() { return 0x43; }

int CCamera::Create() {
    targetId_ = -1;
    nearDistance_ = 1.0f;
    farDistance_ = 2000.0f;
    fieldOfView_ = DefaultAngle();
    return 1;
}
void CCamera::Destroy() { targetId_ = -1; }

// Init(settings, near, far, fov). Takes the views and the start distance and focus from the settings, keeps the
// three distances / angle as float bit patterns, switches to free placement, and resets views 6 and 7 to the
// start distance at 45 degrees (the pitch too). Returns 1.
int CCamera::Init(options::GameOptions* settings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits) {
    ViewsFromSettings(settings);
    settings_ = settings;
    distance_ = settings->cameraDefaultDistance_;
    focus_.x = settings->cameraDefaultXDeploy_;
    focus_.y = settings->cameraDefaultYDeploy_;
    focus_.z = settings->cameraDefaultHeight_;
    std::memcpy(&nearDistance_, &nearBits, 4);
    std::memcpy(&farDistance_, &farBits, 4);
    std::memcpy(&fieldOfView_, &fovBits, 4);
    flags_ |= kFree;
    std::memcpy(&views_[7].distance, &settings->cameraDefaultDistance_, 4);
    pitch_ = DefaultPitch();
    views_[7].pitch = pitch_;
    std::memcpy(&views_[6].distance, &settings->cameraDefaultDistance_, 4);
    pitch_ = DefaultPitch();
    views_[6].pitch = pitch_;
    return 1;
}

// The point the camera is placed by: its own position when playing a file or placed freely, else the focus.
const Vector3* CameraAnchor(const CCamera* self) {
    return (self->flags_ & CCamera::kPlaced) ? &self->localPosition_ : &self->focus_;
}

namespace {

const char kCurrentRecord[] = ".\\camera\\camera.cam";
const char kNumberedRecords[] = ".\\camera\\camera0*.cam";
const char kNumberedRecord[] = ".\\camera\\camera%.4d.cam";
const char kRecordFolder[] = ".\\camera\\";

} // namespace

// Starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from its title past the fourth character ("ra0005": always 0, so the
// rename goes to camera0001.cam; kept as the game has it). Sets bit 0x10 of a global flags word either
// way. Returns 1 (file name kept in fileName_, flag 0x10 set) when the file can be created, else 0.
int CCamera::OpenFile(const char* name) {
    lib::FileFind find;
    lib::File file;
    if (find.FindFile(kCurrentRecord) == 1) {
        int last = 0;
        if (find.FindFile(kNumberedRecords)) {
            int more;
            do {
                more = find.FindNext();
                std::string title = find.GetTitle();
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
        failed = !file.Open(full, lib::File::modeCreate | lib::File::modeWrite);
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

// While recording (flag 0x10), appends a frame {time, -, local position, local rotation} to fileName_. Returns 1
// when written, 0 when not recording or the file does not open.
int CCamera::RecordFrame(std::uint32_t time) {
    CameraKey record;
    lib::File file;
    if (!(flags_ & CCamera::kRecording)) return 0;
    if (!file.Open(fileName_, lib::File::modeNoTruncate | lib::File::modeWrite)) return 0;   // (an existing file only)
    file.Seek(0, FILE_END);
    std::memcpy(&record.time, &time, sizeof(record.time));
    record.reserved = 0;
    std::memcpy(&record.position, &localPosition_, sizeof(record.position));
    std::memcpy(&record.rotation, &localRotation_, sizeof(record.rotation));
    file.Write(&record, sizeof(record));
    file.Close();
    return 1;
}

// Closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int CameraCloseFile(CCamera* self) {
    self->flags_ &= ~CCamera::kRecording;
    self->fileName_.clear();
    return 1;
}

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

// Saves the camera state (focus, rotation, pitch, distance, the rates, flags, target and views 4..7).
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
