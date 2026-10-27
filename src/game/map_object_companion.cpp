// The companions of map objects (map_object.h): CMapObjectServer (class id 0x39) and CMapObjectClientSpecific
// (0x3A). Both classes are declared in unit.h; each holds the map object it belongs to.

#include "map_object.h"
#include "object_tables.h"
#include "random.h"
#include "sound.h"
#include "spell.h"    // CLight
#include "terrain.h"  // Terrain::MoveLight (the light manager)
#include "unit.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstring>
#include <string>

// The light's distance test and position must round like the original's.

namespace {
constexpr std::uint32_t kFlagDestroyed = 1;          // CEngineObject::flags_ bit set on death
constexpr std::uint32_t kFlagHidden = 0x80;          // the light's flags_: not shown
constexpr int kEndSoundTicks = 0x1E;                 // end sound when fewer ticks are left
constexpr int kSoundStench = 0x13;
constexpr int kSoundFeebleMind = 0x20;
constexpr std::uint32_t kStenchPeriod = 0x10;
constexpr float kLightHeight = 2.0f;
constexpr float kEndSoundNear = 8.0f;
constexpr float kEndSoundFar = 20.0f;
constexpr float kStenchNear = 10.0f;
constexpr float kStenchFar = 50.0f;
const char* const kEndSoundName = "magic\\end1.wav";
const char* const kStenchSoundFormat = "magic\\Stench\\%i.wav";

CMapObject* OwnerOf(CEngineObject* companion) { return CompanionOwner(companion); }

// The light's position (a point light of the light factory, class 0x31).
float* PositionOf(CLight* light) { return &light->localPosition_.x; }

bool IsScaleEffect(int type) {
    return type == mapeffect::kStrengthen || type == mapeffect::kWeaken || type == mapeffect::kEnlarge ||
           type == mapeffect::kShrink;
}
} // namespace


int CMapObjectServer::RunTick() {
    if (owner->effects.size <= 0) return 0;
    int i = 0;
    do {
        if (!owner->effects[i].Tick()) {
            int type = owner->effects[i].type;
            int tail = owner->effects.size - i - 1;
            if (tail != 0) std::memmove(&owner->effects[i], &owner->effects[i + 1], tail * sizeof(MapEffect));
            --owner->effects.size;
            bool scaleEffect = IsScaleEffect(type);
            if (scaleEffect) ObjectSlots(owner)->RecomputeScale();
            owner->MarkChanged(3);
            ObjectSlots(owner)->ScaleEffectChanged(scaleEffect ? 1 : 0);
            --i;
            ObjectSlots(owner)->EffectsChanged();
            if (!owner->HasEffect(mapeffect::kStun) && !owner->HasEffect(mapeffect::kStun2))
                ObjectSlots(owner)->SetStunned(0);
        }
        ++i;
    } while (i < owner->effects.size);
    return 0;
}
EI_REWRITTEN(0x00515820, CMapObjectServer::RunTick);

std::uintptr_t CMapObjectServer::SetData(const void* data) {
    std::memcpy(&vitals, data, sizeof(vitals));
    return owner->MarkChanged(0);
}
EI_REWRITTEN(0x00515950, CMapObjectServer::SetData);

BOOL CMapObjectServer::TakeHit(const void* hit) {
    if (!vitals.TakeHit(static_cast<const DamageInfo*>(hit))) return 0;
    owner->MarkChanged(0);
    if (IsDead()) {
        owner->ClearEffects();
        owner->flags_ |= kFlagDestroyed;
    }
    return 1;
}
EI_REWRITTEN(0x00515980, CMapObjectServer::TakeHit);


int CMapObjectClientSpecific::RunTick() {
    // Effects count down here too (the server's list arrives over the network); an effect about
    // to end plays the end sound for text-key objects, unless it is still playing.
    if (OwnerOf(this)->effects.size > 0) {
        for (int i = 0; i < OwnerOf(this)->effects.size; ++i) {
            MapEffect& effect = OwnerOf(this)->effects[i];
            if (effect.ticksLeft <= 0) continue;
            int left = --effect.ticksLeft;
            if (left >= kEndSoundTicks || effect.ending) continue;
            if (!(IsTextKey(OwnerOf(this)->id_) & 0xFF)) continue;
            int handle = endSound;
            CSoundManager* soundSystem = TheSoundManager();
            if (handle != -1 &&
                (soundSystem->FindPlaying(handle) || soundSystem->FindDeferred(handle)))
                continue;
            endSound = TheSoundManager()->Play3D(kEndSoundName, 0, &OwnerOf(this)->localPosition_.x,
                                                  kEndSoundNear, kEndSoundFar, 0, 0);
        }
    }

    // Looping effect sounds: stench every 16 ticks (one of three, picked at random), feeble mind
    // follows the object.
    for (int i = 0; i < sounds.size; ++i) {
        EffectSoundRecord* sound = &sounds[i];
        if (sound->kind == kSoundStench) {
            std::uint32_t timer = sound->timer - 1;
            sound->timer = timer;
            if (!(timer & 0x80000000u)) continue;
            std::string name;
            std::uint32_t variant = IsaacRandom::Global().Next() % 3 + 1;
            if (sound->kind == kSoundStench) {
                name = str::Format(kStenchSoundFormat, variant);
                sound->timer = kStenchPeriod;
            }
            TheSoundManager()->Play3D(name.c_str(), 0, &OwnerOf(this)->localPosition_.x, kStenchNear, kStenchFar, 0, 0);
        } else if (sound->kind == kSoundFeebleMind && sound->handle != -1) {
            TheSoundManager()->MoveSequence(sound->handle, &OwnerOf(this)->localPosition_.x);
        }
    }

    // The light follows the object, kLightHeight above it, aimed down at it.
    CLight* lamp = light;
    if (lamp && !(lamp->flags_ & kFlagHidden)) {
        Vector3 lightPosition;
        std::memcpy(&lightPosition, PositionOf(lamp), sizeof(lightPosition));
        float lightZ = lightPosition.z - kLightHeight;   // FSTP to the copy
        CMapObject* followed = OwnerOf(this);
        double dx = static_cast<double>(lightPosition.x) - followed->localPosition_.x;
        double dy = static_cast<double>(lightPosition.y) - followed->localPosition_.y;
        double dz = static_cast<double>(lightZ) - followed->localPosition_.z;
        double distance2 = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
        EI_NAN_AUDIT(distance2);
        if (distance2 != 0.0) {
            Vector3 direction;
            direction.x = followed->localPosition_.x - lightPosition.x;
            direction.y = followed->localPosition_.y - lightPosition.y;
            direction.z = followed->localPosition_.z - lightZ;
            TheTerrain()->MoveLight(lamp, &direction);
            followed = OwnerOf(this);
            float* position = PositionOf(light);
            std::memcpy(position, &followed->localPosition_.x, 2 * sizeof(float));
            position[2] = static_cast<float>(static_cast<double>(kLightHeight) + followed->localPosition_.z);
        }
    }
    return 1;
}
EI_REWRITTEN(0x00510890, CMapObjectClientSpecific::RunTick);
