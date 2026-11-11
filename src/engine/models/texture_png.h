#pragma once

// Textures as PNG (docs/notes/mods.md, "Textures as PNG"): an .mmp file's first level written as a PNG of straight
// 8-bit RGBA, and a PNG made into the .mmp file the game would have read in its place (its format, levels made by a
// box filter, the extra block). The settings that are not pixels (format, channel layout, level count, extra block)
// travel in the PNG's "ei-texture" text chunk (TOML); without it they are the .mmp's the PNG stands for, else a
// new texture's (A8R8G8B8, every level down to 1 x 1). No game state: ei-convert and the unit check use it as is.

#include "engine/models/texture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace texture_png {

// A level's texels as 8-bit R, G, B, A, rows from the top.
struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// An .mmp file in memory: its header, the bytes after it (the extra block, then the levels) and their count.
struct MmpView {
    const MmpHeader* header = nullptr;
    const std::uint8_t* body = nullptr;
    std::size_t bodyBytes = 0;
};

// Whether a PNG can be made of an .mmp file of this header (a format this file reads and writes).
bool Supported(const MmpHeader& header);
// Level `level`'s texels as the texture holds them (A8R8G8B8 and PNT3 premultiplied, the others straight); the level's
// size is the header's >> level (at least 1). A DXT level is decoded on the CPU from the bytes the file has for it
// (a level under 4 x 4 holds part of a block: the rest zero, as the renderer uploads it). False when the format is
// not Supported or the bytes are short.
bool ReadLevel(const MmpView& file, unsigned level, Image* out);
// The PNG of the file's first level (straight alpha: the premultiplied formats divided by their alpha), RGB when every
// texel is opaque, with the "ei-texture" chunk. Empty with `*error` set when not Supported.
std::string ToPng(const MmpView& file, std::string* error);
// The .mmp file made from `png`: the settings from its chunk, else from `below` (the .mmp it stands for; null when
// none), else a new texture's. False with `*error` set when png::ReadRgba cannot decode the PNG or its settings name a
// format this file does not make.
bool FromPng(std::string_view png, const MmpView* below, std::vector<std::uint8_t>* mmp, std::string* error);

// The pieces, for the checks.
// The chunk's name of a format ("565", "1555", "4444", "8888", "PNT3", "DXT1" .. "DXT5"); empty for another.
std::string_view FormatName(MmpFormat format);
// The 8-bit channel of an n-bit value (its bits repeated: the renderer's widening) and back (rounded).
std::uint32_t Widen(std::uint32_t value, int bits);
std::uint32_t Narrow(std::uint32_t value8, int bits);
// Straight alpha to premultiplied and back (`c` at most `a` comes back exactly; above `a` it comes back as `a`).
std::uint8_t Premultiply(std::uint8_t c, std::uint8_t a);
std::uint8_t Unpremultiply(std::uint8_t c, std::uint8_t a);
// The level below `image`: each texel the rounded mean of the 2 x 2 texels above it (clamped at the edges). With
// `cutout` (one bit of alpha) a texel is opaque, of the mean colour of the opaque ones, when any of the four is
// (alpha 128 or more), else transparent.
Image BoxDown(const Image& image, int width, int height, bool cutout);
// The "ei-texture" chunk's text of a PNG (empty when it has none).
std::string SettingsText(std::string_view png);

} // namespace texture_png
