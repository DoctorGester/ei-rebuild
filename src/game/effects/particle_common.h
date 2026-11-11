#pragma once

// What the particle files (particle_*.cpp) share: each effect's functions, which the source's set-up
// (particle_create.cpp) and tick (particle_manager.cpp) pick by its type, the random helpers and the random scales
// that are not a Range draw. More: docs/notes/effects.md, "Particle sources".
//
// An Emit fills a particle from the pool (`index`: the emission's count this tick; 0 returns the particle to the pool),
// an Update moves one (0 drops it), a Tick works on one element of the source's array (taking the source alone: on the
// whole array), a Setup starts the elements the source's spec made.

#include "game/effects/effects.h"
#include "game/core/random.h"

#include "lib/vecmath.h"

#include <bit>
#include <cstdint>

// The bounds a tick starts from: empty (each minimum above its maximum).
inline constexpr float kBoundsFar = 100000.0f;

// Bag: its balls (the elements, and the first particles).
constexpr int kBagBalls = 80;

// A random point of the cube [-1, 1)^3, drawn x, y, z.
inline Vector3 RandomInCube() {
    return {g_particleRandom.Range(-1.0f, 1.0f), g_particleRandom.Range(-1.0f, 1.0f), g_particleRandom.Range(-1.0f, 1.0f)};
}

// A random point in the unit ball (ten tries, the last one kept) and its squared length.
inline Vector3 RandomInBall(float& lengthSquared) {
    Vector3 v;
    for (int tries = 10;;) {
        v = RandomInCube();
        lengthSquared = LengthSquared(v);
        if (lengthSquared < 1.0f) return v;   // random: never NaN
        if (!(--tries > 0)) return v;
    }
}

inline Vector3 RandomInBall() {
    float lengthSquared;
    return RandomInBall(lengthSquared);
}

// particle_casting.cpp.
int EmitCasting(ParticleSource& s, Particle& p, int index);
int UpdateCasting(ParticleSource& s, Particle& p);
void TickCasting(ParticleSource& s);
int EmitOrbit(ParticleSource& s, Particle& p, int index);
int UpdateOrbit(ParticleSource& s, Particle& p);
int EmitCastRing(ParticleSource& s, Particle& p, int index);
int UpdateCastRing(ParticleSource& s, Particle& p);
void TickCastRing(ParticleSource& s, ParticleElement& e);
int EmitHealing(ParticleSource& s, Particle& p, int index);
int UpdateHealing(ParticleSource& s, Particle& p);
void TickHealing(ParticleSource& s, ParticleElement& e);
int EmitCastSwirl(ParticleSource& s, Particle& p, int index);
int UpdateCastSwirl(ParticleSource& s, Particle& p);
void TickCastSwirl(ParticleSource& s, ParticleElement& e);
int EmitColumn(ParticleSource& s, Particle& p, int index);
int UpdateColumn(ParticleSource& s, Particle& p);
void TickColumn(ParticleSource& s, ParticleElement& e);
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
int EmitStench(ParticleSource& s, Particle& p, int index);
int UpdateStench(ParticleSource& s, Particle& p);

// particle_marks.cpp.
int EmitVisionStar(ParticleSource& s, Particle& p, int index);
int UpdateVisionStar(ParticleSource& s, Particle& p);
int EmitRegeneration(ParticleSource& s, Particle& p, int index);
int UpdateRegeneration(ParticleSource& s, Particle& p);
int EmitSilence(ParticleSource& s, Particle& p, int index);
int UpdateSilence(ParticleSource& s, Particle& p);
int EmitFeebleMind(ParticleSource& s, Particle& p, int index);
int UpdateFeebleMind(ParticleSource& s, Particle& p);
void SetupFeetCloud(ParticleSource& s);
int EmitFeetCloud(ParticleSource& s, Particle& p, int index);
int UpdateFeetCloud(ParticleSource& s, Particle& p);
void TickFeetCloud(ParticleSource& s, ParticleElement& e);
int EmitStun(ParticleSource& s, Particle& p, int index);
int UpdateStun(ParticleSource& s, Particle& p);
int EmitCurseStars(ParticleSource& s, Particle& p, int index);
int UpdateCurseStars(ParticleSource& s, Particle& p);
void SetupStartTrans(ParticleSource& s);
int EmitStartTrans(ParticleSource& s, Particle& p, int index);
int UpdateStartTrans(ParticleSource& s, Particle& p);
void TickStartTrans(ParticleSource& s, ParticleElement& e);
int EmitSparks(ParticleSource& s, Particle& p, int index);
int UpdateSparks(ParticleSource& s, Particle& p);
int EmitFireStar(ParticleSource& s, Particle& p, int index);
int UpdateFireStar(ParticleSource& s, Particle& p);
void SetupShower(ParticleSource& s);
int EmitShower(ParticleSource& s, Particle& p, int index);
int UpdateShower(ParticleSource& s, Particle& p);
void TickShower(ParticleSource& s, ParticleElement& e);
void SetupFalling(ParticleSource& s);
int EmitFalling(ParticleSource& s, Particle& p, int index);
int UpdateFalling(ParticleSource& s, Particle& p);
void TickFalling(ParticleSource& s, ParticleElement& e);
int EmitBag(ParticleSource& s, Particle& p, int index);
int UpdateBag(ParticleSource& s, Particle& p);
void TickBag(ParticleSource& s, ParticleElement& e);

// particle_blasts.cpp.
int EmitFireBall(ParticleSource& s, Particle& p, int index);
int UpdateFireBall(ParticleSource& s, Particle& p);
int EmitArrowSmoke(ParticleSource& s, Particle& p, int index);
int UpdateArrowSmoke(ParticleSource& s, Particle& p);
int EmitAcidRay(ParticleSource& s, Particle& p, int index);
int UpdateAcidRay(ParticleSource& s, Particle& p);
void SetupFireBlast(ParticleSource& s);
int EmitBlast(ParticleSource& s, Particle& p, int index);
int UpdateBlast(ParticleSource& s, Particle& p);
void TickBlast(ParticleSource& s, ParticleElement& e);
int EmitLightning(ParticleSource& s, Particle& p, int index);
int UpdateLightning(ParticleSource& s, Particle& p);
void TickLightning(ParticleSource& s, ParticleElement& e);
int EmitNuke(ParticleSource& s, Particle& p, int index);
int UpdateDebris(ParticleSource& s, Particle& p);
void TickNuke(ParticleSource& s, ParticleElement& e);
int EmitMushroom(ParticleSource& s, Particle& p, int index);
int UpdateMushroom(ParticleSource& s, Particle& p);
void TickMushroom(ParticleSource& s, ParticleElement& e);
void SetupClayRing(ParticleSource& s);
int EmitClayRing(ParticleSource& s, Particle& p, int index);
int UpdateClayRing(ParticleSource& s, Particle& p);
void TickClayRing(ParticleSource& s, ParticleElement& e);
int EmitTeleport(ParticleSource& s, Particle& p, int index);
int UpdateTeleport(ParticleSource& s, Particle& p);
void TickTeleport(ParticleSource& s, ParticleElement& e);
int EmitBanshee(ParticleSource& s, Particle& p, int index);
int UpdateBanshee(ParticleSource& s, Particle& p);
void TickBanshee(ParticleSource& s, ParticleElement& e);
int EmitPoisonFog(ParticleSource& s, Particle& p, int index);
int UpdateCloud(ParticleSource& s, Particle& p);

// particle_world.cpp.
int EmitSmoke(ParticleSource& s, Particle& p, int index);
int UpdateSmoke(ParticleSource& s, Particle& p);
int EmitTendril(ParticleSource& s, Particle& p, int index);
void TickTendril(ParticleSource& s);
int EmitVulkan(ParticleSource& s, Particle& p, int index);
int UpdateVulkan(ParticleSource& s, Particle& p);
int EmitGeyser(ParticleSource& s, Particle& p, int index);
int UpdateGeyser(ParticleSource& s, Particle& p);
void TickGeyser(ParticleSource& s, ParticleElement& e);
void SetupTornado(ParticleSource& s);
void SetupCurseHolder(ParticleSource& s);
int EmitTornado(ParticleSource& s, Particle& p, int index);
int UpdateTornado(ParticleSource& s, Particle& p);
void TickTornado(ParticleSource& s);
int EmitPortal(ParticleSource& s, Particle& p, int index);
int UpdatePortal(ParticleSource& s, Particle& p);
void TickPortal(ParticleSource& s);
int EmitPortalStar(ParticleSource& s, Particle& p, int index);
int UpdatePortalStar(ParticleSource& s, Particle& p);
int EmitZoneExit(ParticleSource& s, Particle& p, int index);
int UpdateZoneExit(ParticleSource& s, Particle& p);
int EmitGlow(ParticleSource& s, Particle& p, int index);
int UpdateGlow(ParticleSource& s, Particle& p);
void TickGlow(ParticleSource& s);
void SetupGroundDust(ParticleSource& s);
int EmitGroundDust(ParticleSource& s, Particle& p, int index);
int UpdateGroundDust(ParticleSource& s, Particle& p);
void TickGroundPlane(ParticleSource& s, ParticleElement& e);
