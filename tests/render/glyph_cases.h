#pragma once

// The glyph-run cases of the interface canvases (src/render/render_ui) against an independent CPU rule, for every
// surface format, and the reference rules the other canvas checks share (tools/ui_check). Nothing here is platform
// code: the caller has started sokol on its device and the canvas layer.

#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glyph_cases {

struct Format {
    const char* name;
    render::PixelFormat format;
};
extern const Format kFormats[3];   // 5:6:5, 5:5:5, 8:8:8

// The test's own quantising to a surface format (0x00RRGGBB in and out).
std::uint32_t RefQuantise(std::uint32_t rgb, const render::PixelFormat& f);
// Each channel's top bits of the format (the low bits 0), as the game's PixelAt gives them.
std::uint32_t RefTopBits(std::uint32_t rgb, const render::PixelFormat& f);
// How many pixels of `a` and `b` (rows of `width`) differ; `example` gets the first. Sizes that differ add 1000000.
int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example);

// Called with each check's outcome and a description.
using CheckFn = void (*)(bool ok, const std::string& what);
void Run(CheckFn check);

}   // namespace glyph_cases
