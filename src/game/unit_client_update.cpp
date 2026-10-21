// The client controller's per-tick update (CUnitClientSpecific slot 6) and the client half's walk
// over its drawn parts. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "ai_map.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "sound.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_world.h"
#include "lib/stlp_list.h"
#include "game_data.h"

#include <cmath>

namespace {
constexpr std::uint32_t kFlagNotDrawn = 8;            // the unit's flags_
constexpr std::uint32_t kFlagTalking = 0x800000;      // the server unit's flags_ while it speaks
constexpr int kDeadShownTicks = 50;                   // after the unit's +0x254
constexpr std::uint32_t kKeyLowBits = 0x7FFF;         // FigureAnimation::key
constexpr std::uint32_t kKeyMovement = 0x38000;
constexpr std::uint32_t kKeyMovementStill = 0x30000;
constexpr std::uint32_t kKeyKind = 0x3C0000;
constexpr std::uint32_t kKeyTransition = 0x240000;
constexpr float kFadeStep = 0.06666667f;              // 0073DF6C, 1/15
constexpr int kSpeechAfterSpeech = 2;                 // ClientSpeech::kind values that wait
constexpr int kSpeechAfterSpeech2 = 0x1C;
constexpr std::int8_t kModeSpecialIdle = -2;          // the unit's +0x255
constexpr int kIdleRestartBelow = 3;                  // CFigureAnimationState::queue.count

// Is sound `handle` still playing or waiting to play?
bool SoundBusy(int handle) {
    if (handle == -1) return false;
    CSoundManager* sounds = sound::Manager();
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

void SetTalking(const CUnit* unit, bool talking) {
    CEngineObject* serverUnit = WorldFindUnit(WorldServer(), nullptr, unit->id_);
    if (!serverUnit) return;
    if (talking) serverUnit->flags_ |= kFlagTalking;
    else serverUnit->flags_ &= ~kFlagTalking;
}
} // namespace

CMaterial* CObject3DClientSpecific::FirstMaterial() {
    if (materials.size <= 0) {
        materialCursor = -1;
        return nullptr;
    }
    materialCursor = 0;
    return materials[0];
}
EI_REPLACE_METHOD(0x0051B6D0, CObject3DClientSpecific::FirstMaterial);

CMaterial* CObject3DClientSpecific::NextMaterial() {
    ++materialCursor;
    return materials.size > materialCursor ? materials[materialCursor] : nullptr;
}
EI_REPLACE_METHOD(0x0051B6F0, CObject3DClientSpecific::NextMaterial);

int CUnitClientSpecific::UpdateClient() {
    if (unit->pathTimeShift == 1) MotionPath()->ShiftTime(1);
    if (unit->motionMode == 1) ++unit->animationStart;
    const FigureAnimation* current = unit->Object3D()->animation->current;
    if (current) ShowWeaponParts((current->key & kKeyLowBits) == 0);

    bool drawn;
    if (unit->dead && static_cast<CWorld*>(ObjectWorld(this))->tick_ >
                          static_cast<std::uint8_t>(unit->animationEntry) + kDeadShownTicks) {
        drawn = false;
    } else {
        drawn = current && ((current->key & kKeyMovement) != kKeyMovementStill ||
                            (current->key & kKeyKind) == kKeyTransition);
    }
    if (drawn) unit->flags_ &= ~kFlagNotDrawn;
    else unit->flags_ |= kFlagNotDrawn;

    if (!(fadeIn >= 1.0f)) {   // below 1, or NaN
        auto* half = static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
        for (CMaterial* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = fadeIn;
        float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = !(next >= 1.0f) ? next : 1.0f;   // the original's select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached)->SetLit(0);

    if (blinks) {
        if (blinkTimer > 0) {
            --blinkTimer;
        } else {
            blinkTimer = gamecrt::rand() * 8 / 0x7FFF % 8 + 2;
            if (eyesClosed == 0) BlinkClose(0);
            else BlinkOpen(0);
        }
    }
    UpdateArrow();
    PlayStepSounds();

    ClientSpeechNode* queue = speech;
    if (queue->next != queue) {
        ClientSpeech* first = &queue->next->value;
        int delay = --first->delay;
        if (delay == 0) {
            if (first->talking) SetTalking(unit, true);
            if (first->kind == kSpeechAfterSpeech || first->kind == kSpeechAfterSpeech2) {
                if (!SoundBusy(speechSound) && Speak(first)) speechSound = first->sound;
            } else {
                Speak(first);
            }
        } else if (delay < 0 && !SoundBusy(first->sound)) {
            if (speech->next->value.talking) SetTalking(unit, false);
            stlp::list<ClientSpeech>::Erase(stlp::AsNode<ClientSpeech>(speech->next));   // 005392F0
        }
    }
    if (unit->secondEntry == kModeSpecialIdle && unit->Object3D()->animation->queue.count < kIdleRestartBelow)
        StartIdleAnimation(0);
    return 1;
}
EI_REPLACE_METHOD(0x005341A0, CUnitClientSpecific::UpdateClient);

// --- Fade-in, level and wounds slots ----------------------------------------------------------

namespace {
// 007C5488 / 007C548C: the experience of level 0 and the factor between levels.
float LevelBase() { return ei::GameData<const float>(0x007C5488); }
float LevelFactor() { return ei::GameData<const float>(0x007C548C); }

CObject3DClientSpecific* ClientHalf(CUnit* unit) {
    return static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
}

// The drawn parts take `alpha`.
void SetPartsAlpha(CObject3DClientSpecific* half, float alpha) {
    for (CMaterial* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = alpha;
}
} // namespace

int CUnitClientSpecific::FiguresLoaded() {
    SetPartsAlpha(ClientHalf(unit), fadeIn);
    return 1;
}
EI_REPLACE_METHOD(0x005344E0, CUnitClientSpecific::FiguresLoaded);

int CUnitClientSpecific::RestartFadeIn() {
    fadeIn = 0.0f;
    SetPartsAlpha(ClientHalf(unit), fadeIn);
    return 0;
}
EI_REPLACE_METHOD(0x005344A0, CUnitClientSpecific::RestartFadeIn);

int CUnitClientSpecific::Level() {
    double experience = x87::FromUnsigned(static_cast<std::uint32_t>(unit->Params()->experience));
    double ratio = x87::flog(experience / LevelBase()) / x87::flog(static_cast<double>(LevelFactor()));
    float level = static_cast<float>(ratio);   // FSTP to a float, then FISTP
    // FISTP gives the integer indefinite 0x80000000 for an infinite, NaN or out-of-range value (the
    // CRT's lrint gives 0). Experience 0 reaches it: ln 0 = -inf.
    return x87::RoundToInt(level);
}
EI_REPLACE_METHOD(0x00539160, CUnitClientSpecific::Level);

int CUnitClientSpecific::RefreshWounds() {
    if (ComposeWoundsTexture()) {
        char name[0x80];
        gamecrt::sprintf(name, "mo%dw", unit->id_);
        ClientHalf(unit)->ShowWounds(name);
    } else {
        ClientHalf(unit)->HideWounds();
    }
    return 1;
}
EI_REPLACE_METHOD(0x00537360, CUnitClientSpecific::RefreshWounds);
