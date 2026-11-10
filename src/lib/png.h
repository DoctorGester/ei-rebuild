#pragma once

// PNG files written and read (docs/notes/mods.md, "Textures as PNG", "Maps as folders"). The writer: grey, grey and
// alpha, RGB or RGBA of 8 or 16 bits a sample, each row with the filter whose bytes sum least as signed values (the
// usual choice), deflated by miniz in segments that read on their own (below). The reader: any PNG, on Wuffs
// (third_party/wuffs), a segmented one's segments on several threads.
//
// Segments: the rows are cut into runs of about 256 KB, each deflated after a full flush (no match reaches back into
// the run before it) and with a first row filtered without the row above it (none or sub), and the chunk "eiSG" says
// where each run starts in the zlib stream. Any reader reads the file as a plain PNG; ours inflates and unfilters the
// runs at once and checks the stream's Adler-32 over them, else reads it as a plain one. The chunk is private and
// unsafe to copy: a tool that changes the pixels drops it.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace png {

// The PNG of `width` x `height` pixels of `channels` samples (1 grey, 2 grey and alpha, 3 RGB, 4 RGBA) of `bits` (8
// or 16) each: `samples` row by row from the top, a pixel's channels together, each below 2^bits (the high bits of a
// larger one are dropped). Empty when the size, the kind or the sample count does not fit.
std::string Write(int width, int height, int channels, int bits, std::span<const std::uint16_t> samples);
// The same of 8-bit samples.
std::string Write(int width, int height, int channels, std::span<const std::uint8_t> samples);

// Any PNG (every colour type and depth, interlaced or not) as 8-bit RGBA with straight alpha, rows from the top: a
// 16-bit sample's high byte, a palette's or tRNS's alpha, no gamma or colour profile applied. False with `*error`
// set when it does not decode.
bool ReadRgba(std::string_view png, int* width, int* height, std::vector<std::uint8_t>* rgba, std::string* error);
// The same as 16-bit RGBA (native order): an 8-bit sample v is v * 257, a 16-bit one comes whole.
bool ReadRgba16(std::string_view png, int* width, int* height, std::vector<std::uint16_t>* rgba, std::string* error);

}   // namespace png
