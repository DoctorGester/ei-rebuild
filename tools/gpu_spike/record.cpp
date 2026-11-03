// The SDL_GPU spike's frame recorder (record.h): sokol_gfx's trace hooks, and Direct3D 11 copies for the read-back.
#include "record.h"

#include "dump.h"

#include "lib/vecmath.h"
#include "shaders/shaders.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include <cstdlib>
#include <set>
#include <string>
#include <unordered_map>

namespace gpu_spike {

namespace {

struct Recorder {
    std::unordered_map<std::uint32_t, Image> images;
    std::unordered_map<std::uint32_t, Buffer> buffers;
    std::unordered_map<std::uint32_t, Sampler> samplers;
    std::unordered_map<std::uint32_t, View> views;
    std::unordered_map<std::uint32_t, Shader> shaders;
    std::unordered_map<std::uint32_t, Pipeline> pipelines;
    std::vector<Command> commands;   // since the last commit
    std::string pending;             // the dump's name, empty: none
};
Recorder* g_recorder = nullptr;

std::string Label(const char* label) { return label ? label : ""; }

template <class T> void Strip(T& desc) { desc.label = nullptr; }

void MakeImage(const sg_image_desc* desc, sg_image result, void*) {
    Image i;
    i.id = result.id;
    i.desc = *desc;
    i.label = Label(desc->label);
    Strip(i.desc);
    i.desc.data = {};
    g_recorder->images[result.id] = std::move(i);
}
void MakeBuffer(const sg_buffer_desc* desc, sg_buffer result, void*) {
    Buffer b;
    b.id = result.id;
    b.desc = *desc;
    b.label = Label(desc->label);
    Strip(b.desc);
    b.desc.data = {};
    g_recorder->buffers[result.id] = std::move(b);
}
void MakeSampler(const sg_sampler_desc* desc, sg_sampler result, void*) {
    Sampler s{result.id, *desc};
    Strip(s.desc);
    g_recorder->samplers[result.id] = s;
}
void MakeView(const sg_view_desc* desc, sg_view result, void*) {
    View v{result.id, *desc};
    Strip(v.desc);
    g_recorder->views[result.id] = v;
}
// A shader is named by the program whose generated description it was made from (shaders/shaders.h).
struct Known {
    const char* name;
    const sg_shader_desc* (*desc)(sg_backend);
};
const Known kKnown[] = {
    {"terrain", &terrain_shader_desc},           {"mesh", &mesh_shader_desc},
    {"solid", &solid_shader_desc},               {"shadow_caster", &shadow_caster_shader_desc},
    {"shadow_terrain", &shadow_terrain_shader_desc}, {"shadow_reduce", &shadow_reduce_shader_desc},
    {"shadow_dilate", &shadow_dilate_shader_desc}, {"present", &present_shader_desc},
    {"blit", &blit_shader_desc},
};
void MakeShader(const sg_shader_desc* desc, sg_shader result, void*) {
    std::string name = Label(desc->label);
    for (const Known& k : kKnown) {
        const sg_shader_desc* d = k.desc(sg_query_backend());
        if (d && d->vertex_func.bytecode.ptr == desc->vertex_func.bytecode.ptr &&
            d->fragment_func.bytecode.ptr == desc->fragment_func.bytecode.ptr &&
            d->vertex_func.source == desc->vertex_func.source && d->fragment_func.source == desc->fragment_func.source)
            name = k.name;
    }
    g_recorder->shaders[result.id] = Shader{result.id, name};
}
void MakePipeline(const sg_pipeline_desc* desc, sg_pipeline result, void*) {
    Pipeline p;
    p.id = result.id;
    p.desc = *desc;
    p.label = Label(desc->label);
    Strip(p.desc);
    g_recorder->pipelines[result.id] = std::move(p);
}
void DestroyImage(sg_image i, void*) { g_recorder->images.erase(i.id); }
void DestroyBuffer(sg_buffer b, void*) { g_recorder->buffers.erase(b.id); }
void DestroySampler(sg_sampler s, void*) { g_recorder->samplers.erase(s.id); }
void DestroyView(sg_view v, void*) { g_recorder->views.erase(v.id); }
void DestroyShader(sg_shader s, void*) { g_recorder->shaders.erase(s.id); }
void DestroyPipeline(sg_pipeline p, void*) { g_recorder->pipelines.erase(p.id); }

void BeginPass(const sg_pass* pass, void*) {
    Command c;
    c.kind = Record::BeginPass;
    c.pass = *pass;
    c.label = Label(pass->label);
    c.pass.label = nullptr;
    c.pass.swapchain.d3d11 = {};
    g_recorder->commands.push_back(std::move(c));
}
void Rectangle(Record kind, int x, int y, int w, int h, bool topLeft) {
    Command c;
    c.kind = kind;
    c.args[0] = x;
    c.args[1] = y;
    c.args[2] = w;
    c.args[3] = h;
    c.flag = topLeft;
    g_recorder->commands.push_back(std::move(c));
}
void Viewport(int x, int y, int w, int h, bool topLeft, void*) { Rectangle(Record::Viewport, x, y, w, h, topLeft); }
void Scissor(int x, int y, int w, int h, bool topLeft, void*) { Rectangle(Record::Scissor, x, y, w, h, topLeft); }
void ApplyPipeline(sg_pipeline p, void*) {
    Command c;
    c.kind = Record::ApplyPipeline;
    c.id = p.id;
    g_recorder->commands.push_back(std::move(c));
}
void ApplyBindings(const sg_bindings* b, void*) {
    Command c;
    c.kind = Record::ApplyBindings;
    c.bindings = *b;
    g_recorder->commands.push_back(std::move(c));
}
void ApplyUniforms(int slot, const sg_range* data, void*) {
    Command c;
    c.kind = Record::ApplyUniforms;
    c.args[0] = slot;
    const auto* p = static_cast<const std::uint8_t*>(data->ptr);
    c.bytes.assign(p, p + data->size);
    g_recorder->commands.push_back(std::move(c));
}
void DrawEx(int base, int count, int instances, int baseVertex, int baseInstance, void*) {
    Command c;
    c.kind = Record::Draw;
    c.args[0] = base;
    c.args[1] = count;
    c.args[2] = instances;
    c.args[3] = baseVertex;
    c.args[4] = baseInstance;
    g_recorder->commands.push_back(std::move(c));
}
void Draw(int base, int count, int instances, void* user) { DrawEx(base, count, instances, 0, 0, user); }
void EndPass(void*) {
    Command c;
    c.kind = Record::EndPass;
    g_recorder->commands.push_back(std::move(c));
}

// ---- The read-back ------------------------------------------------------------------------------------------

ID3D11Device* Device() { return static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device())); }
ID3D11DeviceContext* Context() {
    return static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
}

// Each level's texels of every slice, rows packed.
void ReadImage(Image& image) {
    const sg_image_desc& d = image.desc;
    if (d.sample_count > 1 || d.type == SG_IMAGETYPE_3D) return;
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(sg_image{image.id}).tex2d));
    if (!texture) return;
    D3D11_TEXTURE2D_DESC td;
    texture->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(Device()->CreateTexture2D(&td, nullptr, &staging))) return;
    Context()->CopyResource(staging, texture);
    image.levels.resize(static_cast<std::size_t>(td.MipLevels));
    for (UINT level = 0; level < td.MipLevels; ++level) {
        const int w = (std::max)(1, d.width >> level), h = (std::max)(1, d.height >> level);
        const int rowBytes = sg_query_row_pitch(d.pixel_format, w, 1);
        const int rows = sg_query_surface_pitch(d.pixel_format, w, h, 1) / rowBytes;
        std::vector<std::uint8_t>& out = image.levels[level];
        out.resize(static_cast<std::size_t>(rowBytes) * rows * td.ArraySize);
        for (UINT slice = 0; slice < td.ArraySize; ++slice) {
            D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(Context()->Map(staging, D3D11CalcSubresource(level, slice, td.MipLevels), D3D11_MAP_READ, 0, &m)))
                continue;
            for (int y = 0; y < rows; ++y)
                std::memcpy(&out[(static_cast<std::size_t>(slice) * rows + y) * rowBytes],
                            static_cast<const std::uint8_t*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch,
                            static_cast<std::size_t>(rowBytes));
            Context()->Unmap(staging, D3D11CalcSubresource(level, slice, td.MipLevels));
        }
    }
    staging->Release();
}

void ReadBuffer(Buffer& buffer) {
    auto* b = static_cast<ID3D11Buffer*>(const_cast<void*>(sg_d3d11_query_buffer_info(sg_buffer{buffer.id}).buf));
    if (!b) return;
    D3D11_BUFFER_DESC bd;
    b->GetDesc(&bd);
    bd.Usage = D3D11_USAGE_STAGING;
    bd.BindFlags = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    ID3D11Buffer* staging = nullptr;
    if (FAILED(Device()->CreateBuffer(&bd, nullptr, &staging))) return;
    Context()->CopyResource(staging, b);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(Context()->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        const auto* p = static_cast<const std::uint8_t*>(m.pData);
        buffer.bytes.assign(p, p + bd.ByteWidth);
        Context()->Unmap(staging, 0);
    }
    staging->Release();
}

// The frame since the last commit, with what it uses.
void Dump(const std::string& name) {
    Recorder& r = *g_recorder;
    std::set<std::uint32_t> images, buffers, samplers, views, shaders, pipelines;
    auto view = [&](sg_view v) {
        if (!v.id || !r.views.count(v.id)) return;
        views.insert(v.id);
        const sg_view_desc& d = r.views[v.id].desc;
        for (sg_image i : {d.texture.image, d.color_attachment.image, d.resolve_attachment.image,
                           d.depth_stencil_attachment.image, d.storage_image.image})
            if (i.id) images.insert(i.id);
    };
    for (const Command& c : r.commands) {
        if (c.kind == Record::BeginPass) {
            for (const sg_view& v : c.pass.attachments.colors) view(v);
            for (const sg_view& v : c.pass.attachments.resolves) view(v);
            view(c.pass.attachments.depth_stencil);
        } else if (c.kind == Record::ApplyPipeline) {
            pipelines.insert(c.id);
            if (r.pipelines.count(c.id)) shaders.insert(r.pipelines[c.id].desc.shader.id);
        } else if (c.kind == Record::ApplyBindings) {
            for (const sg_buffer& b : c.bindings.vertex_buffers)
                if (b.id) buffers.insert(b.id);
            if (c.bindings.index_buffer.id) buffers.insert(c.bindings.index_buffer.id);
            for (const sg_view& v : c.bindings.views) view(v);
            for (const sg_sampler& s : c.bindings.samplers)
                if (s.id) samplers.insert(s.id);
        }
    }
    Frame frame;
    for (std::uint32_t id : shaders)
        if (r.shaders.count(id)) frame.shaders.push_back(r.shaders[id]);
    for (std::uint32_t id : images)
        if (r.images.count(id)) {
            Image i = r.images[id];
            ReadImage(i);
            frame.images.push_back(std::move(i));
        }
    for (std::uint32_t id : buffers)
        if (r.buffers.count(id)) {
            Buffer b = r.buffers[id];
            ReadBuffer(b);
            frame.buffers.push_back(std::move(b));
        }
    for (std::uint32_t id : samplers)
        if (r.samplers.count(id)) frame.samplers.push_back(r.samplers[id]);
    for (std::uint32_t id : views)
        if (r.views.count(id)) frame.views.push_back(r.views[id]);
    for (std::uint32_t id : pipelines)
        if (r.pipelines.count(id)) frame.pipelines.push_back(r.pipelines[id]);
    frame.commands = r.commands;
    const char* folder = std::getenv("EI_GPU_SPIKE_DUMP");
    const std::string path = std::string(folder) + "\\" + name + ".eigpu";
    CreateDirectoryA(folder, nullptr);
    WriteFrame(path.c_str(), frame);
}

void Commit(void*) {
    if (!g_recorder->pending.empty()) {
        Dump(g_recorder->pending);
        g_recorder->pending.clear();
    }
    g_recorder->commands.clear();
}

} // namespace

void InstallRecorder() {
    if (g_recorder) return;
    g_recorder = new Recorder;
    sg_trace_hooks h{};
    h.make_image = &MakeImage;
    h.make_buffer = &MakeBuffer;
    h.make_sampler = &MakeSampler;
    h.make_view = &MakeView;
    h.make_shader = &MakeShader;
    h.make_pipeline = &MakePipeline;
    h.destroy_image = &DestroyImage;
    h.destroy_buffer = &DestroyBuffer;
    h.destroy_sampler = &DestroySampler;
    h.destroy_view = &DestroyView;
    h.destroy_shader = &DestroyShader;
    h.destroy_pipeline = &DestroyPipeline;
    h.begin_pass = &BeginPass;
    h.apply_viewport = &Viewport;
    h.apply_scissor_rect = &Scissor;
    h.apply_pipeline = &ApplyPipeline;
    h.apply_bindings = &ApplyBindings;
    h.apply_uniforms = &ApplyUniforms;
    h.draw = &Draw;
    h.draw_ex = &DrawEx;
    h.end_pass = &EndPass;
    h.commit = &Commit;
    sg_install_trace_hooks(&h);
}

void RequestDump(const char* name) {
    if (g_recorder && std::getenv("EI_GPU_SPIKE_DUMP")) g_recorder->pending = name;
}

} // namespace gpu_spike
