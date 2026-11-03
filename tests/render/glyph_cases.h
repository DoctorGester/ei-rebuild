#pragma once

// The glyph-run cases of the interface canvases (src/render/render_ui) against an independent CPU rule, and the
// reference rules the other canvas checks share (tools/ui_check). Nothing here is platform
// code: the caller has started the renderer's GPU (render/gpu.h) and the canvas layer.

#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glyph_cases {

// How many pixels of `a` and `b` (rows of `width`) differ; `example` gets the first. Sizes that differ add 1000000.
int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example);

// Called with each check's outcome and a description.
using CheckFn = void (*)(bool ok, const std::string& what);
void Run(CheckFn check);

}   // namespace glyph_cases
