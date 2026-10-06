// The briefing / base screen's mouse input and its topic list (CBriefingScreen, ui_briefing.h):
// the click and double click (input slots 2 and 7), the person's topics and their drawing. See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "item_db_text.h"
#include "map_object.h"
#include "maths.h"
#include "unit.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "originals_ui.h"
#include "originals_world.h"

namespace {

void* ClientWorld() { return reinterpret_cast<void*>(0x007AF5B8); }
void* ServerWorld() { return reinterpret_cast<void*>(0x007AFF60); }
const char* ClickSound() { return reinterpret_cast<const char*>(0x00793394); }    // "buttons\base\click1.wav"
const char* TopicPrefixKey() { return reinterpret_cast<const char*>(0x007934F4); }   // "topic_prefix"
const char* GoodbyeTopic() { return reinterpret_cast<const char*>(0x007934EC); }     // "goodbye"
const char* TopicSeparator() { return reinterpret_cast<const char*>(0x0078DA40); }   // " "

constexpr std::uint32_t kUnitBlocked = 0x800000;   // CEngineObject::flags_: script BlockUnit
constexpr int kCommandIdle = 0;
constexpr int kCommandStand = 0xB;
constexpr int kNextCommandNone = 9;
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

// 00565D00: the client world's unit drawn at a screen point (thiscall, RET 4: the point's x, y).
__declspec(naked) CEngineObject* __fastcall ClientUnitAtAt(void* /*world*/, void*, const int* /*point*/)
    EI_ORIGINAL(0x00565D00)
// 006078C0: a topic's title (thiscall on the screen, RET 8: out, the topic's name; returns out).
__declspec(naked) mfc::CString* __fastcall TopicTitleAt(CBriefingScreen*, void*, mfc::CString* /*out*/,
                                                        const char* /*topic*/) EI_ORIGINAL(0x006078C0)

// The server's unit with the id of a client object, or null.
CUnit* ServerUnit(int id) { return reinterpret_cast<CUnit*>(WorldFindUnit(ServerWorld(), nullptr, id)); }
UnitController* ControllerOf(CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }

// A person can talk when their controller is idle (command 0 or 0xB, nothing queued); people
// without a server unit or controller can always.
bool CanTalk(CEngineObject* person) {
    CUnit* unit = ServerUnit(person->id_);
    if (!unit || !unit->controller) return true;
    UnitController* controller = ControllerOf(unit);
    return (controller->command == kCommandStand || controller->command == kCommandIdle) &&
           controller->nextCommand == kNextCommandNone;
}

bool IsPerson(CEngineObject* object) { return object && static_cast<CMapObjectVirtuals*>(object)->IsDead() == 0; }

} // namespace

// The leader walks (or runs) to the ground point under the screen point (x, y), unless blocked.
static void WalkLeaderTo(CBriefingScreen* screen, int x, int y, int run) {
    float picked[3];
    float* p = HoverPickSurface(ClientWorld(), nullptr, picked, x, y);
    Vector3 point{p[0], p[1], p[2]};
    if (!screen->leader_) return;
    CUnit* unit = ServerUnit(screen->leader_->id_);
    if (!unit || (unit->flags_ & kUnitBlocked)) return;
    ControllerOf(unit)->Brain()->Walk(&point, run, 0);
}

// --- 006030F0: input slot 2 ---------------------------------------------------------------------

void CBriefingScreen::OnLeftDown(int buttons, int x, int y) {
    if (chatActive_) {
        if (chat_) chat_->Input()->Event(UiInputSide::kLeftDown, buttons, x, y);
        return;
    }
    InputOf(this)->BaseLButtonDown(buttons, x, y);
    if (inputHandled_) return;
    switch (state_) {
    case 0: {
        int point[2] = {x, y};
        CEngineObject* object = ClientUnitAtAt(ClientWorld(), nullptr, point);
        if (IsPerson(object)) {
            if (CanTalk(object)) {
                interface_->PlaySound(ClickSound(), 0);
                BriefingOpenTopicsOriginal(this, nullptr, object);
            }
            return;
        }
        if (!noWalk_) WalkLeaderTo(this, x, y, 0);
        return;
    }
    case 1: {
        int region = RegionAt(x, y);
        if (region < 0 || region >= kTopicRows || region + topicTop_ >= topics_.size_) return;
        interface_->PlaySound(ClickSound(), 0);
        topic_ = topics_[region + topicTop_];
        ClearRegions();
        BriefingStartDialogueOriginal(this, nullptr, 0, 1);
        return;
    }
    case 3:
        interface_->PlaySound(ClickSound(), 0);
        BriefingNextPhraseOriginal(this, nullptr, 0);
        return;
    case 4:
        interface_->PlaySound(ClickSound(), 0);
        BriefingTakeRewardsOriginal(this);
        return;
    default:
        return;
    }
}

// --- 00603360: input slot 7 ---------------------------------------------------------------------

void CBriefingScreen::OnLeftDouble(int buttons, int x, int y) {
    if (chatActive_) return;
    UiContainerDoubleClick(InputSide(), nullptr, buttons, x, y);
    if (inputHandled_) return;
    if (state_ != 0) {
        Input()->Event(UiInputSide::kLeftDown, buttons, x, y);
        return;
    }
    int point[2] = {x, y};
    CEngineObject* object = ClientUnitAtAt(ClientWorld(), nullptr, point);
    if (IsPerson(object)) {
        for (BriefingMover* mover = movers_->next; mover != movers_; mover = mover->next)
            if (mover->person->id_ == object->id_) return;
        Input()->Event(UiInputSide::kLeftDown, buttons, x, y);
        return;
    }
    if (!noWalk_) WalkLeaderTo(this, x, y, 1);
}

// --- 00608B60 -----------------------------------------------------------------------------------

void CBriefingScreen::OpenTopics(CEngineObject* person) {
    BriefingFindTopicsOriginal(this, nullptr, person->id_);
    if (topics_.size_ <= 0) {
        state_ = 0;
        return;
    }
    if (leader_ && !noWalk_) {
        CUnit* unit = ServerUnit(leader_->id_);
        if (unit && !(unit->flags_ & kUnitBlocked)) ControllerOf(unit)->Brain()->StopAction();
    }
    for (int top = kRowRegionTop; top < kRowRegionEnd; top += kRowHeight)
        AddRegion()->CallSetRect(kRowRegionLeft, top, kRowRegionRight, top + kRowHeight);
    topics_.SetAtGrow(topics_.size_, GoodbyeTopic());
    ScrollbarSetRangeOriginal(topicBar_, nullptr, topics_.size_ > kTopicRows ? topics_.size_ - kTopicRows : 0);
    topicBar_->CallSetPosition(0);
    topicTop_ = 0;
    hoveredTopic_ = -1;
    BriefingDrawTopicsOriginal(this);
    state_ = 1;
}

// --- 006071E0 -----------------------------------------------------------------------------------

void CBriefingScreen::DrawTopics() {
    mfc::CString prefix;
    GameTexts()->GetString(TopicPrefixKey(), &prefix);
    CUiSurface* surface = surfaces_.data_[0];
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    int row = topicTop_;
    for (int bottom = kRowTextFirstBottom; bottom < kRowTextEnd; bottom += kRowHeight, ++row) {
        if (row >= topics_.size_) break;
        RECT rect{kRowTextLeft, bottom - kRowTextHeight, kRowTextRight, bottom};
        std::uint32_t colour = row != hoveredTopic_ ? kTopicColour : kHoveredTopicColour;
        mfc::CString title;
        TopicTitleAt(this, nullptr, &title, topics_[row]);
        mfc::CString text = prefix;
        text += TopicSeparator();
        text += title;
        SurfaceDrawText(surface, nullptr, text, &rect, kTextFormat, colour, 0, 1);
    }
    SurfaceReleaseDC(surface);
}

namespace {
void __fastcall BriefingLeftDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CBriefingScreen>(input)->OnLeftDown(buttons, x, y);
}
void __fastcall BriefingLeftDouble(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CBriefingScreen>(input)->OnLeftDouble(buttons, x, y);
}
} // namespace
EI_REPLACE(0x006030F0, BriefingLeftDown);
EI_REPLACE(0x00603360, BriefingLeftDouble);
EI_REPLACE_METHOD(0x00608B60, CBriefingScreen::OpenTopics);
EI_REPLACE_METHOD(0x006071E0, CBriefingScreen::DrawTopics);
