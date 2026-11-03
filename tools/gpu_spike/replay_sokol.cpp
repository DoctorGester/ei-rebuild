// ei_gpu_spike's sokol_gfx path (replay.h): the plan drawn as the game draws, on the platform's backend
// (platform/graphics.h: Direct3D 11 on Windows, OpenGL 4.5 elsewhere) with the generated GLSL programs.
#include "replay.h"

#include "platform/graphics.h"

#include <cstring>

// The generated programs' descriptions (shaders/shaders.h, compiled into ei_render by render_shaders.cpp).
const sg_shader_desc* terrain_shader_desc(sg_backend backend);
const sg_shader_desc* mesh_shader_desc(sg_backend backend);
const sg_shader_desc* solid_shader_desc(sg_backend backend);
const sg_shader_desc* shadow_caster_shader_desc(sg_backend backend);
const sg_shader_desc* shadow_terrain_shader_desc(sg_backend backend);
const sg_shader_desc* shadow_reduce_shader_desc(sg_backend backend);
const sg_shader_desc* shadow_dilate_shader_desc(sg_backend backend);
const sg_shader_desc* present_shader_desc(sg_backend backend);

namespace gpu_spike {

namespace {

const sg_shader_desc* ShaderDesc(Program p) {
    const sg_backend b = sg_query_backend();
    switch (p) {
    case Program::Terrain: return terrain_shader_desc(b);
    case Program::Mesh: return mesh_shader_desc(b);
    case Program::Solid: return solid_shader_desc(b);
    case Program::ShadowCaster: return shadow_caster_shader_desc(b);
    case Program::ShadowTerrain: return shadow_terrain_shader_desc(b);
    case Program::ShadowReduce: return shadow_reduce_shader_desc(b);
    case Program::ShadowDilate: return shadow_dilate_shader_desc(b);
    default: return nullptr;
    }
}

bool IsDepth(sg_pixel_format f) { return f == SG_PIXELFORMAT_DEPTH || f == SG_PIXELFORMAT_DEPTH_STENCIL; }

struct Scene {
    std::map<std::uint32_t, sg_image> images;
    std::map<std::uint32_t, sg_view> textures;      // by recorded texture view
    std::map<std::uint32_t, sg_view> attachments;   // by image
    std::map<std::uint32_t, sg_buffer> buffers;
    std::map<std::uint32_t, sg_sampler> samplers;
    std::map<std::uint32_t, sg_pipeline> pipelines;
    sg_shader shaders[kPrograms] = {};
    sg_pipeline present{};
    sg_image ramp{};
    sg_view rampView{}, frameView{};
    sg_sampler linear{};
};

void MakeImages(const Plan& plan, Scene& s) {
    for (const auto& [id, image] : plan.images) {
        sg_image_desc d = image->desc;
        d._start_canary = d._end_canary = 0;
        d.label = nullptr;
        d.sample_count = 1;
        d.usage = sg_image_usage{};
        d.data = {};
        const bool attachment = plan.attachments.count(id) != 0;
        if (attachment || IsDepth(d.pixel_format) || image->levels.empty()) {
            if (image == plan.colour || image == plan.depth) {
                d.width = plan.width;
                d.height = plan.height;
            }
            d.num_mipmaps = 1;
            if (IsDepth(d.pixel_format)) d.usage.depth_stencil_attachment = true;
            else d.usage.color_attachment = true;
        } else {
            d.usage.immutable = true;
            for (std::size_t k = 0; k < image->levels.size() && k < SG_MAX_MIPMAPS; ++k)
                d.data.mip_levels[k] = sg_range{image->levels[k].data(), image->levels[k].size()};
        }
        sg_image made = sg_make_image(&d);
        s.images[id] = made;
        if (d.usage.color_attachment || d.usage.depth_stencil_attachment) {
            sg_view_desc v{};
            if (d.usage.color_attachment) v.color_attachment.image = made;
            else v.depth_stencil_attachment.image = made;
            s.attachments[id] = sg_make_view(&v);
        }
    }
    for (const auto& [id, view] : plan.views) {
        const sg_texture_view_desc& t = view->desc.texture;
        if (!t.image.id || !s.images.count(t.image.id)) continue;
        sg_view_desc v{};
        v.texture = t;
        v.texture.image = s.images[t.image.id];
        s.textures[id] = sg_make_view(&v);
    }
}

void MakeBuffers(const Plan& plan, Scene& s) {
    for (const auto& [id, buffer] : plan.buffers) {
        sg_buffer_desc d{};
        d.usage.vertex_buffer = buffer->desc.usage.vertex_buffer;
        d.usage.index_buffer = buffer->desc.usage.index_buffer;
        d.usage.immutable = true;
        d.data = sg_range{buffer->bytes.data(), buffer->bytes.size()};
        s.buffers[id] = sg_make_buffer(&d);
    }
    for (const auto& [id, sampler] : plan.samplers) {
        sg_sampler_desc d = sampler->desc;
        d._start_canary = d._end_canary = 0;
        d.label = nullptr;
        s.samplers[id] = sg_make_sampler(&d);
    }
}

void MakePipelines(const Plan& plan, Scene& s, sg_pixel_format windowFormat) {
    for (int k = 0; k < kPrograms; ++k) s.shaders[k] = sg_make_shader(ShaderDesc(static_cast<Program>(k)));
    for (const PlannedPass& pass : plan.passes)
        for (const PlannedDraw& d : pass.draws) {
            if (s.pipelines.count(d.pipeline->id)) continue;
            sg_pipeline_desc p = d.pipeline->desc;
            p._start_canary = p._end_canary = 0;
            p.label = nullptr;
            p.shader = s.shaders[static_cast<int>(d.program)];
            p.sample_count = 1;
            s.pipelines[d.pipeline->id] = sg_make_pipeline(&p);
        }
    sg_pipeline_desc p{};
    p.shader = sg_make_shader(present_shader_desc(sg_query_backend()));
    p.colors[0].pixel_format = windowFormat;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    s.present = sg_make_pipeline(&p);
    std::uint8_t ramp[256 * 4];
    for (int i = 0; i < 256; ++i) ramp[i * 4] = ramp[i * 4 + 1] = ramp[i * 4 + 2] = ramp[i * 4 + 3] = static_cast<std::uint8_t>(i);
    sg_image_desc r{};
    r.width = 256;
    r.height = 1;
    r.pixel_format = SG_PIXELFORMAT_RGBA8;
    r.data.mip_levels[0] = SG_RANGE(ramp);
    s.ramp = sg_make_image(&r);
    sg_view_desc v{};
    v.texture.image = s.ramp;
    s.rampView = sg_make_view(&v);
    v.texture.image = s.images[plan.colour->id];
    s.frameView = sg_make_view(&v);
    sg_sampler_desc smp{};
    smp.min_filter = smp.mag_filter = SG_FILTER_NEAREST;
    smp.wrap_u = smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    s.linear = sg_make_sampler(&smp);
}

void DrawPasses(const Plan& plan, Scene& s) {
    for (const PlannedPass& pass : plan.passes) {
        sg_pass p{};
        p.action = pass.action;
        if (pass.colour) p.attachments.colors[0] = s.attachments[pass.colour->id];
        if (pass.depth) p.attachments.depth_stencil = s.attachments[pass.depth->id];
        sg_begin_pass(&p);
        std::uint32_t pipeline = 0;
        for (const PlannedDraw& d : pass.draws) {
            if (d.pipeline->id != pipeline) {
                pipeline = d.pipeline->id;
                sg_apply_pipeline(s.pipelines[pipeline]);
            }
            int r[4];
            ScaleRect(plan, pass, d.viewport, r);
            platform::graphics::ApplyViewport(r[0], r[1], r[2], r[3]);
            if (d.scissored) ScaleRect(plan, pass, d.scissor, r);
            else r[0] = r[1] = 0, r[2] = pass.width, r[3] = pass.height;
            platform::graphics::ApplyScissorRect(r[0], r[1], r[2], r[3]);
            sg_bindings b{};
            static const sg_bindings kNone{};
            const bool none = std::memcmp(&d.bindings, &kNone, sizeof(kNone)) == 0;
            for (int k = 0; k < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++k) {
                b.vertex_buffers[k] = s.buffers[d.bindings.vertex_buffers[k].id];
                b.vertex_buffer_offsets[k] = d.bindings.vertex_buffer_offsets[k];
            }
            if (d.bindings.index_buffer.id) {
                b.index_buffer = s.buffers[d.bindings.index_buffer.id];
                b.index_buffer_offset = d.bindings.index_buffer_offset;
            }
            for (int k = 0; k < SG_MAX_VIEW_BINDSLOTS; ++k)
                if (d.bindings.views[k].id) b.views[k] = s.textures[d.bindings.views[k].id];
            for (int k = 0; k < SG_MAX_SAMPLER_BINDSLOTS; ++k)
                if (d.bindings.samplers[k].id) b.samplers[k] = s.samplers[d.bindings.samplers[k].id];
            if (!none) sg_apply_bindings(&b);
            for (int k = 0; k < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++k)
                if (d.uniforms[k]) sg_apply_uniforms(k, sg_range{d.uniforms[k]->data(), d.uniforms[k]->size()});
            sg_draw_ex(d.base, d.count, d.instances, d.baseVertex, d.baseInstance);
        }
        sg_end_pass();
    }
}

bool Present(const Plan& plan, Scene& s, SDL_Window* window, bool vsync) {
    platform::NativeWindow native = window;
#ifdef _WIN32
    native = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
    platform::graphics::PresentMode mode;
    mode.vsync = vsync;
    sg_swapchain swapchain{};
    if (!platform::graphics::BeginDrawable(native, plan.width, plan.height, mode, &swapchain)) return false;
    sg_pass p{};
    p.swapchain = swapchain;
    p.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_begin_pass(&p);
    sg_apply_pipeline(s.present);
    sg_bindings b{};
    b.views[0] = s.frameView;
    b.views[1] = s.rampView;
    b.samplers[0] = s.linear;
    sg_apply_bindings(&b);
    const float source[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    sg_apply_uniforms(0, SG_RANGE(source));
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
    return platform::graphics::EndDrawable();
}

void ReadFrame(const Plan& plan, Scene& s, platform::graphics::Readback& rb, std::vector<std::uint8_t>* rgba) {
    if (!rb.Read(s.images[plan.colour->id], platform::Rect{0, 0, plan.width, plan.height})) return;
    rgba->resize(static_cast<std::size_t>(plan.width) * plan.height * 4);
    for (int y = 0; y < plan.height; ++y)
        std::memcpy(&(*rgba)[static_cast<std::size_t>(y) * plan.width * 4], rb.Row(y),
                    static_cast<std::size_t>(plan.width) * 4);
}

void Log(const char* tag, uint32_t level, uint32_t item, const char* message, uint32_t line, const char*, void*) {
    if (level <= 1) std::fprintf(stderr, "sokol %s %u: %s (line %u)\n", tag ? tag : "", item, message ? message : "", line);
}

} // namespace

bool RunSokol(const Plan& plan, const Options& o, SDL_Window* window, Timings* t, std::string* error) {
    const Clock::time_point start = Clock::now();
    sg_desc desc{};
    if (!platform::graphics::Start(&desc.environment)) {
        *error = platform::graphics::Error();
        return false;
    }
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    desc.environment.defaults.sample_count = 1;
    desc.image_pool_size = 16384;
    desc.view_pool_size = 16384;
    desc.buffer_pool_size = 1024;
    desc.pipeline_pool_size = 1024;
    desc.sampler_pool_size = 256;
    desc.logger.func = &Log;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        *error = "sg_setup failed";
        return false;
    }
    t->device = sg_query_backend() == SG_BACKEND_D3D11 ? "Direct3D 11" : "OpenGL 4.5";
    const Clock::time_point device = Clock::now();
    t->deviceMs = Ms(start, device);
    Scene s;
    MakeImages(plan, s);
    MakeBuffers(plan, s);
    const Clock::time_point resources = Clock::now();
    t->resourcesMs = Ms(device, resources);
    MakePipelines(plan, s, SG_PIXELFORMAT_BGRA8);
    const Clock::time_point pipelines = Clock::now();
    t->pipelinesMs = Ms(resources, pipelines);
    DrawPasses(plan, s);
    if (!Present(plan, s, window, o.vsync)) {
        *error = "no window drawable";
        return false;
    }
    platform::graphics::Readback rb;
    std::vector<std::uint8_t> scratch;
    ReadFrame(plan, s, rb, &scratch);   // the first frame on the screen and done
    const Clock::time_point first = Clock::now();
    t->firstFrameMs = Ms(pipelines, first);
    t->startupMs = Ms(start, first);
    Clock::time_point last = Clock::now();
    for (int f = 0; f < o.frames && Ms(start, last) < 40000.0; ++f) {
        SDL_PumpEvents();
        const Clock::time_point begin = Clock::now();
        DrawPasses(plan, s);
        if (!Present(plan, s, window, o.vsync)) break;
        const Clock::time_point now = Clock::now();
        t->cpuMs.push_back(Ms(begin, now));
        t->frameMs.push_back(Ms(last, now));
        last = now;
    }
    ReadFrame(plan, s, rb, &scratch);   // idle afterwards
    Clock::time_point a = Clock::now();
    ReadFrame(plan, s, rb, &t->rgba);
    t->readbackMs = Ms(a, Clock::now());
    a = Clock::now();
    DrawPasses(plan, s);
    sg_commit();
    ReadFrame(plan, s, rb, &scratch);
    t->readbackFrameMs = Ms(a, Clock::now());
    rb.Release();
    sg_shutdown();
    platform::graphics::Stop();
    return true;
}

} // namespace gpu_spike
