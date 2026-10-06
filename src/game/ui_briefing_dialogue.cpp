// The briefing / base screen's dialogue (CBriefingScreen, ui_briefing.h): the phrases' start, the
// speakers' animations and the camera on a speaker. See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "camera.h"
#include "core_maths.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "random.h"
#include "sound.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"

#include <cmath>
#include <cstdint>

namespace {

const char* BriefingSection() { return reinterpret_cast<const char*>(0x007906E4); }   // "briefing "
const char* HumanPrototype() { return reinterpret_cast<const char*>(0x007908C8); }    // "unhuma"
const char* EmptyText() { return reinterpret_cast<const char*>(0x0079B298); }          // ""
void* MusicSystem() { return *reinterpret_cast<void* const*>(0x007AF760); }

// 006023F0: what a speaker's controller calls when its dialogue animation has finished.
using ActionDone = void(__fastcall*)(CUnit*);
ActionDone SpeakerActionDone() { return reinterpret_cast<ActionDone>(0x006023F0); }

WorldPlayer* LocalServerPlayer() { return static_cast<WorldPlayer*>(WorldGetPlayer(WorldServer(), nullptr, 0)); }

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
    MusicFadeOutOriginal(MusicSystem());
    // (The original also constructs and destroys an unused CStringArray here.)
    mfc::CString key(BriefingSection());
    key += topic_;
    if (!TextsOpenSection(GameTexts(), nullptr, key)) {
        state_ = 0;
        return;
    }
    void* server = LocalServerPlayer()->server;
    if (server) PlayerSendDialogueEventOriginal(server, nullptr, kEventPhrases, 0);

    // The camera's position, and that plus its rotation's z axis (third column of the matrix).
    const CCamera* camera = camera::TheCamera();
    savedCamera_ = camera->localPosition_;
    const Quaternion& q = camera->localRotation_;
    double zz = q.z * q.z;
    double axisZ = (q.w * q.w - ((q.x * q.x + q.y * q.y) + zz)) + (zz + zz);
    float axisY = StoreFloat(static_cast<float>((q.y * q.z - q.x * q.w) * 2.0));
    float axisX = StoreFloat(static_cast<float>((q.x * q.z + q.y * q.w) * 2.0));
    lookAt_.x = StoreFloat(static_cast<float>(savedCamera_.x + axisX));
    lookAt_.y = StoreFloat(static_cast<float>(savedCamera_.y + axisY));
    lookAt_.z = StoreFloat(static_cast<float>(axisZ + savedCamera_.z));

    dialogueText_.Empty();
    LogSetTextOriginal(text_, nullptr, EmptyText());
    state_ = kStatePhrases;
    firstPhrase_ = true;
    BriefingNextPhraseOriginal(this, nullptr, 0);
}
EI_REPLACE_METHOD(0x00608210, CBriefingScreen::StartPhrases);

// --- 00607A60 -----------------------------------------------------------------------------------

void CBriefingScreen::PlaySpeakerAnimation(CUnit* unit, bool talking, bool third) {
    // (The original compares a CString copy of the name.)
    bool human = GameMbscmp(unit->prototypeName, HumanPrototype()) == 0;
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
    auto* controller = reinterpret_cast<UnitController*>(serverUnit->controller);
    ControllerStopActionOriginal(controller);
    controller->actionDone = SpeakerActionDone();
    if (FigureAnimation* animation = unit->Object3D()->FindSkillAnimation(number))
        ControllerPlayAnimationOriginal(controller, nullptr, animation);
}
EI_REPLACE_METHOD(0x00607A60, CBriefingScreen::PlaySpeakerAnimation);

// --- 00607BB0 -----------------------------------------------------------------------------------

void CBriefingScreen::ViewSpeaker(CUnit* unit, float angle, float distance, float height) {
    float cosAngle = StoreFloat(static_cast<float>(std::cos(angle)));
    float sinAngle = StoreFloat(static_cast<float>(std::sin(angle)));
    Vector3 eye{};
    double x;   // the facing, kept unrounded as the original does
    double y;
    if (!unit) {
        // The middle of the first two places, looked at across the line between them.
        double sumX = places_[kPersonPlace].x + places_[kLeaderPlace].x;
        float sumY = StoreFloat(places_[kPersonPlace].y + places_[kLeaderPlace].y);
        float sumZ = StoreFloat(places_[kPersonPlace].z + places_[kLeaderPlace].z);
        eye.x = StoreFloat(static_cast<float>(sumX * 0.5));
        eye.y = StoreFloat(sumY * 0.5f);
        eye.z = StoreFloat(sumZ * 0.5f);
        x = static_cast<double>(places_[kLeaderPlace].y) - places_[kPersonPlace].y;
        y = static_cast<double>(places_[kPersonPlace].x) - places_[kLeaderPlace].x;
    } else {
        Vector3 place{unit->positionX, unit->positionY, unit->positionZ};
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
        float reachX = StoreFloat(facing.x * reach);
        float reachY = StoreFloat(facing.y * reach);
        eye.x = StoreFloat(reachX + place.x);
        eye.y = StoreFloat(place.y + reachY);
        eye.z = StoreFloat(place.z + prototype->viewHeight);
        x = facing.x;
        y = facing.y;
    }
    double length = std::sqrt(y * y + x * x);
    if (!(length == 0.0)) {
        double inverse = 1.0 / length;
        x *= inverse;
        y *= inverse;
    }
    Vector3 at;
    float alongX = StoreFloat(static_cast<float>(x * cosAngle + y * sinAngle));
    alongX = StoreFloat(alongX * distance);
    double alongY = (y * cosAngle - x * sinAngle) * distance;
    at.x = StoreFloat(eye.x + alongX);
    at.y = StoreFloat(static_cast<float>(eye.y + alongY));
    at.z = StoreFloat(eye.z + height);
    PlayerSendViewOriginal(LocalServerPlayer()->server, nullptr, &at, &eye, 1);
}
EI_REPLACE_METHOD(0x00607BB0, CBriefingScreen::ViewSpeaker);
