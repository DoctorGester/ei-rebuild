// The companions of map objects (map_object.h): MapObjectServer (class id 0x39) and MapObjectClientSpecific
// (0x3A). Both classes are declared in unit.h; each holds the map object it belongs to.
#include "effects.h"   // ParticleSource
#include "game_singletons.h"
#include "lib/bytes.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "map_object.h"
#include "net_delta.h"
#include "object_tables.h"
#include "random.h"
#include "sound.h"
#include "terrain.h"
#include "spell.h"    // Light
#include "unit.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr int kNoSound = -1;
constexpr int kEndSoundTicks = 0x1E;                 // end sound when fewer ticks are left
constexpr std::uint32_t kStenchPeriod = 0x10;
constexpr float kLightHeight = 2.0f;
constexpr float kEndSoundNear = 8.0f;
constexpr float kEndSoundFar = 20.0f;
constexpr float kStenchNear = 10.0f;
constexpr float kStenchFar = 50.0f;
const char* const kEndSoundName = "magic\\end1.wav";
const char* const kStenchSoundFormat = "magic\\Stench\\%i.wav";

bool IsScaleEffect(SpellType type) {
    return type == SpellType::Strengthen || type == SpellType::Weaken || type == SpellType::Enlarge ||
           type == SpellType::Shrink;
}
} // namespace


bool MapObjectServer::RunTick() {
    if (std::ssize(owner->effects) <= 0) return false;
    int i = 0;
    do {
        if (!owner->effects[i].Tick()) {
            SpellType spellType = owner->effects[i].Type();
            owner->effects.erase(owner->effects.begin() + i);
            bool scaleEffect = IsScaleEffect(spellType);
            if (scaleEffect) owner->RecomputeScale();
            owner->MarkChanged(3);
            owner->ScaleEffectChanged(scaleEffect);
            --i;
            owner->EffectsChanged();
            if (!owner->HasEffect(SpellType::Stun) &&
                !owner->HasEffect(SpellType::Teleport))
                owner->SetStunned(0);
        }
        ++i;
    } while (i < std::ssize(owner->effects));
    return false;
}

void MapObjectServer::SetData(const UnitVitals* data) {
    lib::LoadBytes(vitals, data);
    owner->MarkChanged(0);
}

bool MapObjectServer::TakeHit(const DamageInfo* hit) {
    vitals.TakeHit(hit);
    owner->MarkChanged(0);
    if (IsDead()) {
        owner->ClearEffects();
        owner->flags |= MapObjectFlag::Removed;
    }
    return true;
}

bool MapObjectClientSpecific::RunTick() {
    // Effects count down here too (the server's list arrives over the network); an effect about
    // to end plays the end sound for text-key objects, unless it is still playing.
    if (std::ssize(CompanionOwner(this)->effects) > 0) {
        for (int i = 0; i < std::ssize(CompanionOwner(this)->effects); ++i) {
            MapEffect& effect = CompanionOwner(this)->effects[i];
            if (effect.ticksLeft <= 0) continue;
            int left = --effect.ticksLeft;
            if (left >= kEndSoundTicks || effect.ending) continue;
            if (!IsTextKey(CompanionOwner(this)->id)) continue;
            int handle = endSound;
            SoundManager* soundSystem = g_clientWorld.sounds;
            if (handle != -1 &&
                (soundSystem->FindPlaying(handle) || soundSystem->FindDeferred(handle)))
                continue;
            endSound = g_clientWorld.sounds->Play3D(kEndSoundName, 0, &CompanionOwner(this)->localPosition.x,
                                                  kEndSoundNear, kEndSoundFar, 0);
        }
    }

    // Looping effect sounds: stench every 16 ticks (one of three, picked at random), feeble mind
    // follows the object.
    for (int i = 0; i < std::ssize(sounds); ++i) {
        EffectSoundRecord* sound = &sounds[i];
        if (sound->kind == SpellType::Stench) {
            --sound->timer;
            if (static_cast<std::int32_t>(sound->timer) >= 0) continue;
            std::string name;
            std::uint32_t variant = g_clientRandom.Next() % 3 + 1;
            if (sound->kind == SpellType::Stench) {
                name = str::Format(kStenchSoundFormat, variant);
                sound->timer = kStenchPeriod;
            }
            g_clientWorld.sounds->Play3D(name, 0, &CompanionOwner(this)->localPosition.x, kStenchNear, kStenchFar, 0);
        } else if (sound->kind == SpellType::Feeblemind && sound->handle != -1) {
            g_clientWorld.sounds->MoveSequence(sound->handle, &CompanionOwner(this)->localPosition.x);
        }
    }

    // The light follows the object, kLightHeight above it, aimed down at it.
    Light* lamp = light;
    if (lamp && !(lamp->flags & LightFlag::Hidden)) {
        Vector3 lightPosition;
        lib::LoadBytes(lightPosition, &lamp->localPosition.x);
        float lightZ = lightPosition.z - kLightHeight;   // stored as a float
        MapObject* followed = CompanionOwner(this);
        float dx = lightPosition.x - followed->localPosition.x;
        float dy = lightPosition.y - followed->localPosition.y;
        float dz = lightZ - followed->localPosition.z;
        float distance2 = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
        EI_NAN_AUDIT(distance2);
        if (distance2 != 0.0f) {
            float* position = &light->localPosition.x;
            std::memcpy(position, &followed->localPosition.x, 2 * sizeof(float));
            position[2] = kLightHeight + followed->localPosition.z;
        }
    }
    return true;
}

// --- The companions' lifetime ----------------------------------------------------------------------

MapObjectServer::MapObjectServer() {
    vitals.Reset();
}

MapObjectServer::~MapObjectServer() {
    vitals.Reset();
    strings.clear();
    owner = nullptr;
}

MapObjectClientSpecific::MapObjectClientSpecific() {
    lightStateA = 0;
    lightStateB = 0;
}

MapObjectClientSpecific::~MapObjectClientSpecific() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    if (light) {
        light->flags &= ~LightFlag::Held;
        if (Light* shown = light; shown && !(shown->flags & LightFlag::Hidden) && !(shown->flags & LightFlag::Held)) {
            shown->flags |= LightFlag::Hidden;
            g_clientWorld.terrain->RemoveLight(light);
            if (Unit* unit = OwnerUnit()) {
                if (Object3DSpecific* half = unit->AsObject3D()->attached) half->SetLit(false);
            }
        }
        int lightId = light->id;
        std::erase(g_clientWorld.lightFactory->litIds, lightId);
        light.Reset();
    }
    owner = nullptr;
}

namespace {
constexpr const char* kClientClassName = "CMapObjectClientSpecific";
constexpr const char* kServerClassName = "CMapObjectServer";
} // namespace

// --- Create and Destroy -----------------------------------------------------------------------------

void MapObjectClientSpecific::Create() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    endSound = kNoSound;
    owner = nullptr;
}

void MapObjectServer::Create() {
    owner = nullptr;
    vitals.Reset();
}

void MapObjectServer::Destroy() {
    vitals.Reset();
    strings.clear();
    owner = nullptr;
}

// --- The base companion's small virtuals (what the server's MapObjectServer mostly keeps) ---------------

MapObjectCompanion::~MapObjectCompanion() { owner = nullptr; }

void MapObjectCompanion::Create() {
    owner = nullptr;
}
void MapObjectCompanion::Destroy() { owner = nullptr; }

// --- The client's and the server's small virtuals ------------------------------------------------------

void MapObjectClientSpecific::Hit(int damage, int kind) {
    if (owner->IsDead() != 0) return;
    if (!(owner->flags & MapObjectFlag::InParty)) return;
    ClientWorld()->ShowNumber(owner->id, damage, kind);
}

// Class name and id.
const char* MapObjectClientSpecific::ClassName() { return kClientClassName; }
ClassId MapObjectClientSpecific::GetClassId() { return ClassId::MapObjectClientSpecific; }

// The client companion's particle records.
ParticleRecord::ParticleRecord(ParticleRecord&&) noexcept = default;
ParticleRecord& ParticleRecord::operator=(ParticleRecord&&) noexcept = default;
ParticleRecord::~ParticleRecord() = default;

const char* MapObjectServer::ClassName() { return kServerClassName; }
ClassId MapObjectServer::GetClassId() { return ClassId::MapObjectServer; }

// --- The client companion's particle records and the network records ---------------------------------
