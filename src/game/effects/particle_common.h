#pragma once

// What the particle files (particle_*.cpp) share: each effect's functions, which the source's set-up
// (particle_create.cpp) and tick (particle_manager.cpp) pick by its type, and their constants. The ball and cube
// draws are particle_expr.h's (shared with the data effects). More: docs/notes/effects.md, "Particle sources".
//
// An Emit fills a particle from the pool (`index`: the emission's count this tick; 0 returns the particle to the pool),
// an Update moves one (0 drops it), a Tick works on one element of the source's array (taking the source alone: on the
// whole array), a Setup starts the elements the source's spec made.

#include "game/effects/effects.h"
#include "game/effects/particle_record.h"
#include "game/core/random.h"

#include "lib/vecmath.h"

#include <bit>
#include <cstdint>

// The bounds a tick starts from: empty (each minimum above its maximum).
inline constexpr float kBoundsFar = 100000.0f;

// particle_blasts.cpp.
void SetupFireBlast(ParticleSource& s);
int EmitBlast(ParticleSource& s, Particle& p, int index);
int UpdateBlast(ParticleSource& s, Particle& p);
void TickBlast(ParticleSource& s, ParticleElement& e);
int EmitLightning(ParticleSource& s, Particle& p, int index);
int UpdateLightning(ParticleSource& s, Particle& p);
void TickLightning(ParticleSource& s, ParticleElement& e);

// particle_world.cpp.
int EmitGeyser(ParticleSource& s, Particle& p, int index);
int UpdateGeyser(ParticleSource& s, Particle& p);
void TickGeyser(ParticleSource& s, ParticleElement& e);
void SetupTornado(ParticleSource& s);
void SetupCurseHolder(ParticleSource& s);
int EmitTornado(ParticleSource& s, Particle& p, int index);
int UpdateTornado(ParticleSource& s, Particle& p);
void TickTornado(ParticleSource& s);
int EmitGlow(ParticleSource& s, Particle& p, int index);
int UpdateGlow(ParticleSource& s, Particle& p);
void TickGlow(ParticleSource& s);
