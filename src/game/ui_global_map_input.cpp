// The global map's input side (right-button drag, mouse, keys), its close and message-result slots
// (GlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
#include "game_strings.h"
#include "ui.h"
#include "ui_camp.h"

#include "camera_trace.h"
#include "console.h"
#include "core_streams.h"
#include "global_map_clouds.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "screen_layout.h"

#include "lib/rebirth.h"
#include "platform/platform.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "lib/str.h"

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

} // namespace

void GlobalMap::Deactivate() {
    dragging = false;
    selectPending = true;
    // The cursor stops being held and goes back to where the drag began.
    const Point home = g_cursor.position;
    g_cursor.motion = {};
    g_cursor.SetHeld(0);
    platform::SetCursorPosition(home);
}

void GlobalMap::RightDown(int, Point point) {
    dragStart = point;
    dragging = true;
    turning = 0;
    ui->ScalePointFromScreen(&dragStart);
    dragAngle = angle;
    g_cursor.SetHeld(1);
}

void GlobalMap::MouseMove(int, Point point) {
    if (dragging) {
        Point drag = g_cursor.motion;
        console::EndLine(console::g_graphicsLogOut << static_cast<int>(drag.x) << ':' << static_cast<int>(drag.y));
        // The turn per mouse count is the same at every window size (screen_layout::kLayoutPerCount, kept as a
        // fraction); the original took raw counts at 800x600 and Rebirth's WideScreen scaled them to the window.
        const float layoutX = static_cast<float>(drag.x) * screen_layout::kLayoutPerCount;
        float turned = layoutX * 0.0033333334f + angle;    // 1 / 300
        camera_trace::MapDrag(drag, layoutX, turned);
        angle = std::fmod(turned, kGlobalMapFullTurn);
        Quaternion spin, tilt;
        spin.SetAngleAxis(angle, Vector3{0.0f, 0.0f, 1.0f});
        tilt.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, 3.1415927f * 0.8f);
        Quaternion rotation = tilt * spin;
        turn = rotation;
        g_cursor.motion = {};
        return;
    }
    bool isLink = false;
    int index = -1;
    int region = RegionAt(point);
    if (region >= std::ssize(links) + kFirstLinkRegion) return;
    if (turning == 0) {
        if (region >= kFirstLinkRegion) {
            index = region - kFirstLinkRegion;
            isLink = true;
        }
        if (region < 0) {
            // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; the window's 4:3
            // part's margins come off (Rebirth's WideScreen took the margin off x).
            const Point shift = screen_layout::PickShift();
            std::uint32_t pixel = backdrop->PixelAt(point / 2 - shift);
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
    Rect titleRect{10, 10, 300, 50};
    surfaces[0]->DrawTextIn(text, &titleRect, kTipFont, kTipColour, kTipFormat, 1);
    // A zone's tip lists the quests in it.
    int lines = 0;
    if (!isLink) {
        int bottom = 60;
        for (int i = 0; i < std::ssize(quests); ++i) {
            GlobalMapPlace& quest = quests[i];
            if (quest.zone != index) continue;
            Rect area{20, bottom - kTipLine, 350, bottom};
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
    turning = 0;
    dragging = false;
    selectPending = true;
    Input()->MouseMove(0, g_cursor.position);
}

void GlobalMap::LeftUp(int, Point) {
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
