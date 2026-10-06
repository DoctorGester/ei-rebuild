// The briefing / base screen's dialogue start (CBriefingScreen::StartDialogue, ui_briefing.h): who
// takes part, where they stand and how they get there. See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "class_templates.h"
#include "console.h"
#include "core_maths.h"
#include "item_db_text.h"
#include "object_tables.h"
#include "text_reader.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cmath>

namespace {

const char* GoodbyeTopic() { return reinterpret_cast<const char*>(0x007934EC); }       // "goodbye"
const char* ConstructorTopic() { return reinterpret_cast<const char*>(0x007933B4); }   // "constr"
const char* BriefingSection() { return reinterpret_cast<const char*>(0x007906E4); }    // "briefing "
const char* ShowLine() { return reinterpret_cast<const char*>(0x007906DC); }           // "#show"
const ClassTemplateVector* ClassTemplates() { return reinterpret_cast<const ClassTemplateVector*>(0x007AA5D8); }
void* UnitGlue() { return reinterpret_cast<void*>(0x007AFEA6); }
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }

constexpr int kConstructorPrefix = 6;    // "constr"
constexpr int kConstructorNumber = 7;    // where the constructor's number starts
constexpr int kCampScreen = 5;
constexpr int kStateStarting = 2;
constexpr float kSpacing = 2.5f;         // 0073CE18, added to the prototype's viewDistance

int Compare(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

Vector3 Position(const CUnit* unit) { return Vector3{unit->positionX, unit->positionY, unit->positionZ}; }

Vector3 Facing(const CUnit* unit) {
    Vector3 axis;
    unit->orientation.AxisY(&axis);
    return Vector3{-axis.x, -axis.y, -axis.z};
}

Vector3 Middle(const Vector3& a, const Vector3& b) {
    return Vector3{StoreFloat((a.x + b.x) * 0.5f), StoreFloat((a.y + b.y) * 0.5f), (a.z + b.z) * 0.5f};
}

Vector3 Difference(const Vector3& a, const Vector3& b) { return Vector3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// Normalised unless its length is zero (or NaN: FCOM, TEST AH,0x40).
void Normalise(Vector3* v) {
    float length = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    if (length == 0.0f || x87::IsNaN(length)) return;
    float inverse = 1.0f / length;
    v->x = inverse * v->x;
    v->y = inverse * v->y;
    v->z = inverse * v->z;
}

// 00609A40 inlined: the node unlinked and back to the pool.
void EraseMover(BriefingMover* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    stlp::deallocate(node, sizeof(BriefingMover));
}

// The first mover of `person` is dropped; its target goes to `place` / `direction` unless the
// dialogue waited for it.
void DropMover(BriefingMover* movers, const CUnit* person, Vector3* place, Vector3* direction) {
    for (BriefingMover* node = movers->next; node != movers; node = node->next) {
        if (node->person != reinterpret_cast<const CEngineObject*>(person)) continue;
        if (!node->waited) {
            *place = Vector3{node->target[0], node->target[1], node->target[2]};
            *direction = Vector3{node->direction[0], node->direction[1], node->direction[2]};
        }
        EraseMover(node);
        return;
    }
}

} // namespace

// --- 006064E0 -----------------------------------------------------------------------------------

void CBriefingScreen::StartDialogue(bool immediate, bool remember) {
    immediate_ = immediate;
    if (!leader_) {
        state_ = 0;
        briefingStarted_ = false;
        return;
    }
    if (Compare(topic_, GoodbyeTopic()) == 0) {
        state_ = 0;
        return;
    }
    if (Compare(topic_.Left(kConstructorPrefix), ConstructorTopic()) == 0) {
        int constructor = GameAtoi(console::Ops(topic_).Mid(kConstructorNumber));
        if (ClassTemplatesFind(ClassTemplates(), nullptr, constructor)) {
            GameSetConstructorOriginal(UnitGlue(), nullptr, static_cast<float>(constructor));
            GameRequestScreenOriginal(ClientGlue(), nullptr, kCampScreen);
        }
        state_ = 0;
        return;
    }
    person_ = nullptr;
    third_ = nullptr;
    CTextManager* texts = GameTexts();
    mfc::CString key(BriefingSection());
    if (!TextOpen(texts, nullptr, key + topic_)) {
        state_ = 0;
        briefingStarted_ = false;
        return;
    }
    console::CStringArray words;
    while (TextFindLine(texts, nullptr, ShowLine())) {
        TextSplit(texts, nullptr, &words, 1);
        if (words.GetSize() < 2) continue;
        int id = TextKeyFromName(words[1]);
        if (id == leader_->id_) continue;
        auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(ClientGlue(), nullptr, id));
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

    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(person_->GetPrototype());
    float reach = StoreFloat(prototype->viewDistance + kSpacing);
    Vector3& personPlace = places_[0];
    Vector3& leaderPlace = places_[1];
    Vector3& thirdPlace = places_[2];
    // The one who stands still and the one who walks up to it (2.5 + viewDistance away, in front).
    CUnit* standing = noWalk_ ? leader_ : person_;
    Vector3& standingPlace = noWalk_ ? leaderPlace : personPlace;
    Vector3& walkingPlace = noWalk_ ? personPlace : leaderPlace;
    Vector3 axis;
    standing->orientation.AxisY(&axis);
    Vector3 at = Position(standing);
    float stepY = StoreFloat(axis.y * reach);
    float stepX = StoreFloat(axis.x * reach);
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
        BriefingMovePersonOriginal(this, nullptr, leader_, leaderPlace.x, leaderPlace.y, leaderPlace.z,
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
        BriefingMovePersonOriginal(this, nullptr, third_, thirdPlace.x, thirdPlace.y, thirdPlace.z, facing.x, facing.y,
                                   facing.z, 1, immediate_, 0);
    }
    if (noWalk_) {
        BriefingMovePersonOriginal(this, nullptr, person_, personPlace.x, personPlace.y, personPlace.z,
                                   leaderPlace.x - personPlace.x, leaderPlace.y - personPlace.y,
                                   leaderPlace.z - personPlace.z, 1, immediate_, 0);
    }
    if (immediate_)
        BriefingStartPhrasesOriginal(this);
    else
        state_ = kStateStarting;
}
EI_REPLACE_METHOD(0x006064E0, CBriefingScreen::StartDialogue);
