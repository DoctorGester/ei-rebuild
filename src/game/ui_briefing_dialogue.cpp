// The briefing / base screen's dialogue (CBriefingScreen, ui_briefing.h): the phrases' start, the
// speakers' animations and the camera on a speaker. See docs/notes/ui.md, "Briefing / base screen".
#include "player_messages.h"
#include "game_strings.h"
#include "world_glue.h"
#include "combat.h"
#include "ui.h"
#include "ui_briefing.h"

#include "camera.h"
#include "core_maths.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "random.h"
#include "sound.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "game_singletons.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace {

const char* BriefingSection() { return "briefing "; }
const char* HumanPrototype() { return "unhuma"; }

CPlayer* LocalServerPlayer() { return g_serverWorld->GetPlayer(0); }

constexpr int kEventPhrases = 3;
constexpr int kStatePhrases = 3;

// The skill animation numbers (the tables).
constexpr int kTalkThird = 3;
constexpr int kTalk = 1;
constexpr int kListen = 2;
constexpr int kHumanTalk[4] = {1, 6, 7, 8};
constexpr int kHumanListen[4] = {2, 0xA, 0xC, 0xD};

constexpr int kPersonPlace = 0;
constexpr int kLeaderPlace = 1;
constexpr int kThirdPlace = 2;

constexpr float kPi = 3.14159274f;
constexpr float kSixth = 0.166666672f;
constexpr float kTwelfth = 0.0833333358f;
constexpr float kQuarter = 0.25f;
constexpr float kSunk = -100.0f;

// A speaker who is not in the picture is moved below the ground.
void Sink(CUnit* unit) {
    if (unit) GameSetUnitHeight(unit->id_, kSunk);
}

constexpr int kEventLeaves = 2;

} // namespace


void CBriefingScreen::StartPhrases() {
    if (voice_ != -1) TheSoundManager()->StopSound(voice_);
    voice_ = -1;
    TheMusicSystem()->FadeOut();
    std::string key = BriefingSection();
    key += topic_;
    if (!TheTextManager()->CTextManager::Open(key.c_str())) {
        state_ = 0;
        return;
    }
    CPlayerServer* server = LocalServerPlayer()->Server();
    if (server) server->SendPair19(kEventPhrases, 0);

    // The camera's position, and that plus its rotation's z axis (third column of the matrix).
    const CCamera* camera = &g_camera.Get();
    savedCamera_ = camera->localPosition_;
    const Quaternion& q = camera->localRotation_;
    double zz = q.z * q.z;
    double axisZ = (q.w * q.w - (LengthSquared2DUnrounded(q.x, q.y) + zz)) + (zz + zz);
    float axisY = static_cast<float>((q.y * q.z - q.x * q.w) * 2.0);
    // x and y are doubled before multiplying; here the compiler doubles z instead of x,
    // which gives the same value (doubling is exact). Dot2D(x, y, z, w) * 2.0 would regroup the sum.
    float axisX = static_cast<float>(Dot2DUnrounded<double>(q.x * 2.0, q.y * 2.0, q.z, q.w));
    lookAt_.x = static_cast<float>(savedCamera_.x + axisX);
    lookAt_.y = static_cast<float>(savedCamera_.y + axisY);
    lookAt_.z = static_cast<float>(axisZ + savedCamera_.z);

    dialogueText_.clear();
    text_->SetText(game::kEmptyText);
    state_ = kStatePhrases;
    firstPhrase_ = true;
    NextPhrase(0);
}

void CBriefingScreen::PlaySpeakerAnimation(CUnit* unit, bool talking, bool third) {
    // (The name is compared through a CString copy.)
    bool human = str::Compare(unit->name, HumanPrototype()) == 0;
    int number;
    if (talking) {
        if (third)
            number = kTalkThird;
        else
            number = human ? kHumanTalk[g_random.Next() & 3] : kTalk;
    } else {
        number = human ? kHumanListen[g_random.Next() & 3] : kListen;
    }
    auto* serverUnit = g_serverWorld->FindUnit(unit->id_);
    if (!serverUnit) return;
    auto* controller = serverUnit->ServerController();
    controller->StopAction();
    controller->actionDone = SpeakerAnimationDone;
    if (FigureAnimation* animation = unit->Object3D()->FindSkillAnimation(number))
        controller->PlayAnimation(animation->name);
}

// SpeakerAnimationDone, the speakers' controllers' actionDone. When
// a server unit's dialogue animation has ended, its client unit, when it is one of the briefing's
// speakers, plays a listening animation again (once per entry). The active window is taken to be the
// briefing screen unchecked; the count is read again after each call.
void SpeakerAnimationDone(CUnit* serverUnit) {
    if (!serverUnit) return;
    auto* unit = g_clientWorld->FindUnit(serverUnit->id_);
    if (!unit) return;
    auto* screen = g_interface->ActiveWindowAs<CBriefingScreen>();
    for (std::size_t i = 0; i < screen->speakers_.size(); ++i)
        if (screen->speakers_[i].unit == unit) screen->PlaySpeakerAnimation(unit, false, false);
}

void CBriefingScreen::ViewSpeaker(CUnit* unit, float angle, float distance, float height) {
    float cosAngle = static_cast<float>(x87::fcos(angle));
    float sinAngle = static_cast<float>(x87::fsin(angle));
    Vector3 eye{};
    double x;   // the facing, kept unrounded
    double y;
    if (!unit) {
        // The middle of the first two places, looked at across the line between them.
        double sumX = places_[kPersonPlace].x + places_[kLeaderPlace].x;
        float sumY = static_cast<float>(places_[kPersonPlace].y + places_[kLeaderPlace].y);
        float sumZ = static_cast<float>(places_[kPersonPlace].z + places_[kLeaderPlace].z);
        eye.x = static_cast<float>(sumX * 0.5);
        eye.y = static_cast<float>(sumY * 0.5f);
        eye.z = static_cast<float>(sumZ * 0.5f);
        x = static_cast<double>(places_[kLeaderPlace].y) - places_[kPersonPlace].y;
        y = static_cast<double>(places_[kPersonPlace].x) - places_[kLeaderPlace].x;
    } else {
        Vector3 place{unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z};
        Vector3 axis;
        unit->localRotation_.AxisY(&axis);
        Vector3 facing{-axis.x, -axis.y, -axis.z};
        int index = -1;
        if (unit == leader_)
            index = kLeaderPlace;
        else if (unit == person_)
            index = kPersonPlace;
        else if (unit == third_)
            index = kThirdPlace;
        if (index >= 0) {
            place = places_[index];
            facing = directions_[index];
        }
        auto* prototype = unit->GetPrototype();
        float reach = prototype->viewDistance;
        float reachX = static_cast<float>(facing.x * reach);
        float reachY = static_cast<float>(facing.y * reach);
        eye.x = static_cast<float>(reachX + place.x);
        eye.y = static_cast<float>(place.y + reachY);
        eye.z = static_cast<float>(place.z + prototype->viewHeight);
        x = facing.x;
        y = facing.y;
    }
    double length = std::sqrt(LengthSquared2DUnrounded(y, x));
    // FCOM with zero, TEST AH,0x40.
    EI_NAN_AUDIT(length);
    if (length != 0.0) {
        double inverse = 1.0 / length;
        x *= inverse;
        y *= inverse;
    }
    Vector3 at;
    float alongX = static_cast<float>(x * cosAngle + y * sinAngle);
    alongX = static_cast<float>(alongX * distance);
    double alongY = (y * cosAngle - x * sinAngle) * distance;
    at.x = static_cast<float>(eye.x + alongX);
    at.y = static_cast<float>(eye.y + alongY);
    at.z = static_cast<float>(eye.z + height);
    LocalServerPlayer()->Server()->SendPosition0F(&at, &eye, 1);
}

void CBriefingScreen::ViewPreset(int preset) {
    double dx = static_cast<double>(places_[kLeaderPlace].x) - places_[kPersonPlace].x;
    double dy = static_cast<double>(places_[kLeaderPlace].y) - places_[kPersonPlace].y;
    float apart = static_cast<float>(std::sqrt(LengthSquared2DUnrounded(dy, dx)));
    auto view = [this](CUnit* unit, float angle, float distance, float height) {
        ViewSpeaker(unit, static_cast<float>(angle), static_cast<float>(distance), height);
    };
    switch (preset) {
    case 1:
        view(nullptr, 0.0f, apart * 3.0f, 1.5f);
        break;
    case 2:
        Sink(person_);
        if (leader_) view(leader_, kPi * -kSixth, 4.0f, 1.5f);
        break;
    case 3:
        Sink(leader_);
        if (person_) view(person_, kPi * kSixth, 4.0f, 1.5f);
        break;
    case 4:
        if (third_) view(third_, 0.0f, 4.0f, 1.5f);
        break;
    case 5:
        view(nullptr, kPi * kTwelfth, apart * 2.7f, 3.0f);
        break;
    case 6:
        Sink(person_);
        if (leader_) view(leader_, kPi * -kQuarter, 3.5f, 3.0f);
        break;
    case 7:
        Sink(leader_);
        if (person_) view(person_, kPi * kQuarter, 3.5f, 3.0f);
        break;
    case 8:
        if (third_) view(third_, kPi * kTwelfth, 3.5f, 3.0f);
        break;
    case 9:
        view(nullptr, kPi * -kTwelfth, apart * 2.4f, 3.0f);
        break;
    case 10:
        if (leader_) view(leader_, kPi * -kTwelfth, 3.0f, 1.0f);
        break;
    case 11:
        if (person_) view(person_, kPi * kTwelfth, 3.0f, 1.0f);
        break;
    case 12:
        Sink(person_);
        Sink(leader_);
        if (third_) view(third_, kPi * -kTwelfth, 3.0f, 1.0f);
        break;
    default:
        break;
    }
}

void CBriefingScreen::HideSpeaker(const char* name, int immediate, int time) {
    auto* person = GameFindClientUnit(TextKeyFromName(name));
    if (!person) {
        for (auto node = shown_.begin(); node != shown_.end(); ++node) {
            if (text::CompareIgnoringCase(node->name,
                                name) != 0)
                continue;
            if (node->item) {
                RemoveObject(node->item->texture_);
                RemoveSceneObject(node->item);
            }
            if (node->back) RemoveSceneObject(node->back);
            if (node->frame) RemoveSceneObject(node->frame);
            shown_.erase(node);
            return;
        }
        return;
    }
    auto entry = std::find_if(speakers_.begin(), speakers_.end(), [person](const BriefingSpeaker& s) { return s.unit == person; });
    if (entry != speakers_.end()) {
        CPlayerServer* server = LocalServerPlayer()->Server();
        if (server) server->SendPair19(kEventLeaves, person->id_);
        CUnitClientSpecific* model = person->ClientController();
        model->blinks = 0;
        model->BlinkOpen(true);
        if (auto* serverUnit = g_serverWorld->FindUnit(person->id_)) {
            serverUnit->SetFlag40000000Off(true);
            GameSetUnitHeight(person->id_, 0.0f);
            auto* controller = serverUnit->ServerController();
            controller->actionDone = nullptr;
            controller->StopAction();
            // (The person is then looked up among the party's first members; the result is not used.)
        }
        speakers_.erase(entry);
    }
    if (person == third_) {
        MovePerson(person, thirdPlace_.x, thirdPlace_.y, thirdPlace_.z, thirdDirection_.x,
                                   thirdDirection_.y, thirdDirection_.z, 0, immediate, time);
        third_ = nullptr;
    }
}
