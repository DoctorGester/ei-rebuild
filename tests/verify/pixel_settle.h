// Shared by the verify wrappers that compare surface pixels (tickets X5, X8).
//
// GDI surface pixels can be disturbed from outside (the desktop is shared by parallel game
// processes), so now and then a side-by-side draw differs although ours is right. On a pixel
// difference the wrapper redraws the ORIGINAL from the saved pixels until two redraws in a row
// agree (up to 5 tries, a little longer wait each time) and compares ours with that settled draw.
#pragma once

#include <windows.h>

#include <utility>

namespace ei::verify {

// How many pixel comparisons are running (a draw compared inside another's comparison: the text
// draw 005FDE20 inside the trade lists', the wrapped text's, an edit field's). The inner one draws
// ours only then: the outer compares the result, and drawing twice inside it made the outer's
// original unstable (CTradePanel::DrawLists).
inline int& PixelComparisonDepth() {
    static int depth = 0;
    return depth;
}
struct PixelComparisonScope {
    PixelComparisonScope() { ++PixelComparisonDepth(); }
    ~PixelComparisonScope() { --PixelComparisonDepth(); }
    PixelComparisonScope(const PixelComparisonScope&) = delete;
    PixelComparisonScope& operator=(const PixelComparisonScope&) = delete;
};

struct SettleOutcome {
    bool settled = false; // two redraws in a row agreed
    int tries = 0;
    int drift = -1; // difference count between the last two redraws, -1 if none could be made
};

// `previous` holds the first original draw on entry and the last redraw on exit (the settled draw
// when `settled`). `redraw(Pixels* out)` restores the saved pixels, draws the original and reads the
// result into *out, returning false on failure. `diffCount(a, b)` returns the number of differing
// pixels (0 = equal).
template <class Pixels, class Redraw, class DiffCount>
SettleOutcome SettleOriginal(Pixels& previous, Redraw&& redraw, DiffCount&& diffCount) {
    SettleOutcome out;
    Pixels next;
    while (out.tries < 5 && !out.settled) {
        Sleep(10 * ++out.tries);
        if (!redraw(&next)) break;
        out.drift = diffCount(previous, next);
        out.settled = out.drift == 0;
        std::swap(previous, next);
    }
    return out;
}

} // namespace ei::verify
