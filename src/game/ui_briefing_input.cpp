// The briefing / base screen's mouse input and its topic list (BriefingScreen, ui_briefing.h):
// the click and double click (the input virtuals), the person's topics and their drawing. See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"

#include "camera.h"
#include "combat.h"
#include "game_singletons.h"
#include "map_object.h"
#include "lib/vecmath.h"
#include "text_manager.h"
#include "text_split.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "lib/containers.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

namespace {

constexpr const char* kClickSound = "buttons\\base\\click1.wav";
constexpr const char* kTopicPrefixKey = "topic_prefix";
constexpr const char* kGoodbyeTopic = "goodbye";
constexpr const char* kTopicSeparator = " ";



// The topic rows (layout): regions at x 210..570 from y 440, 24 high; text rows on surface 0 at
// x 15..380 with their bottoms from 34, 24 apart, up to 154.
constexpr int kRowRegionLeft = 0xD2;
constexpr int kRowRegionRight = 0x23A;
constexpr int kRowRegionTop = 0x1B8;

constexpr int kRowHeight = 0x18;
constexpr int kRowTextLeft = 0xF;
constexpr int kRowTextRight = 0x17C;
constexpr int kRowTextFirstBottom = 0x22;
constexpr int kRowTextEnd = 0x9A;
constexpr int kRowTextHeight = 0x16;
constexpr std::uint32_t kHoveredTopicColour = 0xFFFFFF;
constexpr int kFont = 1;

// Cursor shapes.

// The person's name label (surface 1) left and up of the mouse.
constexpr int kLabelOffsetX = 0x7D;
constexpr int kLabelOffsetY = 0x14;

constexpr const char* kNoZone = "none";
constexpr const char* kZoneVariablePrefix = "z.";

// A person can talk when their controller is idle (command 0 or 0xB, nothing queued); people
// without a server unit or controller can always.
bool CanTalk(EngineObject* person) {
    Unit* unit = g_serverWorld.FindUnit(person->id);
    if (!unit || !unit->controller) return true;
    UnitServer* controller = unit->ServerController();
    return (controller->action.kind == UnitOrderKind::Stop || controller->action.kind == UnitOrderKind::Idle) &&
           controller->action2.kind == UnitOrderKind::None;
}

bool IsPerson(MapObject* object) { return object && object->IsDead() == 0; }

} // namespace

// The leader walks (or runs) to the ground point under the screen point (x, y), unless blocked.
static void WalkLeaderTo(BriefingScreen* screen, int x, int y, bool run) {
    Vector3 point = g_clientWorld.PickSurface(x, y);
    if (!screen->leader) return;
    Unit* unit = g_serverWorld.FindUnit(screen->leader->id);
    if (!unit || (unit->flags & MapObjectFlag::Blocked)) return;
    unit->ServerController()->brain.Walk(point, run);
}

void BriefingScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void BriefingScreen::LeftDown(UiEvent& event) {
    if (chatActive) {
        if (chat) chat->OnEvent(event);
        return;
    }
    UiContainer::OnEvent(event);
    if (event.handled) return;
    switch (state) {
    case 0: {
        Unit* object = g_clientWorld.UnitAt(event.at, HitTest::Parts);
        if (IsPerson(object)) {
            if (CanTalk(object)) {
                ui->PlayInterfaceSound(kClickSound);
                OpenTopics(object);
            }
            return;
        }
        if (!noWalk) WalkLeaderTo(this, event.at.x, event.at.y, false);
        return;
    }
    case 1: {
        const int row = RegionIndex(topicRows, RegionUnder(event.at));
        if (row < 0 || row + topicTop >= std::ssize(topics)) return;
        ui->PlayInterfaceSound(kClickSound);
        topic = topics[row + topicTop];
        ClearRegions();
        std::ranges::fill(topicRows, nullptr);
        StartDialogue(false, true);
        return;
    }
    case 3:
        ui->PlayInterfaceSound(kClickSound);
        NextPhrase(false);
        return;
    case 4:
        ui->PlayInterfaceSound(kClickSound);
        FinishDialogue();
        return;
    default:
        return;
    }
}

void BriefingScreen::MouseMove(UiEvent& event) {
    if (chatActive) {
        g_cursor.SetShape(CursorShape::Cancel);
        if (chat) chat->OnEvent(event);
        return;
    }
    g_cursor.SetShape(CursorShape::Default);
    UiContainer::OnEvent(event);
    if (state == 1) {
        const int row = RegionIndex(topicRows, RegionUnder(event.at));
        int hovered = -1;
        if (row >= 0 && row + topicTop < std::ssize(topics)) hovered = row + topicTop;
        if (hovered != hoveredTopic) {
            hoveredTopic = hovered;
            DrawTopics();
        }
    } else if (state == 3) {
        g_cursor.SetShape(CursorShape::Talk);
    } else if (state == 4) {
        g_cursor.SetShape(CursorShape::Default);
    }
    if (ui->rightDown || state != 0) return;

    Unit* object = g_clientWorld.UnitAt(event.at, HitTest::Parts);
    if (IsPerson(object)) {
        int objectId = object->id;
        if (IsMultiplayerClient() && leader && leader->id == objectId) return;
        if (!CanTalk(object)) return;
        FindTopics(objectId);
        if (!topics.empty()) g_cursor.SetShape(CursorShape::Talk);
        auto node = personTexts.find(objectId);
        if (node == personTexts.end()) return;
        UiSurface* label = surfaces[1].get();
        personLabel = {{node->second, Rect{0, 0, 0, 0}, {kFont, kUiTextColour, render::text::kCenter}}};
        Point at = event.at;
        ui->ScalePointFromScreen(&at);
        label->MoveTo(at - Point{kLabelOffsetX, kLabelOffsetY});
        label->Show();
        return;
    }
    Vector3 ground = g_clientWorld.PickSurface(event.at.x, event.at.y);
    if (!g_clientWorld.FindExitAt(ground)) return;
    std::string zone = g_clientWorld.ExitTarget(exit);
    if (text::CompareIgnoringCase(zone,
                        kNoZone) == 0)
        return;
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    if (visited != 1.0f) g_cursor.SetShape(CursorShape::Move);
}

void BriefingScreen::LeftDouble(UiEvent& event) {
    if (chatActive) return;
    UiContainer::OnEvent(event);
    if (event.handled) return;
    if (state != 0) {
        event.kind = UiEventKind::LeftDown;   // a click
        LeftDown(event);
        return;
    }
    Unit* object = g_clientWorld.UnitAt(event.at, HitTest::Parts);
    if (IsPerson(object)) {
        for (const BriefingMover& mover : movers)
            if (mover.person->id == object->id) return;
        event.kind = UiEventKind::LeftDown;   // a click
        LeftDown(event);
        return;
    }
    if (!noWalk) WalkLeaderTo(this, event.at.x, event.at.y, true);
}

void BriefingScreen::OpenTopics(EngineObject* speaker) {
    FindTopics(speaker->id);
    if (topics.empty()) {
        state = 0;
        return;
    }
    if (leader && !noWalk) {
        Unit* unit = g_serverWorld.FindUnit(leader->id);
        if (unit && !(unit->flags & MapObjectFlag::Blocked)) unit->ServerController()->brain.StopAction();
    }
    for (int row = 0, top = kRowRegionTop; row < kTopicRows; ++row, top += kRowHeight) {
        topicRows[row] = AddRegion();
        topicRows[row]->SetRect(kRowRegionLeft, top, kRowRegionRight, top + kRowHeight);
    }
    topics.push_back(kGoodbyeTopic);
    const int count = lib::Count(topics);
    topicBar->SetMaximum(count > kTopicRows ? count - kTopicRows : 0);
    topicBar->SetPosition(0);
    topicTop = 0;
    hoveredTopic = -1;
    DrawTopics();
    state = 1;
}

void BriefingScreen::DrawTopics() {
    std::string prefix = g_clientWorld.texts->GetString(kTopicPrefixKey);
    topicLabels.clear();
    int row = topicTop;
    for (int bottom = kRowTextFirstBottom; bottom < kRowTextEnd; bottom += kRowHeight, ++row) {
        if (row >= std::ssize(topics)) break;
        Rect area{kRowTextLeft, bottom - kRowTextHeight, kRowTextRight, bottom};
        std::uint32_t colour = row != hoveredTopic ? kUiTextColour : kHoveredTopicColour;
        std::string line = prefix;
        line += kTopicSeparator;
        line += TopicTitle(topics[row]);
        topicLabels.push_back({line, area, {kFont, colour}});
    }
}

// --- Ui briefing keys --------------------------------------------------------------------------------

// The briefing / base screen's key handler (BriefingScreen::KeyDown). See
// docs/notes/ui.md, "Briefing / base screen".

namespace {

constexpr const char* kQuickSaveName = "saveq";
constexpr const char* kQuestPrefix = "q.";

constexpr int kActionObjectives = 4;
constexpr int kActionLeader = 0xB;
constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';
constexpr int kQuestNameParts = 3;               // "q", zone, quest

// Action 4: the objectives window of the current zone, when a quest of a zone in its allod is
// active.
void OpenObjectives(Interface3D* ui) {
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    if (!zone) return;
    std::vector<std::string> parts;
    NamedFloatTable quests;
    PlayerZeroVariables(kQuestPrefix, &quests);
    for (auto& node : quests) {
        parts = text::Split(node.first, '.');
        if (parts.size() != kQuestNameParts || node.second != 1.0f) continue;
        MapRecord* questZone = g_mapDatabase.FindMap(parts[1]);
        if (!questZone) continue;
        if (!IsMultiplayerClient() && text::CompareIgnoringCase(questZone->allod, zone->allod) != 0) continue;
        auto* window = new ObjectivesWindow(ui);
        window->Setup(LocalPlayerZone(), nullptr, false, true);
        ui->PushWindow(window, WindowBackdrop::FirstWindow);
        break;
    }
}

} // namespace

void BriefingScreen::KeyDown(UiEvent& event) {
    if (chatActive) {
        if (event.key == platform::Key::Escape) chatActive = 0;
        return;
    }
    UiContainer::OnEvent(event);
    switch (event.key) {
    case platform::Key::Backspace:
        if (IsMultiplayerClient()) {
            ui->ChatKeyBackspace();
            event.handled = true;
            return;
        }
        break;
    case platform::Key::Return:
        if (IsMultiplayerClient()) {
            ui->ChatKeyEnter();
        } else if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            while (state == 3) NextPhrase(true);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        }
        event.handled = true;
        return;
    case platform::Key::Escape:
        if (state == 0) {
            ui->PushWindow(new EscMenu(ui), WindowBackdrop::FirstWindow);
        } else if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            while (state == 3) NextPhrase(true);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        } else if (state == 2) {
            StartDialogue(true, false);
        } else if (state == 1) {
            state = 0;
        }
        event.handled = true;
        return;
    case platform::Key::Space:
        if (state == 3) {
            ui->PlayInterfaceSound(kClickSound);
            NextPhrase(false);
        } else if (state == 4) {
            ui->PlayInterfaceSound(kClickSound);
            FinishDialogue();
        } else if (state == 2) {
            StartDialogue(true, false);
        }
        event.handled = true;
        return;
    default:
        break;
    }
    if (state != 0) return;
    switch (g_keyActions[event.scan]) {
    case kActionObjectives:
        OpenObjectives(ui);
        break;
    case kActionLeader:
        if (leader) g_camera.SetTarget(leader);
        event.handled = true;
        break;
    case kActionQuickSave:
        ::QuickSave(kQuickSaveName, false);
        break;
    case kActionQuickLoad:
        QuickLoad(kQuickSaveName);
        break;
    default:
        break;
    }
}
