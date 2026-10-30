#pragma once

namespace render {

// A rectangle of the frame, of a surface or of laid-out text, in pixels.
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

} // namespace render
