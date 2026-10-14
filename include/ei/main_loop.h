#pragma once

// The calls of the main loop that tools tell apart by where they come from.
//
// The verify harness (tests/verify/harness.cpp) recognises three calls of the original WinMain
// (00456440) by their return addresses: the two PeekMessageA calls that take posted input (before
// and after a frame) and the timeGetTime call that measures a frame. The rewritten WinMain
// (src/game/app_main.cpp) makes the same calls from other addresses, so it names the call it is
// making here for the duration of the call.

namespace ei::mainloop {

enum class Site {
    None,
    PeekBeforeFrame,   // the original's return address 00457119
    PeekAfterFrame,    // 00457610
    FrameTime,         // 004571E2
};

inline Site g_site = Site::None;

// Names `site` until the end of the scope.
struct InSite {
    explicit InSite(Site site) { g_site = site; }
    ~InSite() { g_site = Site::None; }
    InSite(const InSite&) = delete;
    InSite& operator=(const InSite&) = delete;
};

} // namespace ei::mainloop
