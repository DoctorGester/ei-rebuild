// Differential checks for the camera (src/game/camera.cpp). Most camera methods write only the
// camera's own fields: the original runs on a scratch copy of the camera's first bytes, ours on
// the real camera, and the bytes are compared. Exceptions are noted at each wrapper.
#include "verify.h"

#include "../../src/game/camera.h"

#include <cstring>

namespace {

// Bytes of the camera the functions read or write: below +0x120 (motion, focus, view, target),
// up to the saved views (+0x1E0), or up to the playback state (+0x308).
constexpr std::size_t kMotion = 0x120;
constexpr std::size_t kViews = 0x1E0;
constexpr std::size_t kPlayback = 0x308;

template <std::size_t N>
struct Scratch {
    alignas(8) unsigned char bytes[N];
    explicit Scratch(const CCamera* c) { std::memcpy(bytes, c, N); }
    CCamera* get() { return reinterpret_cast<CCamera*>(bytes); }
    bool Same(const CCamera* c) const { return std::memcmp(bytes, c, N) == 0; }
};

// First differing dword (for the log).
template <std::size_t N>
unsigned FirstDiff(const Scratch<N>& s, const CCamera* c) {
    for (unsigned i = 0; i < N; i += 4)
        if (std::memcmp(s.bytes + i, reinterpret_cast<const unsigned char*>(c) + i, 4) != 0) return i;
    return N;
}

template <std::size_t N>
void Report(const char* name, const Scratch<N>& expected, const CCamera* self) {
    unsigned at = FirstDiff(expected, self);
    std::uint32_t a = 0, b = 0;
    if (at < N) {
        std::memcpy(&a, expected.bytes + at, 4);
        std::memcpy(&b, reinterpret_cast<const unsigned char*>(self) + at, 4);
    }
    ei::verify::Check(name, true, at == N, "+%03X %08X vs %08X", at, a, b);
}

// 0040D9B0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x1C (3). Outputs only: ours into scratch.
void(__stdcall* s_sample)(const Vector3*, const Vector3*, const Vector3*, float, float, float, float*, float*, float*);
void __stdcall SampleVerify(const Vector3* p, const Vector3* u1, const Vector3* u2, float step, float halfX,
                            float halfY, float* average, float* minimum, float* maximum) {
    float actual[3];
    SampleTerrain(p, u1, u2, step, halfX, halfY, &actual[0], &actual[1], &actual[2]);
    s_sample(p, u1, u2, step, halfX, halfY, average, minimum, maximum);
    float expected[3] = {*average, *minimum, *maximum};
    ei::verify::Check("SampleTerrain", true, std::memcmp(expected, actual, sizeof(actual)) == 0,
                      "(%g %g) step %g: %g %g %g vs %g %g %g", p->x, p->y, step, expected[0], expected[1], expected[2],
                      actual[0], actual[1], actual[2]);
}

// 0040E460: FLD [ESP+8] (4), FMUL [ECX+0x2C] (3). Output only.
Vector3*(__fastcall* s_curve)(const CubicCurve3*, void*, Vector3*, float);
Vector3* __fastcall CurveVerify(const CubicCurve3* self, void*, Vector3* out, float t) {
    Vector3 actual;
    Vector3* ret = self->Evaluate(&actual, t);
    Vector3* expectedRet = s_curve(self, nullptr, out, t);
    ei::verify::Check("CubicCurve3::Evaluate", true,
                      ret == &actual && expectedRet == out && std::memcmp(out, &actual, sizeof(actual)) == 0, "t %g", t);
    return expectedRet;
}

// 0040F1B0: MOV EAX,[ECX+0x2F4] (6).
int(__fastcall* s_playStart)(CCamera*, void*);
int __fastcall PlayStartVerify(CCamera* self, void*) {
    Scratch<kPlayback> expected(self);
    int expectedRet = s_playStart(expected.get(), nullptr);
    int ret = self->PlaybackStart();
    Report("CCamera::PlaybackStart", expected, self);
    ei::verify::Check("CCamera::PlaybackStart result", expectedRet, ret, "");
    return ret;
}

// 0040EFB0: SUB ESP,0x1C (3), PUSH EBX (1), MOV EBX,[ECX+8] (3).
int(__fastcall* s_playStep)(CCamera*, void*, float);
int __fastcall PlayStepVerify(CCamera* self, void*, float time) {
    Scratch<kPlayback> expected(self);
    int expectedRet = s_playStep(expected.get(), nullptr, time);
    int ret = self->PlaybackStep(time);
    Report("CCamera::PlaybackStep", expected, self);
    ei::verify::Check("CCamera::PlaybackStep result", expectedRet, ret, "time %g", time);
    return ret;
}

// 0040F230: SUB ESP,0x24 (3), PUSH ESI (1), PUSH EDI (1).
void(__fastcall* s_pan)(CCamera*, void*, const float*);
void __fastcall PanVerify(CCamera* self, void*, const float* direction) {
    Scratch<kMotion> expected(self);
    s_pan(expected.get(), nullptr, direction);
    self->Pan(direction);
    Report("CCamera::Pan", expected, self);
}

// 0040F430: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x2C (3).
void(__fastcall* s_stop)(CCamera*, void*);
void __fastcall StopVerify(CCamera* self, void*) {
    Scratch<kMotion> expected(self);
    s_stop(expected.get(), nullptr);
    self->StopMotion();
    Report("CCamera::StopMotion", expected, self);
}

// 0040F5C0: MOV EAX,[ESP+4] (4), SUB ESP,8 (3). `p` may point into the camera: it is read
// before the copy is changed, so the original on the scratch copy sees the same values.
void(__fastcall* s_focus)(CCamera*, void*, const Vector3*);
void __fastcall FocusVerify(CCamera* self, void*, const Vector3* p) {
    Vector3 at = *p;
    Scratch<kMotion> expected(self);
    s_focus(expected.get(), nullptr, &at);
    self->SetFocus(p);
    Report("CCamera::SetFocus", expected, self);
}

// 0040F8B0: SUB ESP,0x30 (3), PUSH EBX (1), PUSH EBP (1). Its callee 0040E4C0 reads the camera's
// settings pointer and position and writes the height only.
void(__fastcall* s_apply)(CCamera*, void*);
void __fastcall ApplyVerify(CCamera* self, void*) {
    Scratch<kMotion> expected(self);
    s_apply(expected.get(), nullptr);
    self->ApplyView();
    Report("CCamera::ApplyView", expected, self);
}

// 0040FB30: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x30 (3). The shake list is shared with the
// scratch copy and the original erases finished shakes from it, so while shakes are running ours
// runs alone. The sound listener is set twice with the same values.
void(__fastcall* s_update)(CCamera*, void*, float);
void __fastcall UpdateVerify(CCamera* self, void*, float dt) {
    if (self->shakes_->next != self->shakes_) {
        self->Update(dt);
        return;
    }
    Scratch<kMotion> expected(self);
    s_update(expected.get(), nullptr, dt);
    self->Update(dt);
    Report("CCamera::Update", expected, self);
}

// 004107A0: FLD qword [0x0073BAD8] (6).
void(__fastcall* s_zoom)(CCamera*, void*, float);
void __fastcall ZoomVerify(CCamera* self, void*, float steps) {
    Scratch<kMotion> expected(self);
    s_zoom(expected.get(), nullptr, steps);
    self->ZoomBy(steps);
    Report("CCamera::ZoomBy", expected, self);
}

// The settings' view records (+0x178, 0x60 bytes), which ViewsToSettings writes.
struct SettingsViews {
    unsigned char bytes[sizeof(CameraViewRecord) * 4];
};
SettingsViews Snapshot(const CameraSettings* s) {
    SettingsViews v;
    std::memcpy(v.bytes, s->views, sizeof(v.bytes));
    return v;
}
void Restore(CameraSettings* s, const SettingsViews& v) { std::memcpy(s->views, v.bytes, sizeof(v.bytes)); }
bool Same(const CameraSettings* s, const SettingsViews& v) { return std::memcmp(s->views, v.bytes, sizeof(v.bytes)) == 0; }

// 00410D70: MOV EAX,[ESP+4] (4), SUB ESP,0xC (3). Writes the settings record: the original runs
// first, its result is kept and the record restored for ours.
void(__fastcall* s_toSettings)(CCamera*, void*, CameraSettings*);
void __fastcall ToSettingsVerify(CCamera* self, void*, CameraSettings* settings) {
    SettingsViews before = Snapshot(settings);
    Scratch<kViews> expected(self);
    s_toSettings(expected.get(), nullptr, settings);
    SettingsViews expectedViews = Snapshot(settings);
    Restore(settings, before);
    self->ViewsToSettings(settings);
    ei::verify::Check("CCamera::ViewsToSettings", true, Same(settings, expectedViews) && expected.Same(self), "");
}

// 00410860: MOV EAX,[ESP+4] (4), CMP EAX,8 (3). Also writes the settings record (as above).
bool(__fastcall* s_save)(CCamera*, void*, unsigned);
bool __fastcall SaveVerify(CCamera* self, void*, unsigned slot) {
    CameraSettings* settings = self->settings_;
    SettingsViews before;
    if (settings) before = Snapshot(settings);
    Scratch<kViews> expected(self);
    bool expectedRet = s_save(expected.get(), nullptr, slot);
    SettingsViews expectedViews;
    if (settings) {
        expectedViews = Snapshot(settings);
        Restore(settings, before);
    }
    bool ret = self->SaveView(slot);
    Report("CCamera::SaveView", expected, self);
    ei::verify::Check("CCamera::SaveView settings", true, !settings || Same(settings, expectedViews), "slot %u", slot);
    ei::verify::Check("CCamera::SaveView result", expectedRet, ret, "slot %u", slot);
    return ret;
}

// 004108D0: MOV EAX,[ESP+4] (4), CMP EAX,8 (3).
bool(__fastcall* s_restore)(CCamera*, void*, unsigned);
bool __fastcall RestoreVerify(CCamera* self, void*, unsigned slot) {
    Scratch<kViews> expected(self);
    bool expectedRet = s_restore(expected.get(), nullptr, slot);
    bool ret = self->RestoreView(slot);
    Report("CCamera::RestoreView", expected, self);
    ei::verify::Check("CCamera::RestoreView result", expectedRet, ret, "slot %u", slot);
    return ret;
}

// 00411470: MOV EAX,[ESP+4] (4), MOV EDX,[EAX] (2).
void(__fastcall* s_distPitch)(CCamera*, void*, const CameraView*);
void __fastcall DistPitchVerify(CCamera* self, void*, const CameraView* view) {
    CameraView v = *view;
    Scratch<kMotion> expected(self);
    s_distPitch(expected.get(), nullptr, &v);
    self->SetDistancePitch(view);
    Report("CCamera::SetDistancePitch", expected, self);
}

// 00410D10: MOV EAX,[ESP+4] (4), SUB ESP,0xC (3).
void(__fastcall* s_fromSettings)(CCamera*, void*, const CameraSettings*);
void __fastcall FromSettingsVerify(CCamera* self, void*, const CameraSettings* settings) {
    Scratch<kViews> expected(self);
    s_fromSettings(expected.get(), nullptr, settings);
    self->ViewsFromSettings(settings);
    Report("CCamera::ViewsFromSettings", expected, self);
}

// 004109E0: SUB ESP,0xC (3), MOV EAX,[ESP+0x10] (4).
void(__fastcall* s_heading)(CCamera*, void*, float);
void __fastcall HeadingVerify(CCamera* self, void*, float angle) {
    Scratch<kMotion> expected(self);
    s_heading(expected.get(), nullptr, angle);
    self->SetHeading(angle);
    Report("CCamera::SetHeading", expected, self);
}

// 00410E50: MOV AL,[ESP+4] (4), TEST AL,AL (2).
void(__fastcall* s_free)(CCamera*, void*, bool);
void __fastcall FreeVerify(CCamera* self, void*, bool on) {
    Scratch<kMotion> expected(self);
    s_free(expected.get(), nullptr, on);
    self->SetFree(on);
    Report("CCamera::SetFree", expected, self);
}

// 00411060: SUB ESP,0xC (3), MOV EAX,[ECX+8] (3). Its callee 004110D0 writes the rotations only.
void(__fastcall* s_lookFrom)(CCamera*, void*, const Vector3*, const Vector3*, int);
void __fastcall LookFromVerify(CCamera* self, void*, const Vector3* from, const Vector3* to, int unused) {
    Vector3 f = *from, t = *to;
    Scratch<kMotion> expected(self);
    s_lookFrom(expected.get(), nullptr, &f, &t, unused);
    self->LookFrom(from, to, unused);
    Report("CCamera::LookFrom", expected, self);
}

// 00411360: MOV EDX,[ESP+4] (4), PUSH ESI (1).
void(__fastcall* s_bounds)(CCamera*, void*, const Vector3*, float);
void __fastcall BoundsVerify(CCamera* self, void*, const Vector3* center, float radius) {
    Vector3 c = *center;
    Scratch<kMotion> expected(self);
    s_bounds(expected.get(), nullptr, &c, radius);
    self->SetBounds(center, radius);
    Report("CCamera::SetBounds", expected, self);
}

// 004113A0: SUB ESP,0x10 (3), PUSH ESI (1), MOV ESI,ECX (2). Adds a node to the shake list: the
// original runs on a scratch camera whose list is a private empty one, the node it adds is read
// and erased again (back to the node allocator), then ours runs on the real list and its new last
// node must hold the same values.
__declspec(naked) void __fastcall ShakeEraseNode(CameraShake* const*, void*, CameraShake**, CameraShake*)
    EI_ORIGINAL(0x00411540)
void(__fastcall* s_shake)(CCamera*, void*, const Vector3*, float, float, float);
void __fastcall ShakeVerify(CCamera* self, void*, const Vector3* source, float duration, float amplitude,
                            float decay) {
    Vector3 at = *source;
    CameraShake sentinel{};
    sentinel.next = sentinel.prev = &sentinel;
    Scratch<kMotion> scratch(self);
    scratch.get()->shakes_ = &sentinel;
    s_shake(scratch.get(), nullptr, &at, duration, amplitude, decay);
    bool expectedAdded = sentinel.next != &sentinel;
    float expected[4] = {};
    if (expectedAdded) {
        CameraShake* node = sentinel.next;
        std::memcpy(expected, &node->elapsed, sizeof(expected));
        CameraShake* const list = &sentinel;
        CameraShake* next;
        ShakeEraseNode(&list, nullptr, &next, node);
    }
    CameraShake* lastBefore = self->shakes_->prev;
    self->Shake(source, duration, amplitude, decay);
    CameraShake* last = self->shakes_->prev;
    bool added = last != lastBefore;
    bool same = added == expectedAdded && (!added || std::memcmp(expected, &last->elapsed, sizeof(expected)) == 0);
    ei::verify::Check("CCamera::Shake", true, same, "added %d/%d", expectedAdded ? 1 : 0, added ? 1 : 0);
}

} // namespace

EI_VERIFY(0x0040D9B0, SampleVerify, 6, s_sample);
EI_VERIFY(0x0040E460, CurveVerify, 7, s_curve);
EI_VERIFY(0x0040F1B0, PlayStartVerify, 6, s_playStart);
EI_VERIFY(0x0040EFB0, PlayStepVerify, 7, s_playStep);
EI_VERIFY(0x0040F230, PanVerify, 5, s_pan);
EI_VERIFY(0x0040F430, StopVerify, 6, s_stop);
EI_VERIFY(0x0040F5C0, FocusVerify, 7, s_focus);
EI_VERIFY(0x0040F8B0, ApplyVerify, 5, s_apply);
EI_VERIFY(0x0040FB30, UpdateVerify, 6, s_update);
EI_VERIFY(0x004107A0, ZoomVerify, 6, s_zoom);
EI_VERIFY(0x00410D70, ToSettingsVerify, 7, s_toSettings);
EI_VERIFY(0x00410860, SaveVerify, 7, s_save);
EI_VERIFY(0x004108D0, RestoreVerify, 7, s_restore);
EI_VERIFY(0x00411470, DistPitchVerify, 6, s_distPitch);
EI_VERIFY(0x00410D10, FromSettingsVerify, 7, s_fromSettings);
EI_VERIFY(0x004109E0, HeadingVerify, 7, s_heading);
EI_VERIFY(0x00410E50, FreeVerify, 6, s_free);
EI_VERIFY(0x00411060, LookFromVerify, 6, s_lookFrom);
EI_VERIFY(0x00411360, BoundsVerify, 5, s_bounds);
EI_VERIFY(0x004113A0, ShakeVerify, 6, s_shake);
