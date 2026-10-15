#pragma once

// What the original's saves carried from memory layout, canonical in the original too (verify build,
// runs with EI_NO_HOOKS=1; ticket SV7b2a, docs/notes/standalone-verification.md). Ours writes the
// same canonical values (kept in the standalone):
//
// - An animation step's padding byte (AnimationStep +3) is 0. The step constructor 00550A60 and the
//   builders' inline stack steps set bytes 0..2 only, so each step carried a stack byte into the
//   unit's saved step lists: AddStep 00544CE0, AddEmote 00544D50, BuildOrderSteps 00550100,
//   BuildDeathAnimation 00550A80, BuildIdle 00550D90 (two), IdleAnimation 00550F30 (two),
//   BuildMoveSteps 005511A0 (two), BuildTransition 005516D0. (00544B70 builds one too; it has no
//   caller.)
// - A script area part (ScriptAreaPart, 0x14 bytes) has zero padding after isRect, and a round part
//   (AddRound 004B2070) y2 0: both were stack bytes AddRect 004B1ED0 / AddRound copied into the area.
// - A sound effect's name (CEffectSound +0x14C, char[0x100], saved whole) is zero past its
//   terminator: Reset 00676200 clears all of it, not only name_[0] (Construct resets too).
// - The item stock's modifier codes (ItemStock::SaveLists 0069BAC0) end after their four bytes: the
//   stack copy's fifth byte is 0 (it was never set; the sixth was).
// - The chunk archive (scenario.sav's party manager) keys its objects by number of first save
//   (1, 2, ...) instead of address: SaveObjectRef 0040A440's and Close 0040A000's AppendChild calls
//   and Close's index write go through harness functions that write the number. Close already writes
//   the records in first-save order (its saved list); the loader maps any key to the object it
//   creates, so the format is the same.
//
// The stores are patched in place, or replaced by a JMP to a stub that does them as the wider store
// (and the replaced instructions) and jumps back; the archive's three CALLs are redirected. Every
// site sits in a function ours replaces, so (as harness_leftovers.cpp's heap families) only runs
// without hooks patch them: a harness patch in a body makes InstallHooks leave the rewrite out.
// `EI_SAVE_LEFTOVERS=0` leaves the original as it is.

namespace ei::verify::save_leftovers {

void Install();

} // namespace ei::verify::save_leftovers
