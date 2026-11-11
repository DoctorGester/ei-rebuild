// The client companion's effect looks and sounds (MapObjectClientSpecific, map_object.h / unit.h): the particle
// sources, rings, figure flags and sounds that follow the object's effect list, each from its kind's record
// (SpellRecord::look ...; docs/notes/items_spells.md, "Active effects on scripts"), and the object's light.

#include "game/effects/effects.h"
#include "engine/particles/particle_data.h"
#include "engine/scene/light.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/core/base_manager.h"
#include "game/spells/spell.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "game/units/unit.h"

#include "game/console/console.h"
#include "lib/crt.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "game/app/singletons.h"
#include "game/app/options.h"
#include "game/world/world.h"

#include <list>

namespace {

constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // power / 16

// The sound distances (10 and 50), read from the exe.
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 50.0f;

// The particle type of the data effect `name` (the game's by its script name, a mod's from Res\effects); none for an
// empty name or one no effect has (the registry logs it).
std::optional<int> EffectTypeNamed(const std::string& name) {
    if (name.empty()) return std::nullopt;
    if (std::shared_ptr<const effectdata::Record> effect = effectdata::g_effects.Find(name)) return effect->id;
    return std::nullopt;
}

// The kind's ring (SpellRecord::lookRing) on `owner` at `power`: power / 16 times the record's ringScale, negated
// as the kind goes (`ending`).
void SpawnRing(MapObject* owner, const itemdb::SpellRecord& kind, int power, bool ending) {
    const std::optional<int> type = EffectTypeNamed(kind.lookRing);
    if (!type) return;
    ParticleSource* ring = g_clientWorld.particleManager->CreateSource(*type);
    if (!ring) return;
    ring->Attach(owner);
    const float size = static_cast<float>(power) * kPowerScale * kind.ringScale;
    const float factor = size / ring->baseSize;
    ring->ScaleSize(ending ? -factor : factor);
}

// A kind's sounds as it comes: its loop as a sequence led by the start sound (`play`: not when the effect arrives
// with the object), or the start sound alone; a loop by ticks plays at the next tick (RunTick).
void StartKindSounds(EffectSoundRecord& record, const itemdb::SpellRecord& kind, bool play,
                     const Vector3& position) {
    SoundManager* sounds = g_clientWorld.sounds.get();
    const bool withStart = play && !kind.startSound.empty();
    if (!kind.loopSounds.empty() && kind.loopTicks <= 0) {
        std::vector<std::string> names;
        if (withStart) names.push_back(kind.startSound);
        names.insert(names.end(), kind.loopSounds.begin(), kind.loopSounds.end());
        record.handle = sounds->AddSequence(names, 0, position, kSoundNear, kSoundFar, false, -1,
                                            ChooserFor(static_cast<int>(kind.loopSounds.size()), withStart));
        return;
    }
    if (withStart) sounds->Play3D(kind.startSound, 0, position, kSoundNear, kSoundFar, false);
}

// A kind's sounds as it goes: its loop stopped, its end sound played when `play` (the object is not dead).
void EndKindSounds(EffectSoundRecord& record, const itemdb::SpellRecord* kind, bool play, const Vector3& position) {
    SoundManager* sounds = g_clientWorld.sounds.get();
    if (record.handle != -1) {
        sounds->RemoveSequence(record.handle);
        record.handle = -1;
    }
    if (play && kind && !kind->endSound.empty())
        sounds->Play3D(kind->endSound, 0, position, kSoundNear, kSoundFar, false);
}

// A new light's flags (ShowLight then clears Hidden).
constexpr LightFlag kLightNew = LightFlag::Hidden | LightFlag::Moving | LightFlag::Off;


// The light goes out for good: no longer held, hidden, its id off the list, released.
void DropLight(MapObjectClientSpecific* self) {
    self->light->flags &= ~LightFlag::Held;
    self->HideLight();
    self->light->flags |= LightFlag::Held;
    int id = self->light->id;
    std::erase(g_clientWorld.lightFactory->litIds, id);
    self->light.Reset();
}

} // namespace

void MapObjectClientSpecific::EffectsChanged(bool first) {
    UpdateEffectParticles(first);
    UpdateEffectSounds(first);
}

void MapObjectClientSpecific::UpdateEffectParticles(bool first) {
    // The effects' kinds, once each at the highest power.
    struct Wanted {
        int kind = -1;
        int power = 0;
    };
    std::vector<Wanted> wanted;
    for (const MapEffect& effect : owner->effects) {
        if (!effect.KindRecord()) continue;
        const int kind = effect.Kind();
        const auto same = std::ranges::find(wanted, kind, &Wanted::kind);
        if (same == wanted.end()) wanted.push_back({kind, effect.power});
        else if (effect.power > same->power) same->power = effect.power;
    }

    for (ParticleRecord& record : particles) record.present = 0;

    for (const Wanted& want : wanted) {
        // The same kind at the same power keeps its record.
        auto kept = std::ranges::find_if(particles, [&want](const ParticleRecord& record) {
            return !record.present && record.kind == want.kind && record.power == want.power;
        });
        if (kept != particles.end()) {
            kept->present = 1;
            continue;
        }
        // A look of the same kind takes the new power.
        kept = std::ranges::find_if(particles, [&want](const ParticleRecord& record) {
            return !record.present && record.kind == want.kind && record.source;
        });
        if (kept != particles.end()) {
            kept->power = want.power;
            ParticleSource* source = kept->source;
            source->ScaleSize(static_cast<float>(want.power) * kPowerScale / source->baseSize);
            kept->present = 1;
            continue;
        }

        const itemdb::SpellRecord& kind = *itemdb::SpellAt(static_cast<std::uint32_t>(want.kind));
        ParticleRecord added;
        added.kind = want.kind;
        added.power = want.power;
        added.present = 1;
        if (!first) SpawnRing(owner, kind, want.power, false);
        if (const std::optional<int> look = EffectTypeNamed(kind.look)) {
            if (ParticleSource* source = g_clientWorld.particleManager->CreateSource(*look)) {
                added.source = source;
                source->Attach(owner);
                source->ScaleSize(static_cast<float>(want.power) * kPowerScale / source->baseSize);
                if (first)
                    for (int tick = 0; tick < kWarmUpTicks; ++tick) source->RunTick();
            }
        }
        particles.push_back(std::move(added));
    }

    for (std::size_t i = 0; i < particles.size();) {
        ParticleRecord& record = particles[i];
        if (record.present) {
            ++i;
            continue;
        }
        const itemdb::SpellRecord* kind = itemdb::SpellAt(static_cast<std::uint32_t>(record.kind));
        if (kind && !owner->IsDead()) SpawnRing(owner, *kind, record.power, true);
        if (record.source) record.source->Stop();
        particles.erase(particles.begin() + static_cast<std::ptrdiff_t>(i));
    }

    // The figure flags follow the kinds left; the figure's alpha or tint goes back as its flag goes.
    bool faded = false;
    bool tinted = false;
    for (const ParticleRecord& record : particles) {
        const itemdb::SpellRecord* kind = itemdb::SpellAt(static_cast<std::uint32_t>(record.kind));
        if (!kind) continue;
        faded = faded || kind->figure == EffectFigure::Faded;
        tinted = tinted || kind->figure == EffectFigure::Tinted;
    }
    if (faded) {
        owner->flags |= MapObjectFlag::Invisible;
    } else if (lib::Has(owner->flags, MapObjectFlag::Invisible)) {
        owner->flags &= ~MapObjectFlag::Invisible;
        owner->ClientSpecific()->ResetPartAlpha();
    }
    if (tinted) {
        owner->flags |= MapObjectFlag::Stunned;
    } else if (lib::Has(owner->flags, MapObjectFlag::Stunned)) {
        owner->flags &= ~MapObjectFlag::Stunned;
        owner->ClientSpecific()->ClearPartTints();
    }
}

void MapObjectClientSpecific::UpdateEffectSounds(bool first) {
    // The effects' kinds, once each.
    std::vector<int> kinds;
    for (const MapEffect& effect : owner->effects) {
        if (!effect.KindRecord()) continue;
        const int kind = effect.Kind();
        if (std::ranges::find(kinds, kind) == kinds.end()) kinds.push_back(kind);
    }

    for (EffectSoundRecord& record : sounds) record.present = 0;

    for (const int kind : kinds) {
        const auto kept = std::ranges::find_if(
            sounds, [kind](const EffectSoundRecord& record) { return !record.present && record.kind == kind; });
        if (kept != sounds.end()) {
            kept->present = 1;
            continue;
        }
        EffectSoundRecord added;
        added.kind = kind;
        added.present = 1;
        StartKindSounds(added, *itemdb::SpellAt(static_cast<std::uint32_t>(kind)), !first, owner->localPosition);
        sounds.push_back(added);
    }

    for (std::size_t i = 0; i < sounds.size();) {
        EffectSoundRecord& record = sounds[i];
        if (record.present) {
            ++i;
            continue;
        }
        EndKindSounds(record, itemdb::SpellAt(static_cast<std::uint32_t>(record.kind)), !owner->IsDead(),
                      owner->localPosition);
        sounds.erase(sounds.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void Object3DClientSpecific::ResetPartAlpha() {
    for (int i = 0; i < std::ssize(materials); ++i) materials[i]->colors.diffuse.w = 1.0f;
}

void Object3DClientSpecific::ClearPartTints() {
    for (int i = 0; i < std::ssize(materials); ++i) {
        Material* part = materials[i];
        part->colors.specular = {0.0f, 0.0f, 0.0f, part->colors.specular.w};
        materials[i]->flags &= ~MaterialFlag::KeepSpecular;
    }
}

void MapObjectClientSpecific::HideLight() {
    if (!light || (light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags |= LightFlag::Hidden;
    g_clientWorld.terrain->RemoveLight(light);
    MapObject* mapObject = owner;
    if (mapObject && mapObject->attached) mapObject->attached->SetLit(false);
}

void MapObjectClientSpecific::ShowLight() {
    if (!light || !(light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags &= ~LightFlag::Hidden;
    g_clientWorld.terrain->AddLight(light);
    MapObject* mapObject = owner;
    if (mapObject && mapObject->GetClassId() == ClassId::Unit && mapObject->attached) mapObject->attached->SetLit(true);
}

void MapObjectClientSpecific::CreateLight(const float* colour, float radius, bool flag) {
    HideLight();
    light.Assign(g_clientWorld.lightFactory->CreateLight(kLightTypePoint));
    Vector3 rgb = {colour[0], colour[1], colour[2]};
    light->SetPoint(rgb, rgb);
    MapObject* mapObject = owner;
    Light* point = light;
    point->localPosition = {mapObject->localPosition.x, mapObject->localPosition.y,
                             kLightHeight + mapObject->localPosition.z};
    point->range = radius;
    float squared = radius * radius;
    point->rangeSquared = squared;
    point->inverseRangeSquared = 1.0f / squared;
    light->flags |= kLightNew;
    if (flag) light->flags |= LightFlag::Specular;
    ShowLight();
}

void MapObjectClientSpecific::SetLightMode(int mode) {
    if (mode == 1) {
        if (!light) {
            const float* quest = &options::kQuestLight.red;   // red, green, blue
            float colour[4] = {quest[0], quest[1], quest[2], 1.0f};
            CreateLight(colour, options::kQuestLight.radius, true);
        } else {
            light->flags &= ~LightFlag::Held;
            ShowLight();
        }
        light->flags |= LightFlag::Held;
        int lightId = light->id;
        g_clientWorld.lightFactory->litIds.push_back(lightId);
        return;
    }
    if (mode == 2 && light) DropLight(this);
}

void MapObjectClientSpecific::Destroy() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    if (light) DropLight(this);
    owner = nullptr;
}
