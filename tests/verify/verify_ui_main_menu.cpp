// Checks for the main menu (ui_main_menu.cpp). The interface's saved-state reset and the regions'
// SetRect from a RECT run side by side; the menu's own slots run ours only (they build the window,
// play animations and sounds, change the weather or open a message box). Every scenario starts on
// the main menu: Create, Update and the mouse move run in all of them (the harness moves the
// mouse), Close when a game starts; ui_container_input presses Esc on the menu (the quit box) and
// Esc again (its answer 0x10002); ui_main_menu cancels the options and the difficulty question and
// closes the titles (0xCCCCCCCC) over the menu.
#include "verify.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;

float& MapEdgeFade() { return *reinterpret_cast<float*>(0x0079BD34); }
constexpr std::uintptr_t kMessageBoxVtable = 0x00742348;

int InputCount(const CUiContainer* window) { return static_cast<int>(window->inputsEnd_ - window->inputsBegin_); }
} // namespace

// CInterface3D::ResetSavedState (005F51F0): the original first, the 15 bytes +0x89..+0x97 recorded
// and put back, then ours; the bytes must match. XOR EAX,EAX; MOV [ECX+0x89],AL (8).
static void(__fastcall* s_resetSavedState)(CInterface3D*);
static void __fastcall ResetSavedStateVerify(CInterface3D* self) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self) + 0x89;
    std::uint8_t before[15], expected[15];
    std::memcpy(before, bytes, 15);
    s_resetSavedState(self);
    std::memcpy(expected, bytes, 15);
    std::memcpy(bytes, before, 15);
    self->CInterface3D::ResetSavedState();
    ei::verify::Check("CInterface3D::ResetSavedState", true, std::memcmp(expected, bytes, 15) == 0, "");
}
EI_VERIFY(0x005F51F0, ResetSavedStateVerify, 8, s_resetSavedState);

// CUiRegion::SetRectFrom (0062AC90): the original on a copy of the region without vertices (so its
// SetRect frees nothing), ours on the region; bounds, flags and vertex fields must match. Under
// Rebirth both reach the patched SetRect. MOV EAX,[ESP+0x10]; MOV EDX,[ESP+0xC] (8).
static void(__fastcall* s_setRectFrom)(CUiRegion*, void*, RECT);
static void __fastcall SetRectFromVerify(CUiRegion* self, void*, RECT rect) {
    CUiRegion scratch;
    std::memcpy(&scratch, self, sizeof(CUiRegion));
    scratch.points_ = nullptr;
    s_setRectFrom(&scratch, nullptr, rect);
    self->CUiRegion::SetRectFrom(rect);
    bool ok = std::memcmp(&scratch.bounds_, &self->bounds_, sizeof(RECT)) == 0 && scratch.enabled_ == self->enabled_ &&
              scratch.rectOnly_ == self->rectOnly_ && self->points_ == nullptr &&
              scratch.pointCount_ == self->pointCount_ && scratch.pointCapacity_ == self->pointCapacity_;
    ei::verify::Check("CUiRegion::SetRectFrom", true, ok, "(%d,%d,%d,%d) -> (%d,%d,%d,%d)", rect.left, rect.top,
                      rect.right, rect.bottom, self->bounds_.left, self->bounds_.top, self->bounds_.right,
                      self->bounds_.bottom);
}
EI_VERIFY(0x0062AC90, SetRectFromVerify, 8, s_setRectFrom);

// CMainMenu::AddVersionLine (006335E0): one surface more.
static void __fastcall AddVersionLineVerify(CMainMenu* self) {
    int surfaces = self->surfaces_.size_;
    self->CMainMenu::AddVersionLine();
    ei::verify::Check("CMainMenu::AddVersionLine", true, self->surfaces_.size_ == surfaces + 1, "surfaces %d -> %d",
                      surfaces, self->surfaces_.size_);
}
EI_VERIFY(0x006335E0, AddVersionLineVerify, 0, s_unused);

// CMainMenu::Create (006326B0): no region hovered, the full screen, six regions, one surface and one
// extra input side more, the hour 0, the interface's saved state clear, the map edge fade kept and
// set to 6.
static void __fastcall MainMenuCreateVerify(CMainMenu* self) {
    int regions = self->regions_.size_;
    int surfaces = self->surfaces_.size_;
    int inputs = InputCount(self);
    float fade = MapEdgeFade();
    self->CMainMenu::Create();
    CInterface3D* ui = self->interface_;
    bool ok = self->hovered_ == -1 && self->rect_.left == 0 && self->rect_.top == 0 && self->rect_.right == 800 &&
              self->rect_.bottom == 600 && self->regions_.size_ == regions + 6 && self->surfaces_.size_ == surfaces + 1 &&
              InputCount(self) == inputs + 1 && self->hour_ == 0.0f && ui->fastGame_ == 0 && ui->unknown_8C == 0 &&
              ui->unknown_90 == 0 && std::memcmp(&self->savedEdgeFade_, &fade, 4) == 0 && MapEdgeFade() == 6.0f;
    ei::verify::Check("CMainMenu::Create", true, ok, "regions %d surfaces %d inputs %d rain %d daylight %d",
                      self->regions_.size_, self->surfaces_.size_, InputCount(self), self->rain_ ? 1 : 0,
                      self->daylight_ ? 1 : 0);
}
EI_VERIFY(0x006326B0, MainMenuCreateVerify, 0, s_unused);

// CMainMenu::Update (00632B10): the hour kept is the world's time of day now (stored as float).
static void __fastcall MainMenuUpdateVerify(CMainMenu* self) {
    self->CMainMenu::Update();
    float now = static_cast<float>(GameTimeOfDayOriginal(reinterpret_cast<void*>(0x007AFEA6)));
    ei::verify::CheckNear("CMainMenu::Update", now, self->hour_, "hour");
}
EI_VERIFY(0x00632B10, MainMenuUpdateVerify, 0, s_unused);

// CMainMenu::Close (00632910): the map edge fade is the one kept by Create again.
static void __fastcall MainMenuCloseVerify(CMainMenu* self) {
    self->CMainMenu::Close();
    ei::verify::Check("CMainMenu::Close", true, std::memcmp(&MapEdgeFade(), &self->savedEdgeFade_, 4) == 0, "");
}
EI_VERIFY(0x00632910, MainMenuCloseVerify, 0, s_unused);

// CMainMenu::OnMessageResult (00632940): only the titles' 0xCCCCCCCC adds a surface (the version
// line); other answers leave the surfaces (0x10001 and 0x20001 leave the menu and are not checked).
static void __fastcall MainMenuMessageResultVerify(CMainMenu* self, void*, int result) {
    int surfaces = self->surfaces_.size_;
    self->CMainMenu::OnMessageResult(result);
    if (result == 0x10001 || result == 0x20001) return;
    int expected = static_cast<std::uint32_t>(result) == 0xCCCCCCCC ? surfaces + 1 : surfaces;
    ei::verify::Check("CMainMenu::OnMessageResult", true, self->surfaces_.size_ == expected, "result %X surfaces %d -> %d",
                      result, surfaces, self->surfaces_.size_);
}
EI_VERIFY(0x00632940, MainMenuMessageResultVerify, 0, s_unused);

// CMainMenu::OnMouseMove (00633400): with the stones present the hovered region is the region at the
// point afterwards.
static void __fastcall MainMenuMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CMainMenu>(input);
    int region = self->RegionAt(x, y);
    int before = self->hovered_;
    self->CMainMenu::OnMouseMove(buttons, x, y);
    bool ok = self->hovered_ == region || self->hovered_ == before;
    ei::verify::Check("CMainMenu::OnMouseMove", true, ok, "(%d,%d) region %d hovered %d -> %d", x, y, region, before,
                      self->hovered_);
}
EI_VERIFY(0x00633400, MainMenuMouseMoveVerify, 0, s_unused);

// CMainMenu::OnKeyDown (00633540): Esc puts a message box (vtable 00742348) on top; other keys leave
// the top window.
static void __fastcall MainMenuKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CMainMenu>(input);
    CInterface3D* ui = self->interface_;
    CUiWindow* top = ui->GetActiveWindow();
    self->CMainMenu::OnKeyDown(key, repeat, flags);
    CUiWindow* now = ui->GetActiveWindow();
    bool ok = key == VK_ESCAPE ? now != top && now && reinterpret_cast<std::uintptr_t>(now->vtable_) == kMessageBoxVtable
                               : now == top;
    ei::verify::Check("CMainMenu::OnKeyDown", true, ok, "key %d top %p -> %p", key, top, now);
}
EI_VERIFY(0x00633540, MainMenuKeyDownVerify, 0, s_unused);
