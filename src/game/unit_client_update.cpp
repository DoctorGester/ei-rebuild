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
#include "originals_engine.h"
#include "originals_world.h"

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
constexpr int kIdleRestartBelow = 3;                  // CFigureAnimationState::unknown_50

// Not rewritten (forwarders):
// 0051C9F0. The client half: with 0 every drawn part takes back the colour it kept (with 1 it
// keeps it and turns white). RET 4.
__declspec(naked) void __fastcall RestorePartColours(CObject3DClientSpecific*, void*, int) EI_ORIGINAL(0x0051C9F0)
// 00538ED0 / 00539010. The blink: the face texture with closed / open eyes (RET 4).
__declspec(naked) void __fastcall CloseEyes(CUnitClientSpecific*, void*, int) EI_ORIGINAL(0x00538ED0)
__declspec(naked) void __fastcall OpenEyes(CUnitClientSpecific*, void*, int) EI_ORIGINAL(0x00539010)
// 00534B60. The sounds of the animation's steps and hits (plain RET).
__declspec(naked) void __fastcall PlayStepSounds(CUnitClientSpecific*) EI_ORIGINAL(0x00534B60)
// 005380D0. Plays a speech; false when the same one played within 5 seconds (RET 4, AL).
__declspec(naked) bool __fastcall Speak(CUnitClientSpecific*, void*, ClientSpeech*) EI_ORIGINAL(0x005380D0)
// 005392F0 (library: STLport list::erase). Removes `at`, the next node to *out (RET 8).
__declspec(naked) void __fastcall EraseSpeech(ClientSpeechNode**, void*, ClientSpeechNode** /*out*/,
                                              ClientSpeechNode* /*at*/) EI_ORIGINAL(0x005392F0)
// 00535480. Starts the idle animation of the weapon in hand (0: 0050CBA0, else 0050CA50; RET 4).
__declspec(naked) void __fastcall StartIdleAnimation(CUnitClientSpecific*, void*, int) EI_ORIGINAL(0x00535480)

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
    if (current) ShowWeaponParts((current->key & kKeyLowBits) == 0);

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
        for (FigurePart* part = half->FirstDrawnPart(); part; part = half->NextDrawnPart()) part->alpha = fadeIn;
        volatile float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = !(next >= 1.0f) ? next : 1.0f;   // the original's select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        RestorePartColours(static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached), nullptr, 0);

    if (blinks) {
        if (blinkTimer > 0) {
            --blinkTimer;
        } else {
            blinkTimer = gamecrt::rand() * 8 / 0x7FFF % 8 + 2;
            if (eyesClosed == 0) CloseEyes(this, nullptr, 0);
            else OpenEyes(this, nullptr, 0);
        }
    }
    UpdateArrow();
    PlayStepSounds(this);

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
    if (unit->unknown_255 == kModeSpecialIdle && unit->Object3D()->animation->unknown_50 < kIdleRestartBelow)
        StartIdleAnimation(this, nullptr, 0);
    return 1;
}
EI_REPLACE_METHOD(0x005341A0, CUnitClientSpecific::UpdateClient);
