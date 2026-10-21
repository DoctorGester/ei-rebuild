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

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "game_data.h"

#include <cmath>
#include <cstdint>

namespace {

const char* BriefingSection() { return "briefing "; }
const char* HumanPrototype() { return "unhuma"; }
void* MusicSystem() { return ei::GameData<void* const>(0x007AF760); }

// 006023F0: what a speaker's controller calls when its dialogue animation has finished.
using ActionDone = void(__fastcall*)(CUnit*);
ActionDone SpeakerActionDone() { return reinterpret_cast<ActionDone>(0x006023F0); }

WorldPlayer* LocalServerPlayer() { return static_cast<WorldPlayer*>(static_cast<CWorldServer*>(WorldServer())->GetPlayer(0)); }

constexpr int kEventPhrases = 3;
constexpr int kStatePhrases = 3;

// The skill animation numbers (00607A60; the tables at 007410FC / 0074110C).
constexpr int kTalkThird = 3;
constexpr int kTalk = 1;
constexpr int kListen = 2;
constexpr int kHumanTalk[4] = {1, 6, 7, 8};
constexpr int kHumanListen[4] = {2, 0xA, 0xC, 0xD};

constexpr int kPersonPlace = 0;
constexpr int kLeaderPlace = 1;
constexpr int kThirdPlace = 2;

} // namespace

// --- 00608210 -----------------------------------------------------------------------------------

void CBriefingScreen::StartPhrases() {
    if (voice_ != -1) sound::Manager()->StopSound(voice_);
    voice_ = -1;
    static_cast<CMusicSystem*>(MusicSystem())->FadeOut();
    // (The original also constructs and destroys an unused CStringArray here.)
    mfc::CString key(BriefingSection());
    key += topic_;
    if (!TextsOpenSection(GameTexts(), nullptr, key)) {
        state_ = 0;
        return;
    }
    void* server = LocalServerPlayer()->server;
    if (server) static_cast<CPlayerServer*>(server)->SendPair19(kEventPhrases, 0);

    // The camera's position, and that plus its rotation's z axis (third column of the matrix).
    const CCamera* camera = camera::TheCamera();
    savedCamera_ = camera->localPosition_;
    const Quaternion& q = camera->localRotation_;
    double zz = q.z * q.z;
    double axisZ = (q.w * q.w - (LengthSquared2DUnrounded(q.x, q.y) + zz)) + (zz + zz);
    float axisY = static_cast<float>((q.y * q.z - q.x * q.w) * 2.0);
    // The original doubles x and y before multiplying; here the compiler doubles z instead of x,
    // which gives the same value (doubling is exact). Dot2D(x, y, z, w) * 2.0 would regroup the sum.
    float axisX = static_cast<float>(Dot2DUnrounded<double>(q.x * 2.0, q.y * 2.0, q.z, q.w));
    lookAt_.x = static_cast<float>(savedCamera_.x + axisX);
    lookAt_.y = static_cast<float>(savedCamera_.y + axisY);
    lookAt_.z = static_cast<float>(axisZ + savedCamera_.z);

    dialogueText_.Empty();
    static_cast<CUiScrollText*>(text_)->SetText(game::EmptyText());
    state_ = kStatePhrases;
    firstPhrase_ = true;
    NextPhrase(0);
}
EI_REPLACE_METHOD(0x00608210, CBriefingScreen::StartPhrases);

// --- 00607A60 -----------------------------------------------------------------------------------

void CBriefingScreen::PlaySpeakerAnimation(CUnit* unit, bool talking, bool third) {
    // (The original compares a CString copy of the name.)
    bool human = GameMbscmp(unit->name, HumanPrototype()) == 0;
    int number;
    if (talking) {
        if (third)
            number = kTalkThird;
        else
            number = human ? kHumanTalk[IsaacRandom::Global().Next() & 3] : kTalk;
    } else {
        number = human ? kHumanListen[IsaacRandom::Global().Next() & 3] : kListen;
    }
    auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, unit->id_));
    if (!serverUnit) return;
    auto* controller = reinterpret_cast<CUnitServer*>(serverUnit->controller);
    controller->StopAction();
    controller->actionDone = SpeakerActionDone();
    if (FigureAnimation* animation = unit->Object3D()->FindSkillAnimation(number))
        controller->PlayAnimation(animation->name);
}
EI_REPLACE_METHOD(0x00607A60, CBriefingScreen::PlaySpeakerAnimation);

// --- 006023F0 -----------------------------------------------------------------------------------

// 006023F0 (no Ghidra function; S16c): SpeakerActionDone, the speakers' controllers' actionDone. When
// a server unit's dialogue animation has ended, its client unit, when it is one of the briefing's
// speakers, plays a listening animation again (once per entry). The active window is taken to be the
// briefing screen unchecked, as the original does; the count is read again after each call.
void __fastcall SpeakerAnimationDone(CUnit* serverUnit) {
    if (!serverUnit) return;
    auto* unit = reinterpret_cast<CUnit*>(WorldFindUnit(ei::GamePointer<void>(0x007AF5B8), nullptr, serverUnit->id_));
    if (!unit) return;
    auto* screen = static_cast<CBriefingScreen*>(InterfaceActiveWindowOriginal(ei::GamePointer<CInterface3D>(0x007C7D50)));
    for (std::size_t i = 0; i < static_cast<std::size_t>(screen->speakersEnd_ - screen->speakers_); ++i)
        if (screen->speakers_[i].unit == unit) screen->PlaySpeakerAnimation(unit, false, false);
}
EI_REPLACE(0x006023F0, SpeakerAnimationDone);

// --- 00607BB0 -----------------------------------------------------------------------------------

void CBriefingScreen::ViewSpeaker(CUnit* unit, float angle, float distance, float height) {
    float cosAngle = static_cast<float>(x87::fcos(angle));
    float sinAngle = static_cast<float>(x87::fsin(angle));
    Vector3 eye{};
    double x;   // the facing, kept unrounded as the original does
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
        Vector3 place{unit->position.x, unit->position.y, unit->position.z};
        Vector3 axis;
        unit->orientation.AxisY(&axis);
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
        auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
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
    // FCOM with zero, TEST AH,0x40: a NaN length is left alone too.
    if (!(length == 0.0) && !x87::IsNaN(static_cast<float>(length))) {
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
    static_cast<CPlayerServer*>(LocalServerPlayer()->server)->SendPosition0F(&at, &eye, 1);
}
EI_REPLACE_METHOD(0x00607BB0, CBriefingScreen::ViewSpeaker);

// --- 00607EC0 -----------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159274f;          // 007410D0
constexpr float kSixth = 0.166666672f;      // 0073CE40 (and -: 00741440)
constexpr float kTwelfth = 0.0833333358f;   // 00741438 (and -: 00741430)
constexpr float kQuarter = 0.25f;           // 0073C3B0 (and -: 0073CDA8)
constexpr float kSunk = -100.0f;

// A speaker who is not in the picture is moved below the ground.
void Sink(CUnit* unit) {
    if (unit) GameSetUnitHeight(unit->id_, kSunk);
}

} // namespace

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
EI_REPLACE_METHOD(0x00607EC0, CBriefingScreen::ViewPreset);

// --- 006062C0 -----------------------------------------------------------------------------------

namespace {

constexpr int kEventLeaves = 2;

// 00609A80 inlined: the node unlinked, its name destroyed, the node back to the pool.
void EraseShown(BriefingShownItem* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->name.~CString();
    stlp::deallocate(node, sizeof(BriefingShownItem));
}

} // namespace

void CBriefingScreen::HideSpeaker(const char* name, int immediate, int time) {
    auto* person = static_cast<CUnit*>(GameFindClientUnit(TextKeyFromName(name)));
    if (!person) {
        for (BriefingShownItem* node = shown_->next; node != shown_; node = node->next) {
            if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(node->name)),
                                reinterpret_cast<const unsigned char*>(name)) != 0)
                continue;
            if (node->item) {
                RemoveObject(node->item->texture_);
                RemoveSceneObject(reinterpret_cast<CEngineObject*>(node->item));
            }
            if (node->back) RemoveSceneObject(node->back);
            if (node->frame) RemoveSceneObject(node->frame);
            EraseShown(node);
            return;
        }
        return;
    }
    BriefingSpeaker* entry = speakers_;
    while (entry != speakersEnd_ && entry->unit != person) ++entry;
    if (entry != speakersEnd_) {
        void* server = LocalServerPlayer()->server;
        if (server) static_cast<CPlayerServer*>(server)->SendPair19(kEventLeaves, person->id_);
        auto* model = reinterpret_cast<CUnitClientSpecific*>(person->controller);
        model->blinks = 0;
        model->BlinkOpen(true);
        if (auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, person->id_))) {
            serverUnit->SetFlag40000000Off(true);
            GameSetUnitHeight(person->id_, 0.0f);
            auto* controller = reinterpret_cast<CUnitServer*>(serverUnit->controller);
            controller->actionDone = nullptr;
            controller->StopAction();
            // (The original then looks for the person among the party's first members; the result
            // is not used.)
        }
        // vector::erase (0060B490, then the last entry's destructor 0060AC00).
        for (BriefingSpeaker* p = entry; p + 1 != speakersEnd_; ++p) {
            p->unit = p[1].unit;
            p->name = p[1].name;
            p->spareFlag = p[1].spareFlag;
        }
        --speakersEnd_;
        speakersEnd_->name.~CString();
    }
    if (person == third_) {
        MovePerson(person, thirdPlace_.x, thirdPlace_.y, thirdPlace_.z, thirdDirection_.x,
                                   thirdDirection_.y, thirdDirection_.z, 0, immediate, time);
        third_ = nullptr;
    }
}
EI_REPLACE_METHOD(0x006062C0, CBriefingScreen::HideSpeaker);
