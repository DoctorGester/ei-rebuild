#pragma once

// The sizes a display offers as the game's resolution (platform::DisplaySizes; the options' resolution row). Pure
// functions on sizes, so that the backends (SDL's display modes, the harness's fixed list) and the unit check share one
// rule (tests/unit/display_sizes.cpp).

#include "lib/rect.h"

#include <algorithm>
#include <vector>

namespace platform {

// The smallest size offered: the 4:3 layout is built for 800x600.
inline constexpr Size kSmallestResolution{800, 600};

inline bool SizeLess(Size a, Size b) { return a.w != b.w ? a.w < b.w : a.h < b.h; }

// `modes` (a display's modes, one size many times over its refresh rates) as the sizes to choose from: none under
// kSmallestResolution or above `desktop` in either direction, each once, the narrowest first, equal widths by height.
inline std::vector<Size> ChooseSizes(std::vector<Size> modes, Size desktop) {
    std::erase_if(modes, [&](Size s) {
        return s.w < kSmallestResolution.w || s.h < kSmallestResolution.h || s.w > desktop.w || s.h > desktop.h;
    });
    std::sort(modes.begin(), modes.end(), SizeLess);
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    return modes;
}

// `size` put in its place of the ordered `sizes` when it is not there.
inline void AddSize(std::vector<Size>& sizes, Size size) {
    if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
        sizes.insert(std::upper_bound(sizes.begin(), sizes.end(), size, SizeLess), size);
}

} // namespace platform
