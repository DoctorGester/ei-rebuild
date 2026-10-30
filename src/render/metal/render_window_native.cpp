// SDL/Metal presentation: the common sokol shaders keep the frame's aspect, gamma and software cursor.
#include "game/app.h"
#include "hook/log.h"
#include "render/render_internal.h"
#include "render/render_native.h"
#include "render/render_perf.h"

#include <SDL3/SDL.h>
#include <cstring>
namespace render {
namespace {
using namespace detail;
bool g_waitForVsync = true, g_rampDirty = true, g_rampIdentity = true;
sg_shader g_shader{};
sg_pipeline g_pipeline{};
sg_image g_rampImage{};
sg_view g_rampView{};
struct Ramp {
    std::uint8_t entries[256 * 4];
    Ramp() {
        for (int i = 0; i < 256; ++i) {
            entries[4 * i] = entries[4 * i + 1] = entries[4 * i + 2] = i;
            entries[4 * i + 3] = 255;
        }
    }
} g_rampTable;
auto& g_ramp = g_rampTable.entries;
std::uint8_t g_uploaded[256 * 4]{};
Rect g_lastCursor{};
WindowPresentInfo g_info;
struct CursorParams {
    float to[4], frame[4], size[4];
};
bool CreatePipeline() {
    sg_shader_desc s{};
    SetBytecode(s, kPresentShader);
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = 16;
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(CursorParams);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[1].texture.image_type = SG_IMAGETYPE_2D;
    s.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[1].texture.hlsl_register_t_n = 1;
    s.views[2].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[2].texture.image_type = SG_IMAGETYPE_2D;
    s.views[2].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[2].texture.hlsl_register_t_n = 2;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    // The ramp is read with Load; sokol wants every texture in a pair all the same.
    s.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[1].view_slot = 1;
    s.texture_sampler_pairs[1].sampler_slot = 0;
    s.texture_sampler_pairs[2].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[2].view_slot = 2;
    s.texture_sampler_pairs[2].sampler_slot = 0;
    s.label = "ei-present";
    g_shader = sg_make_shader(&s);
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

Rect Placed(int clientWidth, int clientHeight, int frameWidth, int frameHeight) {
    long long a = static_cast<long long>(clientWidth) * frameHeight;
    long long b = static_cast<long long>(clientHeight) * frameWidth;
    int w = clientWidth, h = clientHeight;
    if (a > b)
        w = static_cast<int>((static_cast<long long>(clientHeight) * frameWidth + frameHeight / 2) / frameHeight);
    else if (a < b)
        h = static_cast<int>((static_cast<long long>(clientWidth) * frameHeight + frameWidth / 2) / frameWidth);
    int x = (clientWidth - w) / 2, y = (clientHeight - h) / 2;
    return Rect{x, y, x + w, y + h};
}

bool Placement(int* cw, int* ch, int* fw, int* fh, Rect* placed) {
    auto* window = static_cast<SDL_Window*>(app::g_mainWindow);
    if (!window || !SDL_GetWindowSize(window, cw, ch) || *cw < 1 || *ch < 1 || !GameTargetSize(fw, fh)) return false;
    *placed = Placed(*cw, *ch, *fw, *fh);
    return true;
}
}
namespace detail {
bool DrawToWindow(const BlitBatch* cursor) {
    if (DeviceIsLost()) return false;
    int clientWidth = 0, clientHeight = 0, frameWidth = 0, frameHeight = 0;
    Rect placed{};
    if (!Placement(&clientWidth, &clientHeight, &frameWidth, &frameHeight, &placed)) return false;
    if (!FrameTexture().id) return false;
    if (!g_pipeline.id && !CreatePipeline()) Fatal("Cannot make the Metal presentation pipeline");
    const sg_swapchain drawable = NativeDrawable(app::g_mainWindow, g_waitForVsync, &clientWidth, &clientHeight);
    if (!drawable.metal.current_drawable) return false;
    placed = Placed(clientWidth, clientHeight, frameWidth, frameHeight);
    if (g_rampDirty) {
        sg_image_data data{};
        data.mip_levels[0] = sg_range{g_ramp, sizeof(g_ramp)};
        sg_update_image(g_rampImage, &data);
        std::memcpy(g_uploaded, g_ramp, sizeof(g_uploaded));
        g_rampDirty = false;
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;   // the bars black
    pass.action.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain = drawable;
    pass.label = "ei-present";
    sg_begin_pass(&pass);
    sg_apply_viewport(placed.left, placed.top, placed.right - placed.left, placed.bottom - placed.top, true);
    sg_apply_pipeline(g_pipeline);
    // The cursor texture's slot holds the ramp's when there is no cursor (the rectangle is empty then).
    CursorParams cp{};
    sg_view cursorView = g_rampView;
    g_lastCursor = Rect{0, 0, 0, 0};
    if (cursor && cursor->texture.id) {
        const Rect& to = cursor->destination;
        int dw = to.right - to.left, dh = to.bottom - to.top;
        int sw = cursor->source.right - cursor->source.left, sh = cursor->source.bottom - cursor->source.top;
        if (dw > 0 && dh > 0 && sw > 0 && sh > 0) {
            cursorView = cursor->texture;
            cp = CursorParams{{static_cast<float>(to.left), static_cast<float>(to.top), static_cast<float>(to.right),
                               static_cast<float>(to.bottom)},
                              {static_cast<float>(frameWidth), static_cast<float>(frameHeight),
                               static_cast<float>(sw) / static_cast<float>(dw),
                               static_cast<float>(sh) / static_cast<float>(dh)},
                              {static_cast<float>(sw), static_cast<float>(sh), static_cast<float>(cursor->source.left),
                               static_cast<float>(cursor->source.top)}};
            g_lastCursor = to;
        }
    }
    sg_bindings b{};
    b.views[0] = FrameTexture();
    b.views[1] = g_rampView;
    b.views[2] = cursorView;
    b.samplers[0] = LinearSampler();
    sg_apply_bindings(&b);
    float uniforms[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    sg_range range{uniforms, sizeof(uniforms)};
    sg_apply_uniforms(0, &range);
    sg_range cursorRange{&cp, sizeof(cp)};
    sg_apply_uniforms(1, &cursorRange);
    sg_draw(0, 3, 1);
    perf::Draw(clientWidth, clientHeight, placed);   // over the frame and the cursor, off unless switched on
    sg_end_pass();
    return true;
}

void PresentWindow() { ++g_info.presents; }   // sokol queues the Metal drawable at sg_commit
bool CurrentWindowParts(WindowParts*) { return false; }
void DropWindow() {
    if (g_rampView.id) sg_destroy_view(g_rampView);
    if (g_rampImage.id) sg_destroy_image(g_rampImage);
    if (g_pipeline.id) sg_destroy_pipeline(g_pipeline);
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_rampView = {};
    g_rampImage = {};
    g_pipeline = {};
    g_shader = {};
    g_rampDirty = true;
}
}
void SetWaitForVsync(bool on) { g_waitForVsync = on; }
void SetVsync(bool on) { g_waitForVsync = on; }
bool VsyncSwitchSupported() { return true; }
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue) {
    // The ramp's full scale is the game's 65280 (255 * 256); entries are
    // rounded to the 8 bits a channel of the frame has.
    const std::uint16_t* channels[3] = {red, green, blue};
    bool identity = true;
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            unsigned v = channels[c] ? (static_cast<unsigned>(channels[c][i]) * 255u + 32640u) / 65280u
                                     : static_cast<unsigned>(i);
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
}

WindowPresentInfo WindowPresents() {
    auto info = g_info;
    info.rampIdentity = g_rampIdentity;
    detail::DeviceLossCounts(&info);
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

}
