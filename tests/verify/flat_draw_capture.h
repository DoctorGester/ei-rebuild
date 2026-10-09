#pragma once

// Recording the device's flat geometry draw (004EA4A0, verify_device_shadow.cpp): a wrapper that
// runs an original whose only output is that draw sets `record` around the call; the draw's own
// wrapper then copies its arguments there and, with `skipDraw`, returns 0 without drawing. Works
// only while 004EA4A0's wrapper is installed (it is not in verified.txt); otherwise nothing is
// recorded (`seen` stays false).

#include <cstdint>

namespace flatcapture {

struct Call {
    bool seen;
    int blocks;
    int indexCount;
    std::uint32_t colour;
    std::uint32_t specular;
    const void* bounds;
    float zScale;
    float positions[4][16];       // up to four blocks of four vertices (x[4], y[4], z[4], w[4])
    std::uint32_t uvs[16][2];     // one (u, v) per vertex
    std::uint16_t indices[64];
};

struct Recording {
    Call* record = nullptr;
    bool skipDraw = false;
};
Recording& Current();

} // namespace flatcapture
