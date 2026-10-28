// The briefing / base screen's dialogue start and end (CBriefingScreen::StartDialogue /
// FinishDialogue, ui_briefing.h): who takes part, where they stand, how they get there and back.
// See docs/notes/ui.md, "Briefing / base screen".
#include "player_messages.h"
#include "world_glue.h"
#include "combat.h"
#include "ui_briefing.h"

#include "camera.h"
#include "class_templates.h"
#include "console.h"
#include "core_maths.h"
#include "item_db_text.h"
#include "object_tables.h"
#include "sound.h"
#include "text_reader.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "text_split.h"
#include "game_singletons.h"
#include "party_manager.h"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

const char* GoodbyeTopic() { return "goodbye"; }
const char* ConstructorTopic() { return "constr"; }
const char* BriefingSection() { return "briefing "; }
const char* ShowLine() { return "#show"; }
const TemplateTable* ClassTemplates() { return &g_partyManager->templates; }
const char* TopicPrefix() { return "b."; }

CPlayer* LocalServerPlayer() { return g_serverWorld->GetPlayer(0); }

constexpr int kStateWalking = 0;
constexpr int kEventEnds = 0;
constexpr int kEventLeaves = 2;
constexpr int kTopicNameParts = 3;
constexpr int kConstructorPrefix = 6;    // "constr"
constexpr int kConstructorNumber = 7;    // where the constructor's number starts
constexpr int kCampScreen = 5;
constexpr int kStateStarting = 2;
constexpr float kSpacing = 2.5f;         // added to the prototype's viewDistance

Vector3 Position(const CUnit* unit) { return Vector3{unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z}; }

Vector3 Facing(const CUnit* unit) {
    Vector3 axis;
    unit->localRotation_.AxisY(&axis);
    return Vector3{-axis.x, -axis.y, -axis.z};
}

Vector3 Middle(const Vector3& a, const Vector3& b) {
    return Vector3{static_cast<float>((a.x + b.x) * 0.5f), static_cast<float>((a.y + b.y) * 0.5f), (a.z + b.z) * 0.5f};
}

Vector3 Difference(const Vector3& a, const Vector3& b) { return Vector3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// Normalised unless its length is zero (FCOM, TEST AH,0x40).
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


void CBriefingScreen::StartDialogue(bool immediate, bool remember) {
    immediate_ = immediate;
    if (!leader_) {
        state_ = 0;
        briefingStarted_ = false;
        return;
    }
    if (text::CompareIgnoringCase(topic_, GoodbyeTopic()) == 0) {
        state_ = 0;
        return;
    }
    if (text::CompareIgnoringCase(str::Left(topic_, kConstructorPrefix), ConstructorTopic()) == 0) {
        int constructor = str::ToInt(str::Mid(topic_, kConstructorNumber));
        if (ClassTemplates()->Find(constructor)) {
            GameSetConstructionVariable(std::bit_cast<std::uint32_t>(static_cast<float>(constructor)));
            GameSendLeave04(kCampScreen);
        }
        state_ = 0;
        return;
    }
    person_ = nullptr;
    third_ = nullptr;
    CTextManager* texts = TheTextManager();
    std::string key = BriefingSection();
    if (!texts->Open((key + topic_).c_str())) {
        state_ = 0;
        briefingStarted_ = false;
        return;
    }
    std::vector<std::string> words;
    while (texts->FindLine(ShowLine())) {
        texts->Split(&words, 1);
        if (words.size() < 2) continue;
        int id = TextKeyFromName(words[1]);
        if (id == leader_->id_) continue;
        auto* unit = GameFindClientUnit(id);
        if (!unit) continue;
        if (!person_)
            person_ = unit;
        else if (person_ != unit)
            third_ = unit;
    }
    if (!person_) {
        state_ = 0;
        briefingStarted_ = false;
        return;
    }

    auto* prototype = person_->GetPrototype();
    float reach = static_cast<float>(prototype->viewDistance + kSpacing);
    Vector3& personPlace = places_[0];
    Vector3& leaderPlace = places_[1];
    Vector3& thirdPlace = places_[2];
    // The one who stands still and the one who walks up to it (2.5 + viewDistance away, in front).
    CUnit* standing = noWalk_ ? leader_ : person_;
    Vector3& standingPlace = noWalk_ ? leaderPlace : personPlace;
    Vector3& walkingPlace = noWalk_ ? personPlace : leaderPlace;
    Vector3 axis;
    standing->localRotation_.AxisY(&axis);
    Vector3 at = Position(standing);
    float stepY = static_cast<float>(axis.y * reach);
    float stepX = static_cast<float>(axis.x * reach);
    walkingPlace = Vector3{at.x - stepX, at.y - stepY, at.z - axis.z * reach};
    standingPlace = at;
    // The third place: the middle, moved sideways by the person's offset from it.
    Vector3 middle = Middle(personPlace, leaderPlace);
    thirdPlace = middle;
    thirdPlace.x = (personPlace.y - middle.y) + middle.x;
    thirdPlace.y = (middle.x - personPlace.x) + middle.y;
    if (noWalk_) {
        if (remember) {
            personHome_ = Position(person_);
            personHomeDirection_ = Facing(person_);
        }
    } else {
        MovePerson(leader_, leaderPlace.x, leaderPlace.y, leaderPlace.z,
                                   personPlace.x - leaderPlace.x, personPlace.y - leaderPlace.y,
                                   personPlace.z - leaderPlace.z, 1, immediate_, 0);
    }
    if (third_ && remember) {
        thirdPlace_ = Position(third_);
        thirdDirection_ = Facing(third_);
    }

    directions_[0] = Difference(leaderPlace, personPlace);
    directions_[0].z = 0.0f;
    directions_[1] = Difference(personPlace, leaderPlace);
    directions_[1].z = 0.0f;
    directions_[2] = Difference(Middle(personPlace, leaderPlace), thirdPlace);
    directions_[2].z = 0.0f;
    for (Vector3& direction : directions_) Normalise(&direction);

    if (third_) DropMover(movers_, third_, &thirdPlace_, &thirdDirection_);
    if (noWalk_) DropMover(movers_, person_, &personHome_, &personHomeDirection_);
    if (third_) {
        Vector3 facing = Difference(Middle(personPlace, leaderPlace), thirdPlace);
        MovePerson(third_, thirdPlace.x, thirdPlace.y, thirdPlace.z, facing.x, facing.y,
                                   facing.z, 1, immediate_, 0);
    }
    if (noWalk_) {
        MovePerson(person_, personPlace.x, personPlace.y, personPlace.z,
                                   leaderPlace.x - personPlace.x, leaderPlace.y - personPlace.y,
                                   leaderPlace.z - personPlace.z, 1, immediate_, 0);
    }
    if (immediate_)
        StartPhrases();
    else
        state_ = kStateStarting;
}

void CBriefingScreen::ClearShownItems() {
    for (const BriefingShownItem& shown : shown_) {
        if (shown.item) {
            RemoveObject(shown.item->texture_);
            RemoveSceneObject(shown.item);
        }
        if (shown.back) RemoveSceneObject(shown.back);
        if (shown.frame) RemoveSceneObject(shown.frame);
    }
    shown_.clear();
}

void CBriefingScreen::FinishDialogue() {
    if (voice_ != -1) TheSoundManager()->StopSound(voice_);
    voice_ = -1;
    TheMusicSystem()->FadeIn();
    ClearShownItems();

    for (const BriefingSpeaker& speaker : speakers_) {
        CUnit* unit = speaker.unit;
        CPlayerServer* server = LocalServerPlayer()->Server();
        if (server) server->SendPair19(kEventLeaves, unit->id_);
        CUnitClientSpecific* model = unit->ClientController();
        model->blinks = 0;
        model->BlinkOpen(true);
        auto* serverUnit = g_serverWorld->FindUnit(unit->id_);
        if (!serverUnit) continue;
        serverUnit->SetFlag40000000Off(true);
        GameSetUnitHeight(unit->id_, 0.0f);
        auto* controller = serverUnit->ServerController();
        controller->actionDone = nullptr;
        controller->StopAction();
        // (The unit is then looked up among the party's first members; the result is not used.)
    }
    speakers_.clear();
    speaking_ = nullptr;

    if (leader_) GameSetUnitHeight(leader_->id_, 0.0f);
    if (person_) GameSetUnitHeight(person_->id_, 0.0f);
    if (third_) {
        MovePerson(third_, thirdPlace_.x, thirdPlace_.y, thirdPlace_.z, thirdDirection_.x,
                                   thirdDirection_.y, thirdDirection_.z, 0, 0, 0);
        third_ = nullptr;
    }
    if (noWalk_ && person_) {
        MovePerson(person_, personHome_.x, personHome_.y, personHome_.z,
                                   personHomeDirection_.x, personHomeDirection_.y, personHomeDirection_.z, 0, 0, 0);
    }

    BriefingVariables variables(TopicPrefix());
    std::vector<std::string> parts;
    for (auto node = variables.begin(); node != variables.end(); ++node) {
        text::Split(node->first, &parts, '.');
        if (parts.size() != kTopicNameParts || text::CompareIgnoringCase(topic_, parts[2]) != 0) continue;
        g_serverWorld->CompleteBriefing(0, node->first.c_str());
        break;
    }

    state_ = kStateWalking;
    CPlayerServer* server = LocalServerPlayer()->Server();
    if (server) server->SendPair19(kEventEnds, leader_->id_);
    Vector3 at = Position(leader_);
    if (briefingStarted_) {
        LocalServerPlayer()->Server()->SendPosition04(&at, 0, 1);
        g_camera->flags_ &= ~CCamera::kFree;
        briefingStarted_ = false;
    } else {
        LocalServerPlayer()->Server()->SendPosition0F(&savedCamera_, &lookAt_, 0);
    }
}
