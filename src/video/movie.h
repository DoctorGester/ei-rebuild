#pragma once

// The movies (docs/notes/sound.md, "Movies"): Bink 1 files (.bik) decoded a frame at a time into a CPU buffer,
// which the renderer takes (render::MovieFrame). The game reads back the frame numbers (the movie ends when the last
// frame was decoded; the loading screen's movie stops at its last frame) and asks whether the next frame is due,
// which the movie's own rate decides on a real-time clock (not the game's: the blocking player's loop does not move
// the harness's virtual clock). Frames are numbered from 1.

#include <cstdint>
#include <string_view>

namespace video {

struct Movie;

// Opens the movie file `path` (UTF-8); null when it cannot be opened or is not a Bink 1 movie. With `sound` its
// sound track plays on the audio output.
Movie* Open(std::string_view path, bool sound);
void Close(Movie* movie);   // null: nothing

int Width(const Movie* movie);
int Height(const Movie* movie);
unsigned FrameCount(const Movie* movie);
// The frame DecodeFrame decodes next.
unsigned NextFrameNumber(const Movie* movie);
// The frame decoded last (0 before the first).
unsigned DecodedFrameNumber(const Movie* movie);

// Hands the sound decoded so far to the audio output as its stream frees a half, then says whether the next frame
// is due. A player calls it on every pass, also while it waits.
bool Pump(Movie* movie);
// Decodes the next frame (it becomes the decoded one).
void DecodeFrame(Movie* movie);
// The decoded frame as R, G, B, X bytes, Width x Height, rows Width * 4 bytes apart: the movie's own buffer, good
// until the next call.
const std::uint8_t* FramePixels(Movie* movie);
// Moves on a frame; after the last, back to the first.
void Advance(Movie* movie);
// The frame decoded next becomes `frame` (at most the last).
void GoToFrame(Movie* movie, unsigned frame);
void SetVolume(Movie* movie, int volume);   // 0..127

} // namespace video
