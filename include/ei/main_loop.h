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

// Calls of the main loop and the frame step that a test harness steps into (tests/verify/harness.cpp,
// harness_capture.cpp). In game.exe the harness redirects the original's CALLs at these points; the
// rewritten callers (src/game/app_main.cpp, app_frame.cpp) call the hook instead when one is set, and
// the standalone image, which has no original code to redirect, has only these. Null: the call goes
// where the original's CALL goes. Each hook ends by calling the function it stands in for.
struct HarnessHooks {
    using WorldUpdate = int(__fastcall*)(void* world, void*);
    using CameraUpdate = void(__fastcall*)(void* camera, void*, float seconds);
    using CursorLock = void(__fastcall*)(void* screen);
    WorldUpdate serverUpdate = nullptr;         // CALL 0058DFF0 at 00457314 in WinMain
    WorldUpdate clientSegment = nullptr;        // CALL 005641F0 at 00457368 in WinMain
    CameraUpdate frameCameraUpdate = nullptr;   // CALL 0040FB30 at 00457595 in WinMain (the frame's last)
    CursorLock cursorLock = nullptr;            // CALL 005E65F0 at 004580D7 in the frame step 00458010
};

inline HarnessHooks g_harnessHooks;

} // namespace ei::mainloop
