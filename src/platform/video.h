#pragma once

// Movies (docs/notes/platform.md, "Audio and video"): Bink 1 files (.bik) decoded a frame at a time into a CPU buffer,
// which the renderer takes (render::MovieFrame). The game reads back the frame numbers (the movie ends when the last
// frame was decoded; the loading screen's movie stops at its last frame) and asks whether the next frame is due,
// which the movie's own rate decides on a real-time clock (not the game's: the blocking player's loop does not move
// the harness's virtual clock). Frames are numbered from 1.

#include <cstdint>

namespace platform::video {

struct Movie;

// Opens the movie file `path` (UTF-8); null when it cannot be opened or is not a Bink 1 movie. With `sound` its
// sound track plays on the audio output.
Movie* Open(const char* path, bool sound);
void Close(Movie* movie);   // null: nothing

int Width(const Movie* movie);
int Height(const Movie* movie);
unsigned FrameCount(const Movie* movie);
// The frame DecodeFrame decodes next.
unsigned NextFrameNumber(const Movie* movie);
// The frame decoded last (0 before the first).
unsigned DecodedFrameNumber(const Movie* movie);

// True while the next frame is not due yet.
bool Waiting(Movie* movie);
// Decodes the next frame (it becomes the decoded one).
void DecodeFrame(Movie* movie);
// The decoded frame as R, G, B, X bytes, Width x Height, rows `pitch` bytes apart.
void CopyFrame(const Movie* movie, void* to, int pitch);
// Moves on a frame; after the last, back to the first.
void Advance(Movie* movie);
// The frame decoded next becomes `frame` (at most the last).
void GoToFrame(Movie* movie, unsigned frame);
void SetVolume(Movie* movie, int volume);   // 0..32768

} // namespace platform::video
