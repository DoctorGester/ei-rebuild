// The check of src/render/gpu.{h,cpp} (docs/notes/renderer.md, "gpu.h"; tools/build_checks.py runs it on Windows,
// ctest's port_gpu on Linux) on each GPU driver SDL_GPU has here: Direct3D 12 and Vulkan on Windows, Vulkan on Linux
// (lavapipe without a GPU). Per driver: texture writes and downloads in every format, waited for and asynchronous;
// writes between passes in order with the draws, in place and discarding; vertex, instance and index buffers at
// offsets; the depth test and the targets' orientation; bindings kept across pipelines; a mip level and a resolved
// multisampled target as targets; texture copies; the upload ring past its size and the burst submission; the pipeline
// and sampler caches; the swap chain in every present mode on a small window off the screen (under a second); the
// frame's GPU time; misused calls refused and logged.
//
// usage: ei_gpu_check [--require] [--driver NAME]   (--require: a driver that makes no device fails; else skipped)

#include "render/gpu.h"

#include "shaders/gpu_shaders.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ei {

// gpu.cpp's log: printed; counted, as each line is a problem gpu.cpp found.
int g_logLines = 0;
void Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    std::printf("gpu_check: log: %s\n", text);
    if (std::strncmp(text, "gpu: ", 5) == 0 && std::strstr(text, " device (") == nullptr) ++g_logLines;
}

} // namespace ei

namespace {

using namespace render;
namespace g = render::gpu;

int g_failures = 0;
int g_checks = 0;
std::string g_driver;
int g_misuses = 0;   // calls made to be refused: each one of gpu.cpp's problems

void Fail(const char* format, ...) SDL_PRINTF_VARARG_FUNC(1);
void Fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::printf("gpu_check: %s: FAIL: ", g_driver.c_str());
    std::vprintf(format, args);
    std::fputc('\n', stdout);
    va_end(args);
    ++g_failures;
}

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) Fail("%s", what);
}

// ---- Texels -------------------------------------------------------------------------------------------------------

// A deterministic byte pattern: texel (x, y)'s byte k.
std::uint8_t PatternByte(int x, int y, int k, int seed) {
    return static_cast<std::uint8_t>((x * 7 + y * 13 + k * 29 + seed * 31) & 0xFF);
}

// `rows` x `rowBytes` bytes, `pitch` apart (more than rowBytes: the source's own padding).
std::vector<std::uint8_t> Pattern(int rowBytes, int rows, int pitch, int seed) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(pitch) * rows, 0xEE);
    for (int y = 0; y < rows; ++y)
        for (int b = 0; b < rowBytes; ++b) out[static_cast<std::size_t>(y) * pitch + b] = PatternByte(b, y, 0, seed);
    return out;
}

// Waits for `ticket` and compares its rows with `expected` (`pitch` apart); the ticket released.
bool SameRows(g::Ticket& ticket, const std::uint8_t* expected, int rowBytes, int rows, int pitch, const char* what) {
    bool ok = g::Wait(ticket);
    g::Texels t;
    ok = ok && g::Read(ticket, &t);
    for (int y = 0; ok && y < rows; ++y)
        ok = std::memcmp(t.Row(y), expected + static_cast<std::size_t>(y) * pitch,
                         static_cast<std::size_t>(rowBytes)) == 0;
    g::Release(ticket);
    Check(ok, what);
    return ok;
}

// An RGBA8 texel of a waited download.
struct Rgba {
    int r, g, b, a;
};
Rgba TexelAt(const g::Texels& t, int x, int y) {
    const std::uint8_t* p = t.Row(y) + x * 4;
    return {p[0], p[1], p[2], p[3]};
}
bool Near(Rgba c, int r, int gr, int b) {
    return std::abs(c.r - r) <= 2 && std::abs(c.g - gr) <= 2 && std::abs(c.b - b) <= 2;
}

// ---- Pipelines ----------------------------------------------------------------------------------------------------

g::PipelineDesc SolidDesc(g::Format colour, g::Format depth, g::Compare compare, int samples = 1) {
    g::PipelineDesc d;
    d.program = gpu_shaders::solid::kIndex;
    d.colour = colour;
    d.depth = depth;
    d.depthCompare = compare;
    d.samples = samples;
    return d;
}

void Solid(const Rect& r, int width, int height, float z, float red, float green, float blue) {
    gpu_shaders::SolidParams p{};
    const float rect[4] = {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                           static_cast<float>(r.bottom)};
    std::memcpy(p.rect, rect, sizeof(rect));
    p.target[0] = static_cast<float>(width);
    p.target[1] = static_cast<float>(height);
    p.target[2] = z;
    p.colour[0] = red;
    p.colour[1] = green;
    p.colour[2] = blue;
    p.colour[3] = 1.0f;
    g::PushUniforms(g::Stage::Vertex, gpu_shaders::solid::kBlockSolidParams, p);
    g::Draw(0, 6);
}

// The casters' program: position and texture coordinates per vertex (buffer 0), the light matrix's rows and the cut
// per instance (buffer 1).
struct CasterVertex {
    float position[3];
    float uv[2];
};
struct CasterInstance {
    float rows[4][4];
    float cut[4];
};
static_assert(sizeof(CasterVertex) == 20 && sizeof(CasterInstance) == 80);

g::PipelineDesc CasterDesc() {
    namespace sc = gpu_shaders::shadow_caster;
    g::PipelineDesc d;
    d.program = sc::kIndex;
    d.bufferCount = 2;
    d.buffers[0] = {sizeof(CasterVertex), false};
    d.buffers[1] = {sizeof(CasterInstance), true};
    d.attributeCount = 7;
    d.attributes[0] = {sc::kAttrPos, 0, g::VertexFormat::Float3, 0};
    d.attributes[1] = {sc::kAttrUv, 0, g::VertexFormat::Float2, 12};
    d.attributes[2] = {sc::kAttrRow0, 1, g::VertexFormat::Float4, 0};
    d.attributes[3] = {sc::kAttrRow1, 1, g::VertexFormat::Float4, 16};
    d.attributes[4] = {sc::kAttrRow2, 1, g::VertexFormat::Float4, 32};
    d.attributes[5] = {sc::kAttrRow3, 1, g::VertexFormat::Float4, 48};
    d.attributes[6] = {sc::kAttrCut, 1, g::VertexFormat::Float4, 64};
    d.colour = g::Format::None;
    d.depth = g::Format::Depth32Float;
    d.depthCompare = g::Compare::LessEqual;
    d.depthWrite = true;
    return d;
}

CasterInstance AtDepth(float z) {
    CasterInstance c{};
    c.rows[0][0] = 1.0f;
    c.rows[1][1] = 1.0f;
    c.rows[2][3] = z;   // the depth is z for every corner (their z is 0)
    c.rows[3][3] = 1.0f;
    return c;
}

// A quad over the left (or right) half of the target in clip space.
void HalfQuad(bool left, CasterVertex out[4]) {
    const float x0 = left ? -1.0f : 0.0f, x1 = left ? 0.0f : 1.0f;
    const CasterVertex q[4] = {{{x0, -1.0f, 0.0f}, {0, 0}}, {{x1, -1.0f, 0.0f}, {1, 0}}, {{x1, 1.0f, 0.0f}, {1, 1}},
                               {{x0, 1.0f, 0.0f}, {0, 1}}};
    std::memcpy(out, q, sizeof(q));
}

// ---- The checks ---------------------------------------------------------------------------------------------------

void CheckTextures() {
    struct Case {
        g::Format format;
        int width, height;
        const char* name;
    };
    const Case cases[] = {{g::Format::Rgba8, 61, 37, "RGBA8"},        {g::Format::R8, 50, 9, "R8"},
                          {g::Format::R8Uint, 33, 5, "R8 uint"},       {g::Format::R16Uint, 20, 20, "R16 uint"},
                          {g::Format::Rgba32Float, 9, 7, "RGBA32F"},  {g::Format::Bc1, 16, 8, "BC1"},
                          {g::Format::Bc2, 8, 8, "BC2"},              {g::Format::Bc3, 12, 4, "BC3"}};
    for (const Case& c : cases) {
        const bool integer = c.format == g::Format::R8Uint || c.format == g::Format::R16Uint;
        const std::uint8_t use = integer ? g::kColourTarget : g::kSampled;   // an integer texture is not sampled
        if (!g::FormatSupported(c.format, use)) {
            std::printf("gpu_check: %s: %s textures not supported, left out\n", g_driver.c_str(), c.name);
            continue;
        }
        const bool compressed = c.format == g::Format::Bc1 || c.format == g::Format::Bc2 || c.format == g::Format::Bc3;
        const int block = c.format == g::Format::Bc1 ? 8 : 16;
        const int bytesPerTexel = c.format == g::Format::Rgba8 ? 4 : c.format == g::Format::R16Uint ? 2
                                  : c.format == g::Format::Rgba32Float                            ? 16
                                                                                                  : 1;
        auto rowBytes = [&](int w) { return compressed ? (w + 3) / 4 * block : w * bytesPerTexel; };
        auto rows = [&](int h) { return compressed ? (h + 3) / 4 : h; };
        g::Texture t = g::MakeTexture({c.width, c.height, 2, 1, c.format, use, c.name});
        Check(static_cast<bool>(t), "a texture made in each format");
        if (!t) continue;
        // Level 0 whole from a padded source; level 1 in two writes.
        const int pitch0 = rowBytes(c.width) + 12;
        std::vector<std::uint8_t> level0 = Pattern(rowBytes(c.width), rows(c.height), pitch0, 1);
        if (c.format == g::Format::Rgba32Float)   // finite floats: the bytes of small numbers
            for (std::size_t i = 0; i + 4 <= level0.size(); i += 4) {
                const float f = static_cast<float>(level0[i]) / 7.0f;
                std::memcpy(&level0[i], &f, 4);
            }
        Check(g::Write(t, 0, {0, 0, c.width, c.height}, level0.data(), static_cast<std::size_t>(pitch0)),
              "a texture's level written");
        const int w1 = std::max(1, c.width / 2), h1 = std::max(1, c.height / 2);
        const int pitch1 = rowBytes(w1);
        std::vector<std::uint8_t> level1 = Pattern(pitch1, rows(h1), pitch1, 2);
        const int top = compressed ? 0 : h1 / 2;
        if (compressed) {
            Check(g::Write(t, 1, {0, 0, w1, h1}, level1.data(), static_cast<std::size_t>(pitch1)), "level 1 written");
        } else {
            Check(g::Write(t, 1, {0, 0, w1, top}, level1.data(), static_cast<std::size_t>(pitch1)), "level 1's top");
            Check(g::Write(t, 1, {0, top, w1, h1}, level1.data() + static_cast<std::size_t>(top) * pitch1,
                           static_cast<std::size_t>(pitch1)),
                  "level 1's bottom");
        }
        g::Ticket d0 = g::Download(t, 0, {0, 0, c.width, c.height});
        g::Ticket d1 = g::Download(t, 1, {0, 0, w1, h1});
        Check(!g::Done(d0), "a download is not done before its commands are submitted");
        char what[96];
        std::snprintf(what, sizeof(what), "%s level 0 read back as written", c.name);
        SameRows(d0, level0.data(), rowBytes(c.width), rows(c.height), pitch0, what);
        std::snprintf(what, sizeof(what), "%s level 1 read back as written", c.name);
        SameRows(d1, level1.data(), pitch1, rows(h1), pitch1, what);
        if (!compressed) {
            // A region inside the level.
            g::Ticket part = g::Download(t, 0, {3, 2, c.width - 1, c.height - 1});
            std::snprintf(what, sizeof(what), "%s region read back", c.name);
            SameRows(part, level0.data() + 2 * pitch0 + 3 * bytesPerTexel, (c.width - 4) * bytesPerTexel,
                     c.height - 3, pitch0, what);
        }
        g::Release(t);
    }
}

void CheckAsyncDownload() {
    const int w = 40, h = 30;
    g::Texture t = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kSampled, "async"});
    std::vector<std::uint8_t> data = Pattern(w * 4, h, w * 4, 3);
    g::Write(t, 0, {0, 0, w, h}, data.data(), static_cast<std::size_t>(w) * 4);
    g::Ticket ticket = g::Download(t, 0, {0, 0, w, h});
    g::Texels texels;
    Check(!g::Read(ticket, &texels), "a download not done is not read");
    Check(g::Submit(), "a submission");
    // Polled as a frame would, never waiting.
    int polls = 0;
    const Uint64 start = SDL_GetTicks();
    while (!g::Done(ticket) && SDL_GetTicks() - start < 5000) {
        SDL_DelayNS(100000);
        ++polls;
    }
    Check(g::Done(ticket), "a submitted download is done without a wait");
    bool same = g::Read(ticket, &texels);
    for (int y = 0; same && y < h; ++y) same = std::memcmp(texels.Row(y), data.data() + y * w * 4, w * 4) == 0;
    Check(same, "the asynchronous download's texels");
    g::Release(ticket);
    Check(!ticket, "a released ticket is empty");
    g::Release(t);
    std::printf("gpu_check: %s: an asynchronous download done after %d polls\n", g_driver.c_str(), polls);
}

// The casters' draws (vertex, instance and index buffers at offsets), the depth test, the target's orientation and
// writes between passes, in place and discarding.
void CheckDrawsAndOrder() {
    const int w = 64, h = 64;
    g::Texture colour = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kColourTarget | g::kSampled, "colour"});
    g::Texture depth = g::MakeTexture({w, h, 1, 1, g::Format::Depth32Float, g::kDepthTarget, "depth"});
    g::Texture white = g::MakeTexture({1, 1, 1, 1, g::Format::Rgba8, g::kSampled, "white"});
    const std::uint32_t whiteTexel = 0xFFFFFFFFu;
    g::Write(white, 0, {0, 0, 1, 1}, &whiteTexel, 4);
    g::Buffer vertices = g::MakeBuffer(g::BufferUse::Vertex, 4 * sizeof(CasterVertex), "caster vertices");
    g::Buffer instances = g::MakeBuffer(g::BufferUse::Vertex, 2 * sizeof(CasterInstance), "caster instances");
    g::Buffer indices = g::MakeBuffer(g::BufferUse::Index, 8 * 4, "caster indices");
    CasterVertex quad[4];
    HalfQuad(true, quad);
    // The quad in two writes; the instances with a decoy first; the indices after two unused ones.
    g::Write(vertices, 0, quad, 2 * sizeof(CasterVertex));
    g::Write(vertices, 2 * sizeof(CasterVertex), quad + 2, 2 * sizeof(CasterVertex));
    const CasterInstance both[2] = {AtDepth(0.9f), AtDepth(0.25f)};
    g::Write(instances, 0, both, sizeof(both), g::Contents::Discard);
    const std::uint32_t index[8] = {7, 7, 0, 1, 2, 0, 2, 3};
    g::Write(indices, 0, index, sizeof(index));

    const g::Pipeline caster = g::MakePipeline(CasterDesc(), "caster");
    const g::Pipeline solidLess =
        g::MakePipeline(SolidDesc(g::Format::Rgba8, g::Format::Depth32Float, g::Compare::Less));
    const g::Pipeline solidAlways =
        g::MakePipeline(SolidDesc(g::Format::Rgba8, g::Format::Depth32Float, g::Compare::Always));
    Check(caster && solidLess && solidAlways, "the draws' pipelines made");
    const g::Sampler nearest = g::MakeSampler({g::Filter::Nearest, g::Filter::Nearest});
    auto drawCasters = [&] {
        g::PassDesc p;
        p.depth = {depth, 0, g::Load::Clear, 1.0f, true};
        g::BeginPass(p);
        g::ApplyPipeline(caster);
        const g::BufferBinding buffers[2] = {{vertices, 0}, {instances, 0}};
        g::BindVertexBuffers(0, buffers);
        g::BindIndexBuffer({indices, 8});
        const g::TextureBinding tex[1] = {{white, nearest}};
        g::BindTextures(g::Stage::Fragment, 0, tex);
        g::DrawIndexed(0, 6, 1, 1);   // instance 1 only
        g::EndPass();
    };
    auto colourPass = [&](g::Load load) {
        g::PassDesc p;
        p.colour.texture = colour;
        p.colour.load = load;
        p.depth = {depth, 0, g::Load::Keep, 1.0f, true};
        g::BeginPass(p);
    };
    // A: casters on the left at 0.25; red where the depth is beyond 0.5; green over the top-left quarter.
    drawCasters();
    colourPass(g::Load::Clear);
    g::ApplyPipeline(solidLess);
    Solid({0, 0, w, h}, w, h, 0.5f, 1, 0, 0);
    g::ApplyPipeline(solidAlways);
    Solid({0, 0, w / 2, h / 2}, w, h, 0.0f, 0, 1, 0);
    g::EndPass();
    g::Ticket a = g::Download(colour, 0, {0, 0, w, h});
    // B: the quad moved to the right half (the whole buffer written again, discarding); blue where the depth is beyond.
    HalfQuad(false, quad);
    g::Write(vertices, 0, quad, sizeof(quad), g::Contents::Discard);
    drawCasters();
    colourPass(g::Load::Keep);
    g::ApplyPipeline(solidLess);
    Solid({0, 0, w, h}, w, h, 0.5f, 0, 0, 1);
    g::EndPass();
    g::Ticket b = g::Download(colour, 0, {0, 0, w, h});
    // C: instance 1 moved to 0.9 in place: white everywhere.
    const CasterInstance far = AtDepth(0.9f);
    g::Write(instances, sizeof(CasterInstance), &far, sizeof(far));
    drawCasters();
    colourPass(g::Load::Keep);
    g::ApplyPipeline(solidLess);
    Solid({0, 0, w, h}, w, h, 0.5f, 1, 1, 1);
    g::EndPass();
    g::Ticket c = g::Download(colour, 0, {0, 0, w, h});

    struct Expect {
        int x, y, r, g, b;
    };
    auto verify = [&](g::Ticket& ticket, std::initializer_list<Expect> points, const char* what) {
        g::Texels t;
        bool ok = g::Wait(ticket) && g::Read(ticket, &t);
        for (const Expect& e : points)
            if (ok && !Near(TexelAt(t, e.x, e.y), e.r, e.g, e.b)) {
                const Rgba got = TexelAt(t, e.x, e.y);
                Fail("%s: (%d, %d) is %d %d %d, not %d %d %d", what, e.x, e.y, got.r, got.g, got.b, e.r, e.g, e.b);
                ok = false;
            }
        ++g_checks;
        if (!ok && !t.data) Fail("%s: not read back", what);
        g::Release(ticket);
    };
    verify(a, {{5, 5, 0, 255, 0}, {5, 60, 0, 0, 0}, {40, 5, 255, 0, 0}, {60, 60, 255, 0, 0}, {31, 40, 0, 0, 0},
               {32, 40, 255, 0, 0}},
           "casters, depth test and orientation (top-left green, bottom-left black, right red)");
    verify(b, {{5, 5, 0, 0, 255}, {5, 60, 0, 0, 255}, {40, 5, 255, 0, 0}, {60, 60, 255, 0, 0}},
           "a discarding write after a pass (left blue, right red)");
    verify(c, {{5, 5, 255, 255, 255}, {60, 60, 255, 255, 255}, {40, 20, 255, 255, 255}},
           "a write in place after a pass (all white)");
    g::Release(vertices);
    g::Release(instances);
    g::Release(indices);
    g::Release(white);
    g::Release(depth);
    g::Release(colour);
}

// The blit program's texture and uniforms in both stages, kept across a pipeline change.
void CheckBindings() {
    const int w = 24, h = 16;
    std::vector<std::uint8_t> data = Pattern(w * 4, h, w * 4, 5);
    for (int i = 3; i < w * h * 4; i += 4) data[static_cast<std::size_t>(i)] = 255;   // opaque: nothing cut out
    g::Texture source = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kSampled, "blit source"});
    g::Texture target = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kColourTarget, "blit target"});
    g::Write(source, 0, {0, 0, w, h}, data.data(), static_cast<std::size_t>(w) * 4);
    g::PipelineDesc desc;
    desc.program = gpu_shaders::blit::kIndex;
    desc.colourMask = g::kWriteRed;
    const g::Pipeline red = g::MakePipeline(desc, "blit red");
    desc.colourMask = g::kWriteGreen | g::kWriteBlue | g::kWriteAlpha;
    const g::Pipeline rest = g::MakePipeline(desc, "blit rest");
    Check(red && rest && red.pipeline != rest.pipeline, "two blit pipelines");
    g::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = g::Load::Clear;
    pass.colour.clear[3] = 0.0f;
    g::BeginPass(pass);
    g::ApplyPipeline(red);
    const g::TextureBinding tex[1] = {{source, g::MakeSampler({g::Filter::Nearest, g::Filter::Nearest})}};
    g::BindTextures(g::Stage::Fragment, gpu_shaders::blit::kSamplerTex, tex);
    const gpu_shaders::RectParams rect{{0, 0, static_cast<float>(w), static_cast<float>(h)},
                                       {static_cast<float>(w), static_cast<float>(h), 0, 0}};
    const gpu_shaders::BlitParams blit{{0, 0, 1, 1}, {0, 0, static_cast<float>(w), static_cast<float>(h)}};
    g::PushUniforms(g::Stage::Vertex, gpu_shaders::blit::kBlockRectParams, rect);
    g::PushUniforms(g::Stage::Fragment, gpu_shaders::blit::kBlockBlitParams, blit);
    g::Draw(0, 6);
    g::ApplyPipeline(rest);   // nothing bound or pushed again
    g::BindTextures(g::Stage::Fragment, gpu_shaders::blit::kSamplerTex, tex);
    g::PushUniforms(g::Stage::Fragment, gpu_shaders::blit::kBlockBlitParams, blit);
    g::Draw(0, 6);
    g::EndPass();
    g::Ticket ticket = g::Download(target, 0, {0, 0, w, h});
    SameRows(ticket, data.data(), w * 4, h, w * 4, "a blit in two pipelines, bindings kept across them");
    g::Release(source);
    g::Release(target);
}

// A mip level and a resolved multisampled target as targets; a texture copy.
void CheckTargets() {
    const int w = 32, h = 32;
    g::Texture levels = g::MakeTexture({w, h, 2, 1, g::Format::Rgba8, g::kColourTarget | g::kSampled, "levels"});
    std::vector<std::uint8_t> data = Pattern(w * 4, h, w * 4, 6);
    g::Write(levels, 0, {0, 0, w, h}, data.data(), static_cast<std::size_t>(w) * 4);
    g::PassDesc pass;
    pass.colour.texture = levels;
    pass.colour.level = 1;
    pass.colour.load = g::Load::Clear;
    const float clear[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    std::memcpy(pass.colour.clear, clear, sizeof(clear));
    g::BeginPass(pass);
    g::EndPass();
    std::vector<std::uint8_t> green(static_cast<std::size_t>(w / 2) * (h / 2) * 4);
    for (std::size_t i = 0; i < green.size(); i += 4) {
        green[i + 1] = 255;
        green[i + 3] = 255;
    }
    g::Ticket one = g::Download(levels, 1, {0, 0, w / 2, h / 2});
    SameRows(one, green.data(), w / 2 * 4, h / 2, w / 2 * 4, "level 1 cleared as a target");
    g::Ticket zero = g::Download(levels, 0, {0, 0, w, h});
    SameRows(zero, data.data(), w * 4, h, w * 4, "level 0 kept beside it");

    g::Texture copy = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kSampled, "copy"});
    Check(g::Copy(levels, 0, {4, 6, 20, 30}, copy, 0, 1, 2), "a texture copy recorded");
    g::Ticket copied = g::Download(copy, 0, {1, 2, 17, 26});
    SameRows(copied, data.data() + 6 * w * 4 + 4 * 4, 16 * 4, 24, w * 4, "a texture copy's texels");
    g::Release(copy);
    g::Release(levels);

    const int samples = g::MaxSamples(g::Format::Rgba8, g::Format::Depth32Float, 4);
    std::printf("gpu_check: %s: %d samples (of 8 asked: %d)\n", g_driver.c_str(), samples,
                g::MaxSamples(g::Format::Rgba8, g::Format::Depth32Float, 8));
    if (samples < 4) {
        Fail("no 4 samples for RGBA8 with D32F");
        return;
    }
    g::Texture multi = g::MakeTexture({w, h, 1, 4, g::Format::Rgba8, g::kColourTarget, "multisampled"});
    g::Texture multiDepth =
        g::MakeTexture({w, h, 1, 4, g::Format::Depth32Float, g::kDepthTarget, "multisampled depth"});
    g::Texture resolved = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kColourTarget | g::kSampled, "resolved"});
    g::PassDesc m;
    m.colour.texture = multi;
    m.colour.load = g::Load::Clear;
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    std::memcpy(m.colour.clear, blue, sizeof(blue));
    m.colour.store = false;
    m.colour.resolve = resolved;
    m.depth = {multiDepth, 0, g::Load::Clear, 1.0f, false};
    g::BeginPass(m);
    g::ApplyPipeline(g::MakePipeline(SolidDesc(g::Format::Rgba8, g::Format::Depth32Float, g::Compare::Less, 4)));
    Solid({0, 0, w / 2, h}, w, h, 0.5f, 0, 1, 0);
    g::EndPass();
    g::Ticket r = g::Download(resolved, 0, {0, 0, w, h});
    g::Texels t;
    const bool read = g::Wait(r) && g::Read(r, &t);
    Check(read && Near(TexelAt(t, 3, 3), 0, 255, 0) && Near(TexelAt(t, 28, 20), 0, 0, 255),
          "a multisampled target resolved (left green, right blue)");
    g::Release(r);
    Check(!g::Download(multi, 0, {0, 0, w, h}), "a multisampled texture is not downloaded");
    ++g_misuses;

    // A second colour target beside the first (the depth copy of render_smooth.cpp): R32F, resolved with it.
    if (g::MaxSamples(g::Format::R32Float, g::Format::None, 4) < 4) {
        Fail("no 4 samples for R32F");
    } else {
        g::Texture multiCopy = g::MakeTexture({w, h, 1, 4, g::Format::R32Float, g::kColourTarget, "multisampled copy"});
        g::Texture copyResolved =
            g::MakeTexture({w, h, 1, 1, g::Format::R32Float, g::kColourTarget | g::kSampled, "resolved copy"});
        m.second.texture = multiCopy;
        m.second.load = g::Load::Clear;
        m.second.clear[0] = 1.0f;
        m.second.store = false;
        m.second.resolve = copyResolved;
        g::BeginPass(m);
        g::PipelineDesc desc = SolidDesc(g::Format::Rgba8, g::Format::Depth32Float, g::Compare::Less, 4);
        desc.second = g::Format::R32Float;
        desc.secondMask = g::kWriteRed;
        g::ApplyPipeline(g::MakePipeline(desc));
        Solid({0, 0, w / 2, h}, w, h, 0.25f, 1, 0, 0);
        g::EndPass();
        g::Ticket c = g::Download(copyResolved, 0, {0, 0, w, h});
        g::Ticket r2 = g::Download(resolved, 0, {0, 0, w, h});
        bool ok = g::Wait(c) && g::Read(c, &t) && t.format == g::Format::R32Float;
        float left = 0, right = 0;
        if (ok) {
            std::memcpy(&left, t.Row(3) + 3 * 4, 4);
            std::memcpy(&right, t.Row(20) + 28 * 4, 4);
        }
        Check(ok && std::abs(left - 0.25f) < 1e-6f && right == 1.0f,
              "a second R32F target resolved beside the colour (left 0.25, right the clear)");
        ok = g::Wait(r2) && g::Read(r2, &t);
        Check(ok && Near(TexelAt(t, 3, 3), 255, 0, 0) && Near(TexelAt(t, 28, 20), 0, 0, 255),
              "the first target of a pass with two resolved as well");
        g::Release(c);
        g::Release(r2);
        g::Release(multiCopy);
        g::Release(copyResolved);
    }
    g::Release(multi);
    g::Release(multiDepth);
    g::Release(resolved);
}

// More bytes in one command buffer than the ring holds, a write larger than a quarter of it, and the burst's own
// submission; every texture read back at its first and last rows.
void CheckUploadRing() {
    const std::uint64_t before = g::SubmitCount();
    std::vector<g::Texture> textures;
    std::vector<std::vector<std::uint8_t>> datas;
    // 4 MB through the ring, 8 MB on their own, 0.8 MB through the ring: 77 MB in all.
    const int sizes[][2] = {{1024, 1024}, {2048, 1024}, {700, 300}};
    for (int k = 0; k < 18; ++k) {
        const int w = sizes[k % 3][0], h = sizes[k % 3][1];
        textures.push_back(g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kSampled, "ring"}));
        datas.push_back(Pattern(w * 4, h, w * 4, 10 + k));
        g::Write(textures.back(), 0, {0, 0, w, h}, datas.back().data(), static_cast<std::size_t>(w) * 4);
    }
    const std::uint64_t submitted = g::SubmitCount() - before;
    Check(submitted >= 1, "a burst of writes submitted on its own");
    bool same = true;
    for (std::size_t k = 0; k < textures.size(); ++k) {
        const int w = textures[k].width, h = textures[k].height;
        g::Ticket first = g::Download(textures[k], 0, {0, 0, w, 1});
        g::Ticket last = g::Download(textures[k], 0, {0, h - 1, w, h});
        g::Texels t;
        same = same && g::Wait(last) && g::Read(first, &t) && std::memcmp(t.Row(0), datas[k].data(), w * 4) == 0;
        same = same && g::Read(last, &t) &&
               std::memcmp(t.Row(0), datas[k].data() + static_cast<std::size_t>(h - 1) * w * 4, w * 4) == 0;
        g::Release(first);
        g::Release(last);
        g::Release(textures[k]);
    }
    Check(same, "every texture of the burst read back as written");
    std::printf("gpu_check: %s: %zu textures, %.0f MB written, %llu submissions\n", g_driver.c_str(), textures.size(),
                [&] {
                    double mb = 0;
                    for (const auto& d : datas) mb += static_cast<double>(d.size()) / (1 << 20);
                    return mb;
                }(),
                static_cast<unsigned long long>(submitted));
}

void CheckCaches() {
    const int before = g::PipelineCount();
    const g::PipelineDesc d = SolidDesc(g::Format::Rgba8, g::Format::None, g::Compare::Always);
    const g::Pipeline a = g::MakePipeline(d, "one");
    const g::Pipeline b = g::MakePipeline(d, "two");
    g::PipelineDesc other = d;
    other.blend.on = true;
    const g::Pipeline c = g::MakePipeline(other);
    Check(a && a.pipeline == b.pipeline && c && c.pipeline != a.pipeline && g::PipelineCount() - before <= 2,
          "the pipeline cache: one pipeline a description");
    g::SamplerDesc s;
    s.anisotropy = 8;
    s.mip = g::Filter::Linear;
    g::SamplerDesc compare;
    compare.compare = g::Compare::LessEqual;
    compare.u = compare.v = g::Wrap::Clamp;
    const g::Sampler s1 = g::MakeSampler(s), s2 = g::MakeSampler(s), s3 = g::MakeSampler(compare);
    Check(s1 && s1.sampler == s2.sampler && s3 && s3.sampler != s1.sampler, "the sampler cache, anisotropic, compare");
}

// What gpu.cpp refuses and logs: each counted as one problem.
void CheckMisuse() {
    const int before = g::Problems();
    g::Texture t = g::MakeTexture({8, 8, 1, 1, g::Format::Rgba8, g::kColourTarget, "misuse"});
    g::Buffer b = g::MakeBuffer(g::BufferUse::Vertex, 64);
    g::PassDesc p;
    p.colour.texture = t;
    p.colour.load = g::Load::Clear;
    g::BeginPass(p);
    const std::uint8_t bytes[16] = {};
    Check(!g::Write(b, 0, bytes, 16), "a write inside a pass refused");
    Check(!g::Download(t, 0, {0, 0, 8, 8}), "a download inside a pass refused");
    g::Draw(0, 3);   // no pipeline
    g::EndPass();
    Check(!g::Write(b, 60, bytes, 16), "a write past a buffer's end refused");
    g::PipelineDesc missing = CasterDesc();
    missing.attributeCount = 1;
    Check(!g::MakePipeline(missing), "a pipeline without the program's inputs refused");
    Check(!g::MakeTexture({4, 4, 1, 1, g::Format::R8Uint, g::kSampled, "sampled uint"}),
          "a sampled uint texture refused");
    Check(g::Problems() - before == 6, "each misuse counted once");
    g_misuses += 6;
    g::Release(b);
    g::Release(t);
}

void CheckSwapchain() {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "ei_gpu_check");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, -4000);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, -4000);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 64);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 48);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        Fail("no window: %s", SDL_GetError());
        return;
    }
    Check(g::ClaimWindow(window), "the window claimed");
    const g::Format format = g::SwapchainFormat();
    Check(format == g::Format::Bgra8 || format == g::Format::Rgba8, "the swap chain's format is 8-bit RGBA or BGRA");
    const g::Pipeline solid = g::MakePipeline(SolidDesc(format, g::Format::None, g::Compare::Always));
    const char* names[] = {"vsync", "immediate", "mailbox"};
    int acquired = 0, frames = 0;
    for (g::PresentMode mode : {g::PresentMode::Vsync, g::PresentMode::Immediate, g::PresentMode::Mailbox}) {
        const bool supported = g::PresentModeSupported(mode);
        const g::PresentMode set = g::SetPresentMode(mode);
        Check(supported ? set == mode : set != mode, "an unsupported present mode falls back, a supported one is set");
        std::printf("gpu_check: %s: present mode %s %s (set: %s)\n", g_driver.c_str(), names[static_cast<int>(mode)],
                    supported ? "supported" : "not supported", names[static_cast<int>(set)]);
        for (int k = 0; k < 4; ++k) {
            SDL_PumpEvents();
            g::Texture target;
            if (!g::AcquireSwapchain(&target)) {
                Fail("the swap chain's texture: %s", g::Error());
                break;
            }
            ++frames;
            if (target) {
                ++acquired;
                int pw = 0, ph = 0;
                SDL_GetWindowSizeInPixels(window, &pw, &ph);
                Check(target.width == pw && target.height == ph && target.format == format,
                      "the texture at the window's size, in the swap chain's format");
                g::PassDesc p;
                p.colour.texture = target;
                p.colour.load = g::Load::Clear;
                g::BeginPass(p);
                g::ApplyPipeline(solid);
                Solid({0, 0, 32, 24}, 64, 48, 0.0f, 1, 0, 1);
                g::EndPass();
                Check(!g::Download(target, 0, {0, 0, 8, 8}), "the swap chain's texture is not downloaded");
                ++g_misuses;
            }
            Check(g::Submit(), "a frame presented");
        }
    }
    Check(acquired > 0, "the swap chain gave textures to draw into");
    std::printf("gpu_check: %s: swap chain %s, %d of %d frames drawn into\n", g_driver.c_str(),
                format == g::Format::Bgra8 ? "BGRA8" : "RGBA8", acquired, frames);
    g::ReleaseWindow();
    SDL_DestroyWindow(window);
}

void CheckFrameTiming() {
    const int w = 512, h = 512;
    g::Texture target = g::MakeTexture({w, h, 1, 1, g::Format::Rgba8, g::kColourTarget | g::kSampled, "timed"});
    const g::Pipeline solid = g::MakePipeline(SolidDesc(g::Format::Rgba8, g::Format::None, g::Compare::Always));
    g::SetFrameTiming(true);
    std::vector<double> times;
    g::Ticket last;
    for (int frame = 0; frame < 12; ++frame) {
        g::PassDesc p;
        p.colour.texture = target;
        p.colour.load = g::Load::Clear;
        g::BeginPass(p);
        g::ApplyPipeline(solid);
        for (int k = 0; k < 200; ++k) Solid({0, 0, w, h}, w, h, 0.0f, k / 200.0f, 0, 0);
        g::EndPass();
        g::Release(last);
        last = g::Download(target, 0, {0, 0, 1, 1});
        g::Submit();
        SDL_DelayNS(2000000);
        double ms = 0;
        if (g::FrameGpuTime(&ms)) times.push_back(ms);
    }
    g::Wait(last);
    SDL_DelayNS(2000000);
    double ms = 0;
    if (g::FrameGpuTime(&ms)) times.push_back(ms);
    g::SetFrameTiming(false);
    g::Release(last);
    g::Release(target);
    bool sane = !times.empty();
    double most = 0;
    for (double t : times) {
        sane = sane && t >= 0.0 && t < 1000.0;
        most = std::max(most, t);
    }
    Check(sane, "frame GPU times come back from the fences");
    std::printf("gpu_check: %s: %zu frame GPU times, the longest %.2f ms\n", g_driver.c_str(), times.size(), most);
}

// Every check on `driver`; false when it made no device.
bool CheckDriver(const char* driver) {
    g_driver = driver;
    g::DeviceOptions options;
    options.driver = driver;
    if (!g::Start(options)) {
        std::printf("gpu_check: %s: no device (%s)\n", driver, g::Error());
        return false;
    }
    Check(std::strcmp(g::DriverName(), driver) == 0, "the driver asked for");
    const int failures = g_failures, checks = g_checks, logged = ei::g_logLines, problems = g::Problems();
    g_misuses = 0;
    CheckTextures();
    CheckAsyncDownload();
    CheckDrawsAndOrder();
    CheckBindings();
    CheckTargets();
    CheckUploadRing();
    CheckCaches();
    CheckSwapchain();
    CheckFrameTiming();
    CheckMisuse();
    Check(g::Problems() - problems == g_misuses && ei::g_logLines - logged == g_misuses,
          "no problem but the misuses, each logged");
    std::printf("gpu_check: %s (%s): %d checks, %d failed, %d pipelines\n", driver, g::DeviceName(),
                g_checks - checks, g_failures - failures, g::PipelineCount());
    g::Texture late = g::MakeTexture({4, 4, 1, 1, g::Format::Rgba8, g::kSampled, "late"});
    g::Stop();
    g::Release(late);   // after Stop: nothing done
    Check(!late && !g::Started(), "a handle released after Stop");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool require = false;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--require") == 0) {
            require = true;
        } else if (std::strcmp(argv[i], "--driver") == 0 && i + 1 < argc) {
            only = argv[++i];
        } else {
            std::printf("usage: ei_gpu_check [--require] [--driver NAME]\n");
            return 2;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("gpu_check: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef _WIN32
    const char* drivers[] = {"direct3d12", "vulkan"};
#else
    const char* drivers[] = {"vulkan"};
#endif
    int made = 0;
    for (const char* driver : drivers) {
        if (only && std::strcmp(only, driver) != 0) continue;
        if (CheckDriver(driver)) ++made;
        else if (require) Fail("no device, and --require");
    }
    SDL_Quit();
    std::printf("gpu_check: %d checks, %d failed, on %d drivers\n", g_checks, g_failures, made);
    return g_failures ? 1 : 0;
}
