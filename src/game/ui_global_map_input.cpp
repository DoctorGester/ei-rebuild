// The global map's input side (right-button drag, keys), its close and message-result slots
// (CGlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "console.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_ui.h"

#include <cstring>

namespace {

// The escape menu the map opens on Esc (0x168 bytes, built inline as the battle screen's key
// handler does).
constexpr std::size_t kEscMenuSize = 0x168;
void** EscMenuVtable() { return reinterpret_cast<void**>(0x007412A4); }
void** EscMenuInputVtable() { return reinterpret_cast<void**>(0x00741270); }
const char* QuickSaveName() { return reinterpret_cast<const char*>(0x007933AC); }   // "saveq"

constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';
constexpr int kScanCodeMask = 0x1FF;

// The tip of the zone under the mouse: regions 3.. are the links; the text's font, colour and
// DrawText format; the two sprites behind it follow the markers and the three layers.
constexpr int kFirstLinkRegion = 3;
constexpr int kTipFont = 2;
constexpr std::uint32_t kTipColour = 0xA7D7E4;
constexpr int kTipFormat = 4;
constexpr int kTipBack = 6;
constexpr int kTipFrame = 7;
constexpr int kTipLine = 20;
const char* ZoneSound() { return reinterpret_cast<const char*>(0x007941B8); }   // "buttons\globalmap\zone.wav"

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
// The original keeps the turned angle in a float stack slot.
float StoreFloat(float value) {
    volatile float stored = value;
    return stored;
}
mfc::CString& AsString(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }
bool SameText(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}

// tilt * spin as the drag sums it.
Quaternion TiltTimesSpin(const Quaternion& tilt, const Quaternion& spin) {
    Vector3 tv = {tilt.x, tilt.y, tilt.z};
    Vector3 sv = {spin.x, spin.y, spin.z};
    Quaternion q;
    q.w = spin.w * tilt.w - DotXYZ(tv, sv);
    q.x = ((tilt.y * spin.z - tilt.z * spin.y) + spin.w * tilt.x) + tilt.w * spin.x;
    q.y = ((tilt.z * spin.x - tilt.x * spin.z) + spin.w * tilt.y) + tilt.w * spin.y;
    q.z = ((tilt.x * spin.y - tilt.y * spin.x) + spin.w * tilt.z) + spin.z * tilt.w;
    return q;
}

// The cursor stops being held and goes back to where the drag began.
void ReleaseDragCursor() {
    int x = MouseScreenX();
    int y = MouseScreenY();
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 0);
    gameapi::SetCursorPos(x, y);
}

} // namespace

void CGlobalMap::OnDeactivate() {
    dragging_ = false;
    selectPending_ = 1;
    ReleaseDragCursor();
}

void CGlobalMap::OnRightDown(int, int x, int y) {
    dragStart_ = POINT{x, y};
    dragging_ = true;
    turning_ = 0;
    interface_->CallScalePointFromScreen(&dragStart_);
    dragAngle_ = angle_;
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 1);
}

void CGlobalMap::OnRightUp(int, int, int) {
    dragging_ = false;
    selectPending_ = 1;
    ReleaseDragCursor();
}

void CGlobalMap::OnMouseMove(int, int x, int y) {
    if (dragging_) {
        POINT drag{MouseDragX(), MouseDragY()};
        (console::Out7C5760() <<static_cast<int>(drag.x) << ':' << static_cast<int>(drag.y) << '\n').Flush();
        // Rebirth's WideScreen scales the drag without the margin (0063008C: addon's entry 005F7DFE).
        interface_->CallScalePointFromScreenNoMargin(&drag);
        float turned = StoreFloat(static_cast<float>(drag.x) * GameFloat(0x00740204) + angle_);   // 1 / 300
        angle_ = turned;
        angle_ = static_cast<float>(gamecrt::fmod(turned, GameFloat(0x007C8020)));
        Quaternion spin, tilt;
        spin.SetAngleAxis(angle_, Vector3{0.0f, 0.0f, 1.0f});
        tilt.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, GameFloat(0x00742098) * GameFloat(0x0073CCA8));   // pi * 0.8
        Quaternion turn = TiltTimesSpin(tilt, spin);
        std::memcpy(&turn_, &turn, sizeof(turn));
        MouseDragX() = 0;
        MouseDragY() = 0;
        return;
    }
    bool isLink = false;
    int index = -1;
    int region = RegionAt(x, y);
    if (region >= linkCount_ + kFirstLinkRegion) return;
    if (turning_ == 0) {
        if (region >= kFirstLinkRegion) {
            index = region - kFirstLinkRegion;
            isLink = true;
        }
        if (region < 0) {
            // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; Rebirth's
            // WideScreen takes the margin off its x (0063023C: addon's code at 005FD237).
            std::uint32_t pixel = SurfacePixelAtOriginal(backdrop_, nullptr, x / 2 - widescreen::PickShift(), y / 2);
            index = static_cast<int>((pixel >> 4) & 0xF) - 1;
            if (index >= zoneCount_) index = -1;
            if (gipat_ && index >= 0 && zones_.data[index].link >= 0) {
                isLink = true;
                index = zones_.data[index].link;
            }
        }
    }
    SelectZone(index, isLink);
    if (index == -1 || (!isLink && zones_.data[index].state == 0)) {
        surfaces_.data[0]->Freeze(0, 0);
        SceneMesh(markerCount_ + kTipBack)->visible_ = 0;
        SceneMesh(markerCount_ + kTipFrame)->visible_ = 0;
        AsString(shownName_).Empty();
        return;
    }
    mfc::CString text;
    text = isLink ? AsString(links_.data[index].name) : AsString(zones_.data[index].name);
    if (!SameText(shownName_, text)) interface_->PlaySound(ZoneSound(), 0);
    AsString(shownName_) = text;
    text = isLink ? AsString(links_.data[index].title) : AsString(zones_.data[index].title);
    surfaces_.data[0]->Freeze(1, 0);
    SurfaceGetDC(surfaces_.data[0]);
    surfaces_.data[0]->Clear();
    RECT titleRect{10, 10, 300, 50};
    SurfaceDrawText(surfaces_.data[0], nullptr, text, &titleRect, kTipFont, kTipColour, kTipFormat, 1);
    // A zone's tip lists the quests in it.
    int lines = 0;
    if (!isLink) {
        int bottom = 60;
        for (int i = 0; i < quests_.size; ++i) {
            GlobalMapPlace& quest = quests_.data[i];
            if (quest.zone != index) continue;
            RECT rect{20, bottom - kTipLine, 350, bottom};
            SurfaceDrawText(surfaces_.data[0], nullptr, quest.title, &rect, kTipFont, kTipColour, kTipFormat, 1);
            ++lines;
            bottom += kTipLine;
        }
    }
    SurfaceReleaseDC(surfaces_.data[0]);
    CUiMesh* back = SceneMesh(markerCount_ + kTipBack);
    back->SetRectangle(3.0f, 100.0f, 400.0f, 20.0f, 780.0f, static_cast<float>(lines * kTipLine + 75), 0, 0, 0x100, 0x100);
    back->visible_ = 1;
    CUiMesh* frame = SceneMesh(markerCount_ + kTipFrame);
    frame->SetFrame(3.0f, 100.0f, 395.0f, 15.0f, 785.0f, static_cast<float>((lines * 5 + 20) * 4), 5.0f, 2, 0xFE, 0xB4,
                    0xF9);
    frame->visible_ = 1;
}

void CGlobalMap::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        void* memory = gamecrt::operator_new(kEscMenuSize);
        CUiWindow* menu = nullptr;
        if (memory) {
            ConstructContainerAt(memory, nullptr, interface_);
            menu = static_cast<CUiWindow*>(memory);
            menu->vtable_ = EscMenuVtable();
            menu->inputVtable_ = EscMenuInputVtable();
        }
        InterfacePushWindowOriginal(interface_, nullptr, menu, 1, 1);
        inputHandled_ = true;
    }
    if (inputHandled_) return;
    int action = KeyActions()[flags & kScanCodeMask];
    if (action == kActionQuickSave)
        AutoSaveOriginal(QuickSaveName(), 0);
    else if (action == kActionQuickLoad)
        LoadSavedGameOriginal(QuickSaveName());
}

void CGlobalMap::OnMessageResult(int) {
    int x = MouseScreenX();
    int y = MouseScreenY();
    turning_ = 0;
    dragging_ = false;
    selectPending_ = 1;
    InputMouseMove(0, x, y);
}

void CGlobalMap::Close() {
    if (weather_) weather_->DeletingDestruct(1);
    CUiSurface* backdrop = backdrop_;
    weather_ = nullptr;
    if (backdrop) {
        backdrop->ReleaseContents();
        gamecrt::operator_delete(backdrop);
    }
    backdrop_ = nullptr;
    InterfaceStopSoundOriginal(interface_, nullptr, windSound_);
}
EI_REPLACE_METHOD(0x0062EDD0, CGlobalMap::Close);
EI_REPLACE_METHOD(0x0062EE30, CGlobalMap::OnMessageResult);

namespace {
void __fastcall GlobalMapDeactivate(void* input) { WindowFromInput<CGlobalMap>(input)->OnDeactivate(); }
void __fastcall GlobalMapRightDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnRightDown(buttons, x, y);
}
void __fastcall GlobalMapRightUp(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnRightUp(buttons, x, y);
}
void __fastcall GlobalMapMouseMove(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnMouseMove(buttons, x, y);
}
void __fastcall GlobalMapKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CGlobalMap>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x0062FF10, GlobalMapDeactivate);
EI_REPLACE(0x0062FF60, GlobalMapRightDown);
EI_REPLACE(0x0062FFC0, GlobalMapRightUp);
EI_REPLACE(0x00630010, GlobalMapMouseMove);
EI_REBIRTH_IMPLEMENTS(0x00630010, ei::rebirth::Patch::WideScreen);
EI_REPLACE(0x00630580, GlobalMapKeyDown);
