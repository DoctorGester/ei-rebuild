// The companions of map objects (map_object.h): CMapObjectServer (class id 0x39, vtable 0073DD6C)
// and CMapObjectClientSpecific (0x3A, vtable 0073DBEC). Both classes are declared in unit.h; their
// +0x1C is the map object they belong to.

#include "map_object.h"
#include "object_tables.h"
#include "random.h"
#include "unit.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_effects.h"

#include <cstring>

// The light's distance test and position must round like the original.
#pragma float_control(precise, on)

namespace {
constexpr std::uint32_t kFlagDestroyed = 1;          // CEngineObject::flags_ bit set on death
constexpr std::uint32_t kFlagHidden = 0x80;          // the light's flags_: not shown
constexpr int kEndSoundTicks = 0x1E;                 // end sound when fewer ticks are left
constexpr int kSoundStench = 0x13;
constexpr int kSoundFeebleMind = 0x20;
constexpr std::uint32_t kStenchPeriod = 0x10;
constexpr float kLightHeight = 2.0f;                 // 0073DBE8
constexpr float kEndSoundNear = 8.0f;                // pushed as 0x41000000
constexpr float kEndSoundFar = 20.0f;                // 0x41A00000
constexpr float kStenchNear = 10.0f;                 // 0073DBE0
constexpr float kStenchFar = 50.0f;                  // 0073DBE4
const char* const kEndSoundName = reinterpret_cast<const char*>(0x0078EFCC);     // "magic\end1.wav"
const char* const kStenchSoundFormat = reinterpret_cast<const char*>(0x0078EFB8); // "magic\Stench\%i.wav"

CMapObject* OwnerOf(void* companion) { return CompanionOwner(static_cast<CEngineObject*>(companion)); }

// 00688530 (thiscall). --ticksLeft; is it still positive?
__declspec(naked) BOOL __fastcall TickEffect(MapEffect*) EI_ORIGINAL(0x00688530)
// 00524FE0 (thiscall, units range). Applies a hit to the server's statistics block (+0x20);
// returns 1.
__declspec(naked) BOOL __fastcall ApplyHit(void* /*stats*/, void*, const void* /*hit*/) EI_ORIGINAL(0x00524FE0)
// 0050F5B0 (map_object.cpp), through its game address.
__declspec(naked) std::uintptr_t __fastcall ClearEffectsAt(CMapObject*) EI_ORIGINAL(0x0050F5B0)

// The sound system (global 007AF784) and the light manager (007AF738); thiscall.
void* SoundSystem() { return *reinterpret_cast<void**>(0x007AF784); }
void* LightManager() { return *reinterpret_cast<void**>(0x007AF738); }
// 006B8600. Plays a 3D sound at `position`; returns its handle.
// Arguments: sounds, -, name, 0, position, near distance, far distance, 0, 0.
__declspec(naked) int __fastcall PlaySound3D(void*, void*, const char*, int, const float*, float, float, int, int)
    EI_ORIGINAL(0x006B8600)
// 006B7C10 / 006B7CB0. Status tests of a sound handle (the end sound is not replayed while either
// holds).
__declspec(naked) BOOL __fastcall SoundIsPlaying(void* /*sounds*/, void*, int /*handle*/) EI_ORIGINAL(0x006B7C10)
__declspec(naked) BOOL __fastcall SoundIsQueued(void* /*sounds*/, void*, int /*handle*/) EI_ORIGINAL(0x006B7CB0)
// 006BA400. Moves a looping sound.
__declspec(naked) void __fastcall MoveSound(void* /*sounds*/, void*, int /*handle*/, const float* /*position*/)
    EI_ORIGINAL(0x006BA400)
// 006CA830. Points a light along `direction`.
void __fastcall AimLight(void* a0, void*, CEngineObject* a2, const float* a3) { LightMoveOriginal(a0, nullptr, a2, a3); }

// A looping effect sound of the client companion (+0x40, 0x14 bytes; built by 005112A0).
struct EffectSound {
    int kind;                       // +0x00 the effect type
    int unknown_04;                 // -1
    int handle;                     // +0x08 sound handle (feeble mind), -1 none
    std::uint32_t timer;            // +0x0C ticks to the next stench sound
    std::uint8_t present;           // +0x10 (used while the effect list is compared)
    std::uint8_t unknown_11[3];
};
static_assert(sizeof(EffectSound) == 0x14);

// The client companion's fields beyond unit.h's layout.
EffectSound* Sounds(void* self) { return *reinterpret_cast<EffectSound**>(static_cast<char*>(self) + 0x40); }
int SoundCount(void* self) { return *reinterpret_cast<int*>(static_cast<char*>(self) + 0x44); }
CEngineObject* Light(void* self) { return *reinterpret_cast<CEngineObject**>(static_cast<char*>(self) + 0x50); }
int& EndSound(void* self) { return *reinterpret_cast<int*>(static_cast<char*>(self) + 0x54); }
float* PositionOf(CEngineObject* object) { return reinterpret_cast<float*>(reinterpret_cast<char*>(object) + 0x1C); }

bool IsScaleEffect(int type) {
    return type == mapeffect::kStrengthen || type == mapeffect::kWeaken || type == mapeffect::kEnlarge ||
           type == mapeffect::kShrink;
}
} // namespace

// --- CMapObjectServer ------------------------------------------------------------------------

int CMapObjectServer::Update() {
    if (OwnerOf(this)->effectCount <= 0) return 0;
    int i = 0;
    do {
        if (!TickEffect(&OwnerOf(this)->effects[i])) {
            CMapObject* owner = OwnerOf(this);
            int type = owner->effects[i].type;
            int tail = owner->effectCount - i - 1;
            if (tail != 0) std::memmove(&owner->effects[i], &owner->effects[i + 1], tail * sizeof(MapEffect));
            --owner->effectCount;
            bool scaleEffect = IsScaleEffect(type);
            if (scaleEffect) ei::VCall<int>(OwnerOf(this), mapslot::kRecomputeScale);
            OwnerOf(this)->MarkChanged(3);
            ei::VCall<int>(OwnerOf(this), mapslot::kScaleEffectChanged, scaleEffect ? 1 : 0);
            --i;
            ei::VCall<int>(OwnerOf(this), mapslot::kEffectsChanged);
            if (!OwnerOf(this)->HasEffect(mapeffect::kStun) && !OwnerOf(this)->HasEffect(mapeffect::kStun2))
                ei::VCall<int>(OwnerOf(this), mapslot::kSetStunned, 0);
        }
        ++i;
    } while (i < OwnerOf(this)->effectCount);
    return 0;
}
EI_REPLACE_METHOD(0x00515820, CMapObjectServer::Update);

std::uintptr_t CMapObjectServer::SetData(const void* data) {
    std::memcpy(reinterpret_cast<char*>(this) + 0x20, data, 0x58);
    return OwnerOf(this)->MarkChanged(0);
}
EI_REPLACE_METHOD(0x00515950, CMapObjectServer::SetData);

BOOL CMapObjectServer::TakeHit(const void* hit) {
    if (!ApplyHit(reinterpret_cast<char*>(this) + 0x20, nullptr, hit)) return 0;
    OwnerOf(this)->MarkChanged(0);
    if (ei::VCall<BOOL>(this, 17)) {   // IsDead
        ClearEffectsAt(OwnerOf(this));
        OwnerOf(this)->flags_ |= kFlagDestroyed;
    }
    return 1;
}
EI_REPLACE_METHOD(0x00515980, CMapObjectServer::TakeHit);

// --- CMapObjectClientSpecific ----------------------------------------------------------------

int CMapObjectClientSpecific::Update() {
    // Effects count down here too (the server's list arrives over the network); an effect about
    // to end plays the end sound for text-key objects, unless it is still playing.
    if (OwnerOf(this)->effectCount > 0) {
        for (int i = 0; i < OwnerOf(this)->effectCount; ++i) {
            MapEffect& effect = OwnerOf(this)->effects[i];
            if (effect.ticksLeft <= 0) continue;
            int left = --effect.ticksLeft;
            if (left >= kEndSoundTicks || effect.ending) continue;
            if (!(IsTextKey(OwnerOf(this)->id_) & 0xFF)) continue;   // AL only
            int handle = EndSound(this);
            void* sounds = SoundSystem();
            if (handle != -1 && (SoundIsPlaying(sounds, nullptr, handle) || SoundIsQueued(sounds, nullptr, handle)))
                continue;
            EndSound(this) = PlaySound3D(SoundSystem(), nullptr, kEndSoundName, 0, &OwnerOf(this)->positionX,
                                         kEndSoundNear, kEndSoundFar, 0, 0);
        }
    }

    // Looping effect sounds: stench every 16 ticks (one of three, picked at random), feeble mind
    // follows the object.
    for (int i = 0; i < SoundCount(this); ++i) {
        EffectSound* sound = &Sounds(this)[i];
        if (sound->kind == kSoundStench) {
            std::uint32_t timer = sound->timer - 1;
            sound->timer = timer;
            if (!(timer & 0x80000000u)) continue;
            mfc::CString name;
            std::uint32_t variant = reinterpret_cast<IsaacRandom*>(0x007A9D40)->Next() % 3 + 1;
            if (sound->kind == kSoundStench) {
                name.Format(kStenchSoundFormat, variant);
                sound->timer = kStenchPeriod;
            }
            PlaySound3D(SoundSystem(), nullptr, name, 0, &OwnerOf(this)->positionX, kStenchNear, kStenchFar, 0, 0);
        } else if (sound->kind == kSoundFeebleMind && sound->handle != -1) {
            MoveSound(SoundSystem(), nullptr, sound->handle, &OwnerOf(this)->positionX);
        }
    }

    // The light follows the object, kLightHeight above it, aimed down at it.
    CEngineObject* light = Light(this);
    if (light && !(light->flags_ & kFlagHidden)) {
        float lightPosition[3];
        std::memcpy(lightPosition, PositionOf(light), sizeof(lightPosition));
        float lightZ = lightPosition[2] - kLightHeight;   // FSTP to the copy
        CMapObject* owner = OwnerOf(this);
        double dx = static_cast<double>(lightPosition[0]) - owner->positionX;
        double dy = static_cast<double>(lightPosition[1]) - owner->positionY;
        double dz = static_cast<double>(lightZ) - owner->positionZ;
        double distance2 = (dz * dz + dy * dy) + dx * dx;
        if (distance2 < 0.0 || distance2 > 0.0) {   // FCOMP 0: C3 (equal or unordered) skips
            float direction[3];
            direction[0] = owner->positionX - lightPosition[0];
            direction[1] = owner->positionY - lightPosition[1];
            direction[2] = owner->positionZ - lightZ;
            AimLight(LightManager(), nullptr, light, direction);
            owner = OwnerOf(this);
            float* position = PositionOf(Light(this));
            std::memcpy(position, &owner->positionX, 2 * sizeof(float));
            position[2] = static_cast<float>(static_cast<double>(kLightHeight) + owner->positionZ);
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x00510890, CMapObjectClientSpecific::Update);
