// The interface canvases' glyph cases (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 11) on
// the native renderer: the game's renderer starts itself on OpenGL at the first canvas. Then the window's present
// after resizes: a frame of known rows presented on a window of its own, read back from the window. Exit code 0 when
// all pass.
#include "platform/graphics.h"
#include "render/glyph_cases.h"
#include "sokol_gfx.h"

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

// One frame on the window at `width` x `height`: red, its top row green, its bottom row blue. The scissor rectangle is
// left on the bottom row, as a pass may leave it.
void PresentRows(SDL_Window* window, int width, int height) {
    sg_swapchain drawable{};
    if (!platform::graphics::BeginDrawable(window, width, height, platform::graphics::PresentMode{false}, &drawable))
        return;
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = sg_color{1.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain = drawable;
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();
    // Rows from the top, as every target counts them (glClipControl's upper left).
    const GLfloat green[4] = {0, 1, 0, 1}, blue[4] = {0, 0, 1, 1};
    glScissor(0, 0, width, 1);
    glClearNamedFramebufferfv(drawable.gl.framebuffer, GL_COLOR, 0, green);
    glScissor(0, height - 1, width, 1);
    glClearNamedFramebufferfv(drawable.gl.framebuffer, GL_COLOR, 0, blue);
    platform::graphics::EndDrawable();
}

char ColourAt(XImage* image, int x, int y) {
    const unsigned long pixel = XGetPixel(image, x, y);
    const unsigned r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
    if (r > 200 && g < 50 && b < 50) return 'r';
    if (r < 50 && g > 200 && b < 50) return 'g';
    if (r < 50 && g < 50 && b > 200) return 'b';
    return '?';
}

// The window's framebuffer once took the window's size only at its first frame, so after a resize the frame landed
// shifted, its bottom cut off, older frames above it. Each size: two frames presented, the window read back from the
// X server (what it shows), its corners and middle column checked (top row green, bottom row blue, red between).
void PresentChecks() {
    SDL_Window* window = SDL_CreateWindow("ei-present-check", 320, 200, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        Check(false, std::string("present: a window (") + SDL_GetError() + ")");
        return;
    }
    SDL_ShowWindow(window);
    const int sizes[][2] = {{320, 200}, {480, 300}, {256, 160}, {400, 400}};
    for (const auto& size : sizes) {
        SDL_SetWindowSize(window, size[0], size[1]);
        SDL_SyncWindow(window);
        int width = 0, height = 0;
        for (int frame = 0; frame < 2; ++frame) {
            SDL_PumpEvents();
            SDL_GetWindowSizeInPixels(window, &width, &height);
            PresentRows(window, width, height);
        }
        glFinish();
        // What the X server shows in the window (24-bit visuals: 0x00RRGGBB pixels).
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        auto* display = static_cast<Display*>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
                                                                     nullptr));
        const auto xwindow = static_cast<Window>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (!display || !xwindow) {
            Check(false, "present: not an X11 window");
            break;
        }
        XSync(display, False);
        XImage* image = XGetImage(display, xwindow, 0, 0, width, height, AllPlanes, ZPixmap);
        std::string got;
        for (int y : {0, 1, height / 2, height - 2, height - 1})
            for (int x : {0, width / 2, width - 1}) got += image ? ColourAt(image, x, y) : '-';
        if (image) XDestroyImage(image);
        char what[160];
        std::snprintf(what, sizeof(what), "present at %dx%d (asked %dx%d): rows top to bottom %s", width, height,
                      size[0], size[1], got.c_str());
        Check(got == "gggrrrrrrrrrbbb", what);
    }
    SDL_DestroyWindow(window);
}

} // namespace

int main() {
    glyph_cases::Run(Check);
    sg_commit();
    PresentChecks();
    std::printf("render_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
