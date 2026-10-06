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

#include <cmath>
#include <limits>

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
constexpr float kIntRange = 2147483648.0f;            // 2^31: FISTP's int range is below it

// Not rewritten (forwarders):
// 00538ED0 / 00539010. The blink: the face texture with closed / open eyes (RET 4).
__declspec(naked) void __fastcall CloseEyes(CUnitClientSpecific*, void*, int) EI_ORIGINAL(0x00538ED0)
__declspec(naked) void __fastcall OpenEyes(CUnitClientSpecific*, void*, int) EI_ORIGINAL(0x00539010)
// Rewritten (unit_client_sounds.cpp, unit_client_models.cpp, below), called through their game
// addresses so their verify wrappers stay in the path: 00534B60 the step sounds, 00537CB0 the weapon
// parts, 00538C20 the bow's arrow, 0051B6D0 / 0051B6F0 the drawn parts.
__declspec(naked) void __fastcall CallPlayStepSounds(CUnitClientSpecific*) EI_ORIGINAL(0x00534B60)
__declspec(naked) void __fastcall CallShowWeaponParts(CUnitClientSpecific*, void*, bool) EI_ORIGINAL(0x00537CB0)
__declspec(naked) void __fastcall CallUpdateArrow(CUnitClientSpecific*) EI_ORIGINAL(0x00538C20)
__declspec(naked) FigurePart* __fastcall CallFirstDrawnPart(CObject3DClientSpecific*) EI_ORIGINAL(0x0051B6D0)
__declspec(naked) FigurePart* __fastcall CallNextDrawnPart(CObject3DClientSpecific*) EI_ORIGINAL(0x0051B6F0)
// 005380D0. Plays a speech; false when the same one played within 5 seconds (RET 4, AL).
__declspec(naked) bool __fastcall Speak(CUnitClientSpecific*, void*, ClientSpeech*) EI_ORIGINAL(0x005380D0)
// 005392F0 (library: STLport list::erase). Removes `at`, the next node to *out (RET 8).
__declspec(naked) void __fastcall EraseSpeech(ClientSpeechNode**, void*, ClientSpeechNode** /*out*/,
                                              ClientSpeechNode* /*at*/) EI_ORIGINAL(0x005392F0)

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

FigurePart* CObject3DClientSpecific::FirstDrawnPart() {
    if (drawnPartCount <= 0) {
        drawnPartCursor = -1;
        return nullptr;
    }
    drawnPartCursor = 0;
    return drawnParts[0];
}
EI_REPLACE_METHOD(0x0051B6D0, CObject3DClientSpecific::FirstDrawnPart);

FigurePart* CObject3DClientSpecific::NextDrawnPart() {
    ++drawnPartCursor;
    return drawnPartCount > drawnPartCursor ? drawnParts[drawnPartCursor] : nullptr;
}
EI_REPLACE_METHOD(0x0051B6F0, CObject3DClientSpecific::NextDrawnPart);

int CUnitClientSpecific::UpdateClient() {
    if (unit->unknown_260 == 1) MotionPath()->ShiftTime(1);
    if (unit->unknown_25C == 1) ++unit->unknown_258;
    const FigureAnimation* current = unit->Object3D()->animation->current;
    if (current) CallShowWeaponParts(this, nullptr, (current->key & kKeyLowBits) == 0);

    bool drawn;
    if (unit->dead && static_cast<CWorld*>(ObjectWorld(this))->tick_ >
                          static_cast<std::uint8_t>(unit->unknown_254) + kDeadShownTicks) {
        drawn = false;
    } else {
        drawn = current && ((current->key & kKeyMovement) != kKeyMovementStill ||
                            (current->key & kKeyKind) == kKeyTransition);
    }
    if (drawn) unit->flags_ &= ~kFlagNotDrawn;
    else unit->flags_ |= kFlagNotDrawn;

    if (!(fadeIn >= 1.0f)) {   // below 1, or NaN
        auto* half = static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
        for (FigurePart* part = CallFirstDrawnPart(half); part; part = CallNextDrawnPart(half)) part->alpha = fadeIn;
        volatile float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = !(next >= 1.0f) ? next : 1.0f;   // the original's select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        ClientPartColoursOriginal(static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached), nullptr, 0);

    if (blinks) {
        if (blinkTimer > 0) {
            --blinkTimer;
        } else {
            blinkTimer = gamecrt::rand() * 8 / 0x7FFF % 8 + 2;
            if (eyesClosed == 0) CloseEyes(this, nullptr, 0);
            else OpenEyes(this, nullptr, 0);
        }
    }
    CallUpdateArrow(this);
    CallPlayStepSounds(this);

    ClientSpeechNode* queue = speech;
    if (queue->next != queue) {
        ClientSpeech* first = &queue->next->value;
        int delay = --first->delay;
        if (delay == 0) {
            if (first->talking) SetTalking(unit, true);
            if (first->kind == kSpeechAfterSpeech || first->kind == kSpeechAfterSpeech2) {
                if (!SoundBusy(speechSound) && Speak(this, nullptr, first)) speechSound = first->sound;
            } else {
                Speak(this, nullptr, first);
            }
        } else if (delay < 0 && !SoundBusy(first->sound)) {
            if (speech->next->value.talking) SetTalking(unit, false);
            ClientSpeechNode* after;
            EraseSpeech(&speech, nullptr, &after, speech->next);
        }
    }
    if (unit->unknown_255 == kModeSpecialIdle && unit->Object3D()->animation->queue.count < kIdleRestartBelow)
        ClientStartIdleAnimationOriginal(this, nullptr, 0);
    return 1;
}
EI_REPLACE_METHOD(0x005341A0, CUnitClientSpecific::UpdateClient);

// --- Fade-in, level and wounds slots ----------------------------------------------------------

namespace {
// 007C5488 / 007C548C: the experience of level 0 and the factor between levels.
float LevelBase() { return *reinterpret_cast<const float*>(0x007C5488); }
float LevelFactor() { return *reinterpret_cast<const float*>(0x007C548C); }

// Not rewritten (forwarders): 005370F0 (whether the unit's wounds need a texture, composing it;
// plain RET), the client half's 0051C3D0 (shows wounds texture `name`, RET 4) and 0051C920 (none).
__declspec(naked) int __fastcall NeedsWoundsTexture(CUnitClientSpecific*) EI_ORIGINAL(0x005370F0)
__declspec(naked) void __fastcall ShowWounds(CObject3DClientSpecific*, void*, const char*) EI_ORIGINAL(0x0051C3D0)
__declspec(naked) void __fastcall HideWounds(CObject3DClientSpecific*) EI_ORIGINAL(0x0051C920)

CObject3DClientSpecific* ClientHalf(CUnit* unit) {
    return static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
}

// The drawn parts take `alpha`.
void SetPartsAlpha(CObject3DClientSpecific* half, float alpha) {
    for (FigurePart* part = CallFirstDrawnPart(half); part; part = CallNextDrawnPart(half)) part->alpha = alpha;
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
    double ratio = std::log(experience / LevelBase()) / std::log(static_cast<double>(LevelFactor()));
    volatile float level = static_cast<float>(ratio);   // FSTP to a float, then FISTP
    float rounded = level;
    // FISTP gives the integer indefinite 0x80000000 for an infinite, NaN or out-of-range value;
    // the CRT's lrint gives 0. Experience 0 reaches it: ln 0 = -inf.
    if (!(std::fabs(rounded) < kIntRange)) return std::numeric_limits<int>::min();
    return static_cast<int>(std::lrint(rounded));
}
EI_REPLACE_METHOD(0x00539160, CUnitClientSpecific::Level);

int CUnitClientSpecific::RefreshWounds() {
    if (NeedsWoundsTexture(this)) {
        char name[0x80];
        gamecrt::sprintf(name, "mo%dw", unit->id_);
        ShowWounds(ClientHalf(unit), nullptr, name);
    } else {
        HideWounds(ClientHalf(unit));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00537360, CUnitClientSpecific::RefreshWounds);
