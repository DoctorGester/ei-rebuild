#pragma once

// Where the main loop (app_main.cpp) lets the verify harness (tests/verify) in.
//
// The harness reaches the loop in two ways: it is told which call of the loop is being made (Site), and it may stand
// in for a few of the loop's calls (HarnessHooks). Both are empty in the game itself.

#include <functional>

class Camera;
class WorldClient;
class WorldServer;

namespace ei::mainloop {

// The call of the loop that is being made, for the harness's platform seam: the loop's message peek and clock read go
// through platform::EventWaits and platform::FrameClock, which the harness answers (tests/verify/platform/harness_platform.h),
// and it must tell the loop's calls from any other caller of them (the movie player's, a window switch's wait).
enum class Site {
    None,
    PeekBeforeFrame,   // the peek for a waiting message before a pass: posted input and scenario steps are taken here
    PeekAfterFrame,    // the peek after a pass: a frame was drawn, so the harness counts it and runs the steps
    FrameTime,         // the clock read that measures how long the pass lasted: the virtual clock advances here
};

inline Site g_site = Site::None;

// Names `site` until the end of the scope.
struct InSite {
    explicit InSite(Site site) { g_site = site; }
    ~InSite() { g_site = Site::None; }
    InSite(const InSite&) = delete;
    InSite& operator=(const InSite&) = delete;
};

// Calls of the loop and the frame step that the harness replaces (tests/verify/harness.cpp, harness_capture.cpp). The
// loop calls the hook instead of the function when one is set (CallOr). A hook ends by calling the function it stands
// in for, except beforePresent, which stands in for none. Null: the function itself.
struct HarnessHooks {
    // The server world's tick; the harness's "speed" hold follows it.
    void (*serverUpdate)(WorldServer* world) = nullptr;
    // The client world's tick (false stops the frame); the "speed" hold follows it too.
    bool (*clientSegment)(WorldClient* world) = nullptr;
    // The pass's last camera update for the real time; "freeze" holds it still.
    void (*frameCameraUpdate)(Camera* camera, float seconds) = nullptr;
    // In the frame step, after the interface is drawn and before the cursor and the present: the frame capture reads
    // the finished frame here.
    void (*beforePresent)() = nullptr;
    // A pass's frame step: overlapping paired games hand over to each other before it.
    void (*frameStep)(bool draw) = nullptr;
};

inline HarnessHooks g_harnessHooks;

// `hook` when the harness set it, else `call` (a function, or a member function with its object first) with the same
// arguments.
template <class Hook, class Call, class... Args>
decltype(auto) CallOr(Hook hook, Call call, Args... args) {
    if (hook) return hook(args...);
    return std::invoke(call, args...);
}

} // namespace ei::mainloop
