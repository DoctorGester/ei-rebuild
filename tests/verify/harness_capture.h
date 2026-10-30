#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Frame capture for the harness's "capture <name>" step (verify build only; harness_capture.cpp).
// The image is the new renderer's presented frame, read at the frame step's cursor lock. See
// docs/notes/renderer.md ("Frame capture").

namespace ei::verify::capture {

// Takes the frame step's cursor lock hook point (ei/main_loop.h). True.
bool Install();

// Asks for the frame presented after `skip` more frames to be written to
// <output folder>\frames\<name>.bmp (24-bit, bottom-up, lossless).
void Request(const char* name, int skip);

enum class State { Idle, Pending, Done, Failed };
// The state of the last request; `detail` gets the file written or the reason it failed.
State Poll(const char** detail);

// Writes `bgr` (24-bit rows from the top, `width` x `height`) to <output folder>\frames\<name>.bmp as
// the captures are written; `written` gets the file's path.
bool WriteFrame(const char* name, int width, int height, const std::vector<std::uint8_t>& bgr, std::string* written);

// Compares frames\<a>.bmp with frames\<b>.bmp as tools/compare_frames.py does: `differing` gets the
// fraction of pixels with a channel differing by more than `tolerance`, `largest` the largest
// channel difference. False (with `why`) when a file is missing, unreadable or of another size.
bool Compare(const char* a, const char* b, int tolerance, double* differing, int* largest, const char** why);

// The level steps of frames\<name>.bmp, a capture of the terrain's level map (render::TerrainLevels,
// R12): of the horizontally and vertically neighbouring pixel pairs that both show the map (green
// 0x5A, blue 0xA5), the fraction (`steps`) whose red differs by 24 or more (most of a level: a level
// step, where the texture's sharpness jumps), and how many such pairs there are (`pairs`).
bool LevelSteps(const char* name, double* steps, long long* pairs, const char** why);

// The share of frames\<name>.bmp's pixels in each of four hues (A-I23a, tools/make_loose_bmp.py's quadrants as the
// lit terrain shows them): `shares` gets red, green, blue and yellow, a pixel counting when its brightest channel
// is at least 48 and its hue is clearly that one (red: red 1.8 times green and blue; yellow: red and green each
// 1.5 times blue and within a third of each other).
bool HueShares(const char* name, double shares[4], const char** why);

// The same for any .bmp file (the game's own: 16 or 32 bits with channel masks, or 24) against
// frames\<frame>.bmp; `how` gets the file's size and format.
bool CompareFile(const char* path, const char* frame, int tolerance, double* differing, int* largest, std::string* how,
                 const char** why);

// The save's picture (<slot>\shot.sav, 256x192 5:6:5 as the saves write it) against frames\<frame>.bmp,
// both as averages of the picture's 8x8 blocks (32x24; the frame's matching rectangles), counted as
// Compare does. `centre`: against the frame's centred 4:3 part only.
bool CompareSavePicture(const char* path, const char* frame, bool centre, int tolerance, double* differing, int* largest,
                        const char** why);

} // namespace ei::verify::capture
