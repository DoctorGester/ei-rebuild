// The global map's input (right-button drag, mouse, keys), its close and message-result slots
// (GlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_saveload.h"
#include "game/ui/ui_camp.h"

#include "game/ui/camera_trace.h"
#include "game/console/console.h"
#include "engine/core/core_streams.h"
#include "game/ui/global_map_clouds.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_screens.h"
#include "game/ui/screen_layout.h"

#include "platform/platform.h"
#include "lib/vecmath.h"
#include "game/app/singletons.h"
#include "game/app/key_bindings.h"
#include "lib/str.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr const char* kQuickSaveSlot = "saveq";

constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';

// The tip of the zone under the mouse: the text's font, colour and DrawText format.
constexpr int kTipFont = 2;
constexpr render::text::Format kTipFormat = render::text::Format::VCenter;
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

void GlobalMap::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::RightDown: RightDown(event); break;
    case UiEventKind::RightUp: Deactivate(); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void GlobalMap::RightDown(UiEvent& event) {
    dragStart = screen_layout::FromScreen(event.at);
    dragging = true;
    turning = 0;
    dragAngle = angle;
    g_cursor.SetHeld(1);
}

void GlobalMap::MouseMove(UiEvent& event) {
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
        spin = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, angle);
        tilt = Quaternion::AxisAngle(Vector3{1.0f, 0.0f, 0.0f}, 3.1415927f * 0.8f);
        Quaternion rotation = tilt * spin;
        turn = rotation;
        g_cursor.motion = {};
        return;
    }
    bool isLink = false;
    int index = -1;
    UiRegion* region = RegionUnder(event.at);
    if (turning == 0) {
        if (const int link = RegionIndex(linkAreas, region); link >= 0) {
            index = link;
            isLink = true;
        }
        if (!region) {
            index = ZoneAt(event.at);
            if (gipat && index >= 0 && zones[index].link >= 0) {
                isLink = true;
                index = zones[index].link;
            }
        }
    }
    SelectZone(index, isLink);
    if (index == -1 || (!isLink && zones[index].state == 0)) {
        surfaces[0]->Hide();
        tipBack->visible = false;
        tipFrame->visible = false;
        shownName.clear();
        return;
    }
    std::string text;
    text = isLink ? links[index].name : zones[index].name;
    if (str::Compare(shownName, text) != 0) g_interface.PlayInterfaceSound(kZoneSound);
    shownName = text;
    tipTitle = isLink ? links[index].title : zones[index].title;
    surfaces[0]->Show();
    // A zone's tip lists the quests in it.
    tipQuests.clear();
    if (!isLink) {
        for (const GlobalMapPlace& quest : quests)
            if (quest.zone == index) tipQuests.push_back(quest.title);
    }
    const int lines = static_cast<int>(tipQuests.size());
    tipBack->SetRectangle(kDialogDepth, kSpriteScale, 400.0f, 20.0f, 780.0f, static_cast<float>(lines * kTipLine + 75), 0,
                          0, kUiTextureSize, kUiTextureSize);
    tipBack->visible = true;
    tipFrame->SetFrame(3.0f, 100.0f, 395.0f, 15.0f, 785.0f, static_cast<float>((lines * 5 + 20) * 4), 5.0f, 2, 0xFE,
                       0xB4, 0xF9);
    tipFrame->visible = true;
}

void GlobalMap::PaintTip(UiSurface& surface) const {
    Rect titleRect{10, 10, 300, 50};
    surface.DrawTextIn(tipTitle, titleRect, TextStyle{kTipFont, kUiTextColour, kTipFormat});
    int bottom = 60;
    for (const std::string& quest : tipQuests) {
        Rect area{20, bottom - kTipLine, 350, bottom};
        surface.DrawTextIn(quest, area, TextStyle{kTipFont, kUiTextColour, kTipFormat});
        bottom += kTipLine;
    }
}

void GlobalMap::PaintTitle(UiSurface& surface) const {
    Rect line1 = {0, 5, 150, 25};
    surface.DrawTextIn(allodTitle, line1, TextStyle{2, kUiTextColour, render::text::Format::Center | render::text::Format::VCenter | render::text::Format::SingleLine});
    Rect line2 = {0, 25, 150, 45};
    surface.DrawTextIn(clock, line2, TextStyle{2, kUiTextColour, render::text::Format::Center | render::text::Format::VCenter | render::text::Format::SingleLine});
}

void GlobalMap::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    if (event.key == platform::Key::Escape) {
        g_interface.PushWindow(std::make_unique<EscMenu>(), WindowBackdrop::FirstWindow);
        event.handled = true;
    }
    if (event.handled) return;
    int action = g_keyActions[event.scan];
    if (action == kActionQuickSave)
        ::QuickSave(kQuickSaveSlot, false);
    else if (action == kActionQuickLoad)
        QuickLoad(kQuickSaveSlot);
}

void GlobalMap::MessageResult(std::uint32_t) {
    turning = 0;
    dragging = false;
    selectPending = true;
    UiEvent move{.kind = UiEventKind::MouseMove, .at = g_cursor.position};
    OnEvent(move);
}

void GlobalMap::LeftUp(UiEvent&) {
    if (turning != 0) selectPending = true;
    turning = 0;
}

void GlobalMap::CloseScreen() {
    delete weather;
    weather = nullptr;
    g_interface.StopSound(windSound);
}

// The briefing screen's SaveState, byte for byte.
void GlobalMap::SaveState() {
    int zero = 0;
    g_saveStream.WriteValue(zero);
}
