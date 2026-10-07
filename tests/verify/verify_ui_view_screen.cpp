// Checks for interface screen 7 and its picked units' input (ui_view_screen.cpp). Scenario
// ui_view_screen (the harness's "screen 7" from zone1, clicks on the hero and the ground, Ctrl,
// Space twice, a right click).
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/maths.h"
#include "../../src/game/originals_ui.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/game/ui_view_screen.h"
#include "../../src/game/world.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

int InputCount(const CUiContainer* screen) { return static_cast<int>(screen->inputsEnd_ - screen->inputsBegin_); }
void** VtableOf(const UiInputSide* input) { return input ? input->vtable_ : nullptr; }
} // namespace

// --- 0062A830 Build (slot 25): ours only -------------------------------------------------------------
// Three inputs appended (the inert one, the picked units', the camera keys'), the camera not free,
// focus at (30, 30), the rotation premultiplied by the 225-degree turn about x (recomputed here as
// a Hamilton product, CheckNear per component).

static void __fastcall ViewBuildVerify(CViewScreen* self, void*) {
    CCamera* camera = camera::TheCamera();
    Quaternion before = camera->localRotation_;
    int inputs = InputCount(self);
    self->CViewScreen::Build();
    const Quaternion& after = camera->localRotation_;
    double half = 3.141592653589793 / 180.0 * 225.0 / 2.0;
    float w = static_cast<float>(std::cos(half)), x = static_cast<float>(std::sin(half));
    // turn * before with turn = (w, x, 0, 0).
    float ew = w * before.w - x * before.x;
    float ex = w * before.x + x * before.w;
    float ey = w * before.y - x * before.z;
    float ez = w * before.z + x * before.y;
    ei::verify::CheckNear("CViewScreen::Build rotation w", ew, after.w, "");
    ei::verify::CheckNear("CViewScreen::Build rotation x", ex, after.x, "");
    ei::verify::CheckNear("CViewScreen::Build rotation y", ey, after.y, "");
    ei::verify::CheckNear("CViewScreen::Build rotation z", ez, after.z, "");
    bool ok = InputCount(self) == inputs + 3 && !(camera->flags_ & CCamera::kFree) && camera->focus_.x == 30.0f &&
              camera->focus_.y == 30.0f && VtableOf(self->inputsBegin_[inputs + 1]) == UnitPickInput::Vtable();
    ei::verify::Check("CViewScreen::Build", true, ok, "inputs %d->%d flags %X focus (%g, %g)", inputs, InputCount(self),
                      camera->flags_, camera->focus_.x, camera->focus_.y);
}
EI_VERIFY(0x0062A830, ViewBuildVerify, 0, s_unused);

// --- 0062AA50 DrawView (slot 20) / 0062AA40 OnClose (slot 26): ours only -------------------------------

static BOOL __fastcall ViewDrawVerify(CViewScreen* self, void*, int pass) {
    BOOL result = self->CViewScreen::DrawView(pass);
    ei::verify::Check("CViewScreen::DrawView", TRUE, result, "");
    return result;
}
EI_VERIFY(0x0062AA50, ViewDrawVerify, 0, s_unused);

static void __fastcall ViewCloseVerify(CViewScreen* self, void*) {
    self->CViewScreen::OnClose();
    ei::verify::Check("CViewScreen::OnClose", true, (camera::TheCamera()->flags_ & CCamera::kFree) != 0, "");
}
EI_VERIFY(0x0062AA40, ViewCloseVerify, 0, s_unused);

// --- 005EB8C0 Construct: side by side ------------------------------------------------------------------
// The original into a scratch copy of the memory, ours into the real one; every byte compared but
// the handled flag and its padding (+0x08..+0x0B), which neither writes. Starts MOV EAX,ECX; MOV
// ECX,[ESP+4] (6 bytes).

static UnitPickInput*(__fastcall* s_construct)(UnitPickInput*, void*, CInterface3D*);
static UnitPickInput* __fastcall PickConstructVerify(UnitPickInput* self, void*, CInterface3D* ui) {
    alignas(4) std::uint8_t scratch[sizeof(UnitPickInput)];
    std::memcpy(scratch, self, sizeof(scratch));
    auto* original = reinterpret_cast<UnitPickInput*>(scratch);
    UnitPickInput* expected = s_construct(original, nullptr, ui);
    UnitPickInput* actual = self->Construct(ui);
    bool same = std::memcmp(scratch, self, 8) == 0 && std::memcmp(scratch + 0x0C, reinterpret_cast<std::uint8_t*>(self) + 0x0C,
                                                                   sizeof(scratch) - 0x0C) == 0;
    ei::verify::Check("UnitPickInput::Construct", true, same && expected == original && actual == self, "");
    return actual;
}
EI_VERIFY(0x005EB8C0, PickConstructVerify, 6, s_construct);

// --- 005EB910 Destruct / 005EB8F0 DeleteThis: ours only ----------------------------------------------

static void __fastcall PickDestructVerify(UnitPickInput* self, void*) {
    self->Destruct();
    ei::verify::Check("UnitPickInput::Destruct", true, self->vtable_ == UnitPickInput::BaseVtable(), "");
}
EI_VERIFY(0x005EB910, PickDestructVerify, 0, s_unused);

static UnitPickInput* __fastcall PickDeleteVerify(UnitPickInput* self, void*, int flags) {
    UnitPickInput* result = self->DeleteThis(flags);
    ei::verify::Check("UnitPickInput::DeleteThis", true, result == self, "flags %d", flags);
    return result;
}
EI_VERIFY(0x005EB8F0, PickDeleteVerify, 0, s_unused);

// --- 005EB940 OnLeftDown: ours only ----------------------------------------------------------------
// Without Ctrl, a unit under the point (the same lookup first) is appended to the list; otherwise
// the list keeps its size (the orders go to the server).

static void __fastcall PickLeftDownVerify(UnitPickInput* self, void*, int buttons, int x, int y) {
    const int point[2] = {x, y};
    CEngineObject* unit = ClientUnitAtOriginal(WorldClient(), nullptr, point);
    bool ctrl = static_cast<CInterface3D*>(self->owner_)->ctrlDown_;
    int before = self->picked_.size;
    self->OnLeftDown(buttons, x, y);
    bool added = !ctrl && unit;
    bool ok = self->picked_.size == before + (added ? 1 : 0) &&
              (!added || self->picked_.data[before] == static_cast<std::uint32_t>(unit->id_));
    ei::verify::Check("UnitPickInput::OnLeftDown", true, ok, "(%d,%d) ctrl %d unit %p size %d->%d", x, y, ctrl, unit, before,
                      self->picked_.size);
}
EI_VERIFY(0x005EB940, PickLeftDownVerify, 0, s_unused);

// --- 005EBB40 OnRightDown / 005EBB80 OnKeyDown / 005EBBC0 OnKeyUp: ours only ------------------------

static void __fastcall PickRightDownVerify(UnitPickInput* self, void*, int buttons, int x, int y) {
    self->OnRightDown(buttons, x, y);
    bool ok = !self->picked_.data && self->picked_.size == 0 && self->picked_.maxSize == 0;
    ei::verify::Check("UnitPickInput::OnRightDown", true, ok, "size %d", self->picked_.size);
}
EI_VERIFY(0x005EBB40, PickRightDownVerify, 0, s_unused);

static void __fastcall PickKeyDownVerify(UnitPickInput* self, void*, int key, int repeat, int flags) {
    char flag = ServerFlagOriginal();
    self->OnKeyDown(key, repeat, flags);
    bool ok = true;
    if (key == 0x20) ok = (ServerFlagOriginal() != 0) == (flag == 0);
    if (key == 0x11) ok = GameCursor()->shape_ == 1;
    ei::verify::Check("UnitPickInput::OnKeyDown", true, ok, "key %X flag %d", key, flag);
}
EI_VERIFY(0x005EBB80, PickKeyDownVerify, 0, s_unused);

static void __fastcall PickKeyUpVerify(UnitPickInput* self, void*, int key, int repeat, int flags) {
    self->OnKeyUp(key, repeat, flags);
    bool ok = key != 0x11 || GameCursor()->shape_ == 0;
    ei::verify::Check("UnitPickInput::OnKeyUp", true, ok, "key %X", key);
}
EI_VERIFY(0x005EBBC0, PickKeyUpVerify, 0, s_unused);
