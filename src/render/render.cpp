// The new renderer's frame: the switch, the device behind sokol_gfx (platform/graphics.h), the off-screen target the
// frame is drawn into (the game's resolution) and reading it back for captures. See render.h and
// docs/notes/renderer.md.
#include "render.h"
#include "render_internal.h"
#include "render_perf.h"
#include "render_ui.h"

#include "../hook/log.h"
#include "ei/hook.h"

#include "render_state.h"
#include "../game/texture.h"
#include "../game/app.h"
#include "../game/game_singletons.h"
#include "../game/model.h"
#include "platform/graphics.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace render {

namespace {

using namespace detail;

// --- The switch ------------------------------------------------------------------------------------

bool g_configured = false;
int g_msaa = 1;         // the render quality (SetMsaa, SetAnisotropy); not EI_RENDER's
int g_anisotropy = 1;
#if defined(EI_RENDER_VALIDATION)
bool g_checkAll = false;    // "checkall" (harness build only): every read-back check runs; else the first few of each kind, then every 16th
#endif

// EI_RENDER's words (render.h).
void ParseSpec(const char* spec) {
#if defined(EI_RENDER_VALIDATION)
    g_checkAll = false;
#endif
    std::string s = spec ? spec : "";
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        std::string word = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        while (!word.empty() && word.back() == ' ') word.pop_back();
        bool known = false;
#if defined(EI_RENDER_VALIDATION)
        if (word == "checkall") g_checkAll = known = true;
#endif
        if (!known && !word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word.c_str());
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    g_configured = true;
}

void EnsureConfigured() {
    if (g_configured) return;
    ParseSpec(platform::EnvironmentVariable("EI_RENDER").c_str());
}

// --- Game memory -----------------------------------------------------------------------------------

std::uint32_t GameClearColour() { return g_sceneRenderer.fogColour; }

sg_color FromD3DColour(std::uint32_t c) {
    return sg_color{((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
                    (c & 0xFF) / 255.0f, 1.0f};
}


int g_deviceSamples = 1;   // the most samples up to kSmoothSamples the device draws the frame with

void SokolLog(const char* tag, std::uint32_t level, std::uint32_t item, const char* message, std::uint32_t line,
              const char* /*file*/, void* /*user*/) {
    static const char* const kLevels[] = {"panic", "error", "warning", "info"};
    // Each message at most 10 times (a full pool would otherwise log every frame).
    static unsigned char counts[1024];
    if (level > 0 && item < 1024 && counts[item]++ >= 10) {
        counts[item] = 11;
        return;
    }
    ei::Log("render: %s %s %u (line %u): %s", tag ? tag : "sg", level < 4 ? kLevels[level] : "?", item, line,
            message ? message : "");
    // sokol cannot go on after a panic (it would abort): the game ends with the message.
    if (level == 0) Fatal("The renderer stopped: sokol_gfx panic %u (%s).", item, message ? message : "");
}

// Why the last CreateDevice failed (for the fatal error's message).
const char* g_deviceError = "";

bool CreateDevice() {
    sg_desc desc{};
    if (!platform::graphics::Start(&desc.environment)) {
        g_deviceError = platform::graphics::Error();
        return false;
    }
    desc.environment.defaults.color_format = kColourFormat;
    desc.environment.defaults.depth_format = kDepthFormat;
    desc.environment.defaults.sample_count = 1;
    desc.logger.func = &SokolLog;
    // The game's textures, canvases and the images replaced within a frame are far more than sokol's
    // default pools (128 images, 256 views); a full pool made sg_make_image fail.
    desc.image_pool_size = 16384;
    desc.view_pool_size = 16384;
    desc.buffer_pool_size = 512;
    desc.pipeline_pool_size = 1024;
    desc.sampler_pool_size = 256;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        g_deviceError = "sokol_gfx could not be set up on the device";
        ei::Log("render: %s", g_deviceError);
        platform::graphics::Stop();
        return false;
    }
    g_deviceSamples = platform::graphics::MaxSamples(kSmoothSamples);
    ProtectDevice();
    return true;
}

// --- The off-screen target ---------------------------------------------------------------------------

int g_width = 0, g_height = 0;
sg_image g_colour{}, g_depth{};
sg_view g_colourAttachment{}, g_colourTexture{}, g_depthAttachment{};
bool g_haveFrame = false;
// With multisampling (SetMsaa) the world's runs of commands draw into a multisampled target of
// g_targetSamples (render_smooth.cpp) whose colour each such pass resolves into g_colour; the frame
// target's depth has a texture view then (copied into the multisampled one before a world run).
int g_targetSamples = 1;
sg_image g_multiColour{}, g_multiDepth{};
sg_view g_multiColourAttachment{}, g_multiDepthAttachment{}, g_multiDepthTexture{}, g_resolveAttachment{}, g_depthTexture{};

void DestroyMultisampled() {
    for (sg_view* v : {&g_multiColourAttachment, &g_multiDepthAttachment, &g_multiDepthTexture, &g_resolveAttachment, &g_depthTexture}) {
        if (v->id) sg_destroy_view(*v);
        *v = sg_view{};
    }
    if (g_multiColour.id) sg_destroy_image(g_multiColour);
    if (g_multiDepth.id) sg_destroy_image(g_multiDepth);
    g_multiColour = g_multiDepth = sg_image{};
    g_targetSamples = 1;
}

void DestroyTarget() {
    DestroyMultisampled();
    if (g_colourAttachment.id) sg_destroy_view(g_colourAttachment);
    if (g_colourTexture.id) sg_destroy_view(g_colourTexture);
    if (g_depthAttachment.id) sg_destroy_view(g_depthAttachment);
    if (g_colour.id) sg_destroy_image(g_colour);
    if (g_depth.id) sg_destroy_image(g_depth);
    g_colourAttachment = g_colourTexture = g_depthAttachment = sg_view{};
    g_colour = g_depth = sg_image{};
    g_width = g_height = 0;
    g_haveFrame = false;
}

// The multisampled colour and depth of `samples`; false when the device refused them.
bool MakeMultisampled(int width, int height, int samples) {
    sg_image_desc colour{};
    colour.usage.color_attachment = true;
    colour.width = width;
    colour.height = height;
    colour.pixel_format = kColourFormat;
    colour.sample_count = samples;
    colour.label = "ei-frame-colour-multisampled";
    g_multiColour = sg_make_image(&colour);
    sg_image_desc depth{};
    depth.usage.depth_stencil_attachment = true;
    depth.width = width;
    depth.height = height;
    depth.pixel_format = kDepthFormat;
    depth.sample_count = samples;
    depth.label = "ei-frame-depth-multisampled";
    g_multiDepth = sg_make_image(&depth);
    sg_view_desc v{};
    v.color_attachment.image = g_multiColour;
    g_multiColourAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.depth_stencil_attachment.image = g_multiDepth;
    g_multiDepthAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_multiDepth;
    g_multiDepthTexture = sg_make_view(&v);
    v = sg_view_desc{};
    v.resolve_attachment.image = g_colour;
    g_resolveAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_depth;
    g_depthTexture = sg_make_view(&v);
    bool ok = sg_query_image_state(g_multiColour) == SG_RESOURCESTATE_VALID &&
              sg_query_image_state(g_multiDepth) == SG_RESOURCESTATE_VALID;
    for (sg_view view : {g_multiColourAttachment, g_multiDepthAttachment, g_multiDepthTexture, g_resolveAttachment, g_depthTexture})
        ok = ok && sg_query_view_state(view) == SG_RESOURCESTATE_VALID;
    ok = ok && PrepareSmoothCopies(samples);
    if (!ok) {
        DestroyMultisampled();
        return false;
    }
    g_targetSamples = samples;
    return true;
}

// The frame target of the game's size; the multisampled one beside it when the render quality asks for
// multisampling (SetMsaa: at most the device's samples). A change takes effect at a frame's first pass (`mayChange`).
bool EnsureTarget(int width, int height, bool mayChange) {
    int samples = g_msaa < g_deviceSamples ? g_msaa : g_deviceSamples;
    if (width == g_width && height == g_height && g_colour.id && (samples == g_targetSamples || !mayChange)) return true;
    DestroyTarget();
    sg_image_desc colour{};
    colour.usage.color_attachment = true;
    colour.usage.resolve_attachment = samples > 1;
    colour.width = width;
    colour.height = height;
    colour.pixel_format = kColourFormat;
    colour.sample_count = 1;
    colour.label = "ei-frame-colour";
    g_colour = sg_make_image(&colour);
    sg_image_desc depth{};
    depth.usage.depth_stencil_attachment = true;
    depth.width = width;
    depth.height = height;
    depth.pixel_format = kDepthFormat;
    depth.sample_count = 1;
    depth.label = "ei-frame-depth";
    g_depth = sg_make_image(&depth);
    sg_view_desc v{};
    v.color_attachment.image = g_colour;
    g_colourAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_colour;
    g_colourTexture = sg_make_view(&v);
    v = sg_view_desc{};
    v.depth_stencil_attachment.image = g_depth;
    g_depthAttachment = sg_make_view(&v);
    if (sg_query_image_state(g_colour) != SG_RESOURCESTATE_VALID || sg_query_image_state(g_depth) != SG_RESOURCESTATE_VALID) {
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

bool g_frameDrawn = false;
bool g_started = false;
std::uint32_t g_frameNumber = 0;
std::uint32_t g_commits = 0;

// The device, sokol and the pipelines at the first use. The game cannot run without them: a failure ends it.
void MakePipelines(const char* what) {
    if (!CreateTerrainShader()) Fatal("%s: the terrain shader could not be made.", what);
    if (!CreateMeshShader()) Fatal("%s: the mesh shader could not be made.", what);
    if (!CreateBlitPipeline()) Fatal("%s: the blit pipeline could not be made.", what);
}

} // namespace

void detail::Start() {
    EnsureConfigured();
    if (g_started) return;
    platform::FloatingPointGuard fpu;
    if (!CreateDevice()) Fatal("Can't start the renderer: %s.", g_deviceError);
    MakePipelines("Can't start the renderer");
    g_started = true;
}

namespace {

// --- A lost device -------------------------------------------------------------------------------------

bool g_simulateLoss = false;   // render::LoseDevice: taken at the end of the next presented frame
int g_losses = 0, g_recoveries = 0;
std::uint32_t g_lastLossCode = 0;   // the backend's code of the last loss (0: LoseDevice's)

// Everything made on the lost device is released (sokol's resources through each module, then sg_shutdown),
// the device with them; a new device gets sokol again and the pipelines. The frame target, the
// window's swap chain and textures are made again as the next frames ask for them (from the file levels kept for
// them), the interface canvases from the commands each keeps. No new device ends the game.
void RecreateDevice() {
    platform::FloatingPointGuard fpu;
    ++g_losses;
    std::size_t kept = 0, keptBytes = 0;
    DropCursorPresents();
    DropWindow();
    DestroyTarget();
    DestroyTerrain();
    DestroyMeshes();
    DropBlits();
    DropCanvases();
    DestroyCompose();
    DestroySmooth();
    DestroyShadows();
    perf::Release();
    DropTextures(&kept, &keptBytes);
    sg_shutdown();
    platform::graphics::Stop();
    ei::Log("render: making a new device (%zu textures to make again from %zu KB of their files)", kept,
            keptBytes / 1024);
    // A device right after a driver reset may not be available at once.
    bool made = false;
    for (int attempt = 0; attempt < 5 && !made; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        made = CreateDevice();
    }
    if (!made) Fatal("The device was lost and no new one could be made: %s.", g_deviceError);
    MakePipelines("The device was lost and made again");
    RemakeComposed();
    ++g_recoveries;
}

// The end of a presented frame (nothing of it holds sokol resources any more): a loss a call reported during the
// frame, one the device reports (e.g. while minimised, when nothing presents), or one asked for makes everything
// again.
void CheckDevice() {
    const std::optional<std::uint32_t> lost = platform::graphics::Lost();
    if (!lost && !g_simulateLoss) return;
    g_lastLossCode = lost ? *lost : 0;
    if (lost) ei::Log("render: the device was lost (%08X)", static_cast<unsigned>(*lost));
    else ei::Log("render: the device's loss asked for (render::LoseDevice)");
    g_simulateLoss = false;
    RecreateDevice();
}

#if defined(EI_RENDER_VALIDATION)
// The timer of the device mesh draw running (at most one), made in place so no static destructor comes with it.
alignas(detail::StatsTimer) unsigned char g_deviceMeshTimer[sizeof(detail::StatsTimer)];
bool g_deviceMeshTiming = false;
#endif

#if defined(EI_RENDER_VALIDATION)
struct GameTimer {
    alignas(detail::StatsTimer) unsigned char storage[sizeof(detail::StatsTimer)];
    bool running = false;
    void Begin(double FrameStats::* field) {
        if (running) return;
        new (storage) detail::StatsTimer(field);
        running = true;
    }
    bool End() {
        if (!running) return false;
        std::launder(reinterpret_cast<detail::StatsTimer*>(storage))->~StatsTimer();
        running = false;
        return true;
    }
};
GameTimer g_decalTimer, g_flatDrawTimer, g_shadowBuildTimer;
#endif

FrameStats g_statsSum;      // the presented frames since the last TakeFrameStats
std::uint32_t g_lastPipeline = 0, g_lastTexture = 0;   // ids applied last in this frame
#if defined(EI_RENDER_VALIDATION)
StatsTimer* g_innerTimer = nullptr;
// CountReuse: per key the hashes of the vertices' geometry (positions, texture coordinates) and
// colours, this frame's and the last; this frame's mesh geometry hashes.
struct VertexHashes {
    std::uint64_t geometry, colours;
};
std::unordered_map<std::uint64_t, VertexHashes> g_hashes, g_lastHashes;
std::unordered_set<std::uint64_t> g_frameGeometry;

std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i + 4 <= bytes; i += 4) {
        std::uint32_t word;
        std::memcpy(&word, p + i, 4);
        h = (h ^ word) * 0x100000001B3ull;
    }
    return h;
}
#endif

void AddFrameStats() {
    g_stats.matrices = g_frame.matrices.size();
    if (g_width > 0) {
        g_statsSum.width = g_width;
        g_statsSum.height = g_height;
    }
    FrameStats& s = g_statsSum;
    const FrameStats& f = g_stats;
    s.frames += 1;
    for (auto field : {&FrameStats::terrainPages, &FrameStats::terrainBatches, &FrameStats::meshes, &FrameStats::meshPasses,
                       &FrameStats::transformedDraws, &FrameStats::blits, &FrameStats::depthClears, &FrameStats::stateReads,
                       &FrameStats::flatMeshes, &FrameStats::flatVertexBytes, &FrameStats::matrices, &FrameStats::terrainVertexBytes, &FrameStats::terrainIndexBytes,
                       &FrameStats::meshVertexBytes, &FrameStats::meshIndexBytes, &FrameStats::uniformBytes, &FrameStats::passes,
                       &FrameStats::draws, &FrameStats::pipelineApplies, &FrameStats::pipelineChanges, &FrameStats::bindings,
                       &FrameStats::textureChanges, &FrameStats::morphMeshes,
                       &FrameStats::morphCorners, &FrameStats::decalPrints,
                       &FrameStats::decalVertices, &FrameStats::flatDraws,
                       &FrameStats::flatDrawVertices, &FrameStats::shadowBuilds})
        s.*field += f.*field;
    for (int k = 0; k < FrameStats::UploadKinds; ++k) {
        s.uploads[k] += f.uploads[k];
        s.uploadBytes[k] += f.uploadBytes[k];
    }
    for (int k = 0; k < FrameStats::ReuseKinds; ++k) {
        s.terrainReuse[k] += f.terrainReuse[k];
        s.meshReuse[k] += f.meshReuse[k];
    }
    s.sharedMeshes += f.sharedMeshes;
    s.handOverMs += f.handOverMs;
    s.terrainMs += f.terrainMs;
    s.textureMs += f.textureMs;
    s.endFrameMs += f.endFrameMs;
    s.statsMs += f.statsMs;
    s.deviceMeshMs += f.deviceMeshMs;
    for (auto field : {&FrameStats::decalPrintMs, &FrameStats::flatMs,
                       &FrameStats::shadowBuildMs})
        s.*field += f.*field;
    s.shadowDraws += f.shadowDraws;
    s.shadowMs += f.shadowMs;
    s.gpuFrames += f.gpuFrames;
    s.gpuShadowMs += f.gpuShadowMs;
    s.gpuFrameMs += f.gpuFrameMs;
    g_stats = FrameStats{};
    g_lastPipeline = g_lastTexture = 0;
#if defined(EI_RENDER_VALIDATION)
    g_lastHashes.swap(g_hashes);
    g_hashes.clear();
    g_frameGeometry.clear();
#endif
}

std::size_t g_drawnCommands = 0;   // commands of this frame already drawn into the target
int g_passes = 0;                  // passes drawn this frame
int g_passSamples = 1;             // the sample count of the pass being drawn (PassSampleCount)

// Which target holds the frame's latest pixels: none yet (the frame's first pass clears), the frame
// target, or the multisampled one (its colour also resolved into the frame target).
enum class Holder : std::uint8_t { None, Frame, Multisampled };
Holder g_holder = Holder::None;

SmoothInfo g_counts, g_lastCounts;

#if defined(EI_RENDER_VALIDATION)
SmoothOrder g_smoothOrder = SmoothOrder::Game;
#endif

bool InWorld(std::size_t k) {
    for (const Frame::Range& r : g_frame.worldRanges)
        if (k >= r.first && k < r.end) return true;
    return false;
}

// A pass on the frame target. After a multisampled run its colour is the resolve's; its depth is
// set from the multisampled one.
void BeginFramePass() {
    bool first = g_holder == Holder::None;
    bool afterWorld = g_holder == Holder::Multisampled;
    sg_pass pass{};
    pass.action.colors[0].load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = FromD3DColour(GameClearColour());
    pass.action.depth.load_action = first ? SG_LOADACTION_CLEAR : afterWorld ? SG_LOADACTION_DONTCARE : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = g_colourAttachment;
    pass.attachments.depth_stencil = g_depthAttachment;
    pass.label = "ei-frame";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    g_passSamples = 1;
    if (afterWorld) {
        CopyDepthFromMultisampled(g_multiDepthTexture);
        ++g_counts.depthCopiesBack;
    }
    g_holder = Holder::Frame;
}

// A pass on the multisampled target, resolved into the frame target at its end. After draws into the
// frame target their colour and depth are copied into it first (a pass of its own: the frame target
// is the resolve's destination and cannot be read in that pass).
void BeginMultisampledPass() {
    bool first = g_holder == Holder::None;
    if (g_holder == Holder::Frame) {
        sg_pass copy{};
        copy.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        copy.action.colors[0].store_action = SG_STOREACTION_STORE;
        copy.action.depth.load_action = SG_LOADACTION_DONTCARE;
        copy.action.depth.store_action = SG_STOREACTION_STORE;
        copy.attachments.colors[0] = g_multiColourAttachment;
        copy.attachments.depth_stencil = g_multiDepthAttachment;
        copy.label = "ei-frame-into-multisampled";
        sg_begin_pass(&copy);
        ++g_stats.passes;
        g_passSamples = g_targetSamples;
        CopyIntoMultisampled(g_colourTexture, g_depthTexture);
        sg_end_pass();
        ++g_counts.copiesInto;
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = FromD3DColour(GameClearColour());
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.depth.load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.attachments.colors[0] = g_multiColourAttachment;
    pass.attachments.resolves[0] = g_resolveAttachment;
    pass.attachments.depth_stencil = g_multiDepthAttachment;
    pass.label = "ei-frame-multisampled";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    g_passSamples = g_targetSamples;
    g_holder = Holder::Multisampled;
    ++g_counts.multisampledPasses;
}

#if defined(EI_RENDER_VALIDATION)
// An empty pass on the frame target (render::SetSmoothOrder's orders): the frame's first clears it;
// after a world run it takes the depth back from sample 0.
void EmptyFramePass() {
    BeginFramePass();
    sg_end_pass();
    ++g_passes;
}
#endif

// The performance overlay's pass of command `k` (render_perf.h).
perf::Pass OverlayPass(const Command& c, std::size_t k) {
    if (!InWorld(k)) return perf::Pass::Ui;
    if (c.kind == Command::Terrain) return perf::Pass::Terrain;
    if (c.kind == Command::Mesh) {
        const MeshBatch& b = g_frame.meshBatches[static_cast<std::size_t>(c.index)];
        if (b.model && !b.flat && b.caster) return perf::Pass::Objects;
    }
    return perf::Pass::Other;
}

void DrawCommands(std::size_t from, std::size_t to, int width, int height) {
    for (std::size_t k = from; k < to; ++k) {
        const Command& c = g_frame.commands[k];
        auto i = static_cast<std::size_t>(c.index);
        if (perf::On()) perf::Enter(OverlayPass(c, k));
        switch (c.kind) {
        case Command::Terrain: DrawTerrainBatch(g_frame, g_frame.terrainBatches[i]); break;
        case Command::Mesh: {
            const MeshBatch& batch = g_frame.meshBatches[i];
            // The render quality's anisotropy: the world's models sampled anisotropically at its level; the
            // interface's sprites and previews and the pre-transformed draws as the stage says.
            int anisotropy = batch.model && InWorld(k) ? g_anisotropy : 1;
            if (batch.model) ++(anisotropy > 1 ? g_counts.anisotropicModels : g_counts.plainModels);
            DrawMeshBatch(g_frame, batch, anisotropy);
            break;
        }
        case Command::Blit: DrawBlitBatch(g_frame, g_frame.blitBatches[i], width, height); break;
        case Command::ClearDepth: DrawClearDepth(g_frame.clears[i], width, height); break;
        case Command::Fill: DrawFill(g_frame.fills[i], width, height); break;
        case Command::Canvas: DrawCanvasBatch(g_frame.canvasBatches[i], width, height); break;
        }
    }
}

// Draws the frame's commands not drawn yet into the off-screen target: one pass, the frame's first clearing
// it. A frame is drawn in more than one pass when a copy of it is taken in its middle (CopyFrameTo), and with
// multisampling one pass per run of world and other commands. The interface canvases' queued drawing goes first.
bool DrawPending() {
    perf::BeginGpuFrame();
    perf::Enter(perf::Pass::Ui);
    RunCanvasCommands();
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height) || !EnsureTarget(width, height, g_passes == 0)) return false;
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return true;
    perf::Enter(perf::Pass::Other);
    UploadTerrain(g_frame);
    UploadMeshes(g_frame);
    // The sun's shadow map, once a frame before its first pass (the classic option: none).
    if (!g_frame.shadow.prepared) {
        perf::CpuTimer shadowTime(perf::Cpu::Shadow);
        perf::Enter(perf::Pass::Sun);
        PrepareShadows(g_frame);
        DrawShadowMap(g_frame);
    }
    // Direct3D 7 puts pixel centres at integer screen coordinates, Direct3D 11 half a pixel further: the 3D
    // draws move their positions by half a pixel (Frame::pixelShift); sokol takes whole pixels only.
    g_frame.pixelShift[0] = 1.0f / width;
    g_frame.pixelShift[1] = -1.0f / height;
    std::size_t k = g_drawnCommands, end = g_frame.commands.size();
    bool multisampling = g_targetSamples > 1;
    std::size_t split = SIZE_MAX;   // the first world run ends here (SmoothOrder::Split)
#if defined(EI_RENDER_VALIDATION)
    if (multisampling && g_smoothOrder == SmoothOrder::Before && g_holder == Holder::None && k < end && InWorld(k))
        EmptyFramePass();
    if (multisampling && g_smoothOrder == SmoothOrder::Split && !g_frame.worldRanges.empty()) {
        const Frame::Range& r = g_frame.worldRanges.front();
        std::size_t last = r.end < end ? r.end : end;
        if (last > r.first + 1) split = r.first + (last - r.first) / 2;
    }
#endif
    do {
        bool world = multisampling && k < end && InWorld(k);
        std::size_t stop = k;
        while (stop < end && (multisampling && InWorld(stop)) == world && !(world && stop == split && stop > k)) ++stop;
        // A pass's clear and copies are its first command's (the rest of the world in a world view), the
        // multisampled pass's resolve the world's.
        if (perf::On() && k < end) perf::Enter(InWorld(k) ? perf::Pass::Other : perf::Pass::Ui);
        if (world) BeginMultisampledPass();
        else BeginFramePass();
        DrawCommands(k, stop, width, height);
        if (world) perf::Enter(perf::Pass::Other);
        sg_end_pass();
        g_passSamples = 1;
        ++g_passes;
#if defined(EI_RENDER_VALIDATION)
        if (world && stop == split) EmptyFramePass();
#endif
        k = stop;
    } while (k < end);
    g_drawnCommands = end;
    CanvasBlitsDrawn();
    return true;
}

// DrawPending for a draw in the middle of the frame (CopyFrameTo). The cursor thread draws the frame target
// itself, so it takes none of it while a pass is in progress: the present's lock, no cursor presents until
// the frame's own present.
bool DrawPendingAlone() {
    BeginWindowPresent();
    bool drawn = DrawPending();
    EndWindowPresent(false);
    return drawn;
}

// Nothing of this frame drawn or queued yet (a copy between frames: the quick save's key): the target still
// holds the last frame, as the original's render target did. A pass now would clear it first.
bool BetweenFrames() { return g_haveFrame && g_passes == 0 && g_frame.commands.empty(); }

} // namespace

namespace detail {

void DeviceLossCounts(WindowPresentInfo* info) {
    info->deviceLosses = g_losses;
    info->deviceRecoveries = g_recoveries;
    info->lastLossCode = g_lastLossCode;
}

} // namespace detail

void LoseDevice() {
    if (!g_started) return;
    g_simulateLoss = true;
}

namespace detail {

void Fatal(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("render: fatal: %s", text);
    app::ShowMessageBox(text);
    std::exit(-1);
}

Frame g_frame;
std::uint32_t FrameNumber() { return g_frameNumber; }
std::uint32_t CommitNumber() { return g_commits; }

sg_image FrameImage() { return g_colour; }
sg_view FrameTexture() { return g_colourTexture; }
bool FrameSize(int* width, int* height) {
    *width = g_width;
    *height = g_height;
    return g_colour.id != 0;
}

TextureDevice* DeviceObject() { return g_sceneRenderer.device; }

// The size of the game's screen (the device object's).
bool GameTargetSize(int* width, int* height) {
    const TextureDevice* device = DeviceObject();
    if (!device) return false;
    *width = device->screen.right;
    *height = device->screen.bottom;
    return *width > 0 && *height > 0 && *width <= 16384 && *height <= 16384;
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
}

} // namespace detail

#if defined(EI_RENDER_VALIDATION)
namespace detail {
bool ReadBackDue(long count) { return g_checkAll || count < kReadBackAlways || count % 16 == 0; }
} // namespace detail
#endif

void Configure(const char* spec) {
    std::string combined = spec ? spec : "";
    const std::string environment = platform::EnvironmentVariable("EI_RENDER");
    if (!environment.empty()) combined += (combined.empty() ? "" : ",") + environment;
    ParseSpec(combined.c_str());
}

void PrepareDevice() {
    EnsureConfigured();
    if (!g_started) platform::graphics::PrepareInBackground();
}

void BeginDeviceMeshTime() {
#if defined(EI_RENDER_VALIDATION)
    if (g_deviceMeshTiming) return;
    new (g_deviceMeshTimer) detail::StatsTimer(&FrameStats::deviceMeshMs);
    g_deviceMeshTiming = true;
#endif
}
void EndDeviceMeshTime() {
#if defined(EI_RENDER_VALIDATION)
    if (!g_deviceMeshTiming) return;
    std::launder(reinterpret_cast<detail::StatsTimer*>(g_deviceMeshTimer))->~StatsTimer();
    g_deviceMeshTiming = false;
#endif
}
void BeginDecalTime() {
#if defined(EI_RENDER_VALIDATION)
    if (g_decalTimer.running) return;
    ++detail::g_stats.decalPrints;
    g_decalTimer.Begin(&FrameStats::decalPrintMs);
#endif
}
void EndDecalTime(int vertices) {
#if defined(EI_RENDER_VALIDATION)
    if (g_decalTimer.End()) detail::g_stats.decalVertices += static_cast<std::uint64_t>(vertices);
#else
    (void)vertices;
#endif
}
void BeginFlatDrawTime(int vertices) {
#if defined(EI_RENDER_VALIDATION)
    if (g_flatDrawTimer.running) return;
    ++detail::g_stats.flatDraws;
    detail::g_stats.flatDrawVertices += static_cast<std::uint64_t>(vertices);
    g_flatDrawTimer.Begin(&FrameStats::flatMs);
#else
    (void)vertices;
#endif
}
void EndFlatDrawTime() {
#if defined(EI_RENDER_VALIDATION)
    g_flatDrawTimer.End();
#endif
}
void BeginShadowBuildTime() {
#if defined(EI_RENDER_VALIDATION)
    if (g_shadowBuildTimer.running) return;
    ++detail::g_stats.shadowBuilds;
    g_shadowBuildTimer.Begin(&FrameStats::shadowBuildMs);
#endif
}
void EndShadowBuildTime() {
#if defined(EI_RENDER_VALIDATION)
    g_shadowBuildTimer.End();
#endif
}

void StartPresenting() { Start(); }

void SetMsaa(int samples) {
    int level = 1;   // the largest power of two up to `samples`, at most kSmoothSamples
    while (level * 2 <= samples && level * 2 <= kSmoothSamples) level *= 2;
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
    g_stats.depthClears += rects ? static_cast<std::uint64_t>(count > 0 ? count : 0) : 1;
    if (!rects) {
        g_frame.clears.push_back(ClearBatch{z, Rect{0, 0, 0, 0}});
        g_frame.commands.push_back(Command{Command::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
        return;
    }
    for (int i = 0; i < count; ++i) {
        g_frame.clears.push_back(ClearBatch{z, rects[i]});
        g_frame.commands.push_back(Command{Command::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
    }
}

// --- Frame statistics ----------------------------------------------------------------------------

namespace detail {

FrameStats g_stats;   // the frame being built

StatsTimer::StatsTimer(double FrameStats::* newField) : field(newField), start(0), outer(nullptr) {
#if defined(EI_RENDER_VALIDATION)
    start = Ticks();
    outer = g_innerTimer;
    g_innerTimer = this;
#endif
}

StatsTimer::~StatsTimer() {
#if defined(EI_RENDER_VALIDATION)
    double ms = static_cast<double>(Ticks() - start) * kMsPerTick;
    g_stats.*field += ms;
    if (outer) g_stats.*(outer->field) -= ms;      // the outer timer counts its own work only
    g_innerTimer = outer;
#endif
}

void CountPipeline(sg_pipeline pipeline) {
    ++g_stats.pipelineApplies;
    if (pipeline.id != g_lastPipeline) ++g_stats.pipelineChanges;
    g_lastPipeline = pipeline.id;
}

void CountBindings(const sg_bindings& b) {
    ++g_stats.bindings;
    if (b.views[0].id != g_lastTexture) ++g_stats.textureChanges;
    g_lastTexture = b.views[0].id;
}

void CountDraw(std::size_t uniformBytes) {
    ++g_stats.draws;
    g_stats.uniformBytes += uniformBytes;
}

void CountReuse([[maybe_unused]] bool terrain, [[maybe_unused]] const void* key, [[maybe_unused]] int page,
                [[maybe_unused]] const Vertex* vertices, [[maybe_unused]] std::size_t count) {
#if defined(EI_RENDER_VALIDATION)
    StatsTimer timer(&FrameStats::statsMs);
    VertexHashes h{0xCBF29CE484222325ull, 0xCBF29CE484222325ull};
    for (std::size_t i = 0; i < count; ++i) {
        const Vertex& v = vertices[i];
        h.geometry = Fnv(h.geometry, &v.position, sizeof(v.position));
        h.geometry = Fnv(h.geometry, &v.u, 2 * sizeof(float));
        h.colours = Fnv(h.colours, &v.colour, 2 * sizeof(std::uint32_t));
    }
    std::uint64_t id = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key)) << 8) ^
                       static_cast<std::uint64_t>(page) ^ (terrain ? 1ull << 63 : 0);
    auto last = g_lastHashes.find(id);
    FrameStats::Reuse r = last == g_lastHashes.end()            ? FrameStats::New
                          : last->second.geometry != h.geometry ? FrameStats::Moved
                          : last->second.colours != h.colours   ? FrameStats::Relit
                                                                : FrameStats::Same;
    ++(terrain ? g_stats.terrainReuse : g_stats.meshReuse)[r];
    g_hashes[id] = h;
    if (!terrain && !g_frameGeometry.insert(h.geometry ^ count).second) ++g_stats.sharedMeshes;
#endif
}

void CountUpload(FrameStats::Upload kind, std::size_t bytes) {
    ++g_stats.uploads[kind];
    g_stats.uploadBytes[kind] += bytes;
}

} // namespace detail

FrameStats TakeFrameStats() {
    FrameStats s = g_statsSum;
    g_statsSum = FrameStats{};
    return s;
}

namespace detail {

void SplitFrame() {
    if (!g_started || g_frameDrawn || BetweenFrames()) return;
    platform::FloatingPointGuard fpu;
    DrawPendingAlone();
}

void RunCanvasesNow() {
    Start();
    platform::FloatingPointGuard fpu;
    BeginWindowPresent();
    RunCanvasCommands();
    EndWindowPresent(false);
}

GpuWorkAlone::GpuWorkAlone() { BeginWindowPresent(); }

GpuWorkAlone::~GpuWorkAlone() { EndWindowPresent(false); }

} // namespace detail

std::uint64_t perf::FrameDrawCount() { return g_stats.draws + g_stats.shadowDraws; }

void EndFrame() {
    if (g_frameDrawn) return;
    Start();
    g_frameDrawn = true;
    platform::FloatingPointGuard fpu;
    StatsTimer timer(&FrameStats::endFrameMs);
    perf::CpuTimer submitTime(perf::Cpu::Submit);
    // Onto the game window through the renderer's swap chain, in turn with the cursor thread's presents, which
    // draw the frame as it is (the present draws the cursor over the frame).
    BeginWindowPresent();
    if (!DrawPending()) {
        EndWindowPresent(false);
        return;
    }
    perf::Enter(perf::Pass::Present);
    bool toWindow = DrawToWindow(g_frame.haveCursor ? &g_frame.cursor : nullptr);
    perf::EndGpuFrame();
    g_frame.haveCursor = false;
    ShadowFrameEnd();
    sg_commit();
    submitTime.Stop();   // the present's wait for the display is not the renderer's work
    ++g_commits;
    EndWindowPresent(toWindow && PresentWindow());
    g_haveFrame = true;
}

void Present() {
    EndFrame();
    perf::FrameDone();
    AddFrameStats();
    g_drawnCommands = 0;
    g_passes = 0;
    g_frame.matrices.clear();
    g_frame.terrainPieces.clear();
    g_frame.terrainBatches.clear();
    TerrainFrameDone();
    MeshCacheFrameDone();
    g_frame.meshVertices.clear();
    g_frame.meshIndices.clear();
    g_frame.meshBatches.clear();
    g_frame.meshLights.clear();
    g_frame.blitBatches.clear();
    g_frame.clears.clear();
    g_frame.fills.clear();
    g_frame.canvasBatches.clear();
    g_frame.commands.clear();
    g_frame.worldRanges.clear();
    g_frame.shadow.haveSun = false;
    g_frame.shadow.casters.clear();
    g_frame.shadow.prepared = false;
    g_frame.shadow.on = false;
    g_frame.shadow.lights.clear();
    ShadowFrameDone();
    g_frame.haveCursor = false;
    g_frame.haveFog = false;
    g_frameDrawn = false;
    g_holder = Holder::None;
    g_lastCounts = g_counts;
    g_counts = SmoothInfo{};
    if (g_started) {
        platform::FloatingPointGuard fpu;
        RetireTextures();
        RetireBuffers();
        RetireFrameImages();
        CheckDevice();
    }
    ++g_frameNumber;
}

namespace detail {
int PassSampleCount() { return g_passSamples; }
} // namespace detail

#if defined(EI_RENDER_VALIDATION)
void SetSmoothOrder(SmoothOrder order) {
    static const char* const kNames[] = {"as the game draws", "an empty pass on the frame target before the world",
                                         "the world split in two at its middle command"};
    if (order != g_smoothOrder) ei::Log("render: the multisampled passes: %s", kNames[static_cast<int>(order)]);
    g_smoothOrder = order;
}
#endif

SmoothInfo SmoothState() {
    SmoothInfo info = g_lastCounts;
    info.msaa = g_msaa;
    info.anisotropy = g_anisotropy;
    info.deviceSamples = g_deviceSamples;
    info.targetSamples = g_targetSamples;
    return info;
}

bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr) {
    if (!g_started || !g_haveFrame || !g_colour.id) return false;
    platform::FloatingPointGuard fpu;
    platform::graphics::Readback copy;
    const int w = g_width, h = g_height;
    const bool read = copy.Read(g_colour, platform::Rect{0, 0, w, h});
    if (read) {
        *width = w;
        *height = h;
        bgr->resize(static_cast<std::size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* row = copy.Row(y);
            std::uint8_t* out = bgr->data() + static_cast<std::size_t>(y) * w * 3;
            for (int x = 0; x < w; ++x) {
                out[x * 3 + 0] = row[x * 4 + 2];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 0];
            }
        }
    }
    copy.Release();
    return read;
}

} // namespace render
