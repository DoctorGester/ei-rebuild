// The new renderer's frame: the switch, the device (gpu.h), the off-screen target the frame is drawn into (the game's
// resolution) and reading it back for captures. See render.h and docs/notes/renderer.md.
#include "render.h"

#include "engine/core/program_window.h"
#include "game/app/singletons.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "../platform/log.h"
#include "render_internal.h"
#include "render_perf.h"
#include "render_state.h"
#include "render_ui.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace render {

// --- The switch ------------------------------------------------------------------------------------

static bool g_configured = false;
static int g_msaa = 1;   // the render quality (SetMsaa, SetAnisotropy); not EI_RENDER's
static int g_anisotropy = 1;
#if defined(EI_RENDER_VALIDATION)
static bool g_checkAll =
    false;   // "checkall" (harness build only): every read-back check runs; else the first few of each kind, then every 16th
#endif

// EI_RENDER's words (render.h).
static void ParseSpec(const std::string& s) {
#if defined(EI_RENDER_VALIDATION)
    g_checkAll = false;
#endif
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        std::string word = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        while (!word.empty() && word.back() == ' ') word.pop_back();
        bool known = false;
#if defined(EI_RENDER_VALIDATION)
        if (word == "checkall") {
            g_checkAll = true;
            known = true;
        }
#endif
        if (!known && !word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    g_configured = true;
}

static void EnsureConfigured() {
    if (g_configured) return;
    ParseSpec(platform::EnvironmentVariable("EI_RENDER"));
}

// --- Game memory -----------------------------------------------------------------------------------

static std::uint32_t GameClearColour() { return g_sceneRenderer.fogColour; }

static void SetClear(float out[4], std::uint32_t c) {
    UnpackColour(c, out);
    out[3] = 1.0f;
}

static int g_deviceSamples = 1;   // the most samples up to kSmoothSamples the device draws the frame with

#if defined(_M_X64) || defined(__x86_64__)
// The SSE control bits (rounding, flush-to-zero, denormals-are-zero, the exception masks) the renderer started with.
// The game's code does not change them; a driver or library that did would change every float of the frame.
static unsigned g_mxcsr = 0;
#if defined(EI_RENDER_VALIDATION)
static int g_mxcsrLogged = 0;   // CheckFloatControl's log lines
#endif
#endif

// The largest power of two up to `samples`, at most kSmoothSamples.
static int MsaaLevel(int samples) {
    int level = 1;
    while (level * 2 <= samples && level * 2 <= kSmoothSamples) level *= 2;
    return level;
}

// The most samples up to kSmoothSamples the device draws the frame with: the frame's formats and the depth copy's.
// `maxSamples(colour, depth)`: the most a target of those formats takes (gpu::MaxSamples, or a DeviceLimits').
template <class MaxSamples>
static int DeviceSamples(MaxSamples maxSamples) {
    return (std::min)(maxSamples(kColourFormat, kDepthFormat), maxSamples(kDepthCopyFormat, gpu::Format::None));
}

// The sample count the world is drawn with when `asked` (a power of two up to kSmoothSamples) is wanted: the least count
// the device has at or above it, else the most below it, 1 for none. A device lists the counts it draws (lavapipe has 1
// and 4, no 2): a pass with another count is invalid and draws nothing right. `supports(colour, depth, samples)`: the
// device draws exactly those samples into a target of those formats (gpu::SupportsSamples, or a DeviceLimits').
template <class Supports>
static int SamplesFor(int asked, Supports supports) {
    auto takes = [&supports](int samples) {
        return supports(kColourFormat, kDepthFormat, samples) && supports(kDepthCopyFormat, gpu::Format::None, samples);
    };
    for (int samples = asked; samples > 1 && samples <= kSmoothSamples; samples *= 2)
        if (takes(samples)) return samples;
    for (int samples = asked / 2; samples > 1; samples /= 2)
        if (takes(samples)) return samples;
    return 1;
}

static bool CreateDevice() {
    // A call the device refuses later (a lost device) ends the game here too.
    gpu::SetFailureHandler([](std::string_view reason) { Fatal("The renderer cannot draw the frame: %s.", reason); });
    if (!gpu::Start()) return false;
    g_deviceSamples = DeviceSamples(
        [](gpu::Format colour, gpu::Format depth) { return gpu::MaxSamples(colour, depth, kSmoothSamples); });
#if defined(_M_X64) || defined(__x86_64__)
    g_mxcsr = _mm_getcsr() & 0xFFC0u;
#endif
    return true;
}

// Once a frame on the main thread: the SSE control bits as at the start, else logged (verify builds) and set back.
static void CheckFloatControl() {
#if defined(_M_X64) || defined(__x86_64__)
    const unsigned now = _mm_getcsr();
    if ((now & 0xFFC0u) == g_mxcsr) return;
#if defined(EI_RENDER_VALIDATION)
    LogFirst(g_mxcsrLogged, 10, "render: MXCSR's control bits changed (%04X, at the start %04X); set back",
             now & 0xFFC0u, g_mxcsr);
#endif
    _mm_setcsr((now & 0x3Fu) | g_mxcsr);
#endif
}

// --- The off-screen target ---------------------------------------------------------------------------

static int g_width = 0, g_height = 0;
static gpu::Texture g_colour, g_depth;
static bool g_haveFrame = false;
// With multisampling (SetMsaa) the world's runs of commands draw into a multisampled target of
// g_targetSamples (render_smooth.cpp) whose colour each such pass resolves into g_colour and whose depth copy
// (kDepthCopyFormat) into g_resolvedDepth; the frame target's depth is sampled then (copied into the multisampled
// one before a world run).
static int g_targetSamples = 1;
static gpu::Texture g_multiColour, g_multiDepth, g_multiDepthCopy, g_resolvedDepth;

static void DestroyMultisampled() {
    for (gpu::Texture* t : {&g_multiColour, &g_multiDepth, &g_multiDepthCopy, &g_resolvedDepth}) gpu::Release(*t);
    g_targetSamples = 1;
}

static void DestroyTarget() {
    DestroyMultisampled();
    gpu::Release(g_colour);
    gpu::Release(g_depth);
    g_width = g_height = 0;
    g_haveFrame = false;
}

// The multisampled colour, depth and depth copy of `samples` and the copy's resolve; false when the device
// refused them.
static bool MakeMultisampled(int width, int height, int samples) {
    g_multiColour = gpu::MakeTexture({width, height, 1, samples, kColourFormat, gpu::TextureUse::ColourTarget, "ei-frame-colour-multisampled"});
    g_multiDepthCopy =
        gpu::MakeTexture({width, height, 1, samples, kDepthCopyFormat, gpu::TextureUse::ColourTarget, "ei-frame-depth-copy-multisampled"});
    g_resolvedDepth =
        gpu::MakeTexture({width, height, 1, 1, kDepthCopyFormat, gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled, "ei-frame-depth-copy"});
    g_multiDepth = gpu::MakeTexture({width, height, 1, samples, kDepthFormat, gpu::TextureUse::DepthTarget, "ei-frame-depth-multisampled"});
    const bool ok = g_multiColour && g_multiDepthCopy && g_resolvedDepth && g_multiDepth && PrepareSmoothCopies(samples);
    if (!ok) {
        DestroyMultisampled();
        return false;
    }
    g_targetSamples = samples;
    return true;
}

// The frame target of the game's size; the multisampled one beside it when the render quality asks for
// multisampling (SetMsaa: the device's nearest count, SamplesFor). A change takes effect at a frame's first pass
// (`mayChange`).
static bool EnsureTarget(int width, int height, bool mayChange) {
    const int samples = g_deviceSamples > 1 ? SamplesFor(g_msaa, gpu::SupportsSamples) : 1;
    if (width == g_width && height == g_height && g_colour && (samples == g_targetSamples || !mayChange)) return true;
    DestroyTarget();
    g_colour = gpu::MakeTexture({width, height, 1, 1, kColourFormat, gpu::TextureUse::ColourTarget | gpu::TextureUse::Sampled, "ei-frame-colour"});
    g_depth = gpu::MakeTexture({width, height, 1, 1, kDepthFormat, gpu::TextureUse::DepthTarget | gpu::TextureUse::Sampled, "ei-frame-depth"});
    if (!g_colour || !g_depth) {
        ei::Log("render: cannot create the %dx%d frame target", width, height);
        DestroyTarget();
        return false;
    }
    g_width = width;
    g_height = height;
    if (samples > 1 && !MakeMultisampled(width, height, samples)) {
        ei::Log("render: msaa: no %d-sample %dx%d target; the world is drawn without multisampling", samples, width, height);
        g_deviceSamples = 1;
    }
    if (g_msaa > 1)
        ei::Log("render: msaa %d: the world drawn with %d samples into the %dx%d frame (the device gives %d)", g_msaa,
                g_targetSamples, width, height, g_deviceSamples);
    return true;
}

// --- The frame -----------------------------------------------------------------------------------------

static bool g_frameDrawn = false;
static bool g_started = false;
static std::uint32_t g_frameNumber = 0;
static std::uint32_t g_commits = 0;

// The device, the first pipelines, the white texture and the water's ripple map (written here, outside any pass) at the
// first use. The game cannot run without them: a failure ends it.
void Start() {
    EnsureConfigured();
    if (g_started) return;
    if (!CreateDevice()) Fatal("Can't start the renderer: %s.", gpu::Error());
    if (!CreateTerrainShader()) Fatal("Can't start the renderer: the terrain pipeline could not be made.");
    if (!CreateMeshShader()) Fatal("Can't start the renderer: the mesh pipeline could not be made.");
    if (!CreateBlitPipeline()) Fatal("Can't start the renderer: the blit pipeline could not be made.");
    if (!WhiteView()) Fatal("Can't start the renderer: the white texture could not be made.");
    if (!MakeRippleMap()) Fatal("Can't start the renderer: the water's ripple map could not be made.");
    g_started = true;
}

static std::size_t g_drawnCommands = 0;   // commands of this frame already drawn into the target
static int g_passes = 0;                  // passes drawn this frame
static int g_passSamples = 1;             // the sample count of the pass being drawn (PassSampleCount)
static bool g_passDepthCopy = false;      // the pass being drawn has the depth copy (PassDepthCopy)
// Whether this frame's multisampled passes write the depth copy (DepthCopyRead), taken at its first one.
static std::optional<bool> g_frameDepthCopy;

// Which target holds the frame's latest pixels: none yet (the frame's first pass clears), the frame
// target, or the multisampled one (its colour also resolved into the frame target).
enum class Holder : std::uint8_t { None, Frame, Multisampled };
static Holder g_holder = Holder::None;

static SmoothInfo g_counts, g_lastCounts;

static SmoothOrder g_smoothOrder = SmoothOrder::Game;

static bool InWorld(std::size_t k) {
    for (const Frame::Range& r : g_frame.worldRanges)
        if (k >= r.first && k < r.end) return true;
    return false;
}

// Whether command `c` tests the depth.
static bool TestsDepth(const Command& c) {
    const auto i = static_cast<std::size_t>(c.index);
    if (c.kind == Command::Kind::Mesh) return g_frame.meshBatches[i].state.depthTest;
    if (c.kind == Command::Kind::Terrain) return g_frame.terrainBatches[i].state.depthTest;
    return false;   // the blits, fills, depth clears and interface draws draw with the depth ignored
}

// Whether command `c` sets the depth of the whole frame (DrawClearDepth's rectangle).
static bool ClearsWholeDepth(const Command& c) {
    if (c.kind != Command::Kind::ClearDepth) return false;
    const Rect& r = g_frame.clears[static_cast<std::size_t>(c.index)].rect;
    return r.right <= r.left || (r.left <= 0 && r.top <= 0 && r.right >= g_width && r.bottom >= g_height);
}

// Whether a draw of this frame reads the world's depth copy (renderer.md section 11, "The depth copy"): after a world
// run the frame target's depth is set from it for the draws that test the depth, and a world run that follows copies
// that depth into its samples; a depth clear of the whole frame ends both. True while the frame's later commands are
// not recorded yet (a copy of the frame in its middle: SplitFrame) and in the harness's split order (a pass on the frame
// target after the world's first half). The battle and container screens clear the depth right after the world: their
// frames are drawn without the copy.
static bool DepthCopyRead() {
    if (!g_frameDrawn || g_smoothOrder == SmoothOrder::Split) return true;
    bool world = false, afterWorld = false;
    for (std::size_t k = 0; k < g_frame.commands.size(); ++k) {
        if (InWorld(k)) {
            if (afterWorld) return true;
            world = true;
            continue;
        }
        if (world) afterWorld = true;
        world = false;
        if (!afterWorld) continue;
        const Command& c = g_frame.commands[k];
        if (ClearsWholeDepth(c)) afterWorld = false;
        else if (TestsDepth(c)) return true;
    }
    return false;
}

// A pass on the frame target. After a multisampled run its colour is the resolve's; its depth is
// set from the resolved depth copy (left undefined in a frame without the copy, DepthCopyRead).
static void BeginFramePass() {
    bool first = g_holder == Holder::None;
    bool afterWorld = g_holder == Holder::Multisampled;
    gpu::PassDesc pass;
    pass.colour.texture = g_colour;
    pass.colour.load = first ? gpu::Load::Clear : gpu::Load::Keep;
    SetClear(pass.colour.clear, GameClearColour());
    pass.depth.texture = g_depth;
    pass.depth.load = first ? gpu::Load::Clear : afterWorld ? gpu::Load::DontCare : gpu::Load::Keep;
    pass.depth.clear = 1.0f;
    pass.label = "ei-frame";
    if (!gpu::BeginPass(pass)) Fatal("The renderer cannot draw the frame.");
    ++g_stats.counts[FrameStats::Count::Passes];
    g_passSamples = 1;
    g_passDepthCopy = false;
    if (afterWorld && g_frameDepthCopy.value_or(true)) {
        CopyDepthFromResolved(g_resolvedDepth);
        ++g_counts.depthCopiesBack;
    }
    g_holder = Holder::Frame;
}

// A pass on the multisampled target, resolved into the frame target at its end. After draws into the
// frame target their colour and depth are copied into it first (a pass of its own: the frame target
// is the resolve's destination and cannot be read in that pass).
static void BeginMultisampledPass() {
    bool first = g_holder == Holder::None;
    if (!g_frameDepthCopy) g_frameDepthCopy = DepthCopyRead();
    const bool depthCopy = *g_frameDepthCopy;
    if (g_holder == Holder::Frame) {
        gpu::PassDesc copy;
        copy.colour.texture = g_multiColour;
        copy.colour.load = gpu::Load::DontCare;
        if (depthCopy) {
            copy.second.texture = g_multiDepthCopy;
            copy.second.load = gpu::Load::DontCare;
        }
        copy.depth.texture = g_multiDepth;
        copy.depth.load = gpu::Load::DontCare;
        copy.label = "ei-frame-into-multisampled";
        if (!gpu::BeginPass(copy)) Fatal("The renderer cannot draw the frame.");
        ++g_stats.counts[FrameStats::Count::Passes];
        g_passSamples = g_targetSamples;
        g_passDepthCopy = depthCopy;
        CopyIntoMultisampled(g_colour, g_depth);
        gpu::EndPass();
        ++g_counts.copiesInto;
    }
    gpu::PassDesc pass;
    pass.colour.texture = g_multiColour;
    pass.colour.load = first ? gpu::Load::Clear : gpu::Load::Keep;
    SetClear(pass.colour.clear, GameClearColour());
    pass.colour.resolve = g_colour;
    if (depthCopy) {
        pass.second.texture = g_multiDepthCopy;
        pass.second.load = pass.colour.load;
        pass.second.clear[0] = 1.0f;   // the depth's clear
        pass.second.resolve = g_resolvedDepth;
    }
    pass.depth.texture = g_multiDepth;
    pass.depth.load = first ? gpu::Load::Clear : gpu::Load::Keep;
    pass.depth.clear = 1.0f;
    pass.label = "ei-frame-multisampled";
    if (!gpu::BeginPass(pass)) Fatal("The renderer cannot draw the frame.");
    ++g_stats.counts[FrameStats::Count::Passes];
    g_passSamples = g_targetSamples;
    g_passDepthCopy = depthCopy;
    g_holder = Holder::Multisampled;
    ++g_counts.multisampledPasses;
    if (depthCopy) ++g_counts.depthCopyPasses;
}

// An empty pass on the frame target (render::SetSmoothOrder's orders): the frame's first clears it;
// after a world run it takes the depth back from sample 0.
static void EmptyFramePass() {
    BeginFramePass();
    gpu::EndPass();
    ++g_passes;
}

// The performance overlay's pass of command `k` (render_perf.h).
static perf::Pass OverlayPass(const Command& c, std::size_t k) {
    if (!InWorld(k)) return perf::Pass::Ui;
    if (c.kind == Command::Kind::Terrain) return perf::Pass::Terrain;
    if (c.kind == Command::Kind::Mesh) {
        const MeshBatch& b = g_frame.meshBatches[static_cast<std::size_t>(c.index)];
        if (b.model && !b.flat && b.caster) return perf::Pass::Objects;
    }
    return perf::Pass::Other;
}

// The mesh commands from g_plannedFrom on as runs of draws (MeshDraws), each command's draw in g_commandDraws (-1: none).
static MeshDraws g_meshDraws("ei-mesh-draws");
static std::vector<int> g_commandDraws;
static std::size_t g_plannedFrom = 0;

// The mesh commands in [from, end) added to g_meshDraws and uploaded: a run joins only commands next to each other that
// no pass can come between (a pass may end where the world begins or ends with multisampling, at `split` and at
// `water`) and that the performance overlay counts alike.
static void PlanMeshDraws(std::size_t from, std::size_t end, bool multisampling, std::size_t split, std::size_t water) {
    g_meshDraws.Clear();
    g_plannedFrom = from;
    g_commandDraws.assign(end - from, -1);
    for (std::size_t k = from; k < end; ++k) {
        const Command& c = g_frame.commands[k];
        if (c.kind != Command::Kind::Mesh) continue;
        const MeshBatch& batch = g_frame.meshBatches[static_cast<std::size_t>(c.index)];
        const bool world = InWorld(k);
        // The render quality's anisotropy: the world's models sampled anisotropically at its level; the interface's
        // sprites and previews and the pre-transformed draws as the stage says.
        const int anisotropy = batch.model && world ? g_anisotropy : 1;
        const Command* before = k > from ? &g_frame.commands[k - 1] : nullptr;
        const bool join = before && before->kind == Command::Kind::Mesh && k != split && k != water &&
                          (!multisampling || InWorld(k - 1) == world) && OverlayPass(c, k) == OverlayPass(*before, k - 1);
        if (g_meshDraws.Add(g_frame, batch, anisotropy, join))
            g_commandDraws[k - from] = static_cast<int>(g_meshDraws.Count()) - 1;
    }
    g_meshDraws.Upload();
}

static void DrawCommands(std::size_t from, std::size_t to, int width, int height) {
    for (std::size_t k = from; k < to; ++k) {
        const Command& c = g_frame.commands[k];
        auto i = static_cast<std::size_t>(c.index);
        if (perf::On()) perf::Enter(OverlayPass(c, k));
        switch (c.kind) {
        case Command::Kind::Terrain: DrawTerrainBatch(g_frame, g_frame.terrainBatches[i]); break;
        case Command::Kind::Mesh: {
            const MeshBatch& batch = g_frame.meshBatches[i];
            if (batch.model) {
                int& models = InWorld(k) && g_anisotropy > 1 ? g_counts.anisotropicModels : g_counts.plainModels;
                ++models;
            }
            // A run's first command draws it.
            const int draw = g_commandDraws[k - g_plannedFrom];
            if (draw >= 0) g_meshDraws.Draw(g_frame, static_cast<std::size_t>(draw));
            break;
        }
        case Command::Kind::Blit:
            DrawBlitBatch(g_frame.blitBatches[i], width, height);
            break;
        case Command::Kind::ClearDepth: DrawClearDepth(g_frame.clears[i], width, height); break;
        case Command::Kind::Fill: DrawFill(g_frame.fills[i], width, height); break;
        case Command::Kind::Ui: DrawUiBatch(g_frame.uiBatches[i], width, height); break;
        case Command::Kind::Picture: DrawPictureBatch(g_frame.pictureBatches[i], width, height); break;
        case Command::Kind::Overlay: DrawOverlayBatch(g_frame.overlayBatches[i], width, height); break;
        }
    }
}

// Where the frame is copied for the water's reflections (render_water.cpp): the end of its first world view that
// draws water (the copy the next frame's water reads), or, when the last frame left no copy (the first frame after a
// load), before the water's first draw (the copy this frame's water reads); SIZE_MAX: no such point is drawn yet,
// or this frame's copy was taken.
static std::size_t WaterCopyPoint() {
    if (WaterCopyTaken()) return SIZE_MAX;
    for (const Frame::Range& r : g_frame.worldRanges) {
        for (std::size_t k = r.first; k < std::min(r.end, g_frame.commands.size()); ++k) {
            const Command& c = g_frame.commands[k];
            if (c.kind != Command::Kind::Terrain || !g_frame.terrainBatches[static_cast<std::size_t>(c.index)].water)
                continue;
            if (!LastWaterCopyKept()) return k;
            return r.end <= g_frame.commands.size() ? r.end : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

// Draws the frame's commands not drawn yet into the off-screen target: one pass, the frame's first clearing
// it. A frame is drawn in more than one pass when a copy of it is taken in its middle (CopyFrameTo), at the end of
// a world view with water (the copy the next frame's reflections read), and with multisampling one pass per run of
// world and other commands. The copies waiting for the interface's pictures go first.
static bool DrawPending() {
    perf::BeginDraws();
    perf::Enter(perf::Pass::Ui);
    RunCanvasCommands();
    const std::optional<Size> target = GameTargetSize();
    if (!target || !EnsureTarget(target->w, target->h, g_passes == 0)) return false;
    const int width = target->w, height = target->h;
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return true;
    perf::Enter(perf::Pass::Other);
    UploadTerrain();
    UploadMeshes(g_frame);
    UploadUi(g_frame);
    UploadOverlay(g_frame);
    // The sun's shadow map, the ambient occlusion and the water's sky image, once a frame before its first pass.
    if (!g_frame.shadow.prepared) {
        {
            Timer shadowTime;
            shadowTime.Begin();
            perf::Enter(perf::Pass::Sun);
            PrepareShadows(g_frame);
            DrawShadowMap(g_frame);
            DrawRoofMap(g_frame);
            perf::Enter(perf::Pass::Point);
            DrawPointShadows(g_frame);
            perf::AddCpu(perf::Cpu::Shadow, shadowTime.End());
        }
        perf::Enter(perf::Pass::Other);
        DrawOcclusion(g_frame);
        DrawSkyReflection(g_frame);
    }
    // Direct3D 7 puts pixel centres at integer screen coordinates, today's GPUs half a pixel further: the 3D
    // draws move their positions by half a pixel (Frame::pixelShift); the viewport is kept in whole pixels.
    g_frame.pixelShift[0] = 1.0f / width;
    g_frame.pixelShift[1] = -1.0f / height;
    std::size_t k = g_drawnCommands, end = g_frame.commands.size();
    bool multisampling = g_targetSamples > 1;
    std::size_t split = SIZE_MAX;   // the first world run ends here (SmoothOrder::Split)
    if (multisampling && g_smoothOrder == SmoothOrder::Before && g_holder == Holder::None && k < end && InWorld(k))
        EmptyFramePass();
    if (multisampling && g_smoothOrder == SmoothOrder::Split && !g_frame.worldRanges.empty()) {
        const Frame::Range& r = g_frame.worldRanges.front();
        std::size_t last = r.end < end ? r.end : end;
        if (last > r.first + 1) split = r.first + (last - r.first) / 2;
    }
    const std::size_t water = WaterCopyPoint();
    PlanMeshDraws(k, end, multisampling, split, water);
    if (water == k && g_holder != Holder::None) CopyFrameForWater(g_colour);
    do {
        bool world = multisampling && k < end && InWorld(k);
        std::size_t stop = k;
        while (stop < end && (multisampling && InWorld(stop)) == world && !(world && stop == split && stop > k) &&
               !(stop == water && stop > k))
            ++stop;
        // A pass's clear and copies are its first command's (the rest of the world in a world view), the
        // multisampled pass's resolve the world's.
        if (perf::On() && k < end) perf::Enter(InWorld(k) ? perf::Pass::Other : perf::Pass::Ui);
        if (world) BeginMultisampledPass();
        else BeginFramePass();
        DrawCommands(k, stop, width, height);
        if (world) perf::Enter(perf::Pass::Other);
        gpu::EndPass();
        g_passSamples = 1;
        g_passDepthCopy = false;
        ++g_passes;
        if (world && stop == split) EmptyFramePass();
        if (stop == water) CopyFrameForWater(g_colour);
        k = stop;
    } while (k < end);
    g_drawnCommands = end;
    return true;
}

// Nothing of this frame drawn or queued yet (a copy between frames: the quick save's key): the target still
// holds the last frame, as the original's render target did. A pass now would clear it first.
static bool BetweenFrames() { return g_haveFrame && g_passes == 0 && g_frame.commands.empty(); }

void Fatal(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("render: fatal: %s", text);
    ShowErrorBox(text);
    std::exit(-1);
}

Frame g_frame;
std::uint32_t FrameNumber() { return g_frameNumber; }
std::uint32_t CommitNumber() { return g_commits; }

const gpu::Texture& FrameTexture() { return g_colour; }
std::optional<Size> FrameSize() {
    if (!g_colour) return std::nullopt;
    return Size{g_width, g_height};
}

TextureDevice* DeviceObject() { return g_sceneRenderer.device; }

std::optional<Size> GameTargetSize() {
    const TextureDevice* device = DeviceObject();
    if (!device) return std::nullopt;
    const Size size{device->screen.right, device->screen.bottom};
    if (size.w <= 0 || size.h <= 0 || size.w > 16384 || size.h > 16384) return std::nullopt;
    return size;
}

int AddMatrix(const Matrix4& m) {
    if (!g_frame.matrices.empty() && std::memcmp(&g_frame.matrices.back(), &m, sizeof(Matrix4)) == 0)
        return static_cast<int>(g_frame.matrices.size()) - 1;
    g_frame.matrices.push_back(m);
    return static_cast<int>(g_frame.matrices.size()) - 1;
}

// The game's fog switch and colour, once per frame.
void ReadFog(Frame& f) {
    f.haveFog = true;
    f.fogEnabled = g_renderState.fog;
    f.fogColour = g_renderState.fogColour;
    f.distanceFog = g_renderState.distanceFog;
    f.mapEdge = g_renderState.mapEdge;
}

#if defined(EI_RENDER_VALIDATION)
bool ReadBackDue(long count) { return g_checkAll || count < kReadBackAlways || count % 16 == 0; }
#endif

void Configure(std::string_view spec) {
    std::string combined(spec);
    const std::string environment = platform::EnvironmentVariable("EI_RENDER");
    if (!environment.empty()) combined += (combined.empty() ? "" : ",") + environment;
    ParseSpec(combined);
}

// The pipelines the game always uses: with one sample for the frame target and the interface, with the world view's
// samples (the render quality's `msaa`, fewer as the device draws) for the multisampled runs.
void ListPipelines(const gpu::DeviceLimits& limits, int msaa, std::vector<gpu::PipelineDesc>& out) {
    ListMeshPipelines(out, 1);
    ListTerrainPipelines(out, 1);
    ListWaterPipelines(out);
    ListBlitPipelines(out, 1);
    ListUiPipelines(out, 1);
    ListShadowPipelines(out);
    ListOcclusionPipelines(out);
    ListComposePipelines(out);
    ListPresentPipelines(out);
    const int samples = SamplesFor(msaa, [&limits](gpu::Format colour, gpu::Format depth, int count) {
        return limits.SupportsSamples(colour, depth, count);
    });
    if (samples > 1) {
        // Each with the depth copy and without it (DepthCopyRead).
        const std::size_t first = out.size();
        ListMeshPipelines(out, samples);
        ListTerrainPipelines(out, samples);
        const std::size_t last = out.size();
        for (std::size_t k = first; k < last; ++k) {
            gpu::PipelineDesc without = out[k];
            DropDepthCopy(without);
            out.push_back(without);
        }
        ListSmoothPipelines(out, samples);
    }
}

void PrepareDevice(int msaa) {
    EnsureConfigured();
    if (g_started) return;
    msaa = MsaaLevel(msaa);
    gpu::PrepareInBackground({}, [msaa](const gpu::DeviceLimits& limits, std::vector<gpu::PipelineDesc>& out) {
        ListPipelines(limits, msaa, out);
    });
}

void Shutdown() { perf::Release(); }

void StartPresenting() { Start(); }

void SetMsaa(int samples) {
    const int level = MsaaLevel(samples);
    if (level != g_msaa) ei::Log("render: msaa %d (asked %d)", level, samples);
    g_msaa = level;
}

int Msaa() { return g_msaa; }

void SetAnisotropy(int level) {
    int clamped = level < 1 ? 1 : level > static_cast<int>(kMostAnisotropy) ? static_cast<int>(kMostAnisotropy) : level;
    if (clamped != g_anisotropy) ei::Log("render: anisotropy %d (asked %d)", clamped, level);
    g_anisotropy = clamped;
}

int Anisotropy() { return g_anisotropy; }

bool QualitySettingsSupported() {
#if defined(EI_RENDER_VALIDATION)
    return false;   // the harness build: its pinned settings file lacks the values; its steps set them
#else
    return true;
#endif
}

void BeginWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!ranges.empty() && ranges.back().end == SIZE_MAX) return;
    ranges.push_back(Frame::Range{g_frame.commands.size(), SIZE_MAX});
}

void EndWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!ranges.empty() && ranges.back().end == SIZE_MAX) ranges.back().end = g_frame.commands.size();
}

void ClearDepth(float z, const Rect* rects, int count) {
    g_stats.counts[FrameStats::Count::DepthClears] += rects ? static_cast<std::uint64_t>(count > 0 ? count : 0) : 1;
    if (!rects) {
        g_frame.clears.push_back(ClearBatch{z, Rect{0, 0, 0, 0}});
        g_frame.commands.push_back(Command{Command::Kind::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
        return;
    }
    for (int i = 0; i < count; ++i) {
        g_frame.clears.push_back(ClearBatch{z, rects[i]});
        g_frame.commands.push_back(Command{Command::Kind::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
    }
}

void SplitFrame() {
    if (!g_started || g_frameDrawn || BetweenFrames()) return;
    DrawPending();
}

void RunCanvasesNow() {
    Start();
    RunCanvasCommands();
}

void EndFrame() {
    if (g_frameDrawn) return;
    Start();
    g_frameDrawn = true;
    Timer timer(FrameStats::Time::EndFrameMs);
    Timer submitTime;
    submitTime.Begin();
    // Onto the game window through the swap chain.
    if (!DrawPending()) {
        perf::AddCpu(perf::Cpu::Submit, submitTime.End());
        return;
    }
    perf::Enter(perf::Pass::Present);
    bool toWindow = DrawToWindow();
    perf::EndDraws();
    gpu::Submit();
    perf::AddCpu(perf::Cpu::Submit, submitTime.End());
    ++g_commits;
    if (toWindow) PresentWindow();
    g_haveFrame = true;
}

// A drawn frame done with: its stats counted, each part's per-frame state let go; a new frame begins.
static void NextFrame() {
    perf::FrameDone();
    AddFrameStats(g_frame.matrices.size(), g_width, g_height);
    g_drawnCommands = 0;
    g_passes = 0;
    g_frame.Clear();
    TerrainFrameDone();
    MeshCacheFrameDone();
    UiFrameDone();
    OverlayFrameDone();
    ShadowFrameDone();
    OcclusionFrameDone();
    g_frameDrawn = false;
    g_holder = Holder::None;
    g_frameDepthCopy.reset();
    g_lastCounts = g_counts;
    g_counts = SmoothInfo{};
    if (g_started) {
        ReleaseRetired();
        CheckFloatControl();
    }
    ++g_frameNumber;
}

void Present() {
    EndFrame();
    ShowFrameCursor();
    NextFrame();
}

void EndUnshownFrame() {
    if (!g_frameDrawn) {
        Start();
        g_frameDrawn = true;
        if (DrawPending()) {
            perf::EndDraws();
            gpu::Submit();
            ++g_commits;
            g_haveFrame = true;
        }
    }
    NextFrame();
}

int PassSampleCount() { return g_passSamples; }
bool PassDepthCopy() { return g_passDepthCopy; }

void SetSmoothOrder(SmoothOrder order) {
    static const char* const kNames[] = {"as the game draws", "an empty pass on the frame target before the world",
                                         "the world split in two at its middle command"};
    if (order != g_smoothOrder) ei::Log("render: the multisampled passes: %s", kNames[static_cast<int>(order)]);
    g_smoothOrder = order;
}

SmoothInfo SmoothState() {
    SmoothInfo info = g_lastCounts;
    info.msaa = g_msaa;
    info.anisotropy = g_anisotropy;
    info.deviceSamples = g_deviceSamples;
    info.targetSamples = g_targetSamples;
    return info;
}

std::optional<FrameImage> ReadFrame() {
    if (!g_started || !g_haveFrame || !g_colour) return std::nullopt;
    const int w = g_width, h = g_height;
    gpu::Ticket copy = gpu::Download(g_colour, 0, Rect{0, 0, w, h});
    gpu::Texels texels;
    std::optional<FrameImage> frame;
    if (gpu::Wait(copy) && gpu::Read(copy, &texels)) {
        frame.emplace(FrameImage{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 3)});
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* row = texels.Row(y);
            std::uint8_t* out = frame->bgr.data() + static_cast<std::size_t>(y) * w * 3;
            for (int x = 0; x < w; ++x) {
                out[x * 3 + 0] = row[x * 4 + 2];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 0];
            }
        }
    }
    gpu::Release(copy);
    return frame;
}

} // namespace render
