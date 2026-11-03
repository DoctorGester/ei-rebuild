// ei_gpu_spike: a frame dump's world passes drawn again through sokol_gfx or SDL_GPU, timed (replay.h).
// ei_gpu_spike <dump.eigpu> [--api sokol|sdlgpu] [--driver direct3d12|vulkan] [--size WxH] [--frames N] [--vsync]
//              [--debug] [--out frame.bmp] [--reference reference.bmp]
#include "replay.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>

namespace gpu_spike {

namespace {

Program ProgramOf(const std::string& label) {
    for (int k = 0; k < kPrograms; ++k)
        if (label == kProgramNames[k]) return static_cast<Program>(k);
    return Program::Other;
}

const Image* ViewImage(const Plan& plan, sg_view v) {
    auto it = plan.views.find(v.id);
    if (it == plan.views.end()) return nullptr;
    const sg_view_desc& d = it->second->desc;
    for (sg_image i : {d.color_attachment.image, d.depth_stencil_attachment.image, d.texture.image}) {
        auto found = plan.images.find(i.id);
        if (i.id && found != plan.images.end()) return found->second;
    }
    return nullptr;
}

bool MakePlan(const Frame& frame, const Options& o, Plan* plan, std::string* error) {
    plan->frame = &frame;
    for (const Image& i : frame.images) plan->images[i.id] = &i;
    for (const Buffer& b : frame.buffers) plan->buffers[b.id] = &b;
    for (const Sampler& s : frame.samplers) plan->samplers[s.id] = &s;
    for (const View& v : frame.views) plan->views[v.id] = &v;
    for (const Shader& s : frame.shaders) plan->programs[s.id] = ProgramOf(s.label);
    std::map<std::uint32_t, const Pipeline*> pipelines;
    for (const Pipeline& p : frame.pipelines) pipelines[p.id] = &p;
    auto programOf = [&](std::uint32_t pipeline) {
        auto p = pipelines.find(pipeline);
        if (p == pipelines.end()) return Program::Other;
        auto s = plan->programs.find(p->second->desc.shader.id);
        return s == plan->programs.end() ? Program::Other : s->second;
    };
    // The frame target: the colour image the most terrain and mesh draws draw into.
    std::map<const Image*, int> drawsInto;
    const Image* target = nullptr;
    Program current = Program::Other;
    for (const Command& c : frame.commands) {
        if (c.kind == Record::BeginPass) target = ViewImage(*plan, c.pass.attachments.colors[0]);
        else if (c.kind == Record::ApplyPipeline) current = programOf(c.id);
        else if (c.kind == Record::Draw && target && (current == Program::Terrain || current == Program::Mesh))
            ++drawsInto[target];
    }
    for (const auto& [image, draws] : drawsInto)
        if (!plan->colour || draws > drawsInto[plan->colour]) plan->colour = image;
    if (!plan->colour) {
        *error = "no pass draws the terrain or the meshes";
        return false;
    }
    const int dumpWidth = plan->colour->desc.width, dumpHeight = plan->colour->desc.height;
    plan->width = o.width ? o.width : dumpWidth;
    plan->height = o.height ? o.height : dumpHeight;
    // Every pass but the window's, with the draws of the programs both paths have; passes left with none are
    // dropped.
    PlannedPass pass;
    bool inPass = false;
    PlannedDraw state;
    for (const Command& c : frame.commands) {
        switch (c.kind) {
        case Record::BeginPass: {
            pass = PlannedPass{};
            inPass = !c.pass.compute && c.pass.attachments.colors[0].id + c.pass.attachments.depth_stencil.id != 0;
            pass.action = c.pass.action;
            pass.colour = ViewImage(*plan, c.pass.attachments.colors[0]);
            pass.depth = ViewImage(*plan, c.pass.attachments.depth_stencil);
            pass.frameTarget = pass.colour == plan->colour;
            const Image* size = pass.colour ? pass.colour : pass.depth;
            pass.width = pass.frameTarget ? plan->width : size ? size->desc.width : 0;
            pass.height = pass.frameTarget ? plan->height : size ? size->desc.height : 0;
            state = PlannedDraw{};
            state.viewport[2] = size ? size->desc.width : 0;
            state.viewport[3] = size ? size->desc.height : 0;
            break;
        }
        case Record::Viewport: std::memcpy(state.viewport, c.args, sizeof(state.viewport)); break;
        case Record::Scissor:
            std::memcpy(state.scissor, c.args, sizeof(state.scissor));
            state.scissored = true;
            break;
        case Record::ApplyPipeline: {
            auto p = pipelines.find(c.id);
            state.pipeline = p == pipelines.end() ? nullptr : p->second;
            state.program = programOf(c.id);
            state.bindings = sg_bindings{};   // sokol takes a pipeline's bindings after it (none: a draw without)
            std::fill(std::begin(state.uniforms), std::end(state.uniforms), nullptr);
            break;
        }
        case Record::ApplyBindings: state.bindings = c.bindings; break;
        case Record::ApplyUniforms:
            if (c.args[0] >= 0 && c.args[0] < SG_MAX_UNIFORMBLOCK_BINDSLOTS) state.uniforms[c.args[0]] = &c.bytes;
            break;
        case Record::Draw:
            if (!inPass) break;
            if (state.program == Program::Other || !state.pipeline) {
                ++plan->skipped;
                break;
            }
            state.base = c.args[0];
            state.count = c.args[1];
            state.instances = c.args[2];
            state.baseVertex = c.args[3];
            state.baseInstance = c.args[4];
            pass.draws.push_back(state);
            ++plan->drawCount;
            break;
        case Record::EndPass:
            if (inPass && (!pass.draws.empty() || pass.frameTarget)) {
                if (pass.colour) plan->attachments[pass.colour->id] = true;
                if (pass.depth) plan->attachments[pass.depth->id] = true;
                if (pass.frameTarget && !plan->depth) plan->depth = pass.depth;
                plan->passes.push_back(std::move(pass));
            }
            inPass = false;
            break;
        default: break;
        }
    }
    return true;
}

double Mean(const std::vector<double>& v) {
    return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}
double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())))];
}

bool WriteBmp(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const int rowBytes = (width * 3 + 3) & ~3;
    const std::uint32_t pixels = static_cast<std::uint32_t>(rowBytes) * height;
    std::uint8_t header[54] = {'B', 'M'};
    auto put = [&](int at, std::uint32_t v) { std::memcpy(header + at, &v, 4); };
    put(2, 54 + pixels);
    put(10, 54);
    put(14, 40);
    put(18, static_cast<std::uint32_t>(width));
    put(22, static_cast<std::uint32_t>(height));
    header[26] = 1;
    header[28] = 24;
    put(34, pixels);
    std::fwrite(header, 1, sizeof(header), f);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(rowBytes));
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            row[x * 3] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    return std::fclose(f) == 0;
}

// The fraction of pixels whose channels differ by more than `tolerance`.
double Differing(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int tolerance) {
    if (a.size() != b.size() || a.empty()) return -1.0;
    std::size_t off = 0;
    for (std::size_t i = 0; i < a.size(); i += 4)
        for (int k = 0; k < 3; ++k)
            if (std::abs(a[i + k] - b[i + k]) > tolerance) {
                ++off;
                break;
            }
    return static_cast<double>(off) / static_cast<double>(a.size() / 4);
}

} // namespace

void ScaleRect(const Plan& plan, const PlannedPass& pass, const int in[4], int out[4]) {
    double sx = 1.0, sy = 1.0;
    if (pass.frameTarget) {
        sx = static_cast<double>(plan.width) / plan.colour->desc.width;
        sy = static_cast<double>(plan.height) / plan.colour->desc.height;
    }
    out[0] = static_cast<int>(std::lround(in[0] * sx));
    out[1] = static_cast<int>(std::lround(in[1] * sy));
    out[2] = static_cast<int>(std::lround((in[0] + in[2]) * sx)) - out[0];
    out[3] = static_cast<int>(std::lround((in[1] + in[3]) * sy)) - out[1];
}

const char* const kProgramNames[kPrograms] = {"terrain",        "mesh",          "solid",        "shadow_caster",
                                              "shadow_terrain", "shadow_reduce", "shadow_dilate"};

} // namespace gpu_spike

// The renderer's backend logs through ei::Log (the game's log.cpp is not linked): to stderr here.
namespace ei {
void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}
} // namespace ei

int main(int argc, char** argv) {
    using namespace gpu_spike;
    const Clock::time_point start = Clock::now();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: ei_gpu_spike <dump.eigpu> [--api sokol|sdlgpu] [--driver direct3d12|vulkan] "
                             "[--size WxH] [--frames N] [--vsync] [--debug] [--out frame.bmp] [--reference ref.bmp]\n");
        return 2;
    }
    Options o;
    std::string reference;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--api") o.api = next();
        else if (a == "--driver") o.driver = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--vsync") o.vsync = true;
        else if (a == "--debug") o.debug = true;
        else if (a == "--out") o.out = next();
        else if (a == "--reference") reference = next();
    }
    o.frames = std::clamp(o.frames, 1, 5000);
    Frame frame;
    std::string error;
    if (!ReadFrame(argv[1], &frame, &error)) {
        std::fprintf(stderr, "ei_gpu_spike: %s\n", error.c_str());
        return 1;
    }
    const Clock::time_point loaded = Clock::now();
    Plan plan;
    if (!MakePlan(frame, o, &plan, &error)) {
        std::fprintf(stderr, "ei_gpu_spike: %s\n", error.c_str());
        return 1;
    }
    std::printf("dump: %zu images, %zu buffers, %zu pipelines, %zu commands; read in %.1f ms\n", frame.images.size(),
                frame.buffers.size(), frame.pipelines.size(), frame.commands.size(), Ms(start, loaded));
    std::printf("plan: target %s %dx%d drawn at %dx%d, %zu passes, %d draws (%d of other programs left out)\n",
                plan.colour->label.c_str(), plan.colour->desc.width, plan.colour->desc.height, plan.width,
                plan.height, plan.passes.size(), plan.drawCount, plan.skipped);
    if (o.debug)
        for (const PlannedPass& pass : plan.passes) {
            std::printf("pass %s / %s %dx%d, %zu draws\n", pass.colour ? pass.colour->label.c_str() : "-",
                        pass.depth ? pass.depth->label.c_str() : "-", pass.width, pass.height, pass.draws.size());
            for (const PlannedDraw& d : pass.draws)
                if (d.program != Program::Mesh && d.program != Program::Terrain)
                    std::printf("  %s pipeline %u: %d from %d x%d (vertex %d, instance %d), vb %u+%d %u+%d ib %u+%d, "
                                "slot 2 %u+%d, view %u, viewport %d,%d %dx%d\n",
                                kProgramNames[static_cast<int>(d.program)], d.pipeline->id, d.count, d.base,
                                d.instances, d.baseVertex, d.baseInstance, d.bindings.vertex_buffers[0].id,
                                d.bindings.vertex_buffer_offsets[0], d.bindings.vertex_buffers[1].id,
                                d.bindings.vertex_buffer_offsets[1], d.bindings.index_buffer.id,
                                d.bindings.index_buffer_offset, d.bindings.vertex_buffers[2].id,
                                d.bindings.vertex_buffer_offsets[2], d.bindings.views[0].id, d.viewport[0],
                                d.viewport[1], d.viewport[2], d.viewport[3]);
        }
    if (!reference.empty() && !plan.colour->levels.empty()) {
        std::vector<std::uint8_t> rgba = plan.colour->levels[0];
        WriteBmp(reference, plan.colour->desc.width, plan.colour->desc.height, rgba);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "ei_gpu_spike: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Timings t;
    const Clock::time_point windowStart = Clock::now();
    SDL_WindowFlags flags = 0;
#ifndef _WIN32
    if (o.api == "sokol") flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_Window* window = SDL_CreateWindow("ei_gpu_spike", plan.width, plan.height, flags);
    if (!window) {
        std::fprintf(stderr, "ei_gpu_spike: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    t.window = Ms(windowStart, Clock::now());
    bool ok = o.api == "sdlgpu" ? RunSdlGpu(plan, o, window, &t, &error) : RunSokol(plan, o, window, &t, &error);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!ok) {
        std::fprintf(stderr, "ei_gpu_spike: %s\n", error.c_str());
        return 1;
    }
    std::printf("api: %s (%s)\n", o.api.c_str(), t.device.c_str());
    std::printf("startup ms: window %.1f, device %.1f, resources %.1f, pipelines %.1f, first frame %.1f; "
                "device to first frame %.1f\n",
                t.window, t.deviceMs, t.resourcesMs, t.pipelinesMs, t.firstFrameMs, t.startupMs);
    std::printf("frame ms (%zu frames%s): mean %.3f, median %.3f, p95 %.3f; cpu mean %.3f\n", t.frameMs.size(),
                o.vsync ? ", vsync" : ", no vsync", Mean(t.frameMs), Percentile(t.frameMs, 0.5),
                Percentile(t.frameMs, 0.95), Mean(t.cpuMs));
    std::printf("readback ms (%dx%d RGBA8): idle frame %.3f, frame drawn and read %.3f\n", plan.width, plan.height,
                t.readbackMs, t.readbackFrameMs);
    if (plan.width == plan.colour->desc.width && plan.height == plan.colour->desc.height &&
        !plan.colour->levels.empty())
        std::printf("against the dump's frame: %.4f of the pixels differ by more than 8 (other programs' draws are "
                    "left out of both)\n",
                    Differing(t.rgba, plan.colour->levels[0], 8));
    if (!o.out.empty() && !WriteBmp(o.out, plan.width, plan.height, t.rgba))
        std::fprintf(stderr, "ei_gpu_spike: cannot write %s\n", o.out.c_str());
    return 0;
}
