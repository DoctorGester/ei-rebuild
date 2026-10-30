// Exercise the same GPU glyph oracle as the Windows canvas checker on Metal.
#include "render/render_internal.h"
#include "render/render_text.h"
#include "render/render_ui.h"
#include "standalone/static_init.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using render::Rect;
namespace text = render::text;
namespace ui = render::ui;
namespace {
bool g_verbose = true;
#include "../ui_canvas_glyphs.h"
}
int main() {
    ei::static_init::RunStaticInitialisers();
    CheckGlyphs();
    std::printf("Native glyph checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
