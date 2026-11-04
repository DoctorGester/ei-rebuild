#pragma once

// Where the main loop (app_main.cpp) lets the verify harness (tests/verify) in.
//
// The harness reaches the loop in two ways: it is told which call of the loop is being made (Site), and it may stand
// in for a few of the loop's calls (HarnessHooks). Both are empty in the game itself.

namespace ei::mainloop {

// The call of the loop that is being made, for the harness's platform seam: the loop's message peek and clock read go
// through platform::EventWaits and platform::Milliseconds, which the harness answers (tests/verify/platform/harness_platform.h),
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
// loop calls the hook instead of the function when one is set. A hook ends by calling the function it stands in for,
// except beforePresent, which stands in for none. Null: the function itself.
struct HarnessHooks {
    using WorldUpdate = int (*)(void* world);   // 0 stops the frame
    using CameraUpdate = void (*)(void* camera, float seconds);
    using BeforePresent = void (*)();
    using FrameStep = void (*)(bool draw);
    WorldUpdate serverUpdate = nullptr;         // the server world's tick; the harness's "speed" hold follows it
    WorldUpdate clientSegment = nullptr;        // the client world's tick; the "speed" hold follows it too
    CameraUpdate frameCameraUpdate = nullptr;   // the pass's last camera update for the real time; "freeze" holds it still
    BeforePresent beforePresent = nullptr;      // in the frame step, after the interface is drawn and before the cursor
                                                // and the present: the frame capture reads the finished frame here
    FrameStep frameStep = nullptr;              // a pass's frame step (draw): overlapping paired games hand
                                                // over to each other before it
};

inline HarnessHooks g_harnessHooks;

} // namespace ei::mainloop
