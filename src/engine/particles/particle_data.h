#pragma once

// Data effects in the game (docs/notes/effects.md, "Data effects"): the registry of Res\effects\<name>.effect.toml by
// name and id, the particle side of the runtime (a ParticleSource whose type is a data effect's id runs these instead
// of a type's own functions), and the typed inputs game code drives an effect with.

#include "engine/particles/particle_record.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ParticleManager;
class ParticleSource;
struct Particle;
struct Vector3;

namespace effectdata {

// The effects the layers hold, each loaded at its first use and kept until the mod changes (Reset).
class Registry {
public:
    // The effect `name` (its file's stem, any case), or null when no layer has it or it does not load (each problem
    // logged once).
    std::shared_ptr<const Record> Find(std::string_view name);
    // The effect whose id is `id` (a peer's update, a save): Res\effects is listed through the layers at the first id
    // not met yet. An original type's id answers its built-in effect (assets/effects), null while the type is code.
    std::shared_ptr<const Record> FindById(std::int32_t id);
    // Every effect forgotten (a switch of mods; LoadDatabases).
    void Reset();
    // `record` stands for the effect `name` until Reset (the effect viewer's edit, src/tools/effect_viewer.cpp): Find
    // and FindById answer it, so the game's own sources of it and the effects that start it take it too.
    void Put(std::string_view name, std::shared_ptr<const Record> record);

private:
    struct Entry {
        std::string name;   // as first met
        std::shared_ptr<const Record> record;
        bool loaded = false;
    };
    Entry& EntryOf(std::string_view name);
    void Load(Entry& entry);
    void Scan();

    std::map<std::string, Entry, std::less<>> byName;   // by lower-case name
    std::map<std::int32_t, std::string> idNames;         // id to lower-case name
    bool scanned = false;
};
extern Registry g_effects;

// The text of the built-in effect `name` (assets/effects, built into the executable; any case), empty when there is
// none: the original effects on data, the bottom layer below the mods' files of their names.
std::string_view BuiltinText(std::string_view name);

// `tree` (the effect `name`'s layers merged) built on the effect its `like` names, that effect's layers read as the
// registry reads them (effectdata::ResolveLike, particle_record.h): the effect viewer's edit resolves as the game does.
bool ResolveLike(RawNode& tree, std::string_view name, std::vector<std::string>* errors);

// ---- The particle side (particle_create.cpp, particle_manager.cpp) --------------------------------------------------

// The source set up from `record` (Create's work for a data type; the texture and frames are Create's).
void SetUp(ParticleSource& s, std::shared_ptr<const Record> record);
// The tick's work before the particles move: the effect's state stepped, the emission of this tick set.
void TickData(ParticleSource& s);
// One particle of the regular emission (0: none, the pool takes it back).
int EmitData(ParticleSource& s, Particle& p, int index);
// One particle moved (0: it dies).
int UpdateData(ParticleSource& s, Particle& p);
// The particles the tick's actions asked for, before the regular emission (and while it is off).
void EmitQueued(ParticleSource& s);
// The world's effect object made the source and caught it up (EffectParticle::Spawn): the "spawn" reactions run and
// their actions apply at once (a scorch draws the client's generator there).
void Spawned(ParticleSource& s);

// Makes the effects a source with ParticleSourceFlag::OwnClock starts (StartEffects: its `effect` actions) for what
// ticks that source (the effect viewer, src/tools): each with OwnClock too, so it pauses and scrubs with its parent.
// Null when no tool runs; the manager makes and holds the others (CreateSource).
inline ParticleSource* (*g_startOwnClock)(ParticleManager& manager, int type) = nullptr;

// ---- The inputs: how game code (and the Lua hooks, particle_lua.h) drives an effect ---------------------------------

// A float, bool (0 / 1) or enum (its index) input; false when the source is no data effect or has no such input.
bool SetInput(ParticleSource& s, std::string_view input, float value);
bool SetInput(ParticleSource& s, std::string_view input, const Vector3& value);
// An enum input by its value's name.
bool SetInput(ParticleSource& s, std::string_view input, std::string_view value);
// A list input's points.
bool SetInput(ParticleSource& s, std::string_view input, const std::vector<Vector3>& points);
// An event input fired: its reactions and stage moves run at the next tick.
bool Fire(ParticleSource& s, std::string_view event);

} // namespace effectdata
