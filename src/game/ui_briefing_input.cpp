// The briefing / base screen's mouse input and its topic list (CBriefingScreen, ui_briefing.h):
// the click and double click (the input virtuals), the person's topics and their drawing. See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "world_glue.h"
#include "combat.h"
#include "ui_briefing.h"

#include "item_db_text.h"
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
bool CanTalk(CEngineObject* person) {
    CUnit* unit = g_serverWorld.FindUnit(person->id_);
    if (!unit || !unit->controller) return true;
    CUnitServer* controller = unit->ServerController();
    return (controller->action.kind == UnitOrderKind::Stop || controller->action.kind == UnitOrderKind::Idle) &&
           controller->action2.kind == UnitOrderKind::None;
}


bool IsPerson(CMapObject* object) { return object && object->IsDead() == 0; }

} // namespace

// The leader walks (or runs) to the ground point under the screen point (x, y), unless blocked.
static void WalkLeaderTo(CBriefingScreen* screen, int x, int y, bool run) {
    Vector3 picked;
    Vector3* p = g_clientWorld.PickSurface(&picked, x, y);
    Vector3 point{p->x, p->y, p->z};
    if (!screen->leader_) return;
    CUnit* unit = g_serverWorld.FindUnit(screen->leader_->id_);
    if (!unit || (unit->flags_ & MapObjectFlag::Blocked)) return;
    unit->ServerController()->brain.Walk(&point, run);
}


void CBriefingScreen::LeftDown(int buttons, int x, int y) {
    if (chatActive_) {
        if (chat_) chat_->Input()->LeftDown(buttons, x, y);
        return;
    }
    CUiContainer::LeftDown(buttons, x, y);
    if (handled_) return;
    switch (state_) {
    case 0: {
        POINT point = {x, y};
        CUnit* object = g_clientWorld.UnitAt2(&point);
        if (IsPerson(object)) {
            if (CanTalk(object)) {
                interface_->PlaySound(kClickSound);
                OpenTopics(object);
            }
            return;
        }
        if (!noWalk_) WalkLeaderTo(this, x, y, false);
        return;
    }
    case 1: {
        int region = RegionAt(x, y);
        if (region < 0 || region >= kTopicRows || region + topicTop_ >= std::ssize(topics_)) return;
        interface_->PlaySound(kClickSound);
        topic_ = topics_[region + topicTop_];
        ClearRegions();
        StartDialogue(false, true);
        return;
    }
    case 3:
        interface_->PlaySound(kClickSound);
        NextPhrase(false);
        return;
    case 4:
        interface_->PlaySound(kClickSound);
        FinishDialogue();
        return;
    default:
        return;
    }
}


void CBriefingScreen::MouseMove(int buttons, int x, int y) {
    if (chatActive_) {
        g_cursor.SetShape(kCursorChat);
        if (chat_) chat_->Input()->MouseMove(buttons, x, y);
        return;
    }
    g_cursor.SetShape(kCursorNormal);
    CUiContainer::MouseMove(buttons, x, y);
    if (state_ == 1) {
        int region = RegionAt(x, y);
        int hovered = -1;
        if (region >= 0 && region < kTopicRows && region + topicTop_ < std::ssize(topics_)) hovered = region + topicTop_;
        if (hovered != hoveredTopic_) {
            hoveredTopic_ = hovered;
            DrawTopics();
        }
    } else if (state_ == 3) {
        g_cursor.SetShape(kCursorTalk);
    } else if (state_ == 4) {
        g_cursor.SetShape(kCursorNormal);
    }
    if (interface_->rightDown_ || state_ != 0) return;

    POINT point = {x, y};
    CUnit* object = g_clientWorld.UnitAt2(&point);
    if (IsPerson(object)) {
        int id = object->id_;
        if (IsMultiplayerClient() && leader_ && leader_->id_ == id) return;
        if (!CanTalk(object)) return;
        FindTopics(id);
        if (!topics_.empty()) g_cursor.SetShape(kCursorTalk);
        auto node = personTexts_.find(id);
        if (node == personTexts_.end()) return;
        CUiSurface* label = surfaces_[1];
        label->Clear();
        RECT rect{0, 0, 0, 0};
        label->DrawTextIn(node->second, &rect, kTextFormat, kLabelColour, 1, 1);
        POINT at{x, y};
        interface_->ScalePointFromScreenEntry(&at);
        label->MoveTo(at.x - kLabelOffsetX, at.y - kLabelOffsetY);
        label->Show();
        return;
    }
    Vector3 picked;
    Vector3* p = g_clientWorld.PickSurface(&picked, x, y);
    Vector3 ground{p->x, p->y, p->z};
    int exit = 0;
    if (!g_clientWorld.FindExitAt(&ground.x, &exit)) return;
    std::string zone = GameExitTarget(exit_);
    if (text::CompareIgnoringCase(zone,
                        kNoZone) == 0)
        return;
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    EI_NAN_AUDIT(visited);
    if (visited != 1.0f) g_cursor.SetShape(kCursorExit);
}


void CBriefingScreen::LeftDouble(int buttons, int x, int y) {
    if (chatActive_) return;
    CUiContainer::LeftDouble(buttons, x, y);
    if (handled_) return;
    if (state_ != 0) {
        Input()->LeftDown(buttons, x, y);
        return;
    }
    POINT point = {x, y};
    CUnit* object = g_clientWorld.UnitAt2(&point);
    if (IsPerson(object)) {
        for (const BriefingMover& mover : movers_)
            if (mover.person->id_ == object->id_) return;
        Input()->LeftDown(buttons, x, y);
        return;
    }
    if (!noWalk_) WalkLeaderTo(this, x, y, true);
}


void CBriefingScreen::OpenTopics(CEngineObject* person) {
    FindTopics(person->id_);
    if (topics_.empty()) {
        state_ = 0;
        return;
    }
    if (leader_ && !noWalk_) {
        CUnit* unit = g_serverWorld.FindUnit(leader_->id_);
        if (unit && !(unit->flags_ & MapObjectFlag::Blocked)) unit->ServerController()->brain.StopAction();
    }
    for (int top = kRowRegionTop; top < kRowRegionEnd; top += kRowHeight)
        AddRegion()->SetRect(kRowRegionLeft, top, kRowRegionRight, top + kRowHeight);
    topics_.push_back(kGoodbyeTopic);
    const int count = std::ssize(topics_);
    topicBar_->SetMaximum(count > kTopicRows ? count - kTopicRows : 0);
    topicBar_->SetPosition(0);
    topicTop_ = 0;
    hoveredTopic_ = -1;
    DrawTopics();
    state_ = 1;
}


void CBriefingScreen::DrawTopics() {
    std::string prefix;
    g_clientWorld.texts_->GetString(kTopicPrefixKey, &prefix);
    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    int row = topicTop_;
    for (int bottom = kRowTextFirstBottom; bottom < kRowTextEnd; bottom += kRowHeight, ++row) {
        if (row >= std::ssize(topics_)) break;
        RECT rect{kRowTextLeft, bottom - kRowTextHeight, kRowTextRight, bottom};
        std::uint32_t colour = row != hoveredTopic_ ? kTopicColour : kHoveredTopicColour;
        std::string text = prefix;
        text += kTopicSeparator;
        text += TopicTitle(topics_[row]);
        surface->DrawTextIn(text, &rect, kTextFormat, colour, 0, 1);
    }
}
