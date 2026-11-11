#pragma once

// What the particle files (particle_*.cpp) share: each effect's functions, which the source's set-up
// (particle_create.cpp) and tick (particle_manager.cpp) pick by its type, and their constants. The ball and cube
// draws are particle_record.h's (shared with the data effects). More: docs/notes/effects.md, "Particle sources".
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

// Bag: its balls (the elements, and the first particles).
constexpr int kBagBalls = 80;

// particle_casting.cpp.
int EmitCasting(ParticleSource& s, Particle& p, int index);
int UpdateCasting(ParticleSource& s, Particle& p);
void TickCasting(ParticleSource& s);
int EmitCastRing(ParticleSource& s, Particle& p, int index);
int UpdateCastRing(ParticleSource& s, Particle& p);
void TickCastRing(ParticleSource& s, ParticleElement& e);
int EmitHealing(ParticleSource& s, Particle& p, int index);
int UpdateHealing(ParticleSource& s, Particle& p);
void TickHealing(ParticleSource& s, ParticleElement& e);
int EmitCastSwirl(ParticleSource& s, Particle& p, int index);
int UpdateCastSwirl(ParticleSource& s, Particle& p);
void TickCastSwirl(ParticleSource& s, ParticleElement& e);
void SetupCylinder(ParticleSource& s);
int EmitCylinder(ParticleSource& s, Particle& p, int index);
int UpdateCylinder(ParticleSource& s, Particle& p);
void TickCylinder(ParticleSource& s);
int EmitModifier(ParticleSource& s, Particle& p, int index);
int UpdateModifier(ParticleSource& s, Particle& p);
void TickModifier(ParticleSource& s, ParticleElement& e);
int EmitLink(ParticleSource& s, Particle& p, int index);
int UpdateLink(ParticleSource& s, Particle& p);
void TickLink(ParticleSource& s, ParticleElement& e);

// particle_marks.cpp.
int EmitSilence(ParticleSource& s, Particle& p, int index);
int UpdateSilence(ParticleSource& s, Particle& p);
int EmitBag(ParticleSource& s, Particle& p, int index);
int UpdateBag(ParticleSource& s, Particle& p);
void TickBag(ParticleSource& s, ParticleElement& e);

// particle_blasts.cpp.
void SetupFireBlast(ParticleSource& s);
int EmitBlast(ParticleSource& s, Particle& p, int index);
int UpdateBlast(ParticleSource& s, Particle& p);
void TickBlast(ParticleSource& s, ParticleElement& e);
int EmitLightning(ParticleSource& s, Particle& p, int index);
int UpdateLightning(ParticleSource& s, Particle& p);
void TickLightning(ParticleSource& s, ParticleElement& e);
int EmitMushroom(ParticleSource& s, Particle& p, int index);
int UpdateMushroom(ParticleSource& s, Particle& p);
void TickMushroom(ParticleSource& s, ParticleElement& e);
void SetupClayRing(ParticleSource& s);
int EmitClayRing(ParticleSource& s, Particle& p, int index);
int UpdateClayRing(ParticleSource& s, Particle& p);
void TickClayRing(ParticleSource& s, ParticleElement& e);
int EmitPoisonFog(ParticleSource& s, Particle& p, int index);
int UpdateCloud(ParticleSource& s, Particle& p);

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
void SetupGroundDust(ParticleSource& s);
int EmitGroundDust(ParticleSource& s, Particle& p, int index);
int UpdateGroundDust(ParticleSource& s, Particle& p);
void TickGroundPlane(ParticleSource& s, ParticleElement& e);
