// The global map's input side (right-button drag, mouse, keys), its close and message-result slots
// (GlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
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
#include "platform/cursor.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "lib/str.h"
#include "platform/keys.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr const char* kQuickSaveSlot = "saveq";

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
constexpr const char* kZoneSound = "buttons\\globalmap\\zone.wav";

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

} // namespace

void GlobalMap::Deactivate() {
    dragging = false;
    selectPending = true;
    // The cursor stops being held and goes back to where the drag began.
    int x = g_cursor.position.x;
    int y = g_cursor.position.y;
    g_cursor.motion.y = 0;
    g_cursor.motion.x = 0;
    g_cursor.SetHeld(0);
    platform::SetCursorPosition({x, y});
}

void GlobalMap::RightDown(int, int x, int y) {
    dragStart = POINT{x, y};
    dragging = true;
    turning = 0;
    ui->ScalePointFromScreenEntry(&dragStart);
    dragAngle = angle;
    g_cursor.motion.y = 0;
    g_cursor.motion.x = 0;
    g_cursor.SetHeld(1);
}

void GlobalMap::MouseMove(int, int x, int y) {
    if (dragging) {
        POINT drag{g_cursor.motion.x, g_cursor.motion.y};
        console::EndLine(console::g_graphicsLogOut << static_cast<int>(drag.x) << ':' << static_cast<int>(drag.y));
        // Rebirth's WideScreen scales the drag without the margin (addon's entry).
        ui->CallScalePointFromScreenNoMargin(&drag);
        float turned = static_cast<float>(static_cast<float>(drag.x) * 0.0033333334f + angle);    // 1 / 300
        angle = static_cast<float>(std::fmod(turned, kGlobalMapFullTurn));
        Quaternion spin, tilt;
        spin.SetAngleAxis(angle, Vector3{0.0f, 0.0f, 1.0f});
        tilt.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, 3.1415927f * 0.8f);
        Quaternion rotation = TiltTimesSpin(tilt, spin);
        turn = rotation;
        g_cursor.motion.x = 0;
        g_cursor.motion.y = 0;
        return;
    }
    bool isLink = false;
    int index = -1;
    int region = RegionAt(x, y);
    if (region >= std::ssize(links) + kFirstLinkRegion) return;
    if (turning == 0) {
        if (region >= kFirstLinkRegion) {
            index = region - kFirstLinkRegion;
            isLink = true;
        }
        if (region < 0) {
            // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; Rebirth's
            // WideScreen takes the margin off its x (addon's code).
            std::uint32_t pixel = backdrop->PixelAt(x / 2 - widescreen::PickShift(), y / 2);
            index = static_cast<int>((pixel >> 4) & 0xF) - 1;
            if (index >= std::ssize(zones)) index = -1;
            if (gipat && index >= 0 && zones[index].link >= 0) {
                isLink = true;
                index = zones[index].link;
            }
        }
    }
    SelectZone(index, isLink);
    if (index == -1 || (!isLink && zones[index].state == 0)) {
        surfaces[0]->Hide(false);
        SceneMesh(MarkerCount() + kTipBack)->visible = false;
        SceneMesh(MarkerCount() + kTipFrame)->visible = false;
        shownName.clear();
        return;
    }
    std::string text;
    text = isLink ? links[index].name : zones[index].name;
    if (str::Compare(shownName, text) != 0) ui->PlayInterfaceSound(kZoneSound);
    shownName = text;
    text = isLink ? links[index].title : zones[index].title;
    surfaces[0]->Show();
    surfaces[0]->Clear();
    RECT titleRect{10, 10, 300, 50};
    surfaces[0]->DrawTextIn(text, &titleRect, kTipFont, kTipColour, kTipFormat, 1);
    // A zone's tip lists the quests in it.
    int lines = 0;
    if (!isLink) {
        int bottom = 60;
        for (int i = 0; i < std::ssize(quests); ++i) {
            GlobalMapPlace& quest = quests[i];
            if (quest.zone != index) continue;
            RECT area{20, bottom - kTipLine, 350, bottom};
            surfaces[0]->DrawTextIn(quest.title, &area, kTipFont, kTipColour, kTipFormat, 1);
            ++lines;
            bottom += kTipLine;
        }
    }
    UiMesh* back = SceneMesh(MarkerCount() + kTipBack);
    back->SetRectangle(3.0f, 100.0f, 400.0f, 20.0f, 780.0f, static_cast<float>(lines * kTipLine + 75), 0, 0, 0x100, 0x100);
    back->visible = true;
    UiMesh* frame = SceneMesh(MarkerCount() + kTipFrame);
    frame->SetFrame(3.0f, 100.0f, 395.0f, 15.0f, 785.0f, static_cast<float>((lines * 5 + 20) * 4), 5.0f, 2, 0xFE, 0xB4,
                    0xF9);
    frame->visible = true;
}

void GlobalMap::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (key == platform::kKeyEscape) {
        ui->PushWindow(new EscMenu(ui), true, true);
        handled = true;
    }
    if (handled) return;
    int action = g_keyActions[modifiers & kScanCodeMask];
    if (action == kActionQuickSave)
        ::QuickSave(kQuickSaveSlot, false);
    else if (action == kActionQuickLoad)
        QuickLoad(kQuickSaveSlot);
}

void GlobalMap::MessageResult(int) {
    int x = g_cursor.position.x;
    int y = g_cursor.position.y;
    turning = 0;
    dragging = false;
    selectPending = true;
    Input()->MouseMove(0, x, y);
}

void GlobalMap::LeftUp(int, int, int) {
    if (turning != 0) selectPending = true;
    turning = 0;
}

void GlobalMap::CloseScreen() {
    delete weather;
    weather = nullptr;
    delete backdrop;
    backdrop = nullptr;
    ui->StopSound(windSound);
}

// The briefing screen's SaveState, byte for byte.
void GlobalMap::SaveState() {
    int zero = 0;
    g_saveStream.WriteValue(zero);
}
