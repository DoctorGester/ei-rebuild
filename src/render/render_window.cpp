// The game window's present (render::StartPresenting): the frame is drawn into the swap chain's texture (gpu.h), its
// aspect kept (centred, black bars when the window's client area has another shape), through the display's gamma ramp
// (the options' brightness / contrast / gamma, SetGammaRamp's table) as a shader step, and presented with the vertical
// blank. Also where the frame sits in the window, for the game's mouse coordinates (WindowToFrame / FrameToWindow).
// docs/notes/renderer.md, section 9.
#include "engine/core/program_window.h"
#include "../platform/log.h"
#include "../platform/sdl/sdl_window.h"
#include "render.h"
#include "render_internal.h"
#include "render_perf.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace render {

static platform::NativeWindow GameWindow() { return g_mainWindow; }

// Present waits for the vertical blank unless the settings file's "VSync" is 0 (render::SetVsync: Immediate, which may
// tear) or the harness's virtual clock runs the game (render::SetWaitForVsync: Mailbox), where a frame's length does
// not depend on the display.
static bool g_vsync = true;                                        // the settings file's
static bool g_waitForVsync = true;                                 // the harness's
static SDL_Window* g_claimed = nullptr;                            // the window the device's swap chain is on
static gpu::PresentMode g_wantedMode = gpu::PresentMode::Vsync;    // the mode asked of it
static gpu::PresentMode g_presentMode = gpu::PresentMode::Vsync;   // the mode it gave (an unsupported one falls back)

// The gamma ramp as a 256 x 1 lookup (RGBA8: the red, green and blue ramps' entries scaled to 255),
// identity until the game sets one.
static gpu::Texture g_rampImage;
static std::array<std::uint8_t, 256 * 4> IdentityRamp() {
    std::array<std::uint8_t, 256 * 4> entries{};
    for (int i = 0; i < 256; ++i) {
        entries[i * 4 + 0] = entries[i * 4 + 1] = entries[i * 4 + 2] = static_cast<std::uint8_t>(i);
        entries[i * 4 + 3] = 255;
    }
    return entries;
}
static std::array<std::uint8_t, 256 * 4> g_ramp =
    IdentityRamp();               // (kept across devices: the game sets it once, a new device gets it again)
static bool g_rampDirty = true;   // to be uploaded before the next present
static bool g_rampIdentity = true;
static std::uint32_t g_rampChanges = 0;   // SetGammaRamp's calls (the cursor's shapes are made through the ramp)
static long g_rampsSet = 0;               // verify build: ramps set so far (the ramp check is sampled)
static bool g_checkRamp = false;          // verify build: compare the next presented frame with the ramp applied
static WindowPresentInfo g_info;

static gpu::PresentMode WantedMode() {
    if (!g_vsync) return gpu::PresentMode::Immediate;
    if (!g_waitForVsync) return gpu::PresentMode::Mailbox;
    return gpu::PresentMode::Vsync;
}

// The swap chain on the game window, in the present mode asked for; false when the window cannot have one.
static bool ClaimGameWindow() {
    SDL_Window* window = platform::SdlWindow(GameWindow());
    if (!window) return false;
    const bool claimed = window != g_claimed;
    if (claimed) {
        if (!gpu::ClaimWindow(window)) return false;
        g_claimed = window;
    }
    if (claimed || WantedMode() != g_wantedMode) {
        g_wantedMode = WantedMode();
        g_presentMode = gpu::SetPresentMode(g_wantedMode);
        static const char* const kModes[] = {"waiting for the vertical blank", "no vertical blank wait (tearing allowed)",
                                             "no vertical blank wait (mailbox)"};
        ei::Log("render: presenting on the game window through the %s swap chain, %s (asked for mode %d, got %d)",
                gpu::DriverName(), kModes[static_cast<int>(g_presentMode)], static_cast<int>(g_wantedMode),
                static_cast<int>(g_presentMode));
    }
    return true;
}

static gpu::PipelineDesc PresentDesc(gpu::Format format) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::present::kIndex;
    p.colour = format;
    return p;
}

// The frame through the ramp into `placed` of the bound target (inside its pass).
static void DrawFrame(gpu::Format format, const Rect& placed) {
    const gpu::Pipeline pipeline = gpu::MakePipeline(PresentDesc(format), "ei-present");
    if (!pipeline) Fatal("The renderer cannot present on the game window: no present pipeline.");
    gpu::SetViewport(placed);
    gpu::ApplyPipeline(pipeline);
    const gpu::TextureBinding textures[2] = {{FrameTexture(), LinearSampler()}, {g_rampImage, LinearSampler()}};
    gpu::BindTextures(gpu::Stage::Fragment, gpu_shaders::present::kSamplerTex, textures);
    const PresentParams whole{{0.0f, 0.0f, 1.0f, 1.0f}};
    gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::present::kBlockPresentParams, whole);
    gpu::Draw(0, 3);
}

// The frame's place in a client area: its aspect kept, as large as fits, centred.
static Rect Placed(Size client, Size frame) {
    long long a = static_cast<long long>(client.w) * frame.h;
    long long b = static_cast<long long>(client.h) * frame.w;
    int w = client.w, h = client.h;
    if (a > b) w = static_cast<int>((static_cast<long long>(client.h) * frame.w + frame.h / 2) / frame.h);
    else if (a < b) h = static_cast<int>((static_cast<long long>(client.w) * frame.h + frame.w / 2) / frame.w);
    int x = (client.w - w) / 2, y = (client.h - h) / 2;
    return Rect{x, y, x + w, y + h};
}

#if defined(EI_RENDER_VALIDATION)
// The ramp check's copy of the present (the swap chain's texture is never read back): the same draw into a texture of
// the drawable's size and format, and the downloads of it and of the frame, read after the frame's submission.
static gpu::Texture g_rampCheck;
static gpu::Ticket g_backCopy, g_frameCopy;
static int g_checkWidth = 0, g_checkHeight = 0;

// Before the present's pass: the present drawn into the check's texture too, when a check is due and the frame is
// placed one to one.
static void DrawRampCheck(gpu::Format format, int width, int height, const Rect& placed) {
    const std::optional<Size> frame = FrameSize();
    if (!g_checkRamp || !frame || *frame != Size{width, height} || placed.left != 0 || placed.top != 0 ||
        (format != gpu::Format::Bgra8 && format != gpu::Format::Rgba8)) {
        g_checkRamp = false;
        return;
    }
    if (g_rampCheck.width != width || g_rampCheck.height != height || g_rampCheck.format != format) {
        gpu::Release(g_rampCheck);
        g_rampCheck = gpu::MakeTexture({width, height, 1, 1, format, gpu::TextureUse::ColourTarget, "ei-gamma-ramp-check"});
        if (!g_rampCheck) return;
    }
    gpu::PassDesc pass;
    pass.colour.texture = g_rampCheck;
    pass.colour.load = gpu::Load::Clear;
    pass.label = "ei-gamma-ramp-check";
    if (!gpu::BeginPass(pass)) return;
    DrawFrame(format, placed);
    gpu::EndPass();
    const Rect whole{0, 0, width, height};
    g_backCopy = gpu::Download(g_rampCheck, 0, whole);
    g_frameCopy = gpu::Download(FrameTexture(), 0, whole);
    g_checkWidth = width;
    g_checkHeight = height;
}

// Verify build: the present just drawn against the frame through the ramp, at a grid of points. Logged once per ramp.
static void CheckRamp() {
    g_checkRamp = false;
    const int width = g_checkWidth, height = g_checkHeight;
    gpu::Texels back, frame;
    const bool bgra = g_rampCheck.format == gpu::Format::Bgra8;
    const bool read = g_backCopy && g_frameCopy && gpu::Wait(g_backCopy) && gpu::Wait(g_frameCopy) &&
                      gpu::Read(g_backCopy, &back) && gpu::Read(g_frameCopy, &frame);
    int points = 0, differing = 0;
    for (int y = height / 34; read && y < height; y += height / 17 + 1) {
        for (int x = width / 34; x < width; x += width / 17 + 1) {
            const std::uint8_t* b = back.Row(y) + x * 4;
            const std::uint8_t* f = frame.Row(y) + x * 4;
            // the present's B, G, R (or R, G, B); the frame's R, G, B
            int want[3] = {g_ramp[f[2] * 4 + 2], g_ramp[f[1] * 4 + 1], g_ramp[f[0] * 4 + 0]};
            if (!bgra) std::swap(want[0], want[2]);
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
    gpu::Release(g_backCopy);
    gpu::Release(g_frameCopy);
    if (!read) return;
    g_info.rampChecks++;
    g_info.rampPoints = points;
    g_info.rampDiffering = differing;
    if (differing) ei::Log("render: MISMATCH gamma ramp: %d of %d points of the presented frame differ", differing, points);
    else ei::Log("render: gamma ramp check: %d points of the presented frame match the ramp", points);
}

#endif

void ListPresentPipelines(std::vector<gpu::PipelineDesc>& out) {
    out.push_back(PresentDesc(gpu::Format::Bgra8));   // the swap chain's format on Direct3D 12 and Vulkan
}

bool DrawToWindow() {
    const std::optional<Size> frame = GameTargetSize();
    if (!frame || !FrameTexture()) return false;
    if (!ClaimGameWindow()) Fatal("The renderer cannot present on the game window: %s.", gpu::Error());
    if (!g_rampImage) {
        g_rampImage = gpu::MakeTexture({256, 1, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-gamma-ramp"});
        if (!g_rampImage) Fatal("The renderer cannot present on the game window: no gamma ramp.");
        g_rampDirty = true;
    }
    if (g_rampDirty) {
        gpu::Write(g_rampImage, 0, {0, 0, 256, 1}, g_ramp.data(), g_ramp.size(), gpu::Contents::Discard);
        g_rampDirty = false;
    }
    gpu::Texture target;
    if (!gpu::AcquireSwapchain(&target)) Fatal("The renderer cannot present on the game window: %s.", gpu::Error());
    if (!target) return false;   // minimised or hidden: nothing to draw into
    // The frame's place in the swap chain's texture (the window's pixel size this frame).
    const Rect placed = Placed(Size{target.width, target.height}, *frame);
    perf::Prepare(placed.Height());
#if defined(EI_RENDER_VALIDATION)
    DrawRampCheck(target.format, target.width, target.height, placed);
#endif
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = gpu::Load::Clear;   // the bars black
    pass.label = "ei-present";
    if (!gpu::BeginPass(pass)) Fatal("The renderer cannot present on the game window.");
    DrawFrame(target.format, placed);
    perf::Draw(target.format, target.width, target.height, placed);   // over the frame, off unless switched on
    gpu::EndPass();
    return true;
}

void PresentWindow() {
#if defined(EI_RENDER_VALIDATION)
    if (g_checkRamp) CheckRamp();
#endif
    ++g_info.presents;
}

std::uint32_t GammaRampChanges() { return g_rampChanges; }

void SetWaitForVsync(bool on) { g_waitForVsync = on; }

void SetVsync(bool on) {
    if (!VsyncSwitchSupported()) return;
    if (on != g_vsync) ei::Log("render: vertical blank wait %s (settings file)", on ? "on" : "off");
    g_vsync = on;
}

bool Vsync() { return g_vsync; }

bool VsyncSwitchSupported() {
#if defined(EI_RENDER_VALIDATION)
    return false;   // the harness build: its presents are the virtual clock's (SetWaitForVsync), the pinned settings lack the value
#else
    return true;
#endif
}

const std::uint8_t* GammaRampTable() { return g_ramp.data(); }

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
    g_checkRamp = ReadBackDue(g_rampsSet);
    ++g_rampsSet;
#else
    g_checkRamp = true;
#endif
}

WindowPresentInfo WindowPresents() {
    WindowPresentInfo info = g_info;
    info.rampCheckPending = g_checkRamp;
    info.rampIdentity = g_rampIdentity;
    return info;
}

std::optional<FramePlacement> FrameInWindow() {
    const std::optional<Size> client = platform::WindowPixelSize(GameWindow());
    if (!client) return std::nullopt;
    const std::optional<Size> frame = GameTargetSize();
    if (!frame) return std::nullopt;
    return FramePlacement{*frame, Placed(*client, *frame)};
}

Point WindowToFrame(Point p) {
    const std::optional<FramePlacement> shown = FrameInWindow();
    if (!shown) return p;
    const int fw = shown->frame.w, fh = shown->frame.h;
    const Rect& r = shown->placed;
    int w = r.Width(), h = r.Height();
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return p;
    // Floor division, so that points left of / above the frame stay negative.
    long long px = static_cast<long long>(p.x - r.left) * fw, py = static_cast<long long>(p.y - r.top) * fh;
    return {static_cast<int>(px >= 0 ? px / w : -((-px + w - 1) / w)),
            static_cast<int>(py >= 0 ? py / h : -((-py + h - 1) / h))};
}

Point FrameToWindow(Point p) {
    const std::optional<FramePlacement> shown = FrameInWindow();
    if (!shown) return p;
    const int fw = shown->frame.w, fh = shown->frame.h;
    const Rect& r = shown->placed;
    int w = r.Width(), h = r.Height();
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return p;
    // The centre of the window pixels that map to the frame pixel.
    return {r.left + static_cast<int>((static_cast<long long>(p.x) * w * 2 + w) / (2LL * fw)),
            r.top + static_cast<int>((static_cast<long long>(p.y) * h * 2 + h) / (2LL * fh))};
}

} // namespace render
