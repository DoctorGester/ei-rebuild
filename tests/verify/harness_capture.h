#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Frame capture for the harness's "capture <name>" step (verify build only; harness_capture.cpp).
// The image is the new renderer's presented frame, read at the frame step's cursor lock. See
// docs/notes/renderer.md ("Frame capture").

namespace ei::verify::capture {

// Takes the frame step's cursor lock hook point (game/app/harness_hooks.h). True.
bool Install();

// Asks for the frame presented after `skip` more frames to be written to
// <output folder>\frames\<name>.bmp (24-bit, bottom-up, lossless).
void Request(const char* name, int skip);

// Asks for `count` presented frames, one after another, to be written to frames\<name>_1.bmp .. _<count>.bmp: from the
// next present, or with `start` from the first present at which it says true (a load's first frames); `freeze`: the
// game's clocks frozen (clock::Freeze) once the first is written, so the frames after it differ only by what is drawn
// late.
void RequestBurst(const char* name, int count, bool (*start)(), bool freeze);

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

// As Compare, within the rectangle `left`..`right` x `top`..`bottom` (right and bottom excluded; cut to the frame):
// `differing` is the fraction of the rectangle's pixels.
bool CompareRect(const char* a, const char* b, int tolerance, int left, int top, int right, int bottom,
                 double* differing, int* largest, const char** why);

// The pixels of frames\<smooth>.bmp (the world multisampled) outside the range of their 3 x 3 neighbourhood in
// frames\<plain>.bmp (the same frozen moment drawn without) by more than `tolerance` in a channel, in the rectangle
// `left`..`right` x `top`..`bottom` (right and bottom excluded; cut to the frame less its border pixels): `outside`
// gets their share of the pixels compared, `count` their number. A multisampled pixel mixes surfaces its neighbours
// show at their centres, so it lies within their range. False (with `why`) as Compare, or when the rectangle holds no
// pixel.
bool OutsideNeighbours(const char* plain, const char* smooth, int tolerance, int left, int top, int right, int bottom,
                       double* outside, long long* count, const char** why);

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

// The mean blue, green and red of frames\<name>.bmp's pixels in the rectangle `left`..`right` x `top`..`bottom`
// (right and bottom excluded; cut to the frame) into `bgr`. False (with `why`) when the frame is missing or the
// rectangle holds no pixel of it.
bool MeanColour(const char* name, int left, int top, int right, int bottom, double bgr[3], const char** why);

// Where frames\<with>.bmp and frames\<without>.bmp differ by more than `tolerance` in a channel: the mean blue,
// green and red there of each (`over`, `under`) and the share of the pixels (`share`). False (with `why`) as
// Compare, or when no pixel differs.
bool DrawnOver(const char* with, const char* without, int tolerance, double over[3], double under[3], double* share,
               const char** why);

// The same for any .bmp file (the game's own: 16 or 32 bits with channel masks, or 24) against
// frames\<frame>.bmp; `how` gets the file's size and format.
bool CompareFile(const char* path, const char* frame, int tolerance, double* differing, int* largest, std::string* how,
                 const char** why);

// The save's pictures (<slot>\shot.sav: the 256x192 5:6:5 one and the trailer's, which it must have;
// src/game/saves/save_picture.h) against frames\<frame>.bmp, each as averages of 32x24 matching rectangles,
// counted as Compare does (the worse of the two). `centre`: against the frame's centred 4:3 part only.
bool CompareSavePicture(const char* path, const char* frame, bool centre, int tolerance, double* differing, int* largest,
                        const char** why);
// The shot.sav at `path` cut to its 5:6:5 picture, as the original game writes it; false without one.
bool CutSavePicture(const char* path);

} // namespace ei::verify::capture
