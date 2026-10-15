#pragma once

// Script groups ordered by object id in harness runs (verify build, virtual clock; ticket SV7b1,
// docs/notes/standalone-verification.md "SV7b1").
//
// A script group (ScriptGroup, script.h) keeps its members sorted, and the game sorts them by object
// address: Sort 004B2D60 and Load 004B3360 through the comparator 004B2D80, the binary search of
// Insert 004B2EF0, the merges of Union 004B2620, Difference 004B2920 and Intersection 004B2B60. Which
// unit a script's "first / n-th of group" gets therefore follows the heap layout (SV7a: gobject.i
// 702 or 1884 under EI_HEAP_JITTER). In harness runs both sides order by object id instead (null
// first, then CEngineObject::id_), the way the update countdown already uses the id (harness_heap.h):
//
//   - ours: ScriptGroup::OrderById (the standalone orders by id always);
//   - the original: the comparator's start jumps to an id comparison, and each of the four inline
//     address comparisons (`CMP a,b / JNZ / JBE`, with the instruction before it where five bytes
//     need it) jumps to a stub of ours that compares the ids and jumps back to the original's
//     equal / above / below paths.
//
// The original's sites are patched after InstallHooks (the first message loop pass), not before:
// NoteHarnessPatch would leave our rewrites of the five functions out, and a patch the hook DLL
// did not note makes it skip them as Rebirth-patched. So ours stay installed, their verify wrappers
// compare with the patched original (the trampolines run the original's body in place), and
// --no-hooks runs patch the same bytes at the same point (no group exists before a world loads).
// Two different members with one id would be a tie the id order cannot settle; the stub logs the
// first it meets (none can occur: world ids count up and are not reused while an object lives).

namespace ei::verify::script_order {

// Before the game starts: ours orders by id, and PatchOriginal will patch the original.
void Install();
// On the main thread once hooks are installed (the first message loop pass): the original's sites.
void PatchOriginal();

} // namespace ei::verify::script_order
