#pragma once

// Frame capture for the harness's "capture <name>" step (verify build only; harness_capture.cpp).
// The image comes from the game's own render target (the device object's +0x18, the back buffer in
// full screen or the off-screen target in a window), read at the present point of the frame
// function 00458010 after the scene and the interface are drawn and before the software cursor is
// blitted and the frame flipped. See docs/notes/renderer.md ("Frame capture").

namespace ei::verify::capture {

// Patches the present point (once). False when the game's code there is not what we expect.
bool Install();

// Asks for the frame presented after `skip` more frames to be written to
// <output folder>\frames\<name>.bmp (24-bit, bottom-up, lossless).
void Request(const char* name, int skip);

enum class State { Idle, Pending, Done, Failed };
// The state of the last request; `detail` gets the file written or the reason it failed.
State Poll(const char** detail);

// Frames presented through the hooked point so far.
unsigned FramesPresented();

} // namespace ei::verify::capture
