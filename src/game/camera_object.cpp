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
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

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

Camera::Camera() {
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
    nearDistance = 1.0f;
    farDistance = 2000.0f;
    fieldOfView = DefaultAngle();
}
void Camera::Destroy() { targetId = -1; }

// Init(settings, near, far, fov). Takes the views and the start distance and focus from the settings, keeps the
// three distances / angle as float bit patterns, switches to free placement, and resets views 6 and 7 to the
// start distance at 45 degrees (the pitch too). Returns 1.
int Camera::Init(options::GameOptions* newSettings, std::uint32_t nearBits, std::uint32_t farBits, std::uint32_t fovBits) {
    camera::ForgetViewStep();
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
const Vector3* CameraAnchor(const Camera* self) {
    return (self->flags & Camera::kPlaced) ? &self->localPosition : &self->focus;
}

// Starts recording into ".\camera\" + `name`. When ".\camera\camera.cam" exists it is
// first renamed to the next numbered file: the number after the highest found, where each found
// name's number is read from its title past the fourth character ("ra0005": always 0, so the
// rename goes to camera0001.cam; kept as the game has it). Sets the client world's
// WorldClientFlag::RecordingCamera either way. Returns 1 (file name kept in fileName, flag 0x10 set) when the file can be created, else 0.
int Camera::OpenFile(const char* name) {
    platform::FileFind find;
    platform::File file;
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
        platform::RenameFile(kCurrentRecord, path.c_str());
    }
    g_clientWorld.flags |= WorldClientFlag::RecordingCamera;
    bool failed;
    {
        std::string folder = kRecordFolder;
        std::string full = folder + name;
        failed = !file.Open(full, platform::File::modeCreate | platform::File::modeWrite);
    }
    if (failed) return 0;
    {
        std::string folder = kRecordFolder;
        std::string full = folder + name;
        fileName = full;
    }
    flags |= Camera::kRecording;
    file.Close();
    return 1;
}

// While recording (flag 0x10), appends a frame {time, -, local position, local rotation} to fileName. Returns 1
// when written, 0 when not recording or the file does not open.
int Camera::RecordFrame(std::uint32_t time) {
    CameraKey record;
    platform::File file;
    if (!(flags & Camera::kRecording)) return 0;
    if (!file.Open(fileName, platform::File::modeNoTruncate | platform::File::modeWrite)) return 0;    // (an existing file only)
    file.Seek(0, platform::File::end);
    record.time = std::bit_cast<float>(time);
    record.reserved = 0;
    record.position = localPosition;
    record.rotation = localRotation;
    file.WriteValue(record);
    file.Close();
    return 1;
}

// Closes the camera file: flag 0x10 off, the file name emptied. Returns 1.
int CameraCloseFile(Camera* self) {
    self->flags &= ~Camera::kRecording;
    self->fileName.clear();
    return 1;
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
void Camera::Save(ByteStream* stream) {
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

// --- Camera motion -----------------------------------------------------------------------------------

// Camera helpers the per-frame update uses: aiming along a direction, turning the camera upright towards its
// target and keeping it above nearby objects. See camera.h and docs/notes/core.md. Floating point: float maths in
// the original's order (its x87 registers held floats at the main thread's 24-bit precision); an operation on the
// CRT's double results through lib/pc24.h.

// Aims the camera along (dx, dy, dz): the view heading is the turn about Z that points the camera's X axis
// along the horizontal part (identity when that is straight along Y), then the local rotation tilts by -(90
// degrees + elevation) about its own X axis. False (and nothing done) for a direction shorter than 1e-9.
bool Camera::LookDirection(float dx, float dy, float dz) {
    float lengthSquare = LengthSquaredUnrounded<Sum3::XYZ, float>(dx, dy, dz);
    EI_NAN_AUDIT(lengthSquare);
    if (lengthSquare <= CAM_LOOK_EPSILON) return false;   // the double constant (exact as a compare)
    float inv = 1.0f / std::sqrt(lengthSquare);
    float nx = inv * dx;
    float ny = inv * dy;
    float elevation = static_cast<float>(libcrt::CiAsin(-(inv * dz)));
    // ny over _hypot's double rounds once at 24 bits; the CRT's arc cosine returns a double.
    float heading = static_cast<float>(libcrt::CiAcos(pc24::Div(ny, libcrt::Hypot(nx, ny))));
    EI_NAN_AUDIT(nx, heading);
    if (nx == CAM_ZERO || heading == CAM_ZERO) {
        viewRotation = camera::kIdentityRotation;
    } else {
        float side = -nx;
        float z;
        EI_NAN_AUDIT(side);
        if (side < CAM_ZERO) z = CAM_MINUS_ONE;
        else if (side > CAM_ZERO) z = CAM_ONE;
        else z = CAM_ZERO;
        Vector3 axis;
        axis.x = 0.0f;
        axis.y = 0.0f;
        axis.z = z;
        viewRotation.FromAxisAngle(axis, heading);
    }
    localRotation = viewRotation;
    const Quaternion& q = localRotation;
    float xx = q.x * q.x;
    float e1 = Dot2DUnrounded<float>(q.w, q.x, q.z, q.y);
    float e2 = q.x * q.z - q.w * q.y;
    Vector3 xAxis;
    xAxis.x = (q.w * q.w - (LengthSquared2DUnrounded<float>(q.y, q.z) + xx)) + (xx + xx);
    xAxis.y = e1 + e1;
    xAxis.z = e2 + e2;
    float angle = -(CAM_RIGHT_ANGLE + elevation);
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, xAxis);
    Quaternion& t = localRotation;
    float x = ((t.z * r.y - t.y * r.z) + r.w * t.x) + t.w * r.x;
    float y = ((t.x * r.z - t.z * r.x) + r.w * t.y) + t.w * r.y;
    float zz = ((t.y * r.x - r.y * t.x) + r.w * t.z) + t.w * r.z;
    float w = r.w * t.w - DotUnrounded<Sum3::ZYX, float>(r.x, r.y, r.z, t.x, t.y, t.z);
    t.z = zz;
    t.y = y;
    t.w = w;
    t.x = x;
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
    float hz = 0.0f;
    float inv = CAM_ONE / std::sqrt(hy * hy + zAxis.y * zAxis.y);
    float hx = inv * zAxis.y;
    hy = hy * inv;
    float dot = DotUnrounded<Sum3::ZYX, float>(xAxis.x, xAxis.y, xAxis.z, hx, hy, hz);
    float cosine = dot;
    EI_NAN_AUDIT(dot);
    if (dot > CAM_ONE) cosine = 1.0f;
    else if (cosine < CAM_MINUS_ONE) cosine = -1.0f;
    float angle = static_cast<float>(libcrt::CiAcos(cosine));
    float magnitude = AbsBits(angle);
    if (CAM_DEGREE > magnitude) return;
    float cx = xAxis.y * hz - xAxis.z * hy;
    float cy = xAxis.z * hx - hz * xAxis.x;
    float cz = hy * xAxis.x - xAxis.y * hx;
    float invC = CAM_ONE / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(cx, cy, cz));
    Vector3 axis;
    axis.x = cx * invC;
    axis.y = cy * invC;
    axis.z = cz * invC;
    float axisLength = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(axis));
    if (axisLength > CAM_ALIGN_EPSILON) {   // the double constant (exact as a compare)
        Quaternion turn;
        const Quaternion& r = *turn.SetAngleAxis(angle * CAM_TENTH, axis);
        float x = ((r.y * q.z - q.y * r.z) + q.x * r.w) + r.x * q.w;
        float y = ((q.x * r.z - r.x * q.z) + q.y * r.w) + r.y * q.w;
        float z = ((q.y * r.x - r.y * q.x) + r.w * q.z) + r.z * q.w;
        float w = r.w * q.w - DotUnrounded<Sum3::ZXY, float>(q.x, q.y, q.z, r.x, r.y, r.z);
        q.z = z;
        q.y = y;
        q.w = w;
        q.x = x;
    }
    ClampAboveObjects();
}

// Keeps the camera above the terrain and the objects within 16 of it: an object (all
// extents up to 100) counts with its top height, fully when the camera is inside its footprint
// radius and fading out over the next 10; the camera height becomes at least the highest of these
// (raised over the highest terrain by at most settings->cameraMaxHeight) plus settings->cameraMinHeight.
void Camera::ClampAboveObjects() {
    Vector3* p = &localPosition;
    const float highest = SampleTerrain(*p, 1.0f, 2.0f, 2.0f).maximum;
    std::vector<Object3D*> list = g_clientWorld.ObjectsInRange(*p, 16.0f);
    float best = 0.0f;
    for (auto& node : list) {
        const Object3D* object = node;
        const float limit = CAM_OBJECT_EXTENT_LIMIT;
        if (object->bounds.boxMax.x > limit || object->bounds.boxMax.y > limit || object->bounds.boxMax.z > limit ||
            object->bounds.center.z > limit)
            continue;
        float cx = object->localPosition.x + object->bounds.center.x;
        float cy = object->localPosition.y + object->bounds.center.y;
        float cz = p->z;
        double radius = libcrt::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y);
        float dx = cx - p->x;
        float dy = cy - p->y;
        float dz = cz - p->z;
        // The distance less _hypot's double, rounded once at 24 bits.
        float outside = pc24::Sub(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz)), radius);
        float factor = CAM_ZERO;
        EI_NAN_AUDIT(outside);
        if (outside <= CAM_ZERO) factor = CAM_ONE;
        else if (outside <= CAM_OBJECT_FADE) factor = CAM_ONE - outside * CAM_TENTH;
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
        float z = p->z;
        raised = MaxKeepB(z, wanted);
    } else {
        float top = MaxKeepB(best, highest);
        float wanted = top + s->cameraMinHeight;
        float z = p->z;
        raised = MaxKeepB(z, wanted);
    }
    p->z = raised;
}

// --- Camera path load --------------------------------------------------------------------------------

// The camera's recorded path file load: the file open is redirected to a mod folder's copy
// (CameraPatch, mod_folders.h). Its callers (the camera commands' glue) reach it through its address. Main thread.
// See docs/notes/core.md.

namespace {
// The gap in the keys' time that ends the lead-in.
constexpr float kLeadInGap = 500.0f;
} // namespace

int Camera::LoadPath(const char* name, int settle) {
    platform::File file;
    int opened;
    {
        std::string folder = g_dataFolder;
        std::string path = folder + "camera\\" + name;
        if (ei::rebirth::Enabled(ei::rebirth::Patch::CameraPatch)) {
            // CameraPatch: the mod's Camera\ copy when there is one.
            opened = file.Open(modfolders::CameraFile(path.c_str()), platform::File::modeRead);
        } else {
            opened = file.Open(path, platform::File::modeRead);
        }
    }
    if (!opened) return 0;

    // As many keys as the file holds whole; the array is resized as the engine's aligned arrays
    // are (the old keys kept when it grows) and filled with one read.
    keys.resize(static_cast<int>(file.GetLength() / sizeof(CameraKey)));
    file.Read(keys.data(), static_cast<unsigned>(std::ssize(keys)) * sizeof(CameraKey));

    if (settle != 0 && std::ssize(keys) > settle && settle > 1) {
        // The first key (of the first `settle`) more than the gap after its predecessor: every key
        // before it takes its position and rotation (the times stay).
        for (int first = 1; first < settle; ++first) {
            EI_NAN_AUDIT(keys[first].time, keys[first - 1].time);
            if (keys[first].time - keys[first - 1].time <= kLeadInGap) continue;
            for (int i = 0; i < first; ++i) {
                keys[i].position = keys[first].position;
                keys[i].rotation = keys[first].rotation;
            }
            break;
        }
    }
    file.Close();
    return 1;
}
