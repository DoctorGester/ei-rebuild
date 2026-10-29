// The camera object itself: construction and destruction, its small virtuals, Init, and saving / loading
// its state with the world. See camera.h and docs/notes/core.md.
#include "camera.h"
#include "core_streams.h"
#include "save_versions.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/gameapi.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace {


// The default pitch (degrees as radians: pi / 180 * 45) and the default field of view
// computed at run time as the game does.
float DefaultPitch() { return static_cast<float>(CAM_DEGREE * CAM_FORTY_FIVE); }
float DefaultAngle() { return static_cast<float>(CAM_PI * CAM_TWO_SEVENTHS); }

const char kCurrentRecord[] = ".\\camera\\camera.cam";
const char kNumberedRecords[] = ".\\camera\\camera0*.cam";
const char kNumberedRecord[] = ".\\camera\\camera%.4d.cam";
const char kRecordFolder[] = ".\\camera\\";

} // namespace

CCamera::CCamera() {
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation.SetIdentity();
    viewRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    for (CameraView& view : views) {
        view.distance = 30.0f;
        view.rotation = camera::kIdentityRotation;
        view.pitch = DefaultPitch();
    }
    panVelocity = camera::kZeroVector;
    panTarget = camera::kZeroVector;
    panResponse = 2.0f / 15.0f;
    turnResponse = 1.0f / 3.0f;
    tiltResponse = 1.0f / 3.0f;
    zoomResponse = 1.0f / 3.0f;
    turnRate = 0.0f;
    turnTarget = 0.0f;
    tiltRate = 0.0f;
    focus.x = 10.0f;
    tiltTarget = 0.0f;
    zoomRate = 0.0f;
    focus.y = 10.0f;
    zoomTarget = 0.0f;
    focusHeight = 2.0f;
    focus.z = 2.0f;
    viewRotation = camera::kIdentityRotation;
    targetId = -1;
    nearDistance = 1.0f;
    farDistance = 2000.0f;
    fieldOfView = DefaultAngle();
}

CCamera::~CCamera() {
    targetId = -1;
}

// The camera: constructed by the C++ runtime and destroyed after the game's exit functions (the worlds' destructors
// do not touch it: EmptyWorlds, app_main.cpp, has emptied them by then).
CCamera g_camera;

const char* CCamera::ClassName() { return "CCamera"; }
ClassId CCamera::GetClassId() { return ClassId::Camera; }

void CCamera::Create() {
    targetId = -1;
    nearDistance = 1.0f;
    farDistance = 2000.0f;
    fieldOfView = DefaultAngle();
}
void CCamera::Destroy() { targetId = -1; }

// Init(settings, near, far, fov). Takes the views and the start distance and focus from the settings, keeps the
// three distances / angle as float bit patterns, switches to free placement, and resets views 6 and 7 to the
// start distance at 45 degrees (the pitch too). Returns 1.
int CCamera::Init(options::GameOptions* newSettings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits) {
    ViewsFromSettings(newSettings);
    settings = newSettings;
    distance = newSettings->cameraDefaultDistance;
    focus.x = newSettings->cameraDefaultXDeploy;
    focus.y = newSettings->cameraDefaultYDeploy;
    focus.z = newSettings->cameraDefaultHeight;
    nearDistance = std::bit_cast<float>(nearBits);
    farDistance = std::bit_cast<float>(farBits);
    fieldOfView = std::bit_cast<float>(fovBits);
    flags |= kFree;
    views[7].distance = newSettings->cameraDefaultDistance;
    pitch = DefaultPitch();
    views[7].pitch = pitch;
    views[6].distance = newSettings->cameraDefaultDistance;
    pitch = DefaultPitch();
    views[6].pitch = pitch;
    return 1;
}

// The point the camera is placed by: its own position when playing a file or placed freely, else the focus.
const Vector3* CameraAnchor(const CCamera* self) {
    return (self->flags & CCamera::kPlaced) ? &self->localPosition : &self->focus;
}

// Starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from its title past the fourth character ("ra0005": always 0, so the
// rename goes to camera0001.cam; kept as the game has it). Sets the client world's
// WorldClientFlag::RecordingCamera either way. Returns 1 (file name kept in fileName, flag 0x10 set) when the file can be created, else 0.
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
                int number = str::ToInt(title);
                if (number > last) last = number;
            } while (more);
        }
        std::string path;
        path = str::Format(kNumberedRecord, last + 1);
        gameapi::MoveFileA(kCurrentRecord, path.c_str());
    }
    g_clientWorld.flags |= WorldClientFlag::RecordingCamera;
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
        fileName = full;
    }
    flags |= CCamera::kRecording;
    file.Close();
    return 1;
}

// While recording (flag 0x10), appends a frame {time, -, local position, local rotation} to fileName. Returns 1
// when written, 0 when not recording or the file does not open.
int CCamera::RecordFrame(std::uint32_t time) {
    CameraKey record;
    lib::File file;
    if (!(flags & CCamera::kRecording)) return 0;
    if (!file.Open(fileName, lib::File::modeNoTruncate | lib::File::modeWrite)) return 0;    // (an existing file only)
    file.Seek(0, FILE_END);
    record.time = std::bit_cast<float>(time);
    record.reserved = 0;
    record.position = localPosition;
    record.rotation = localRotation;
    file.WriteValue(record);
    file.Close();
    return 1;
}

// Closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int CameraCloseFile(CCamera* self) {
    self->flags &= ~CCamera::kRecording;
    self->fileName.clear();
    return 1;
}

// Loads the camera state saved by Save. Saves before version 0x6F hold all eight
// views, later ones views 4..7. A followed target is kept when one is saved (its offsets reset).
void CCamera::Load(CStream* stream) {
    stream->ReadValue(focus);
    stream->ReadValue(focusHeight);
    stream->ReadValue(viewRotation);
    stream->ReadValue(pitch);
    stream->ReadValue(distance);
    stream->Read(&panVelocity, 0x1C);
    stream->Read(&turnRate, 12);
    stream->Read(&tiltRate, 12);
    stream->Read(&zoomRate, 12);
    std::uint32_t savedFlags = 0;
    stream->ReadValue(savedFlags);
    int target = 0;
    stream->ReadValue(target);
    if (static_cast<int>(stream->version) < kVersionCameraViews) stream->Read(views, 0xC0);
    else stream->Read(&views[4], 0x60);
    if (static_cast<std::uint8_t>(savedFlags) & kFollowing) {
        if (target != -1) flags |= kFollowing;
        else flags &= ~kFollowing;
    }
    targetId = target;
    if (target != -1) {
        panTarget = camera::kZeroVector;
        panVelocity = camera::kZeroVector;
    }
}

// Saves the camera state (focus, rotation, pitch, distance, the rates, flags, target and views 4..7).
void CCamera::Save(CStream* stream) {
    stream->WriteValue(focus);
    stream->WriteValue(focusHeight);
    stream->WriteValue(viewRotation);
    stream->WriteValue(pitch);
    stream->WriteValue(distance);
    stream->Write(&panVelocity, 0x1C);
    stream->Write(&turnRate, 12);
    stream->Write(&tiltRate, 12);
    stream->Write(&zoomRate, 12);
    stream->WriteValue(flags);
    stream->WriteValue(targetId);
    stream->Write(&views[4], 0x60);
}
