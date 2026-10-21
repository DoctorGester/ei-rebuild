#pragma once

// The interface's two paths compared (verify build only; ticket N3, docs/notes/ui-native.md section 3,
// "Verification"). Under "render ...,uiboth" every surface with a canvas (render_ui.h) is drawn twice, by GDI
// into its memory and on its canvas; after each frame the canvases whose commands or memory changed since
// their last check are read back (render::ui::ReadPixels) and compared with the memory, pixel by pixel:
// exactly (or within `tolerance`) outside the glyph runs' rectangles, within `text` inside them (our own
// fonts draw text a little differently). A surface over its limit is logged with its name (the window's
// vtable and the surface's index, "W00740F28#2", or the interface's own: backdrop, backdrop2, fps, status,
// console), its pair of pictures written to frames\uidiff<n>_<name>_gdi.bmp / _canvas.bmp, and the run fails
// unless a "uidiffcheck" step expected it. Skipped in a run of the original code.
//
// Steps:
//   uipath cpu|native         under uiboth, which of the two the blits show (render::ui::SetPath), from the
//                             next blit: capture the same frozen moment both ways and compare the frames where
//                             the canvases were blitted ("uimask")
//   uimask <name>             frames\<name>.uimask.bmp: white where the frame drawn last blitted canvases
//                             (python tools\compare_frames.py A B --mask-file <name>.uimask.bmp)
//   uidiff [tolerance <n>] [text <n>] [block <n>] [limit <f>] [textlimit <f>] [dumps <n>]
//                             the comparison's settings: a pixel outside the glyph runs differs when a channel
//                             differs by more than `tolerance` (default 0), inside them when the `block` x
//                             `block` averages (default 1) differ by more than `text` (default 24); a surface
//                             differs when more than `limit` (default 0) of the first or `textlimit` (default
//                             0.01) of the second differ; at most `dumps` (default 8) pairs written
//   uidiffcheck <least> [most]
//                             require the surface checks that differed since the last uidiffcheck to number
//                             <least>..<most> (default: exactly <least>); they no longer fail the run
//   uimirror on|off           (before P1 there is nothing native: the harness's own proof) every named surface
//                             gets a canvas fed from its memory (render::ui::Image) whenever the memory changes
//   uifault                   a 4x4 magenta square drawn on one mirrored canvas: the next check must find it

#include <cstdio>
#include <string>

namespace ei::verify::uicompare {

// At the harness's start: the window surfaces' owners (g_uiSurfaceOwnerObserver, chained).
void Install();
// After each frame: the mirror, then the comparison.
void Frame();
// A step of the list above; false when `verb` is none of them. `error` gets a failure.
bool Step(const std::string& verb, const std::string& argument, std::string* error);
// The report's section (nothing when no surface was compared), and the failure: checks that differed
// and no "uidiffcheck" expected (empty when none).
void Report(FILE* f);
std::string Failure();

} // namespace ei::verify::uicompare
