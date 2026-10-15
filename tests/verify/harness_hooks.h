#pragma once

// How the harness steps into the main loop and the frame step (verify build only; harness.cpp).
//
// In game.exe it redirects the original's CALLs (the server update 00457314, the client segment
// 00457368, the frame's camera update 00457595, the cursor lock 004580D7 for frame captures) and also
// sets the rewritten callers' hook points (ei/main_loop.h). The redirections change WinMain 00456440
// and the frame step 00458010, so the hook installer leaves those two to the original.
//
// With hook points only, the original code is left alone and the rewritten callers run with the
// hooks: always in the standalone image (no original code there, INT3), and in game.exe with
// EI_HARNESS_HOOK_POINTS=1 (to check the hook points against the redirections on the goldens).
namespace ei::verify {

bool HookPointsOnly();

} // namespace ei::verify
