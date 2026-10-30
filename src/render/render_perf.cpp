// The performance overlay (render_perf.h, docs/notes/renderer.md section 15): what a frame costs, measured
// while the game runs and drawn over the presented frame. Off unless switched on (render::SetPerfOverlay); when
// off every entry point returns at once.
#include "render_perf.h"

#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cstdio>
#include <deque>

namespace render {

namespace perf {

namespace {

using namespace detail;

bool g_on = false;

// One second of samples: each with the time it was taken (Ticks).
template <int N>
struct Sample {
    long long when = 0;
    double values[N] = {};
};

template <int N>
void Keep(std::deque<Sample<N>>& samples, const Sample<N>& s) {
    samples.push_back(s);
    constexpr long long second = static_cast<long long>(1000.0 / kMsPerTick);
    while (!samples.empty() && s.when - samples.front().when > second) samples.pop_front();
}

// The frames: their interval since the last present, the CPU times, each pass's draws (FrameDraws + pass).
enum FrameValue { Interval, SubmitMs, ShadowCpuMs, FrameDraws, FrameValues = FrameDraws + kPasses };
std::deque<Sample<FrameValues>> g_frames;
long long g_lastPresent = 0;
double g_cpu[static_cast<int>(Cpu::Count)] = {};   // this frame's
double g_draws[kPasses] = {};                       // this frame's
std::uint64_t g_drawsCounted = 0;                   // FrameDrawCount when last given to a pass

// The GPU: the frame's draws and the present (GpuTotal), and each pass's (GpuPasses + pass).
enum GpuValue { GpuTotal, GpuPasses, GpuValues = GpuPasses + kPasses };
std::deque<Sample<GpuValues>> g_gpu;

// The frame's pass (between BeginGpuFrame and EndGpuFrame).
bool g_inFrame = false;
Pass g_pass = Pass::Ui;

// ---- Timestamp queries --------------------------------------------------------------------------------------

constexpr int kStamps = 64;   // a frame's; past them the last stamped pass takes the rest of the frame
constexpr int kSets = 5;      // frames in flight

struct FrameQueries {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* stamps[kStamps] = {};
    Pass passes[kStamps] = {};   // the pass from each stamp on
    int count = 0;               // the stamps taken
    bool pending = false;        // closed, its results not read yet
};
FrameQueries g_sets[kSets];
int g_open = -1;            // the set of the frame being drawn
bool g_queriesFailed = false;

bool MakeSet(FrameQueries& q) {
    ID3D11Device* device = D3DDevice();
    if (!device) return false;
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    if (FAILED(device->CreateQuery(&desc, &q.disjoint))) return false;
    desc.Query = D3D11_QUERY_TIMESTAMP;
    for (ID3D11Query*& s : q.stamps)
        if (FAILED(device->CreateQuery(&desc, &s))) return false;
    return true;
}

void ReleaseSet(FrameQueries& q) {
    if (q.disjoint) q.disjoint->Release();
    for (ID3D11Query*& s : q.stamps) {
        if (s) s->Release();
        s = nullptr;
    }
    q = FrameQueries{};
}

void Stamp(FrameQueries& q, Pass pass) {
    q.passes[q.count] = pass;
    D3DContext()->End(q.stamps[q.count]);
    ++q.count;
}

// The draws since the last change, to the pass they were made in.
void CountDraws() {
    const std::uint64_t now = FrameDrawCount();
    if (now >= g_drawsCounted) g_draws[static_cast<int>(g_pass)] += static_cast<double>(now - g_drawsCounted);
    g_drawsCounted = now;
}

// The closed sets the GPU finished, into the samples (never waiting: a set not ready stays for a later frame).
void Collect() {
    ID3D11DeviceContext* context = D3DContext();
    for (FrameQueries& q : g_sets) {
        if (!q.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
        if (context->GetData(q.disjoint, &d, sizeof(d), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 t[kStamps] = {};
        bool ready = true;
        for (int k = 0; k < q.count && ready; ++k)
            ready = context->GetData(q.stamps[k], &t[k], sizeof(t[k]), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!ready) continue;
        q.pending = false;
        if (d.Disjoint || d.Frequency == 0 || q.count < 2) continue;
        double ms = 1000.0 / static_cast<double>(d.Frequency);
        auto span = [&](int from, int to) { return t[to] > t[from] ? static_cast<double>(t[to] - t[from]) * ms : 0.0; };
        Sample<GpuValues> s;
        s.when = Ticks();
        s.values[GpuTotal] = span(0, q.count - 1);
        for (int k = 0; k + 1 < q.count; ++k) s.values[GpuPasses + static_cast<int>(q.passes[k])] += span(k, k + 1);
        Keep(g_gpu, s);
    }
}

// ---- The font ------------------------------------------------------------------------------------------------

struct Glyph {
    char c;
    const char* rows[kGlyphHeight];
};
// clang-format off
const Glyph kFont[] = {
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
};
// clang-format on

const Glyph* FindGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.c == c) return &g;
    return nullptr;
}

// ---- Drawing --------------------------------------------------------------------------------------------------

constexpr double kRefreshMs = 250.0;   // the text is made again this often

sg_shader g_shader{};
sg_pipeline g_pipeline{};
sg_sampler g_sampler{};
sg_image g_image{};
sg_view g_view{};
int g_imageWidth = 0, g_imageHeight = 0;
bool g_drawFailed = false;
std::vector<std::uint8_t> g_pixels;   // the text's image
int g_pixelsWidth = 0, g_pixelsHeight = 0;
bool g_pixelsChanged = false;
long long g_lastText = 0;
std::vector<std::string> g_lines;

struct OverlayVs {
    float rect[4];
    float target[4];
};
struct OverlayFs {
    float origin[4];
};

bool MakePipeline() {
    sg_shader_desc s{};
    SetBytecode(s, kPerfOverlayShader);
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(OverlayVs);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(OverlayFs);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    // The image is read with Load; sokol wants each texture in a pair.
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = "ei-perf-overlay";
    g_shader = sg_make_shader(&s);
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_BGRA8;   // the game window's back buffer (render_window.cpp)
    p.colors[0].blend.enabled = true;
    p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    p.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.label = "ei-perf-overlay";
    g_pipeline = sg_make_pipeline(&p);
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_NEAREST;
    d.mag_filter = SG_FILTER_NEAREST;
    d.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    d.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    d.label = "ei-perf-overlay";
    g_sampler = sg_make_sampler(&d);
    return sg_query_pipeline_state(g_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_sampler_state(g_sampler) == SG_RESOURCESTATE_VALID;
}

void DropImage() {
    if (g_view.id) sg_destroy_view(g_view);
    if (g_image.id) sg_destroy_image(g_image);
    g_view = sg_view{};
    g_image = sg_image{};
    g_imageWidth = g_imageHeight = 0;
}

// The text's image on the GPU, made again when its size changed.
bool UploadText() {
    if (g_pixels.empty()) return false;
    if (g_imageWidth != g_pixelsWidth || g_imageHeight != g_pixelsHeight) {
        DropImage();
        sg_image_desc image{};
        image.width = g_pixelsWidth;
        image.height = g_pixelsHeight;
        image.pixel_format = SG_PIXELFORMAT_R8;
        image.usage.dynamic_update = true;
        image.label = "ei-perf-overlay";
        g_image = sg_make_image(&image);
        if (sg_query_image_state(g_image) != SG_RESOURCESTATE_VALID) return false;
        sg_view_desc v{};
        v.texture.image = g_image;
        g_view = sg_make_view(&v);
        g_imageWidth = g_pixelsWidth;
        g_imageHeight = g_pixelsHeight;
        g_pixelsChanged = true;
    }
    if (g_pixelsChanged) {
        sg_image_data data{};
        data.mip_levels[0] = sg_range{g_pixels.data(), g_pixels.size()};
        sg_update_image(g_image, &data);
        g_pixelsChanged = false;
    }
    return true;
}

// The text made again from the last second's numbers (every kRefreshMs: readable, and cheap).
void RefreshText() {
    long long now = Ticks();
    if (!g_pixels.empty() && static_cast<double>(now - g_lastText) * kMsPerTick < kRefreshMs) return;
    g_lastText = now;
    std::vector<std::string> lines = Lines(Current());
    if (lines == g_lines && !g_pixels.empty()) return;
    g_lines = std::move(lines);
    Rasterise(g_lines, &g_pixels, &g_pixelsWidth, &g_pixelsHeight);
    g_pixelsChanged = true;
}

} // namespace

bool On() { return g_on; }

void SetOn(bool on) {
    if (on != g_on) ei::Log("render: the performance overlay %s", on ? "on" : "off");
    g_on = on;
}

void BeginGpuFrame() {
    if (!g_on || g_inFrame) return;
    g_inFrame = true;
    g_pass = Pass::Ui;
    g_drawsCounted = FrameDrawCount();
    if (g_queriesFailed || !D3DContext()) return;
    Collect();
    for (int k = 0; k < kSets; ++k) {
        FrameQueries& q = g_sets[k];
        if (q.pending) continue;
        if (!q.disjoint && !MakeSet(q)) {
            ReleaseSet(q);
            g_queriesFailed = true;
            ei::Log("render: the performance overlay has no GPU timestamps (CreateQuery failed)");
            return;
        }
        g_open = k;
        q.count = 0;
        D3DContext()->Begin(q.disjoint);
        Stamp(q, g_pass);
        return;
    }
}

void Enter(Pass pass) {
    if (!g_inFrame || pass == g_pass) return;
    CountDraws();
    g_pass = pass;
    if (g_open >= 0 && g_sets[g_open].count < kStamps - 1) Stamp(g_sets[g_open], pass);   // the last for EndGpuFrame
}

void EndGpuFrame() {
    if (!g_inFrame) return;
    CountDraws();
    g_inFrame = false;
    if (g_open < 0) return;
    FrameQueries& q = g_sets[g_open];
    Stamp(q, g_pass);
    D3DContext()->End(q.disjoint);
    q.pending = true;
    g_open = -1;
}

CpuTimer::CpuTimer(Cpu newPart) : part(newPart), start(g_on ? Ticks() : 0) {}

CpuTimer::~CpuTimer() { Stop(); }

void CpuTimer::Stop() {
    if (!start) return;
    g_cpu[static_cast<int>(part)] += static_cast<double>(Ticks() - start) * kMsPerTick;
    start = 0;
}

void FrameDone() {
    if (!g_on) return;
    long long now = Ticks();
    if (g_lastPresent) {
        Sample<FrameValues> s;
        s.when = now;
        s.values[Interval] = static_cast<double>(now - g_lastPresent) * kMsPerTick;
        s.values[SubmitMs] = g_cpu[static_cast<int>(Cpu::Submit)];
        s.values[ShadowCpuMs] = g_cpu[static_cast<int>(Cpu::Shadow)];
        for (int p = 0; p < kPasses; ++p) s.values[FrameDraws + p] = g_draws[p];
        Keep(g_frames, s);
    }
    g_lastPresent = now;
    for (double& c : g_cpu) c = 0.0;
    for (double& d : g_draws) d = 0.0;
}

void Draw(int width, int height, const Rect& placed) {
    if (!g_on || g_drawFailed || width <= 0 || height <= 0) return;
    if (!g_pipeline.id && !MakePipeline()) {
        g_drawFailed = true;
        ei::Log("render: the performance overlay's pipeline could not be made; no overlay");
        return;
    }
    RefreshText();
    if (!UploadText()) return;
    int scale = Scale(placed.bottom - placed.top);
    float left = static_cast<float>(placed.left + Margin(scale)), top = static_cast<float>(placed.top + Margin(scale));
    float right = left + (g_imageWidth * scale), bottom = top + (g_imageHeight * scale);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_pipeline(g_pipeline);
    sg_bindings b{};
    b.views[0] = g_view;
    b.samplers[0] = g_sampler;
    sg_apply_bindings(&b);
    OverlayVs vs{{left, top, right, bottom}, {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    OverlayFs fs{{left, top, static_cast<float>(scale), 0.0f}};
    sg_range fr{&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fr);
    sg_draw(0, 6, 1);
}

void Release() {
    for (FrameQueries& q : g_sets) ReleaseSet(q);
    g_open = -1;
    g_inFrame = false;
    g_queriesFailed = false;
    DropImage();
    if (g_pipeline.id) sg_destroy_pipeline(g_pipeline);
    if (g_shader.id) sg_destroy_shader(g_shader);
    if (g_sampler.id) sg_destroy_sampler(g_sampler);
    g_pipeline = sg_pipeline{};
    g_shader = sg_shader{};
    g_sampler = sg_sampler{};
    g_drawFailed = false;
    g_pixelsChanged = true;
}

Numbers Current() {
    Numbers n;
    if (!g_frames.empty()) {
        double sum[FrameValues] = {};
        for (const auto& s : g_frames) {
            for (int k = 0; k < FrameValues; ++k) sum[k] += s.values[k];
            n.worstFrameMs = std::max(n.worstFrameMs, s.values[Interval]);
        }
        float count = static_cast<float>(static_cast<int>(g_frames.size()));
        n.frameMs = sum[Interval] / count;
        n.fps = n.frameMs > 0.0 ? 1000.0 / n.frameMs : 0.0;
        n.submitMs = sum[SubmitMs] / count;
        n.shadowCpuMs = sum[ShadowCpuMs] / count;
        for (int p = 0; p < kPasses; ++p) n.draws[p] = sum[FrameDraws + p] / count;
    }
    if (!g_gpu.empty()) {
        double sum[GpuValues] = {};
        for (const auto& s : g_gpu) {
            for (int k = 0; k < GpuValues; ++k) sum[k] += s.values[k];
            n.worstGpuMs = std::max(n.worstGpuMs, s.values[GpuTotal]);
        }
        float count = static_cast<float>(static_cast<int>(g_gpu.size()));
        n.gpu = true;
        n.gpuMs = sum[GpuTotal] / count;
        for (int p = 0; p < kPasses; ++p) n.passMs[p] = sum[GpuPasses + p] / count;
    }
    return n;
}

std::vector<std::string> Lines(const Numbers& n) {
    auto ms = [&](Pass p) { return n.passMs[static_cast<int>(p)]; };
    auto draws = [&](Pass p) { return n.draws[static_cast<int>(p)]; };
    char line[128];
    std::vector<std::string> lines;
    std::snprintf(line, sizeof(line), "FPS %.0f   FRAME %.2f MS   WORST %.2f", n.fps, n.frameMs, n.worstFrameMs);
    lines.emplace_back(line);
    if (n.gpu) {
        std::snprintf(line, sizeof(line), "GPU %.2f MS   WORST %.2f", n.gpuMs, n.worstGpuMs);
        lines.emplace_back(line);
        std::snprintf(line, sizeof(line), "  SHADOW %.2f (SUN %.2f  POINT %.2f)", ms(Pass::Sun) + ms(Pass::Point),
                      ms(Pass::Sun), ms(Pass::Point));
        lines.emplace_back(line);
        std::snprintf(line, sizeof(line), "  WORLD %.2f (TERRAIN %.2f  OBJECTS %.2f  OTHER %.2f)",
                      ms(Pass::Terrain) + ms(Pass::Objects) + ms(Pass::Other), ms(Pass::Terrain), ms(Pass::Objects),
                      ms(Pass::Other));
        lines.emplace_back(line);
        std::snprintf(line, sizeof(line), "  UI %.2f   PRESENT %.2f", ms(Pass::Ui), ms(Pass::Present));
        lines.emplace_back(line);
    } else {
        lines.emplace_back("GPU -");
    }
    std::snprintf(line, sizeof(line), "CPU SUBMIT %.2f MS   SHADOW %.2f", n.submitMs, n.shadowCpuMs);
    lines.emplace_back(line);
    std::snprintf(line, sizeof(line), "DRAWS WORLD %.0f   SUN %.0f   POINT %.0f   UI %.0f",
                  draws(Pass::Terrain) + draws(Pass::Objects) + draws(Pass::Other), draws(Pass::Sun), draws(Pass::Point),
                  draws(Pass::Ui) + draws(Pass::Present));
    lines.emplace_back(line);
    return lines;
}

void Rasterise(const std::vector<std::string>& lines, std::vector<std::uint8_t>* pixels, int* width, int* height) {
    std::size_t columns = 1;
    for (const std::string& l : lines) columns = std::max(columns, l.size());
    int rows = static_cast<int>(lines.size());
    int w = 2 * kBorder + static_cast<int>(columns) * kCellWidth - (kCellWidth - kGlyphWidth);
    int h = 2 * kBorder + (rows > 0 ? rows * kCellHeight - (kCellHeight - kGlyphHeight) : 0);
    pixels->assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int r = 0; r < rows; ++r) {
        const std::string& l = lines[static_cast<std::size_t>(r)];
        for (std::size_t c = 0; c < l.size(); ++c) {
            const Glyph* g = FindGlyph(l[c]);
            if (!g) continue;
            std::uint8_t value = (g->c >= '0' && g->c <= '9') || g->c == '.' ? kValue : kLabel;
            int x0 = kBorder + static_cast<int>(c) * kCellWidth, y0 = kBorder + r * kCellHeight;
            for (int y = 0; y < kGlyphHeight; ++y)
                for (int x = 0; x < kGlyphWidth; ++x)
                    if (g->rows[y][x] == '#')
                        (*pixels)[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x0 + x)] = value;
        }
    }
    *width = w;
    *height = h;
}

bool HasGlyph(char c) { return FindGlyph(c) != nullptr; }

int Scale(int height) {
    int scale = (height + 270) / 540;
    return scale < 1 ? 1 : scale;
}

} // namespace perf

bool PerfOverlaySupported() {
#if defined(EI_RENDER_VALIDATION)
    return false;   // the harness build: no run, capture or frame check may hold it
#else
    return true;
#endif
}

void SetPerfOverlay(bool on) { perf::SetOn(on && PerfOverlaySupported()); }

} // namespace render
