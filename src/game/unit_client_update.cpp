// The client controller's per-tick update (CUnitClientSpecific's Update) and the client half's walk
// over its drawn parts. See unit_client_models.h and docs/notes/units.md.
#include "ai_map.h"
#include "game_singletons.h"
#include "logic_settings.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "sound.h"
#include "unit.h"
#include "unit_client_models.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <cmath>
#include <cstdio>

namespace {
constexpr std::uint32_t kFlagNotDrawn = 8;            // the unit's flags_
constexpr int kDeadShownTicks = 50;                   // after the unit's +0x254
constexpr std::uint32_t kKeyLowBits = 0x7FFF;         // FigureAnimation::key
constexpr std::uint32_t kKeyMovement = 0x38000;
constexpr std::uint32_t kKeyMovementStill = 0x30000;
constexpr float kFadeStep = 0.06666667f;              // 1/15
constexpr std::int8_t kModeSpecialIdle = -2;          // the unit's +0x255
constexpr int kIdleRestartBelow = 3;                  // CFigureAnimationState::queue.count

// Is sound `handle` still playing or waiting to play?
bool SoundBusy(int handle) {
    if (handle == -1) return false;
    CSoundManager* sounds = g_clientWorld->sounds_;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

void SetTalking(const CUnit* unit, bool talking) {
    CEngineObject* serverUnit = g_serverWorld->FindUnit(unit->id_);
    if (!serverUnit) return;
    if (talking) serverUnit->flags_ |= MapObjectFlag::Blocked;
    else serverUnit->flags_ &= ~MapObjectFlag::Blocked;
}

// The drawn parts take `alpha`.
void SetPartsAlpha(CObject3DClientSpecific* half, float alpha) {
    for (CMaterial* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = alpha;
}

} // namespace

CMaterial* CObject3DClientSpecific::FirstMaterial() {
    if (materials.empty()) {
        materialCursor = -1;
        return nullptr;
    }
    materialCursor = 0;
    return materials[0];
}

CMaterial* CObject3DClientSpecific::NextMaterial() {
    ++materialCursor;
    return std::ssize(materials) > materialCursor ? materials[materialCursor] : nullptr;
}

bool CUnitClientSpecific::RunTick() {
    if (unit->pathTimeShift == 1) motion.ShiftTime(1);
    if (unit->motionMode == 1) ++unit->animationStart;
    const FigureAnimation* current = unit->Object3D()->animation->current;
    if (current) ShowWeaponParts((current->key & kKeyLowBits) == 0);

    bool drawn;
    if (unit->dead && ObjectWorld(this)->tick_ >
                          static_cast<std::uint8_t>(unit->animationEntry) + kDeadShownTicks) {
        drawn = false;
    } else {
        drawn =
            current && ((current->key & kKeyMovement) != kKeyMovementStill || current->Kind() == AnimationKind::Cross);
    }
    if (drawn) unit->flags_ &= ~kFlagNotDrawn;
    else unit->flags_ |= kFlagNotDrawn;

    EI_NAN_AUDIT(fadeIn);
    if (fadeIn < 1.0f) {
        auto* half = unit->Object3D()->ClientSpecific();
        for (CMaterial* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = fadeIn;
        float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = next >= 1.0f ? 1.0f : next;   // the select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        unit->Object3D()->ClientSpecific()->SetMaterialsLit(false);

    if (blinks) {
        if (blinkTimer > 0) {
            --blinkTimer;
        } else {
            blinkTimer = libcrt::RandBelow(8) + 2;
            if (eyesClosed == 0) BlinkClose(0);
            else BlinkOpen(0);
        }
    }
    UpdateArrow();
    PlayStepSounds();

    if (!speech.empty()) {
        ClientSpeech* first = &speech.front();
        int delay = --first->delay;
        if (delay == 0) {
            if (first->talking) SetTalking(unit, true);
            if (first->kind == AckKind::Attack || first->kind == AckKind::DecidedToAttack) {
                if (!SoundBusy(speechSound) && Speak(first)) speechSound = first->sound;
            } else {
                Speak(first);
            }
        } else if (delay < 0 && !SoundBusy(first->sound)) {
            if (speech.front().talking) SetTalking(unit, false);
            speech.pop_front();
        }
    }
    if (unit->secondEntry == kModeSpecialIdle && std::ssize(unit->Object3D()->animation->queue) < kIdleRestartBelow)
        StartIdleAnimation(0);
    return true;
}

// --- Fade-in, level and wounds slots ----------------------------------------------------------

int CUnitClientSpecific::FiguresLoaded() {
    SetPartsAlpha(unit->ClientSpecific(), fadeIn);
    return 1;
}

int CUnitClientSpecific::RestartFadeIn() {
    fadeIn = 0.0f;
    SetPartsAlpha(unit->ClientSpecific(), fadeIn);
    return 0;
}

int CUnitClientSpecific::Level() {
    double experience = x87::FromUnsigned(static_cast<std::uint32_t>(unit->params.experience));
    double ratio = x87::flog(experience / g_logicSettings.considerModifier) / x87::flog(static_cast<double>(g_logicSettings.considerLogBase));
    float level = static_cast<float>(ratio);   // stored as a float, then rounded to nearest
    // The rounding gives the integer indefinite 0x80000000 for an infinite, NaN or out-of-range value (the
    // CRT's lrint gives 0). Experience 0 reaches it: ln 0 = -inf.
    return x87::RoundToInt(level);
}

int CUnitClientSpecific::RefreshWounds() {
    if (ComposeWoundsTexture()) {
        char name[0x80];
        std::snprintf(name, sizeof(name), "mo%dw", unit->id_);
        unit->ClientSpecific()->ShowWounds(name);
    } else {
        unit->ClientSpecific()->HideWounds();
    }
    return 1;
}
