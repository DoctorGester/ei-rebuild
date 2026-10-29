// The global map's input side (right-button drag, keys), its close and message-result slots
// (CGlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
#include "game_strings.h"
#include "ui.h"
#include "ui_camp.h"

#include "console.h"
#include "core_streams.h"
#include "global_map_clouds.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

// The escape menu the map opens on Esc (0x168 bytes, built inline as the battle screen's key
const char* QuickSaveName() { return "saveq"; }

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
const char* ZoneSound() { return "buttons\\globalmap\\zone.wav"; }

bool SameText(std::string_view a, std::string_view b) {
    return str::Compare(a, b) == 0;
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
    int x = g_cursor->position_.x;
    int y = g_cursor->position_.y;
    g_cursor->motion_.y = 0;
    g_cursor->motion_.x = 0;
    g_cursor->SetHeld(0);
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
    interface_->ScalePointFromScreenEntry(&dragStart_);
    dragAngle_ = angle_;
    g_cursor->motion_.y = 0;
    g_cursor->motion_.x = 0;
    g_cursor->SetHeld(1);
}

void CGlobalMap::OnRightUp(int, int, int) {
    dragging_ = false;
    selectPending_ = 1;
    ReleaseDragCursor();
}

void CGlobalMap::OnMouseMove(int, int x, int y) {
    if (dragging_) {
        POINT drag{g_cursor->motion_.x, g_cursor->motion_.y};
        console::EndLine(console::g_graphicsLogOut << static_cast<int>(drag.x) << ':' << static_cast<int>(drag.y));
        // Rebirth's WideScreen scales the drag without the margin (addon's entry).
        interface_->CallScalePointFromScreenNoMargin(&drag);
        float turned = static_cast<float>(static_cast<float>(drag.x) * 0.0033333334f + angle_);   // 1 / 300
        angle_ = turned;
        angle_ = static_cast<float>(std::fmod(turned, kGlobalMapFullTurn));
        Quaternion spin, tilt;
        spin.SetAngleAxis(angle_, Vector3{0.0f, 0.0f, 1.0f});
        tilt.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, 3.1415927f * 0.8f);
        Quaternion turn = TiltTimesSpin(tilt, spin);
        turn_ = turn;
        g_cursor->motion_.x = 0;
        g_cursor->motion_.y = 0;
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
            // WideScreen takes the margin off its x (addon's code).
            std::uint32_t pixel = backdrop_->PixelAt(x / 2 - widescreen::PickShift(), y / 2);
            index = static_cast<int>((pixel >> 4) & 0xF) - 1;
            if (index >= zoneCount_) index = -1;
            if (gipat_ && index >= 0 && zones_[index].link >= 0) {
                isLink = true;
                index = zones_[index].link;
            }
        }
    }
    SelectZone(index, isLink);
    if (index == -1 || (!isLink && zones_[index].state == 0)) {
        surfaces_[0]->Freeze(0, 0);
        SceneMesh(markerCount_ + kTipBack)->visible_ = 0;
        SceneMesh(markerCount_ + kTipFrame)->visible_ = 0;
        shownName_.clear();
        return;
    }
    std::string text;
    text = isLink ? links_[index].name : zones_[index].name;
    if (!SameText(shownName_, text)) interface_->PlaySound(ZoneSound(), 0);
    shownName_ = text;
    text = isLink ? links_[index].title : zones_[index].title;
    surfaces_[0]->Freeze(1, 0);
    surfaces_[0]->Clear();
    RECT titleRect{10, 10, 300, 50};
    surfaces_[0]->DrawTextIn(text, &titleRect, kTipFont, kTipColour, kTipFormat, 1);
    // A zone's tip lists the quests in it.
    int lines = 0;
    if (!isLink) {
        int bottom = 60;
        for (int i = 0; i < std::ssize(quests_); ++i) {
            GlobalMapPlace& quest = quests_[i];
            if (quest.zone != index) continue;
            RECT rect{20, bottom - kTipLine, 350, bottom};
            surfaces_[0]->DrawTextIn(quest.title, &rect, kTipFont, kTipColour, kTipFormat, 1);
            ++lines;
            bottom += kTipLine;
        }
    }
    CUiMesh* back = SceneMesh(markerCount_ + kTipBack);
    back->SetRectangle(3.0f, 100.0f, 400.0f, 20.0f, 780.0f, static_cast<float>(lines * kTipLine + 75), 0, 0, 0x100, 0x100);
    back->visible_ = 1;
    CUiMesh* frame = SceneMesh(markerCount_ + kTipFrame);
    frame->SetFrame(3.0f, 100.0f, 395.0f, 15.0f, 785.0f, static_cast<float>((lines * 5 + 20) * 4), 5.0f, 2, 0xFE, 0xB4,
                    0xF9);
    frame->visible_ = 1;
}

void CGlobalMap::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        interface_->PushWindow(new CEscMenu(interface_), 1, 1);
        handled_ = true;
    }
    if (handled_) return;
    int action = g_keyActions[flags & kScanCodeMask];
    if (action == kActionQuickSave)
        ::QuickSave(QuickSaveName(), static_cast<char>(0));
    else if (action == kActionQuickLoad)
        QuickLoad(QuickSaveName());
}

void CGlobalMap::OnMessageResult(int) {
    int x = g_cursor->position_.x;
    int y = g_cursor->position_.y;
    turning_ = 0;
    dragging_ = false;
    selectPending_ = 1;
    Input()->MouseMove(0, x, y);
}

void CGlobalMap::Close() {
    delete weather_;
    CUiSurface* backdrop = backdrop_;
    weather_ = nullptr;
    if (backdrop) {
        delete backdrop;
    }
    backdrop_ = nullptr;
    interface_->StopSound(windSound_);
}

// The briefing screen's SaveState, byte for byte.
void CGlobalMap::SaveState() {
    int zero = 0;
    g_saveStream->WriteValue(zero);
}
