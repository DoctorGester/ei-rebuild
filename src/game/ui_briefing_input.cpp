// The briefing / base screen's mouse input and its topic list (BriefingScreen, ui_briefing.h):
// the click and double click (the input virtuals), the person's topics and their drawing. See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "world_glue.h"
#include "combat.h"
#include "ui_briefing.h"

#include "text_manager.h"
#include "map_object.h"
#include "maths.h"
#include "unit.h"
#include "unit_ai.h"

#include "ui_cursor.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/text_case.h"

#include <string>

namespace {

constexpr const char* kClickSound = "buttons\\base\\click1.wav";
constexpr const char* kTopicPrefixKey = "topic_prefix";
constexpr const char* kGoodbyeTopic = "goodbye";
constexpr const char* kTopicSeparator = " ";

constexpr int kTopicRows = 5;

// The topic rows (layout): regions at x 210..570 from y 440, 24 high; text rows on surface 0 at
// x 15..380 with their bottoms from 34, 24 apart, up to 154.
constexpr int kRowRegionLeft = 0xD2;
constexpr int kRowRegionRight = 0x23A;
constexpr int kRowRegionTop = 0x1B8;
constexpr int kRowRegionEnd = 0x230;
constexpr int kRowHeight = 0x18;
constexpr int kRowTextLeft = 0xF;
constexpr int kRowTextRight = 0x17C;
constexpr int kRowTextFirstBottom = 0x22;
constexpr int kRowTextEnd = 0x9A;
constexpr int kRowTextHeight = 0x16;
constexpr std::uint32_t kTopicColour = 0xA7D7E4;
constexpr std::uint32_t kHoveredTopicColour = 0xFFFFFF;
constexpr int kTextFormat = 1;

// Cursor shapes.
constexpr int kCursorNormal = 0;
constexpr int kCursorChat = 0xD;
constexpr int kCursorTalk = 0xE;
constexpr int kCursorExit = 0xF;

// The person's name label (surface 1) left and up of the mouse.
constexpr int kLabelOffsetX = 0x7D;
constexpr int kLabelOffsetY = 0x14;
constexpr std::uint32_t kLabelColour = 0xA7D7E4;

constexpr const char* kNoZone = "none";
constexpr const char* kZoneVariablePrefix = "z.";

// A person can talk when their controller is idle (command 0 or 0xB, nothing queued); people
// without a server unit or controller can always.
bool CanTalk(EngineObject* person) {
    CUnit* unit = g_serverWorld.FindUnit(person->id);
    if (!unit || !unit->controller) return true;
    UnitServer* controller = unit->ServerController();
    return (controller->action.kind == UnitOrderKind::Stop || controller->action.kind == UnitOrderKind::Idle) &&
           controller->action2.kind == UnitOrderKind::None;
}


bool IsPerson(CMapObject* object) { return object && object->IsDead() == 0; }

} // namespace

// The leader walks (or runs) to the ground point under the screen point (x, y), unless blocked.
static void WalkLeaderTo(BriefingScreen* screen, int x, int y, bool run) {
    Vector3 picked;
    Vector3* p = g_clientWorld.PickSurface(&picked, x, y);
    Vector3 point{p->x, p->y, p->z};
    if (!screen->leader) return;
    CUnit* unit = g_serverWorld.FindUnit(screen->leader->id);
    if (!unit || (unit->flags & MapObjectFlag::Blocked)) return;
    unit->ServerController()->brain.Walk(point, run);
}


void BriefingScreen::LeftDown(int buttons, int x, int y) {
    if (chatActive) {
        if (chat) chat->Input()->LeftDown(buttons, x, y);
        return;
    }
    UiContainer::LeftDown(buttons, x, y);
    if (handled) return;
    switch (state) {
    case 0: {
        POINT point = {x, y};
        CUnit* object = g_clientWorld.UnitAt2(&point);
        if (IsPerson(object)) {
            if (CanTalk(object)) {
                ui->PlayInterfaceSound(kClickSound);
                OpenTopics(object);
            }
            return;
        }
        if (!noWalk) WalkLeaderTo(this, x, y, false);
        return;
    }
    case 1: {
        int region = RegionAt(x, y);
        if (region < 0 || region >= kTopicRows || region + topicTop >= std::ssize(topics)) return;
        ui->PlayInterfaceSound(kClickSound);
        topic = topics[region + topicTop];
        ClearRegions();
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


void BriefingScreen::MouseMove(int buttons, int x, int y) {
    if (chatActive) {
        g_cursor.SetShape(kCursorChat);
        if (chat) chat->Input()->MouseMove(buttons, x, y);
        return;
    }
    g_cursor.SetShape(kCursorNormal);
    UiContainer::MouseMove(buttons, x, y);
    if (state == 1) {
        int region = RegionAt(x, y);
        int hovered = -1;
        if (region >= 0 && region < kTopicRows && region + topicTop < std::ssize(topics)) hovered = region + topicTop;
        if (hovered != hoveredTopic) {
            hoveredTopic = hovered;
            DrawTopics();
        }
    } else if (state == 3) {
        g_cursor.SetShape(kCursorTalk);
    } else if (state == 4) {
        g_cursor.SetShape(kCursorNormal);
    }
    if (ui->rightDown || state != 0) return;

    POINT point = {x, y};
    CUnit* object = g_clientWorld.UnitAt2(&point);
    if (IsPerson(object)) {
        int objectId = object->id;
        if (IsMultiplayerClient() && leader && leader->id == objectId) return;
        if (!CanTalk(object)) return;
        FindTopics(objectId);
        if (!topics.empty()) g_cursor.SetShape(kCursorTalk);
        auto node = personTexts.find(objectId);
        if (node == personTexts.end()) return;
        UiSurface* label = surfaces[1];
        label->Clear();
        RECT area{0, 0, 0, 0};
        label->DrawTextIn(node->second, &area, kTextFormat, kLabelColour, 1, 1);
        POINT at{x, y};
        ui->ScalePointFromScreenEntry(&at);
        label->MoveTo(at.x - kLabelOffsetX, at.y - kLabelOffsetY);
        label->Show();
        return;
    }
    Vector3 picked;
    Vector3* p = g_clientWorld.PickSurface(&picked, x, y);
    Vector3 ground{p->x, p->y, p->z};
    int newExit = 0;
    if (!g_clientWorld.FindExitAt(&ground.x, &newExit)) return;
    std::string zone = GameExitTarget(exit);
    if (text::CompareIgnoringCase(zone,
                        kNoZone) == 0)
        return;
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    EI_NAN_AUDIT(visited);
    if (visited != 1.0f) g_cursor.SetShape(kCursorExit);
}


void BriefingScreen::LeftDouble(int buttons, int x, int y) {
    if (chatActive) return;
    UiContainer::LeftDouble(buttons, x, y);
    if (handled) return;
    if (state != 0) {
        Input()->LeftDown(buttons, x, y);
        return;
    }
    POINT point = {x, y};
    CUnit* object = g_clientWorld.UnitAt2(&point);
    if (IsPerson(object)) {
        for (const BriefingMover& mover : movers)
            if (mover.person->id == object->id) return;
        Input()->LeftDown(buttons, x, y);
        return;
    }
    if (!noWalk) WalkLeaderTo(this, x, y, true);
}


void BriefingScreen::OpenTopics(EngineObject* speaker) {
    FindTopics(speaker->id);
    if (topics.empty()) {
        state = 0;
        return;
    }
    if (leader && !noWalk) {
        CUnit* unit = g_serverWorld.FindUnit(leader->id);
        if (unit && !(unit->flags & MapObjectFlag::Blocked)) unit->ServerController()->brain.StopAction();
    }
    for (int top = kRowRegionTop; top < kRowRegionEnd; top += kRowHeight)
        AddRegion()->SetRect(kRowRegionLeft, top, kRowRegionRight, top + kRowHeight);
    topics.push_back(kGoodbyeTopic);
    const int count = std::ssize(topics);
    topicBar->SetMaximum(count > kTopicRows ? count - kTopicRows : 0);
    topicBar->SetPosition(0);
    topicTop = 0;
    hoveredTopic = -1;
    DrawTopics();
    state = 1;
}


void BriefingScreen::DrawTopics() {
    std::string prefix = g_clientWorld.texts->GetString(kTopicPrefixKey);
    UiSurface* surface = surfaces[0];
    surface->Clear();
    int row = topicTop;
    for (int bottom = kRowTextFirstBottom; bottom < kRowTextEnd; bottom += kRowHeight, ++row) {
        if (row >= std::ssize(topics)) break;
        RECT area{kRowTextLeft, bottom - kRowTextHeight, kRowTextRight, bottom};
        std::uint32_t colour = row != hoveredTopic ? kTopicColour : kHoveredTopicColour;
        std::string line = prefix;
        line += kTopicSeparator;
        line += TopicTitle(topics[row]);
        surface->DrawTextIn(line, &area, kTextFormat, colour, 0, 1);
    }
}
