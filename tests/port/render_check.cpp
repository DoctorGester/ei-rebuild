// The interface canvases' glyph cases (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 11) on
// the native renderer: the game's renderer starts itself on Metal at the first canvas. Exit code 0 when all pass.
#include "render/glyph_cases.h"
#include "sokol_gfx.h"
#include "standalone/static_init.h"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

} // namespace

int main() {
    ei::static_init::RunStaticInitialisers();
    glyph_cases::Run(Check);
    sg_commit();
    std::printf("render_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
