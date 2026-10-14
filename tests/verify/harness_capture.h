#pragma once

#include <string>

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

// Compares frames\<a>.bmp with frames\<b>.bmp as tools/compare_frames.py does: `differing` gets the
// fraction of pixels with a channel differing by more than `tolerance`, `largest` the largest
// channel difference. False (with `why`) when a file is missing, unreadable or of another size.
bool Compare(const char* a, const char* b, int tolerance, double* differing, int* largest, const char** why);

// The same for any .bmp file (the game's own: 16 or 32 bits with channel masks, or 24) against
// frames\<frame>.bmp; `how` gets the file's size and format.
bool CompareFile(const char* path, const char* frame, int tolerance, double* differing, int* largest, std::string* how,
                 const char** why);

} // namespace ei::verify::capture
