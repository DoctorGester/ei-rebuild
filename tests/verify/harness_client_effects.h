#pragma once

// The client effects in the original as in ours (verify build, game.exe's runs only; harness_client_effects.cpp,
// ticket R34a, docs/notes/standalone-verification.md section 5).
//
// Ours draws the client effects (the terrain's animated tiles and ambient sounds, particles, lightning shapes,
// sound picks, decals, the interface's flourishes; since R34c also the client's idle animation picks and the
// previews' start frames) from a generator of their own, g_clientRandom (random.h), seeded from the game's
// generator when CWorld::Create seeds that, and its terrain sectors keep their edge fade from Build on (no
// relighting: no Lit stage, no relight countdowns). The original drew those effects from its ISAAC generator
// 007A9D40 and rand(), and dropped and redid the sectors' lighting on its LightingFreq countdowns, on the frame
// of which a sector's tiles did not animate (a few ISAAC draws fewer). So that the goldens of the original and
// runs of ours agree, the harness makes the original do the same:
//
//   - InstallGenerator (deterministic runs, with the fixed seed): a client generator in the harness; in each of the
//     original's client effect functions (kIsaacFunctions) every operand that addresses 007A9D40..007AA553
//     addresses the same field of it instead (the inlined draws, and the generator handed to Next / Range /
//     Refill), and every CALL of rand() 006E77F7 in kRandFunctions calls ClientRand (a draw of it & 0x7FFF, as
//     ours). StartIdleAnimation's CALL of CUnit::FindAnimation sets a flag around it under which the four rand()
//     CALLs of RandomAnimation / RandomAnimationOfKind draw from the client generator (the server's picks keep
//     rand()), and DelayedProtectionCheck 00455B50 returns at once (ours has none). CWorld::Create's CALL of
//     IsaacRandom::Init (0055F165) also seeds the client generator from the game's memory words through the
//     original's Init (FixedIsaacSeed leaves its seed alone: IsClientGenerator).
//   - Install (every run of the original): SetSun 006C8ED0 keeps the sectors lit (no countdowns, no dropped
//     lighting), and Terrain::SectorAt's calls of SectorData::Ensure 006C3DD0 light a sector as soon as it is
//     built, as ours writes the fade at Build (the digest's water group reads the specular words).

namespace ei::verify::client_effects {

// The terrain's lighting kept from Build on (once, at harness start; nothing in the standalone image).
void Install();
// The client generator and the original's client effect sites patched (once, deterministic runs; nothing in the
// standalone image).
void InstallGenerator();
// Whether `isaac` is the harness's client generator (the original's Seed, FixedIsaacSeed, leaves its seed alone).
bool IsClientGenerator(const void* isaac);

} // namespace ei::verify::client_effects
