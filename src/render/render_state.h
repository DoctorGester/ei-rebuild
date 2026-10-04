#pragma once

// The Direct3D 7 state the game's draw code sets, fed to the new renderer by the code itself: every
// rewritten place that calls SetRenderState / SetTextureStageState / ApplyStateBlock on the old
// device also calls the matching function here with the same values (docs/notes/renderer.md,
// "Pass state"). The new renderer builds each pass's state from this mirror; a state nothing has
// fed yet is still read back from the old device, and those reads are logged so the remaining
// writers can be found and converted. Cheap (a store); safe to call with the new renderer off.

#include <cstdint>

namespace render::state {

// D3DRENDERSTATETYPE `state` set to `value`.
void Render(std::uint32_t state, std::uint32_t value);
// D3DTEXTURESTAGESTATETYPE `state` of `stage` set to `value`.
void Stage(std::uint32_t stage, std::uint32_t state, std::uint32_t value);
// One of the device's recorded state blocks applied (the handle value, as passed to
// ApplyStateBlock). The four the device records at its creation are known; any other handle
// makes the stage states it may touch unknown again.
void Block(std::uint32_t handle);
// The old device was created again: everything it had is unknown.
void Reset();

} // namespace render::state
