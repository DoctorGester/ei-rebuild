// The interface canvases' glyph cases (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 12) on
// the native renderer on Vulkan (render/gpu.h; lavapipe without a GPU). Then the window's present after resizes: a
// frame of known rows presented on a window of its own, read back from the window. Exit code 0 when all pass.
#include "render/glyph_cases.h"
#include "render/gpu.h"

#include "shaders/gpu_shaders.h"

#include <SDL3/SDL.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace gpu = render::gpu;

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

// A rectangle of one colour on a target of `width` x `height` (the solid program; rows from the top).
void Solid(const Rect& r, int width, int height, float red, float green, float blue) {
    gpu_shaders::SolidParams p{};
    const float rect[4] = {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                           static_cast<float>(r.bottom)};
    std::memcpy(p.rect, rect, sizeof(rect));
    p.target[0] = static_cast<float>(width);
    p.target[1] = static_cast<float>(height);
    p.colour[0] = red;
    p.colour[1] = green;
    p.colour[2] = blue;
    p.colour[3] = 1.0f;
    gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::solid::kBlockSolidParams, p);
    gpu::Draw(0, 6);
}

// One frame on the window: red, its top row green, its bottom row blue. The scissor rectangle is left on the bottom
// row, as a pass may leave it. False when the swap chain gave no texture.
bool PresentRows(gpu::Pipeline solid) {
    gpu::Texture target;
    if (!gpu::AcquireSwapchain(&target) || !target) {
        gpu::Submit();
        return false;
    }
    const int width = target.width, height = target.height;
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = gpu::Load::Clear;
    pass.colour.clear[0] = 1.0f;
    pass.colour.clear[3] = 1.0f;
    if (gpu::BeginPass(pass)) {
        gpu::ApplyPipeline(solid);
        gpu::SetScissor({0, 0, width, 1});
        Solid({0, 0, width, 1}, width, height, 0, 1, 0);
        gpu::SetScissor({0, height - 1, width, height});
        Solid({0, height - 1, width, height}, width, height, 0, 0, 1);
        gpu::EndPass();
    }
    return gpu::Submit();
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
    SDL_Window* window = SDL_CreateWindow("ei-present-check", 320, 200, SDL_WINDOW_RESIZABLE);
    if (!window) {
        Check(false, std::string("present: a window (") + SDL_GetError() + ")");
        return;
    }
    SDL_ShowWindow(window);
    if (!gpu::ClaimWindow(window)) {
        Check(false, std::string("present: the window claimed (") + gpu::Error() + ")");
        SDL_DestroyWindow(window);
        return;
    }
    gpu::SetPresentMode(gpu::PresentMode::Vsync);
    gpu::PipelineDesc desc;
    desc.program = gpu_shaders::solid::kIndex;
    desc.colour = gpu::SwapchainFormat();
    desc.depthCompare = gpu::Compare::Always;
    const gpu::Pipeline solid = gpu::MakePipeline(desc, "ei-present-check");
    Check(static_cast<bool>(solid), "present: the solid pipeline");
    const int sizes[][2] = {{320, 200}, {480, 300}, {256, 160}, {400, 400}};
    for (const auto& size : sizes) {
        SDL_SetWindowSize(window, size[0], size[1]);
        SDL_SyncWindow(window);
        int width = 0, height = 0;
        int drawn = 0;
        for (int frame = 0; frame < 2; ++frame) {
            SDL_PumpEvents();
            SDL_GetWindowSizeInPixels(window, &width, &height);
            drawn += PresentRows(solid) ? 1 : 0;
        }
        // The frames finished: the device waited for (a download's wait would do; Stop is too much).
        gpu::Texture probe = gpu::MakeTexture({1, 1, 1, 1, gpu::Format::Rgba8, gpu::kSampled, "ei-present-probe"});
        gpu::Ticket ticket = gpu::Download(probe, 0, {0, 0, 1, 1});
        gpu::Submit();
        gpu::Wait(ticket);
        gpu::Release(ticket);
        gpu::Release(probe);
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
        std::snprintf(what, sizeof(what), "present at %dx%d (asked %dx%d, %d of 2 frames drawn): rows top to bottom %s",
                      width, height, size[0], size[1], drawn, got.c_str());
        Check(got == "gggrrrrrrrrrbbb", what);
    }
    gpu::ReleaseWindow();
    SDL_DestroyWindow(window);
}

} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("render_check: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!gpu::Start()) {
        std::printf("render_check: no device: %s\n", gpu::Error());
        return 1;
    }
    std::printf("render_check: %s, %s\n", gpu::DriverName(), gpu::DeviceName());
    glyph_cases::Run(Check);
    gpu::Submit();
    PresentChecks();
    gpu::Stop();
    SDL_Quit();
    std::printf("render_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
