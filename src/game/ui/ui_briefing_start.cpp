// The briefing / base screen's dialogue start and end (BriefingScreen::StartDialogue /
// FinishDialogue, ui_briefing.h): who takes part, where they stand, how they get there and back.
// See docs/notes/ui.md, "Briefing / base screen".
#include "game/world/camera.h"
#include "game/players/class_templates.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "engine/data/text_manager.h"
#include "game/players/party_manager.h"
#include "game/players/player_messages.h"
#include "game/sound/sound.h"
#include "engine/core/text_split.h"
#include "game/ui/ui_briefing.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/players/player.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/players/player_client.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

constexpr const char* kGoodbyeTopic = "goodbye";
constexpr const char* kConstructorTopic = "constr";
constexpr const char* kBriefingSection = "briefing ";
constexpr const char* kShowLine = "#show";

constexpr int kStateWalking = 0;
constexpr int kEventEnds = 0;
constexpr int kEventLeaves = 2;
constexpr int kConstructorPrefix = 6;    // "constr"
constexpr int kConstructorNumber = 7;    // where the constructor's number starts
constexpr int kCampScreen = 5;
constexpr int kStateStarting = 2;
constexpr float kSpacing = 2.5f;         // added to the prototype's viewDistance

Vector3 Facing(const Unit* unit) { return -unit->localRotation.AxisY(); }

// The first mover of `person` is dropped; its target goes to `place` / `direction` unless the
// dialogue waited for it.
void DropMover(std::vector<BriefingMover>& movers, const Unit* person, Vector3* place, Vector3* direction) {
    for (auto node = movers.begin(); node != movers.end(); ++node) {
        if (node->person != person) continue;
        if (!node->waited) {
            *place = node->target;
            *direction = node->direction;
        }
        movers.erase(node);
        return;
    }
}

} // namespace


void BriefingScreen::StartDialogue(bool newImmediate, bool remember) {
    immediate = newImmediate;
    if (!leader) {
        state = 0;
        briefingStarted = false;
        return;
    }
    if (text::CompareIgnoringCase(topic, kGoodbyeTopic) == 0) {
        state = 0;
        return;
    }
    if (text::CompareIgnoringCase(str::Left(topic, kConstructorPrefix), kConstructorTopic) == 0) {
        int constructor = str::ToInt(str::Mid(topic, kConstructorNumber));
        if (g_partyManager.templates.Find(constructor)) {
            g_serverWorld.SetConstructionVariable(static_cast<float>(constructor));
            g_interface.closeAll = 1;
            g_clientWorld.LocalView()->RequestGameState(kCampScreen);
        }
        state = 0;
        return;
    }
    person = nullptr;
    third = nullptr;
    TextManager* texts = g_clientWorld.texts.get();
    std::string key = kBriefingSection;
    if (!texts->Open((key + topic))) {
        state = 0;
        briefingStarted = false;
        return;
    }
    std::vector<std::string> words;
    while (texts->FindLine(kShowLine)) {
        words = texts->Split(true);
        if (words.size() < 2) continue;
        int textKey = TextKeyFromName(words[1]);
        if (textKey == leader->id) continue;
        auto* unit = g_clientWorld.FindUnit(textKey);
        if (!unit) continue;
        if (!person)
            person = unit;
        else if (person != unit)
            third = unit;
    }
    if (!person) {
        state = 0;
        briefingStarted = false;
        return;
    }

    auto* prototype = person->GetPrototype();
    float reach = prototype->viewDistance + kSpacing;
    Vector3& personPlace = places[0];
    Vector3& leaderPlace = places[1];
    Vector3& newThirdPlace = places[2];
    // The one who stands still and the one who walks up to it (2.5 + viewDistance away, in front).
    Unit* standing = noWalk ? leader : person;
    Vector3& standingPlace = noWalk ? leaderPlace : personPlace;
    Vector3& walkingPlace = noWalk ? personPlace : leaderPlace;
    Vector3 axis = standing->localRotation.AxisY();
    Vector3 at = standing->localPosition;
    walkingPlace = at - axis * reach;
    standingPlace = at;
    // The third place: the middle, moved sideways by the person's offset from it.
    const Vector3 middle = (personPlace + leaderPlace) * 0.5f;
    newThirdPlace = middle;
    newThirdPlace.x = (personPlace.y - middle.y) + middle.x;
    newThirdPlace.y = (middle.x - personPlace.x) + middle.y;
    if (noWalk) {
        if (remember) {
            personHome = person->localPosition;
            personHomeDirection = Facing(person);
        }
    } else {
        MovePerson(leader, leaderPlace, personPlace - leaderPlace, true, immediate, false);
    }
    if (third && remember) {
        thirdPlace = third->localPosition;
        thirdDirection = Facing(third);
    }

    directions[0] = leaderPlace - personPlace;
    directions[0].z = 0.0f;
    directions[1] = personPlace - leaderPlace;
    directions[1].z = 0.0f;
    directions[2] = middle - newThirdPlace;
    directions[2].z = 0.0f;
    for (Vector3& direction : directions) direction = NormalisedUnlessZero(direction);

    if (third) DropMover(movers, third, &thirdPlace, &thirdDirection);
    if (noWalk) DropMover(movers, person, &personHome, &personHomeDirection);
    if (third) {
        MovePerson(third, newThirdPlace, middle - newThirdPlace, true, immediate,
                   false);
    }
    if (noWalk) {
        MovePerson(person, personPlace, leaderPlace - personPlace, true, immediate, false);
    }
    if (immediate)
        StartPhrases();
    else
        state = kStateStarting;
}

void BriefingScreen::ClearShownItems() {
    for (const BriefingShownItem& item : shown) {
        if (item.item) {
            RemoveObject(item.item->texture);
            RemoveSceneObject(item.item);
        }
        if (item.back) RemoveSceneObject(item.back);
        if (item.frame) RemoveSceneObject(item.frame);
    }
    shown.clear();
}

void BriefingScreen::FinishDialogue() {
    if (voice != -1) g_clientWorld.sounds->StopSound(voice);
    voice = -1;
    g_clientWorld.sounds->FadeInStream();
    ClearShownItems();

    for (const BriefingSpeaker& speaker : speakers) {
        Unit* unit = speaker.unit;
        PlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
        if (server) server->SendUnitControl(kEventLeaves, unit->id);
        StopTalking(unit);
        ReleaseSpeaker(unit);
    }
    speakers.clear();
    speaking = nullptr;

    if (leader) g_serverWorld.SetUnitHeight(leader->id, 0.0f);
    if (person) g_serverWorld.SetUnitHeight(person->id, 0.0f);
    if (third) {
        MovePerson(third, thirdPlace, thirdDirection, false, false, false);
        third = nullptr;
    }
    if (noWalk && person) {
        MovePerson(person, personHome, personHomeDirection, false, false, false);
    }

    std::vector<std::string> parts;
    for (auto& node : PlayerZeroVariables(kTopicPrefix)) {
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts || text::CompareIgnoringCase(topic, parts[2]) != 0) continue;
        g_serverWorld.CompleteBriefing(0, node.first);
        break;
    }

    state = kStateWalking;
    PlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
    if (server) server->SendUnitControl(kEventEnds, leader->id);
    Vector3 at = leader->localPosition;
    if (briefingStarted) {
        g_serverWorld.GetPlayer(0)->Server()->SendCameraPlacement(at, 0, 1);
        g_camera.flags &= ~Camera::kFree;
        briefingStarted = false;
    } else {
        g_serverWorld.GetPlayer(0)->Server()->SendFreeCamera(savedCamera, lookAt, 0);
    }
}
