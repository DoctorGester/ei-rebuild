#pragma once

// Never-set bytes start zeroed in the original too (verify build, every harness run; tickets SV7b3,
// SV7b2, SV7d3, SV7d4, docs/notes/standalone-verification.md).
//
// The functions that build a unit's statistics (UnitStats, 0x704 bytes) and vitals (UnitVitals, 0x58)
// do it in stack copies that their resets do not fully clear: UnitStats::Reset 005233E0 leaves the
// layers' spareAbsorb, the slots' share and factor, builtFlags, fixedMaxHitPoints and others,
// UnitVitals::Reset 00523370 leaves builtFlags. What the stack held there (earlier callees' locals,
// heap addresses among them) then goes into the object: the main menu's unmoco2 (GiveDefaults, no
// prototype) gets hit points from its slots' shares (a NaN that followed the heap), and every unit's
// spareAbsorb went into mission.sav (UnitStats::SaveTo). Ours zeroes these copies (a determinism
// fix kept in the standalone); Install() makes the original do the same: each CALL of a reset (or of
// the builder, for the one copy no reset clears) on such a copy goes through a harness function that
// zeroes the block first and then calls the original target.
//
// Sites: GiveDefaults 00513630, ApplyPrototype 00513B60 (no prototype), BuildFromPrototype 005136D0
// (its callers' copies), CUnit::TakePrototype 0052EB70, CMapObject::TakePrototype 0050E9B0, and
// CParty::AddMember 006628D0's stack record (0x784 bytes, whose statistics BuildNpcStats 00513DA0
// fills: the party members' and so the heroes' statistics). Left alone: the resets of blocks that
// live in objects (CUnitServer::ResetController 00546480, the CMapObjectServer lifetime
// 00515610..005157E0); their objects start zeroed instead (below).
//
// Heap blocks whose constructors leave fields (SV7b2, SV7d3, SV7d4): the objects of the two factories
// (CServerSpecificManager::NewObjectImpl 004D6970: CUnitServer's waitFrame, which ResolveBlocker
// reads; CMapObjectManager::CreateObject 00512730: a unit's levels4C8 and motion turn fields) are
// allocated zeroed (each operator new call redirected), and so are the Suspection and Fear modes
// (AddSuspectionMode / AddFearMode allocate them zeroed; the calls of their constructors 004BA610 /
// 005D2850 from ApplyBehaviour 005D6630 and the script zero their fields first): Evaluate 005D1E90
// keeps a Suspection point that compares the same and the lower level, so its leftovers picked the
// brain's mode. Ours does the same (gamecrt::operator_new_zeroed, SuspectionMode / FearMode::
// ClearFields). These families are patched only with EI_NO_HOOKS=1: every site sits in a function ours
// replaces, and a harness patch in a body makes InstallHooks leave the rewrite out.
//
// Also (S17a) CObject3D::Construct 00515BD0 leaves the view position (+0x28), which the draw sorts
// read for an object listed only for its shadow before it was ever in view (a NaN there makes
// UnitOrderLess irreflexive and STLport's unguarded scans run off the list). Ours zeroes it; its one
// caller, CMapObject::Construct's CALL at 0050E2F2, goes through a harness function that zeroes it
// after the original's construction (EI_LEFTOVERS family bit 6).
//
// And (SV7b7) NewParticleSource 004981B0's source is allocated zeroed (its operator new CALL at 004981CD,
// family bit 7, without hooks only): nothing sets the emission point (+0x98) before Attach, and the first
// Tick takes the motion, so the number of emission tries and GlobalRandom draws, from it.
//
// And (SV7b6a) CLightFactory::NewObjectImpl 0068DA40's light is allocated zeroed (its operator new CALL at
// 0068DA7C, family bit 8, without hooks only): the constructor leaves the transform's positions and nothing
// sets a directional light's, but the terrain's sun lighting (ShadeSun 006C36B0) reads the sun's z under water.

namespace ei::verify::leftovers {

// After the harness knows it runs on game.exe's code (not the standalone, which has ours only).
void Install();

} // namespace ei::verify::leftovers
