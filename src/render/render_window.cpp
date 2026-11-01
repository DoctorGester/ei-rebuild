// The game window's present (render::StartPresenting): the frame is drawn into the window's drawable (a flip-model
// swap chain on Windows, platform/graphics.h), its aspect kept (centred, black bars when the window's client area has
// another shape), through the display's gamma ramp (the options' brightness / contrast / gamma, SetGammaRamp's table)
// as a shader step, and presented with the vertical blank. Also where the frame sits in the window, for the
// game's mouse coordinates (WindowToFrame / FrameToWindow). docs/notes/renderer.md, section 9.
#include "render.h"
#include "render_internal.h"
#include "render_perf.h"

#include "../hook/log.h"

#include "../game/app.h"

#include <cmath>
#include <cstdint>

namespace render {

namespace {

using namespace detail;

platform::NativeWindow GameWindow() { return app::g_mainWindow; }

// Present waits for the vertical blank unless the settings file's "VSync" is 0 (render::SetVsync, set before the first
// present; the chain may tear then) or the harness's virtual clock runs the game (render::SetWaitForVsync), where a
// frame's length does not depend on the display.
platform::graphics::PresentMode g_presentMode;
int g_drawableWidth = 0, g_drawableHeight = 0;   // the last DrawToWindow's

sg_shader g_shader{};
sg_pipeline g_pipeline{};

// The gamma ramp as a 256 x 1 lookup (RGBA8: the red, green and blue ramps' entries scaled to 255),
// identity until the game sets one.
sg_image g_rampImage{};
sg_view g_rampView{};
struct Ramp {
    std::uint8_t entries[256 * 4];
    Ramp() {
        for (int i = 0; i < 256; ++i) {
            entries[i * 4 + 0] = entries[i * 4 + 1] = entries[i * 4 + 2] = static_cast<std::uint8_t>(i);
            entries[i * 4 + 3] = 255;
        }
    }
};
Ramp g_rampTable;   // (kept across devices: the game sets it once, a new device gets it again)
std::uint8_t (&g_ramp)[256 * 4] = g_rampTable.entries;
bool g_rampDirty = true;     // to be uploaded before the next present
bool g_rampIdentity = true;
std::uint32_t g_rampChanges = 0;   // SetGammaRamp's calls (the cursor's shapes are made through the ramp)
long g_rampsSet = 0;          // verify build: ramps set so far (the ramp check is sampled)
bool g_checkRamp = false;     // verify build: compare the next presented frame with the ramp applied
WindowPresentInfo g_info;

bool CreatePipeline() {
    g_shader = sg_make_shader(present_shader_desc(sg_query_backend()));
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_BGRA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.label = "ei-present";
    g_pipeline = sg_make_pipeline(&p);
    if (sg_query_pipeline_state(g_pipeline) != SG_RESOURCESTATE_VALID) return false;

    sg_image_desc image{};
    image.width = 256;
    image.height = 1;
    image.pixel_format = SG_PIXELFORMAT_RGBA8;
    image.usage.dynamic_update = true;
    image.label = "ei-gamma-ramp";
    g_rampImage = sg_make_image(&image);
    sg_view_desc v{};
    v.texture.image = g_rampImage;
    g_rampView = sg_make_view(&v);
    g_rampDirty = true;
    return sg_query_image_state(g_rampImage) == SG_RESOURCESTATE_VALID;
}

// The frame's place in a client area: its aspect kept, as large as fits, centred.
Rect Placed(int clientWidth, int clientHeight, int frameWidth, int frameHeight) {
    long long a = static_cast<long long>(clientWidth) * frameHeight;
    long long b = static_cast<long long>(clientHeight) * frameWidth;
    int w = clientWidth, h = clientHeight;
    if (a > b) w = static_cast<int>((static_cast<long long>(clientHeight) * frameWidth + frameHeight / 2) / frameHeight);
    else if (a < b) h = static_cast<int>((static_cast<long long>(clientWidth) * frameHeight + frameWidth / 2) / frameWidth);
    int x = (clientWidth - w) / 2, y = (clientHeight - h) / 2;
    return Rect{x, y, x + w, y + h};
}

// The client area, the frame's size and its place; false when there is no frame or window.
bool Placement(int* clientWidth, int* clientHeight, int* frameWidth, int* frameHeight, Rect* placed) {
    if (!platform::WindowPixelSize(GameWindow(), clientWidth, clientHeight) || !GameTargetSize(frameWidth, frameHeight))
        return false;
    *placed = Placed(*clientWidth, *clientHeight, *frameWidth, *frameHeight);
    return true;
}

#if defined(EI_RENDER_VALIDATION)
// The checks' copies of the drawable and of the frame, kept from check to check (released in DropWindow).
platform::graphics::Readback g_backCopy, g_frameCopy;

// Verify build: the drawable just drawn against the frame through the ramp, at a grid of points (one to one placement
// only). Logged once per ramp.
void CheckRamp(int width, int height) {
    g_checkRamp = false;
    int fw = 0, fh = 0;
    if (!FrameSize(&fw, &fh) || fw != width || fh != height) return;
    const platform::Rect whole{0, 0, width, height};
    if (!g_backCopy.ReadDrawable(whole) || !g_frameCopy.Read(FrameImage(), whole)) return;
    int points = 0, differing = 0;
    for (int y = height / 34; y < height; y += height / 17 + 1) {
        for (int x = width / 34; x < width; x += width / 17 + 1) {
            const std::uint8_t* b = g_backCopy.Row(y) + x * 4;
            const std::uint8_t* f = g_frameCopy.Row(y) + x * 4;
            // drawable B, G, R; frame R, G, B
            int want[3] = {g_ramp[f[2] * 4 + 2], g_ramp[f[1] * 4 + 1], g_ramp[f[0] * 4 + 0]};
            for (int c = 0; c < 3; ++c) {
                int d = static_cast<int>(b[c]) - want[c];
                if (d > 1 || d < -1) {
                    ++differing;
                    break;
                }
            }
            ++points;
        }
    }
    g_info.rampChecks++;
    g_info.rampPoints = points;
    g_info.rampDiffering = differing;
    if (differing) ei::Log("render: MISMATCH gamma ramp: %d of %d points of the presented frame differ", differing, points);
    else ei::Log("render: gamma ramp check: %d points of the presented frame match the ramp", points);
}

#endif

} // namespace

namespace detail {

bool DrawToWindow() {
    int clientWidth = 0, clientHeight = 0, frameWidth = 0, frameHeight = 0;
    Rect placed{};
    if (!Placement(&clientWidth, &clientHeight, &frameWidth, &frameHeight, &placed)) return false;
    if (!FrameTexture().id) return false;
    sg_swapchain drawable{};
    if ((!g_pipeline.id && !CreatePipeline()) ||
        !platform::graphics::BeginDrawable(GameWindow(), clientWidth, clientHeight, g_presentMode, &drawable)) {
        // A lost device is made again at the frame's end (render.cpp, CheckDevice); on a working one the game cannot
        // go on.
        if (!platform::graphics::Lost())
            Fatal("The renderer cannot present on the game window: %s (%dx%d).",
                  g_pipeline.id ? platform::graphics::Error() : "no present pipeline", clientWidth, clientHeight);
        return false;
    }
    g_drawableWidth = clientWidth;
    g_drawableHeight = clientHeight;
    if (g_rampDirty) {
        sg_image_data data{};
        data.mip_levels[0] = sg_range{g_ramp, sizeof(g_ramp)};
        sg_update_image(g_rampImage, &data);
        g_rampDirty = false;
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;   // the bars black
    pass.action.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain = drawable;
    pass.label = "ei-present";
    sg_begin_pass(&pass);
    platform::graphics::ApplyViewport(placed.left, placed.top, placed.right - placed.left, placed.bottom - placed.top);
    sg_apply_pipeline(g_pipeline);
    sg_bindings b{};
    b.views[0] = FrameTexture();
    b.views[1] = g_rampView;
    b.samplers[0] = LinearSampler();
    sg_apply_bindings(&b);
    const present_params_t whole{{0.0f, 0.0f, 1.0f, 1.0f}};
    sg_apply_uniforms(UB_present_params, SG_RANGE(whole));
    sg_draw(0, 3, 1);
    perf::Draw(clientWidth, clientHeight, placed);   // over the frame, off unless switched on
    sg_end_pass();
    return true;
}

bool PresentWindow() {
#if defined(EI_RENDER_VALIDATION)
    if (g_checkRamp) CheckRamp(g_drawableWidth, g_drawableHeight);
#endif
    if (!platform::graphics::EndDrawable()) return false;
    ++g_info.presents;
    return true;
}

const std::uint8_t* GammaRamp() { return g_ramp; }
std::uint32_t GammaRampChanges() { return g_rampChanges; }

void DropWindow() {
    if (g_rampView.id) sg_destroy_view(g_rampView);
    if (g_rampImage.id) sg_destroy_image(g_rampImage);
    if (g_pipeline.id) sg_destroy_pipeline(g_pipeline);
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_rampView = sg_view{};
    g_rampImage = sg_image{};
    g_pipeline = sg_pipeline{};
    g_shader = sg_shader{};
    g_rampDirty = true;   // the ramp the game set goes to the new device's lookup texture
    g_drawableWidth = g_drawableHeight = 0;
    g_info.presentsAtRecovery = g_info.presents;
#if defined(EI_RENDER_VALIDATION)
    g_backCopy.Release();
    g_frameCopy.Release();
#endif
}

} // namespace detail

void SetWaitForVsync(bool on) { g_presentMode.vsync = on; }

void SetVsync(bool on) {
    if (!VsyncSwitchSupported()) return;
    if (on != g_presentMode.vsync) ei::Log("render: vertical blank wait %s (settings file)", on ? "on" : "off");
    g_presentMode.vsync = on;
    g_presentMode.allowTearing = !on;
}

bool VsyncSwitchSupported() {
#if defined(EI_RENDER_VALIDATION)
    return false;   // the harness build: its presents are the virtual clock's (SetWaitForVsync), the pinned settings lack the value
#else
    return true;
#endif
}

const std::uint8_t* GammaRampTable() { return g_ramp; }

void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue) {
    // The ramp's full scale is the game's 65280 (255 * 256); entries are
    // rounded to the 8 bits a channel of the frame has.
    const std::uint16_t* channels[3] = {red, green, blue};
    bool identity = true;
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            unsigned v = channels[c] ? (static_cast<unsigned>(channels[c][i]) * 255u + 32640u) / 65280u : static_cast<unsigned>(i);
            if (v > 255) v = 255;
            g_ramp[i * 4 + c] = static_cast<std::uint8_t>(v);
            identity = identity && v == static_cast<unsigned>(i);
        }
        g_ramp[i * 4 + 3] = 255;
    }
    if (identity != g_rampIdentity || !identity)
        ei::Log("render: gamma ramp %s (entry 64: %u %u %u, 128: %u %u %u)", identity ? "identity" : "set", g_ramp[256],
                g_ramp[257], g_ramp[258], g_ramp[512], g_ramp[513], g_ramp[514]);
    g_rampIdentity = identity;
    g_rampDirty = true;
    ++g_rampChanges;
#if defined(EI_RENDER_VALIDATION)
    // Reading the presented frame back stalls on the GPU: sampled (ReadBackDue).
    g_checkRamp = detail::ReadBackDue(g_rampsSet);
    ++g_rampsSet;
#else
    g_checkRamp = true;
#endif
}

WindowPresentInfo WindowPresents() {
    WindowPresentInfo info = g_info;
    info.rampCheckPending = g_checkRamp;
    info.rampIdentity = g_rampIdentity;
    DeviceLossCounts(&info);
    return info;
}

bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed) {
    int cw = 0, ch = 0;
    return Placement(&cw, &ch, frameWidth, frameHeight, placed);
}

void WindowToFrame(int* x, int* y) {
    int fw = 0, fh = 0;
    Rect r{};
    if (!FrameInWindow(&fw, &fh, &r)) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return;
    // Floor division, so that points left of / above the frame stay negative.
    long long px = static_cast<long long>(*x - r.left) * fw, py = static_cast<long long>(*y - r.top) * fh;
    *x = static_cast<int>(px >= 0 ? px / w : -((-px + w - 1) / w));
    *y = static_cast<int>(py >= 0 ? py / h : -((-py + h - 1) / h));
}

void FrameToWindow(int* x, int* y) {
    int fw = 0, fh = 0;
    Rect r{};
    if (!FrameInWindow(&fw, &fh, &r)) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return;
    // The centre of the window pixels that map to the frame pixel.
    *x = r.left + static_cast<int>((static_cast<long long>(*x) * w * 2 + w) / (2LL * fw));
    *y = r.top + static_cast<int>((static_cast<long long>(*y) * h * 2 + h) / (2LL * fh));
}

} // namespace render
