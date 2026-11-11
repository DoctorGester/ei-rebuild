// The briefing / base screen's dialogue (BriefingScreen, ui_briefing.h): the phrases' start, the
// speakers' animations and the camera on a speaker. See docs/notes/ui.md, "Briefing / base screen".
#include "game/players/player_messages.h"
#include "game/combat/combat.h"
#include "game/ui/ui.h"
#include "game/ui/ui_scroll_text.h"
#include "game/ui/ui_briefing.h"

#include "engine/scene/camera.h"
#include "engine/core/core_maths.h"
#include "engine/data/text_manager.h"
#include "game/objects/object3d.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/str.h"
#include "game/app/singletons.h"
#include "game/players/player.h"
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
constexpr float kSideViewFraction = 0.25f;   // the side views turn a quarter of pi
constexpr float kSunk = -100.0f;

// A speaker who is not in the picture is moved below the ground.
void Sink(Unit* unit) {
    if (unit) g_serverWorld.SetUnitHeight(unit->id, kSunk);
}

constexpr int kEventLeaves = 2;

} // namespace


void BriefingScreen::StartPhrases() {
    if (voice != -1) g_clientWorld.sounds->StopSound(voice);
    voice = -1;
    g_clientWorld.sounds->FadeOutStream();
    std::string key = kBriefingSection;
    key += topic;
    if (!g_clientWorld.texts->TextManager::Open(key)) {
        state = 0;
        return;
    }
    PlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
    if (server) server->SendUnitControl(kEventPhrases, 0);

    // The camera's position, and that plus its rotation's z axis (third column of the matrix).
    const Camera* camera = &g_camera;
    savedCamera = camera->localPosition;
    lookAt = savedCamera + camera->localRotation.AxisZ();

    dialogueText.clear();
    text->SetText("");
    state = kStatePhrases;
    firstPhrase = true;
    NextPhrase(false);
}

void BriefingScreen::PlaySpeakerAnimation(Unit* unit, bool talking, bool thirdPerson) {
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
    StartSpeakerAnimation(unit, number);
}

// SpeakerAnimationDone, the speakers' controllers' actionDone. When
// a server unit's dialogue animation has ended, its client unit, when it is one of the briefing's
// speakers, plays a listening animation again (once per entry). The game takes the active window to be the
// briefing screen unchecked; ours does nothing under any other. The count is read again after each call.
void SpeakerAnimationDone(Unit* serverUnit) {
    if (!serverUnit) return;
    auto* unit = g_clientWorld.FindUnit(serverUnit->id);
    if (!unit) return;
    BriefingScreen* screen = g_interface.OnTop(g_interface.briefing);
    if (!screen) return;
    for (const BriefingSpeaker& speaker : screen->speakers)
        if (speaker.unit == unit) screen->PlaySpeakerAnimation(unit, false, false);
}

void BriefingScreen::ViewSpeaker(Unit* unit, float angle, float distance, float height) {
    float cosAngle = lib::Cos(angle);
    float sinAngle = lib::Sin(angle);
    Vector3 eye{};
    float x;   // the facing
    float y;
    if (!unit) {
        // The middle of the first two places, looked at across the line between them.
        eye = (places[kPersonPlace] + places[kLeaderPlace]) * 0.5f;
        x = places[kLeaderPlace].y - places[kPersonPlace].y;
        y = places[kPersonPlace].x - places[kLeaderPlace].x;
    } else {
        Vector3 place = unit->localPosition;
        Vector3 axis = unit->localRotation.AxisY();
        Vector3 facing = -axis;
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
        float reachX = facing.x * reach;
        float reachY = facing.y * reach;
        eye.x = reachX + place.x;
        eye.y = place.y + reachY;
        eye.z = place.z + prototype->viewHeight;
        x = facing.x;
        y = facing.y;
    }
    float length = std::sqrt(LengthSquared2D(y, x));
    // Compared with zero.
    if (length != 0.0f) {
        float inverse = 1.0f / length;
        x *= inverse;
        y *= inverse;
    }
    Vector3 at;
    float alongX = (x * cosAngle + y * sinAngle) * distance;
    float alongY = (y * cosAngle - x * sinAngle) * distance;
    at = eye + Vector3{alongX, alongY, height};
    g_serverWorld.GetPlayer(0)->Server()->SendFreeCamera(at, eye, 1);
}

void BriefingScreen::ViewPreset(int preset) {
    float apart = std::sqrt(DistanceSquaredXY(places[kLeaderPlace], places[kPersonPlace]));
    auto view = [this](Unit* unit, float angle, float distance, float height) {
        ViewSpeaker(unit, angle, distance, height);
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
        if (leader) view(leader, kPi * -kSideViewFraction, 3.5f, 3.0f);
        break;
    case 7:
        Sink(leader);
        if (person) view(person, kPi * kSideViewFraction, 3.5f, 3.0f);
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

void BriefingScreen::HideSpeaker(std::string_view name, bool immediately, bool run) {
    auto* speaker = g_clientWorld.FindUnit(TextKeyFromName(name));
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
        PlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
        if (server) server->SendUnitControl(kEventLeaves, speaker->id);
        StopTalking(speaker);
        ReleaseSpeaker(speaker);
        speakers.erase(entry);
    }
    if (speaker == third) {
        MovePerson(speaker, thirdPlace, thirdDirection, false, immediately, run);
        third = nullptr;
    }
}
