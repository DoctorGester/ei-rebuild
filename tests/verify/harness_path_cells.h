#pragma once

// Path cells with plain words in the original code (verify build only; harness_path_cells.cpp,
// ticket SV7d2).
//
// The original path builders give each cell's speed word and terrain bits (PathCell +4) a stack slot
// they never write: SearchPathTo (005C3200) its caller's saved register, BuildCells (005B9A20) the
// row offset the search left, and the smoother (PathSmoother::Smooth 005C46C0) the open cost of the
// local search's meeting cell or the planner's list vtable for the rebuilt tail, and a saved frame
// pointer or that vtable for the other rebuilt cells. The last step's speed word reaches the saves
// (controller.motion.order.stepN.flags, UnitMotion's step speeds). The rewrites give every cell the
// plain words instead (kPlainStepSpeed, ai_map.h); the harness redirects the original's calls of the
// three builders through wrappers that set the same words on the cells afterwards, so goldens of the
// original and runs of ours agree.

namespace ei::verify::path_cells {

// Redirects the original's calls of the three builders (once, at harness start; not in the
// standalone image, which has no original code).
void Install();

} // namespace ei::verify::path_cells
