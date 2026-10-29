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
#include "text_manager.h"
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
#include "lib/vecmath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace {

constexpr const char* kBriefingSection = "briefing ";
constexpr const char* kHumanPrototype = "unhuma";

constexpr int kEventPhrases = 3;
constexpr int kStatePhrases = 3;

// The speakers' animations (the animation tables' names).
constexpr SkillAnimation kTalkThird = SkillAnimation::Briefing03;
constexpr SkillAnimation kTalk = SkillAnimation::Briefing01;
constexpr SkillAnimation kListen = SkillAnimation::Briefing02;
constexpr SkillAnimation kHumanTalk[4] = {SkillAnimation::Briefing01, SkillAnimation::Briefing06,
                                          SkillAnimation::Briefing07, SkillAnimation::Briefing08};
constexpr SkillAnimation kHumanListen[4] = {SkillAnimation::Briefing02, SkillAnimation::Briefing10,
                                            SkillAnimation::Briefing12, SkillAnimation::Briefing13};

constexpr int kPersonPlace = 0;
constexpr int kLeaderPlace = 1;
constexpr int kThirdPlace = 2;

constexpr float kSixth = 0.166666672f;
constexpr float kTwelfth = 0.0833333358f;
constexpr float kQuarter = 0.25f;
constexpr float kSunk = -100.0f;

// A speaker who is not in the picture is moved below the ground.
void Sink(CUnit* unit) {
    if (unit) GameSetUnitHeight(unit->id, kSunk);
}

constexpr int kEventLeaves = 2;

} // namespace


void CBriefingScreen::StartPhrases() {
    if (voice != -1) g_clientWorld.sounds->StopSound(voice);
    voice = -1;
    g_clientWorld.music->FadeOut();
    std::string key = kBriefingSection;
    key += topic;
    if (!g_clientWorld.texts->CTextManager::Open(key)) {
        state = 0;
        return;
    }
    CPlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
    if (server) server->SendPair19(kEventPhrases, 0);

    // The camera's position, and that plus its rotation's z axis (third column of the matrix).
    const CCamera* camera = &g_camera;
    savedCamera = camera->localPosition;
    const Quaternion& q = camera->localRotation;
    double zz = q.z * q.z;
    double axisZ = (q.w * q.w - (LengthSquared2DUnrounded(q.x, q.y) + zz)) + (zz + zz);
    float axisY = static_cast<float>((q.y * q.z - q.x * q.w) * 2.0);
    // x and y are doubled before multiplying; here the compiler doubles z instead of x,
    // which gives the same value (doubling is exact). Dot2D(x, y, z, w) * 2.0 would regroup the sum.
    float axisX = static_cast<float>(Dot2DUnrounded<double>(q.x * 2.0, q.y * 2.0, q.z, q.w));
    lookAt.x = static_cast<float>(savedCamera.x + axisX);
    lookAt.y = static_cast<float>(savedCamera.y + axisY);
    lookAt.z = static_cast<float>(axisZ + savedCamera.z);

    dialogueText.clear();
    text->SetText(game::kEmptyText);
    state = kStatePhrases;
    firstPhrase = true;
    NextPhrase(false);
}

void CBriefingScreen::PlaySpeakerAnimation(CUnit* unit, bool talking, bool thirdPerson) {
    // (The name is compared through a copy.)
    bool human = str::Compare(unit->name, kHumanPrototype) == 0;
    SkillAnimation number;
    if (talking) {
        if (thirdPerson)
            number = kTalkThird;
        else
            number = human ? kHumanTalk[g_clientRandom.Next() & 3] : kTalk;
    } else {
        number = human ? kHumanListen[g_clientRandom.Next() & 3] : kListen;
    }
    auto* serverUnit = g_serverWorld.FindUnit(unit->id);
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
    auto* unit = g_clientWorld.FindUnit(serverUnit->id);
    if (!unit) return;
    auto* screen = g_interface.ActiveWindowAs<CBriefingScreen>();
    for (std::size_t i = 0; i < screen->speakers.size(); ++i)
        if (screen->speakers[i].unit == unit) screen->PlaySpeakerAnimation(unit, false, false);
}

void CBriefingScreen::ViewSpeaker(CUnit* unit, float angle, float distance, float height) {
    float cosAngle = static_cast<float>(x87::fcos(angle));
    float sinAngle = static_cast<float>(x87::fsin(angle));
    Vector3 eye{};
    double x;   // the facing, kept unrounded
    double y;
    if (!unit) {
        // The middle of the first two places, looked at across the line between them.
        double sumX = places[kPersonPlace].x + places[kLeaderPlace].x;
        float sumY = static_cast<float>(places[kPersonPlace].y + places[kLeaderPlace].y);
        float sumZ = static_cast<float>(places[kPersonPlace].z + places[kLeaderPlace].z);
        eye.x = static_cast<float>(sumX * 0.5);
        eye.y = static_cast<float>(sumY * 0.5f);
        eye.z = static_cast<float>(sumZ * 0.5f);
        x = static_cast<double>(places[kLeaderPlace].y) - places[kPersonPlace].y;
        y = static_cast<double>(places[kPersonPlace].x) - places[kLeaderPlace].x;
    } else {
        Vector3 place{unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
        Vector3 axis;
        unit->localRotation.AxisY(&axis);
        Vector3 facing{-axis.x, -axis.y, -axis.z};
        int index = -1;
        if (unit == leader)
            index = kLeaderPlace;
        else if (unit == person)
            index = kPersonPlace;
        else if (unit == third)
            index = kThirdPlace;
        if (index >= 0) {
            place = places[index];
            facing = directions[index];
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
    // Compared with zero.
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
    g_serverWorld.GetPlayer(0)->Server()->SendPosition0F(at, eye, 1);
}

void CBriefingScreen::ViewPreset(int preset) {
    double dx = static_cast<double>(places[kLeaderPlace].x) - places[kPersonPlace].x;
    double dy = static_cast<double>(places[kLeaderPlace].y) - places[kPersonPlace].y;
    float apart = static_cast<float>(std::sqrt(LengthSquared2DUnrounded(dy, dx)));
    auto view = [this](CUnit* unit, float angle, float distance, float height) {
        ViewSpeaker(unit, static_cast<float>(angle), static_cast<float>(distance), height);
    };
    switch (preset) {
    case 1:
        view(nullptr, 0.0f, apart * 3.0f, 1.5f);
        break;
    case 2:
        Sink(person);
        if (leader) view(leader, kPi * -kSixth, 4.0f, 1.5f);
        break;
    case 3:
        Sink(leader);
        if (person) view(person, kPi * kSixth, 4.0f, 1.5f);
        break;
    case 4:
        if (third) view(third, 0.0f, 4.0f, 1.5f);
        break;
    case 5:
        view(nullptr, kPi * kTwelfth, apart * 2.7f, 3.0f);
        break;
    case 6:
        Sink(person);
        if (leader) view(leader, kPi * -kQuarter, 3.5f, 3.0f);
        break;
    case 7:
        Sink(leader);
        if (person) view(person, kPi * kQuarter, 3.5f, 3.0f);
        break;
    case 8:
        if (third) view(third, kPi * kTwelfth, 3.5f, 3.0f);
        break;
    case 9:
        view(nullptr, kPi * -kTwelfth, apart * 2.4f, 3.0f);
        break;
    case 10:
        if (leader) view(leader, kPi * -kTwelfth, 3.0f, 1.0f);
        break;
    case 11:
        if (person) view(person, kPi * kTwelfth, 3.0f, 1.0f);
        break;
    case 12:
        Sink(person);
        Sink(leader);
        if (third) view(third, kPi * -kTwelfth, 3.0f, 1.0f);
        break;
    default:
        break;
    }
}

void CBriefingScreen::HideSpeaker(std::string_view name, bool immediately, bool run) {
    auto* speaker = GameFindClientUnit(TextKeyFromName(name));
    if (!speaker) {
        for (auto node = shown.begin(); node != shown.end(); ++node) {
            if (text::CompareIgnoringCase(node->name,
                                name) != 0)
                continue;
            if (node->item) {
                RemoveObject(node->item->texture);
                RemoveSceneObject(node->item);
            }
            if (node->back) RemoveSceneObject(node->back);
            if (node->frame) RemoveSceneObject(node->frame);
            shown.erase(node);
            return;
        }
        return;
    }
    auto entry = std::find_if(speakers.begin(), speakers.end(), [speaker](const BriefingSpeaker& s) { return s.unit == speaker; });
    if (entry != speakers.end()) {
        CPlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
        if (server) server->SendPair19(kEventLeaves, speaker->id);
        CUnitClientSpecific* model = speaker->ClientController();
        model->blinks = 0;
        model->BlinkOpen(true);
        if (auto* serverUnit = g_serverWorld.FindUnit(speaker->id)) {
            serverUnit->ShowHelmet(true);
            GameSetUnitHeight(speaker->id, 0.0f);
            auto* controller = serverUnit->ServerController();
            controller->actionDone = nullptr;
            controller->StopAction();
            // (The person is then looked up among the party's first members; the result is not used.)
        }
        speakers.erase(entry);
    }
    if (speaker == third) {
        MovePerson(speaker, thirdPlace, thirdDirection, false, immediately, run);
        third = nullptr;
    }
}
