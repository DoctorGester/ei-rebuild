#pragma once

// The interface's drawing into a frame for the offline checks (tools/ui_check on Direct3D 12, tests/port/render_check
// on Vulkan or Metal): a frame target of their own, drawn by render_ui's batches as render.cpp draws them, and the
// glyph-run cases against an independent CPU rule. Nothing here is platform code: the caller has started the
// renderer's GPU (render/gpu.h).

#include "render/gpu.h"
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glyph_cases {

// A new frame target of `width` x `height`, cleared to black by the frame's first pass.
void NewFrame(int width, int height);
// render.cpp's DrawPending: the copies waiting first, the quads uploaded, then the frame's commands not drawn yet.
void DrawFrame();
// The frame target as 0x00RRGGBB rows after the frame's draws.
bool ReadFrame(std::vector<std::uint32_t>* rgb);
// render.cpp's Present: the frame's draws let go, the commands sent.
void EndFrame();
// The frame target, its size and the commits so far (what render.cpp gives render_ui.cpp).
const render::gpu::Texture& Target();
Size TargetSize();
std::uint32_t Commits();
void ReleaseFrame();

std::uint32_t RgbOfColorref(std::uint32_t c);
// How many pixels of `a` and `b` (rows of `width`) differ by more than `tolerance` in a channel, or by more than
// `blends` gives a pixel when that is more; `example` gets the first. Sizes that differ add 1000000.
int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example,
           int tolerance = 0, const std::vector<std::uint8_t>* blends = nullptr);
std::string Outcome(int differ, const std::string& example);

// Called with each check's outcome and a description.
using CheckFn = void (*)(bool ok, const std::string& what);
// The glyph runs (render_text layouts, underlines, clipping, over fills, the shadow under the text) against each
// quad's coverage blended over what is there in turn, within one level a channel for each blend at the pixel. The
// rule rounds each blend's sum once, as GPUs' float blenders do; Mesa's lavapipe blends 8-bit targets in fixed
// point and rounds the source's and the destination's products apart, so each blend can land one level off (Vulkan
// asks blending only for the target's own precision) and a pixel under both the shadow and the text two (PL21a).
void Run(CheckFn check);

}   // namespace glyph_cases
