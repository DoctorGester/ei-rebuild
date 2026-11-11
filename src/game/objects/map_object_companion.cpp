// The companions of map objects (map_object.h): MapObjectServer (class id 0x39) and MapObjectClientSpecific
// (0x3A). Both classes are declared in unit.h; each holds the map object it belongs to.
#include "game/effects/effects.h"   // ParticleSource
#include "game/app/singletons.h"
#include "lib/bytes.h"
#include "lib/str.h"
#include "game/objects/map_object.h"
#include "game/net/net_delta.h"
#include "game/data/text_manager.h"
#include "game/core/random.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "game/scene/light.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr int kEndSoundTicks = 0x1E;                 // end sound when fewer ticks are left
constexpr float kEndSoundNear = 8.0f;
constexpr float kEndSoundFar = 20.0f;
constexpr float kLoopNear = 10.0f;                   // the distances of a loop by ticks
constexpr float kLoopFar = 50.0f;
const char* const kEndSoundName = "magic\\end1.wav";
} // namespace


bool MapObjectServer::RunTick() {
    if (std::ssize(owner->effects) <= 0) return false;
    int i = 0;
    do {
        if (!owner->effects[i].Tick()) {
            const bool scales = owner->ScalesWith(owner->effects[i]);
            owner->effects.erase(owner->effects.begin() + i);
            owner->EffectRemoved(scales);
            --i;
        }
        ++i;
    } while (i < std::ssize(owner->effects));
    return false;
}

void MapObjectServer::SetData(const UnitVitals* data) {
    vitals = lib::Load<UnitVitals>(data);
    owner->MarkChanged(NetField::Vitals);
}

bool MapObjectClientSpecific::RunTick() {
    // Effects count down here too (the server's list arrives over the network); an effect about
    // to end plays the end sound for text-key objects, unless it is still playing.
    if (std::ssize(owner->effects) > 0) {
        for (int i = 0; i < std::ssize(owner->effects); ++i) {
            MapEffect& effect = owner->effects[i];
            if (effect.ticksLeft <= 0) continue;
            int left = --effect.ticksLeft;
            if (left >= kEndSoundTicks || effect.ending) continue;
            if (!IsTextKey(owner->id)) continue;
            if (g_clientWorld.sounds->IsPlaying(endSound)) continue;
            endSound = g_clientWorld.sounds->Play3D(kEndSoundName, 0, owner->localPosition,
                                                  kEndSoundNear, kEndSoundFar, false);
        }
    }

    // The kinds' loops (SpellRecord::loopSounds): one by ticks plays one of its sounds at random every loopTicks + 1
    // ticks (stench's every 17), a sequence follows the object.
    for (EffectSoundRecord& sound : sounds) {
        const itemdb::SpellRecord* kind = itemdb::SpellAt(static_cast<std::uint32_t>(sound.kind));
        if (kind && kind->loopTicks > 0 && !kind->loopSounds.empty()) {
            --sound.timer;
            if (static_cast<std::int32_t>(sound.timer) >= 0) continue;
            const std::uint32_t variant = g_clientRandom.Next() % static_cast<std::uint32_t>(kind->loopSounds.size());
            sound.timer = static_cast<std::uint32_t>(kind->loopTicks);
            g_clientWorld.sounds->Play3D(kind->loopSounds[variant], 0, owner->localPosition, kLoopNear, kLoopFar,
                                         false);
        } else if (sound.handle != -1) {
            g_clientWorld.sounds->MoveSequence(sound.handle, owner->localPosition);
        }
    }

    // The light follows the object, kLightHeight above it, aimed down at it.
    Light* lamp = light;
    if (lamp && !(lamp->flags & LightFlag::Hidden)) {
        const Vector3 lightPosition = lamp->localPosition;
        float lightZ = lightPosition.z - kLightHeight;   // stored as a float
        MapObject* followed = owner;
        float dx = lightPosition.x - followed->localPosition.x;
        float dy = lightPosition.y - followed->localPosition.y;
        float dz = lightZ - followed->localPosition.z;
        float distance2 = LengthSquared(Vector3{dx, dy, dz});
        if (distance2 != 0.0f) {
            Vector3& position = light->localPosition;
            position.x = followed->localPosition.x;
            position.y = followed->localPosition.y;
            position.z = kLightHeight + followed->localPosition.z;
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

MapObjectClientSpecific::MapObjectClientSpecific() = default;

MapObjectClientSpecific::~MapObjectClientSpecific() { MapObjectClientSpecific::Destroy(); }

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
