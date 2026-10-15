#pragma once

// The shadow's turn toward the light is undone exactly in the original too (verify build, every
// harness run; ticket SV7b4a, docs/notes/standalone-verification.md).
//
// DrawShadow 0051CE60 turns the object toward the light (0051D479: Quaternion::PreMultiply 004018A0
// by the light's turn), builds its world matrix and turns it back (0051D4E0: PreMultiply by the
// conjugate). The round trip is not exact, so each re-rendered shadow moved the orientation by an
// ulp, and how often a shadow is re-rendered followed the frame timing and memory (EI_HEAP_JITTER).
// Ours keeps the orientation and puts it back (map_object_shadow.cpp, kept in the standalone);
// Install() makes the original do the same: the first call goes through a harness function that
// keeps the orientation before the turn, the second through one that puts it back instead of
// multiplying. EI_SHADOW_TURN=0 leaves the original's round trip.

namespace ei::verify::shadow_turn {

// After the harness knows it runs on game.exe's code (not the standalone, which has ours only).
void Install();

} // namespace ei::verify::shadow_turn
