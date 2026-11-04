// The camera object itself: construction and destruction, its small virtuals, Init, and saving / loading
// its state with the world. See camera.h and docs/notes/core.md.
#include "camera.h"

#include "core_streams.h"
#include "game_singletons.h"
#include "map_object_figures.h"
#include "mod_folders.h"
#include "save_versions.h"
#include "world.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

// The default pitch: 45 degrees in radians.
constexpr float kDefaultPitch = ei::kRadiansPerDegree * 45.0f;
constexpr float kLookEpsilon = 1e-18f;   // squared length
constexpr float kAlignEpsilon = 0.1f;
constexpr float kObjectExtentLimit = 100.0f;
constexpr float kObjectFade = 10.0f;

// A save holds views 4..7, each as its 24 bytes (distance, rotation, pitch); older saves all eight.
constexpr int kSavedViews = 4;
static_assert(sizeof(CameraView) == 0x18);

const char kCurrentRecord[] = ".\\camera\\camera.cam";
const char kNumberedRecords[] = ".\\camera\\camera0*.cam";
const char kNumberedRecord[] = ".\\camera\\camera%.4d.cam";
const char kRecordFolder[] = ".\\camera\\";

} // namespace

Camera::Camera() {
    localRotation = camera::kIdentityRotation;
    worldRotation.SetIdentity();
    for (CameraView& view : views) view = {30.0f, camera::kIdentityRotation, kDefaultPitch};
}

Camera::~Camera() {
    targetId = -1;
}

// The camera: constructed by the C++ runtime and destroyed after the game's exit functions (the worlds' destructors
// do not touch it: EmptyWorlds, app_main.cpp, has emptied them by then).
Camera g_camera;

const char* Camera::ClassName() { return "CCamera"; }
ClassId Camera::GetClassId() { return ClassId::Camera; }

void Camera::Create() {
    targetId = -1;
    nearDistance = camera::kDefaultNear;
    farDistance = camera::kDefaultFar;
    fieldOfView = camera::kDefaultFieldOfView;
}
void Camera::Destroy() { targetId = -1; }

// Takes the views and the start distance and focus from the settings and places the camera at that focus, keeps
// the near and far distances and the field of view, switches to free placement, and resets views 6 and 7 to the
// start distance at 45 degrees (the pitch too).
void Camera::Init(options::GameOptions* newSettings, float nearClip, float farClip, float fov) {
    camera::ForgetViewStep();
    ViewsFromSettings(newSettings);
    settings = newSettings;
    distance = newSettings->cameraDefaultDistance;
    focus = {newSettings->cameraDefaultXDeploy, newSettings->cameraDefaultYDeploy, newSettings->cameraDefaultHeight};
    localPosition = focus;
    nearDistance = nearClip;
    farDistance = farClip;
    fieldOfView = fov;
    flags |= kFree;
    pitch = kDefaultPitch;
    for (int slot : {6, 7}) {
        views[slot].distance = newSettings->cameraDefaultDistance;
        views[slot].pitch = pitch;
    }
}

// Starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from the last four characters of its title ("camera0005": 5). Sets the client world's
// WorldClientFlag::RecordingCamera either way. True (file name kept in fileName, flag 0x10 set) when the file can be
// created.
bool Camera::OpenFile(const char* name) {
    platform::File file;
    if (platform::PathExists(kCurrentRecord)) {
        int last = 0;
        for (const platform::FolderEntry& entry : platform::FolderEntries(kNumberedRecords)) {
            const int number = str::ToInt(str::Right(entry.Title(), 4));
            if (number > last) last = number;
        }
        platform::RenameFile(kCurrentRecord, str::Format(kNumberedRecord, last + 1).c_str());
    }
    g_clientWorld.flags |= WorldClientFlag::RecordingCamera;
    const std::string full = std::string(kRecordFolder) + name;
    if (!file.Open(full, platform::FileMode::Create)) return false;
    fileName = full;
    flags |= Camera::kRecording;
    file.Close();
    return true;
}

// While recording (flag 0x10), appends a frame {time, -, local position, local rotation} to fileName; nothing when
// the file does not open.
void Camera::RecordFrame(float time) {
    platform::File file;
    if (!(flags & Camera::kRecording)) return;
    if (!file.Open(fileName, platform::FileMode::WriteExisting)) return;    // (an existing file only)
    file.Seek(0, platform::SeekFrom::End);
    const CameraKey record = {time, 0, localPosition, localRotation};
    file.WriteValue(record);
    file.Close();
}

// Closes the camera file: flag 0x10 off, the file name emptied.
void Camera::CloseFile() {
    flags &= ~kRecording;
    fileName.clear();
}

// Loads the camera state saved by Save. Saves before version 0x6F hold all eight
// views, later ones views 4..7. A followed target is kept when one is saved (its offsets reset).
void Camera::Load(ByteStream* stream) {
    camera::ForgetViewStep();
    stream->ReadValue(focus);
    stream->ReadValue(focusHeight);
    stream->ReadValue(viewRotation);
    stream->ReadValue(pitch);
    stream->ReadValue(distance);
    stream->ReadValue(panVelocity);
    stream->ReadValue(panTarget);
    stream->ReadValue(panResponse);
    stream->ReadValue(turnRate);
    stream->ReadValue(turnTarget);
    stream->ReadValue(turnResponse);
    stream->ReadValue(tiltRate);
    stream->ReadValue(tiltTarget);
    stream->ReadValue(tiltResponse);
    stream->ReadValue(zoomRate);
    stream->ReadValue(zoomTarget);
    stream->ReadValue(zoomResponse);
    std::uint32_t savedFlags = 0;
    stream->ReadValue(savedFlags);
    int target = 0;
    stream->ReadValue(target);
    if (stream->Version() < kVersionCameraViews) stream->ReadValue(views);
    else stream->Read(&views[4], kSavedViews * sizeof(CameraView));
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
void Camera::Save(ByteStream* stream) {
    stream->WriteValue(focus);
    stream->WriteValue(focusHeight);
    stream->WriteValue(viewRotation);
    stream->WriteValue(pitch);
    stream->WriteValue(distance);
    stream->WriteValue(panVelocity);
    stream->WriteValue(panTarget);
    stream->WriteValue(panResponse);
    stream->WriteValue(turnRate);
    stream->WriteValue(turnTarget);
    stream->WriteValue(turnResponse);
    stream->WriteValue(tiltRate);
    stream->WriteValue(tiltTarget);
    stream->WriteValue(tiltResponse);
    stream->WriteValue(zoomRate);
    stream->WriteValue(zoomTarget);
    stream->WriteValue(zoomResponse);
    stream->WriteValue(flags);
    stream->WriteValue(targetId);
    stream->Write(&views[4], kSavedViews * sizeof(CameraView));
}

// --- Camera motion -----------------------------------------------------------------------------------

// Camera helpers the per-frame update uses: aiming along a direction, turning the camera upright towards its
// target and keeping it above nearby objects. See camera.h and docs/notes/core.md.

// Aims the camera along (dx, dy, dz): the view heading is the turn about Z that points the camera's X axis
// along the horizontal part (identity when that is straight along Y), then the local rotation tilts by -(90
// degrees + elevation) about its own X axis. False (and nothing done) for a direction shorter than 1e-9.
bool Camera::LookDirection(float dx, float dy, float dz) {
    float lengthSquare = LengthSquared(Vector3{dx, dy, dz});
    if (lengthSquare <= kLookEpsilon) return false;
    float inv = 1.0f / std::sqrt(lengthSquare);
    float nx = inv * dx;
    float ny = inv * dy;
    float elevation = lib::Asin(-(inv * dz));
    float heading = lib::Acos(ny / lib::Hypot(nx, ny));
    if (nx == 0.0f || heading == 0.0f) {
        viewRotation = camera::kIdentityRotation;
    } else {
        float side = -nx;
        float z;
        if (side < 0.0f) z = -1.0f;
        else if (side > 0.0f) z = 1.0f;
        else z = 0.0f;
        viewRotation = Quaternion::AxisAngle({0.0f, 0.0f, z}, heading);
    }
    localRotation = viewRotation;
    const Vector3 xAxis = localRotation.AxisX();
    float angle = -(camera::kRightAngle + elevation);
    localRotation = Quaternion::AxisAngle(xAxis, angle) * localRotation;
    return true;
}

// Turns the camera a tenth of the way about its own X-axis-and-horizontal cross product
// so that its X axis lines up with the horizontal direction across the view (Z axis turned a
// quarter about +Z); nothing below one degree. Then lifts the camera over nearby objects.
void Camera::AlignToTarget() {
    Quaternion& q = localRotation;
    const Vector3 zAxis = q.AxisZ();
    const Vector3 xAxis = q.AxisX();
    float hy = -zAxis.x;
    float inv = 1.0f / std::sqrt(hy * hy + zAxis.y * zAxis.y);
    Vector3 h = {inv * zAxis.y, hy * inv, 0.0f};
    float dot = Dot(xAxis, h);
    float cosine = dot;
    if (dot > 1.0f) cosine = 1.0f;
    else if (cosine < -1.0f) cosine = -1.0f;
    float angle = lib::Acos(cosine);
    float magnitude = AbsBits(angle);
    if (ei::kRadiansPerDegree > magnitude) return;
    Vector3 c = Cross(xAxis, h);
    Vector3 axis = c * (1.0f / std::sqrt(LengthSquared(c)));
    float axisLength = std::sqrt(LengthSquared(axis));
    if (axisLength > kAlignEpsilon) q = Quaternion::AxisAngle(axis, angle * 0.1f) * q;
    ClampAboveObjects();
}

// Keeps the camera above the terrain and the objects within 16 of it: an object (all
// extents up to 100) counts with its top height, fully when the camera is inside its footprint
// radius and fading out over the next 10; the camera height becomes at least the highest of these
// (raised over the highest terrain by at most settings->cameraMaxHeight) plus settings->cameraMinHeight.
void Camera::ClampAboveObjects() {
    const Vector3& p = localPosition;
    const float highest = SampleTerrain(p, 1.0f, 2.0f, 2.0f).maximum;
    std::vector<Object3D*> list = g_clientWorld.ObjectsInRange(p, 16.0f);
    float best = 0.0f;
    for (auto& node : list) {
        const Object3D* object = node;
        if (object->bounds.boxMax.x > kObjectExtentLimit || object->bounds.boxMax.y > kObjectExtentLimit ||
            object->bounds.boxMax.z > kObjectExtentLimit || object->bounds.center.z > kObjectExtentLimit)
            continue;
        float cx = object->localPosition.x + object->bounds.center.x;
        float cy = object->localPosition.y + object->bounds.center.y;
        float cz = p.z;
        float radius = lib::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y);
        float dx = cx - p.x;
        float dy = cy - p.y;
        float dz = cz - p.z;
        float outside = std::sqrt(LengthSquared(Vector3{dx, dy, dz})) - radius;
        float factor = 0.0f;
        if (outside <= 0.0f) factor = 1.0f;
        else if (outside <= kObjectFade) factor = 1.0f - outside * 0.1f;
        float height = ((object->localPosition.z + object->bounds.boxMax.z) + object->bounds.center.z) * factor;
        best = MaxKeepB(best, height);
    }
    const options::GameOptions* s = settings;
    float raised;
    if (best > highest) {
        float over = best - highest;
        float limit = s->cameraMaxHeight;
        float used = MinKeepA(over, limit);
        float wanted = (used + highest) + s->cameraMinHeight;
        raised = MaxKeepB(p.z, wanted);
    } else {
        float top = MaxKeepB(best, highest);
        float wanted = top + s->cameraMinHeight;
        raised = MaxKeepB(p.z, wanted);
    }
    localPosition.z = raised;
}

// --- Camera path load --------------------------------------------------------------------------------

namespace {
// The gap in the keys' time that ends the lead-in.
constexpr float kLeadInGap = 500.0f;
} // namespace

bool Camera::LoadPath(const char* name, int settle) {
    platform::File file;
    const std::string path = g_dataFolder + "camera\\" + name;
    // CameraPatch: the mod's Camera\ copy when there is one.
    const bool opened = ei::rebirth::Enabled(ei::rebirth::Patch::CameraPatch)
                            ? file.Open(modfolders::CameraFile(path.c_str()), platform::FileMode::Read)
                            : file.Open(path, platform::FileMode::Read);
    if (!opened) return false;

    // As many keys as the file holds whole; the array is resized as the engine's aligned arrays
    // are (the old keys kept when it grows) and filled with one read.
    keys.resize(static_cast<int>(file.Size() / sizeof(CameraKey)));
    file.Read(keys.data(), static_cast<unsigned>(std::ssize(keys)) * sizeof(CameraKey));

    if (settle != 0 && std::ssize(keys) > settle && settle > 1) {
        // The first key (of the first `settle`) more than the gap after its predecessor: every key
        // before it takes its position and rotation (the times stay).
        for (int first = 1; first < settle; ++first) {
            if (keys[first].time - keys[first - 1].time <= kLeadInGap) continue;
            for (int i = 0; i < first; ++i) {
                keys[i].position = keys[first].position;
                keys[i].rotation = keys[first].rotation;
            }
            break;
        }
    }
    file.Close();
    return true;
}
