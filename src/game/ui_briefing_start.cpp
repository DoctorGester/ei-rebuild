// The briefing / base screen's dialogue start and end (CBriefingScreen::StartDialogue /
// FinishDialogue, ui_briefing.h): who takes part, where they stand, how they get there and back.
// See docs/notes/ui.md, "Briefing / base screen".
#include "camera.h"
#include "class_templates.h"
#include "combat.h"
#include "console.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "text_manager.h"
#include "object_tables.h"
#include "party_manager.h"
#include "player_messages.h"
#include "sound.h"
#include "text_split.h"
#include "ui_briefing.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"
#include "world_glue.h"

#include "lib/nan_audit.h"
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
constexpr const char* kTopicPrefix = "b.";

constexpr int kStateWalking = 0;
constexpr int kEventEnds = 0;
constexpr int kEventLeaves = 2;
constexpr int kTopicNameParts = 3;
constexpr int kConstructorPrefix = 6;    // "constr"
constexpr int kConstructorNumber = 7;    // where the constructor's number starts
constexpr int kCampScreen = 5;
constexpr int kStateStarting = 2;
constexpr float kSpacing = 2.5f;         // added to the prototype's viewDistance

Vector3 Facing(const CUnit* unit) {
    Vector3 axis;
    unit->localRotation.AxisY(&axis);
    return Vector3{-axis.x, -axis.y, -axis.z};
}

Vector3 Middle(const Vector3& a, const Vector3& b) {
    return Vector3{static_cast<float>((a.x + b.x) * 0.5f), static_cast<float>((a.y + b.y) * 0.5f), (a.z + b.z) * 0.5f};
}

Vector3 Difference(const Vector3& a, const Vector3& b) { return Vector3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// Normalised unless its length is zero.
void Normalise(Vector3* v) {
    float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, float>(*v)));
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return;
    float inverse = 1.0f / length;
    v->x = inverse * v->x;
    v->y = inverse * v->y;
    v->z = inverse * v->z;
}

// The first mover of `person` is dropped; its target goes to `place` / `direction` unless the
// dialogue waited for it.
void DropMover(std::list<BriefingMover>& movers, const CUnit* person, Vector3* place, Vector3* direction) {
    for (auto node = movers.begin(); node != movers.end(); ++node) {
        if (node->person != person) continue;
        if (!node->waited) {
            *place = Vector3{node->target.x, node->target.y, node->target.z};
            *direction = Vector3{node->direction.x, node->direction.y, node->direction.z};
        }
        movers.erase(node);
        return;
    }
}

} // namespace


void CBriefingScreen::StartDialogue(bool newImmediate, bool remember) {
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
            GameSetConstructionVariable(std::bit_cast<std::uint32_t>(static_cast<float>(constructor)));
            GameSendLeave04(kCampScreen);
        }
        state = 0;
        return;
    }
    person = nullptr;
    third = nullptr;
    CTextManager* texts = g_clientWorld.texts;
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
        auto* unit = GameFindClientUnit(textKey);
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
    float reach = static_cast<float>(prototype->viewDistance + kSpacing);
    Vector3& personPlace = places[0];
    Vector3& leaderPlace = places[1];
    Vector3& newThirdPlace = places[2];
    // The one who stands still and the one who walks up to it (2.5 + viewDistance away, in front).
    CUnit* standing = noWalk ? leader : person;
    Vector3& standingPlace = noWalk ? leaderPlace : personPlace;
    Vector3& walkingPlace = noWalk ? personPlace : leaderPlace;
    Vector3 axis;
    standing->localRotation.AxisY(&axis);
    Vector3 at = standing->localPosition;
    float stepY = static_cast<float>(axis.y * reach);
    float stepX = static_cast<float>(axis.x * reach);
    walkingPlace = Vector3{at.x - stepX, at.y - stepY, at.z - axis.z * reach};
    standingPlace = at;
    // The third place: the middle, moved sideways by the person's offset from it.
    Vector3 middle = Middle(personPlace, leaderPlace);
    newThirdPlace = middle;
    newThirdPlace.x = (personPlace.y - middle.y) + middle.x;
    newThirdPlace.y = (middle.x - personPlace.x) + middle.y;
    if (noWalk) {
        if (remember) {
            personHome = person->localPosition;
            personHomeDirection = Facing(person);
        }
    } else {
        MovePerson(leader, leaderPlace, Difference(personPlace, leaderPlace), true, immediate, false);
    }
    if (third && remember) {
        thirdPlace = third->localPosition;
        thirdDirection = Facing(third);
    }

    directions[0] = Difference(leaderPlace, personPlace);
    directions[0].z = 0.0f;
    directions[1] = Difference(personPlace, leaderPlace);
    directions[1].z = 0.0f;
    directions[2] = Difference(Middle(personPlace, leaderPlace), newThirdPlace);
    directions[2].z = 0.0f;
    for (Vector3& direction : directions) Normalise(&direction);

    if (third) DropMover(movers, third, &thirdPlace, &thirdDirection);
    if (noWalk) DropMover(movers, person, &personHome, &personHomeDirection);
    if (third) {
        MovePerson(third, newThirdPlace, Difference(Middle(personPlace, leaderPlace), newThirdPlace), true, immediate,
                   false);
    }
    if (noWalk) {
        MovePerson(person, personPlace, Difference(leaderPlace, personPlace), true, immediate, false);
    }
    if (immediate)
        StartPhrases();
    else
        state = kStateStarting;
}

void CBriefingScreen::ClearShownItems() {
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

void CBriefingScreen::FinishDialogue() {
    if (voice != -1) g_clientWorld.sounds->StopSound(voice);
    voice = -1;
    g_clientWorld.music->FadeIn();
    ClearShownItems();

    for (const BriefingSpeaker& speaker : speakers) {
        CUnit* unit = speaker.unit;
        CPlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
        if (server) server->SendPair19(kEventLeaves, unit->id);
        CUnitClientSpecific* model = unit->ClientController();
        model->blinks = 0;
        model->BlinkOpen(true);
        auto* serverUnit = g_serverWorld.FindUnit(unit->id);
        if (!serverUnit) continue;
        serverUnit->ShowHelmet(true);
        GameSetUnitHeight(unit->id, 0.0f);
        auto* controller = serverUnit->ServerController();
        controller->actionDone = nullptr;
        controller->StopAction();
        // (The unit is then looked up among the party's first members; the result is not used.)
    }
    speakers.clear();
    speaking = nullptr;

    if (leader) GameSetUnitHeight(leader->id, 0.0f);
    if (person) GameSetUnitHeight(person->id, 0.0f);
    if (third) {
        MovePerson(third, thirdPlace, thirdDirection, false, false, false);
        third = nullptr;
    }
    if (noWalk && person) {
        MovePerson(person, personHome, personHomeDirection, false, false, false);
    }

    NamedFloatTable variables;
    PlayerZeroVariables(kTopicPrefix, &variables);
    std::vector<std::string> parts;
    for (auto& node : variables) {
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts || text::CompareIgnoringCase(topic, parts[2]) != 0) continue;
        g_serverWorld.CompleteBriefing(0, node.first);
        break;
    }

    state = kStateWalking;
    CPlayerServer* server = g_serverWorld.GetPlayer(0)->Server();
    if (server) server->SendPair19(kEventEnds, leader->id);
    Vector3 at = leader->localPosition;
    if (briefingStarted) {
        g_serverWorld.GetPlayer(0)->Server()->SendPosition04(at, 0, 1);
        g_camera.flags &= ~CCamera::kFree;
        briefingStarted = false;
    } else {
        g_serverWorld.GetPlayer(0)->Server()->SendPosition0F(savedCamera, lookAt, 0);
    }
}
