#pragma once

// A tap on the device's mesh draws (004E9010 / 004E9A50, verify_device_mesh.cpp) for the checks of
// their callers (verify_link_mesh_draw.cpp): the caller's rewrite draws with `mode` Record, then
// the original caller runs with `mode` Skip, where the draw is only recorded and returns `result`.
// Both leave the arguments (as the dwords on the stack) and a sum of the positions' x, y, z rows.

#include <cstdint>

namespace meshcapture {

enum class Mode { Off, Record, Skip };

struct Draw {
    int calls;
    bool textured;
    std::uint32_t textures[2];
    std::uint32_t arguments[18];
    std::uint32_t positionSum;
};

struct State {
    Mode mode;
    int result;   // what a skipped draw returns
    Draw draw;
};

State& Tap();

} // namespace meshcapture
