#pragma once

// Stack copies of unit statistics start zeroed in the original too (verify build, every harness run;
// ticket SV7b3, docs/notes/standalone-verification.md).
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
// 00515610..005157E0).
//
// Also (S17a) CObject3D::Construct 00515BD0 leaves the view position (+0x28), which the draw sorts
// read for an object listed only for its shadow before it was ever in view (a NaN there makes
// UnitOrderLess irreflexive and STLport's unguarded scans run off the list). Ours zeroes it; its one
// caller, CMapObject::Construct's CALL at 0050E2F2, goes through a harness function that zeroes it
// after the original's construction (EI_LEFTOVERS bit 10).

namespace ei::verify::leftovers {

// After the harness knows it runs on game.exe's code (not the standalone, which has ours only).
void Install();

} // namespace ei::verify::leftovers
