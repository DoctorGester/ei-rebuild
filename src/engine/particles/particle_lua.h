#pragma once

// The effects' Lua hooks (docs/notes/effects.md, "Lua hooks"): an effect's `script = "<file>.lua"` (Res\effects, through
// the mod layers) returns a table of hooks that drive the effect's typed inputs from Lua (on_start, on_tick, on_event)
// and shape its particles (on_spawn, and the opt-in on_update). They run in a Lua state of the client's own, apart
// from the world scripts' (game/script/script_lua.h): the same sandbox (engine/core/lua_sandbox.h), the particles' own
// generator for its draws, never saved (particles are not kept by saves) and never on the server. The calls per
// particle share one budget per tick (kParticleBudget), so a catch-up loop of many ticks in one frame stays bounded.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ParticleSource;
struct Particle;
struct Vector3;

namespace effectdata {
struct Record;
}

namespace effectlua {

struct Script;   // particle_lua.cpp: a loaded script file and its hooks

// One source's hooks while it runs its effect (ParticleSource::script): its table `e` in the Lua state, kept from hook to
// hook (the script's own fields: `e.ring = 0`).
class Instance {
public:
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance();

    Script* script = nullptr;
    int table = -2;                 // the registry's reference (LUA_NOREF: none)
    std::uint32_t generation = 0;   // the state's it was made in (a Reset makes it stale)
    bool started = false;           // on_start ran
    bool ended = false;             // "end" was told
};

// The hooks of `record`'s script for a new source of it; null when it has none, or its script does not load (logged once
// per file; the effect then runs as its data alone).
std::unique_ptr<Instance> Attach(const effectdata::Record& record);

// The tick's hooks before the data's step (particle_data.cpp, TickData): on_start at the first, on_tick, then on_event
// for "stop" (`stopped`: the game stopped the emission since the last tick) and each event input fired.
void BeforeStep(ParticleSource& s, bool stopped);
// After the step: "end", once, when the effect ended.
void AfterStep(ParticleSource& s);
// The game's effect object put the effect into the world (effectdata::Spawned): on_event "spawn".
void Spawned(ParticleSource& s);
// on_spawn for a particle the data made, whose place in its space is measured from `spaceOrigin` (the source's origin,
// its anchor's place; unread in world space); false when the hook killed it (it is not kept).
bool OnSpawn(ParticleSource& s, Particle& p, int index, const Vector3& spaceOrigin);
// on_update for a live particle the data moved; false when the hook killed it.
bool OnUpdate(ParticleSource& s, Particle& p, const Vector3& spaceOrigin);
// Whether the source's script has the hook (the callers skip the space's origin otherwise).
bool HasSpawn(const ParticleSource& s);
bool HasUpdate(const ParticleSource& s);

// The particle calls (on_spawn, on_update) of every source between two renewals: past them a tick's particles go
// without their hooks (logged once per script).
inline constexpr int kParticleBudget = 2000;
// The budget renewed: once per tick (ParticleManager::TickSources; the effect viewer's own tick).
void NewTick();
// The particle calls left in this tick (the checks).
int BudgetLeft();

// Every script forgotten and the state closed (a switch of mods: effectdata::Registry::Reset); the sources still running
// lose their hooks.
void Reset();
// `text` stands for the script file `file` until Reset, as Registry::Put for a record (ei-convert's checks): read in
// place of the layers' file at the script's first use.
void PutScript(const std::string& file, std::string text);
// The methods `e` and `p` have in the state ("particle:kill", "source:set", sorted): ei-convert --lua-check holds them
// to the declarations (particle_lua_api.inc).
std::vector<std::string> MethodNames();

} // namespace effectlua
