#pragma once

// The SDL_GPU spike's frame dump (tools/gpu_spike/README.md): one frame of the renderer's sokol_gfx calls
// with every buffer and image they use, read back from the GPU at the frame's end. Written by the spike build's
// recorder (record.cpp, from the harness's capture step), read by ei_gpu_spike, which draws it again through
// sokol_gfx or SDL_GPU. sokol's descriptions are kept as their bytes (the same header on every system); their
// pointers are meaningless in the file and their handles are the recording's ids.

#include "sokol_gfx.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gpu_spike {

constexpr char kMagic[8] = {'E', 'I', 'G', 'P', 'U', '0', '0', '1'};

enum class Record : std::uint32_t {
    Image = 1,      // id, sg_image_desc, label, levels: each level's bytes (all slices), none when not read
    Buffer,         // id, sg_buffer_desc, label, bytes
    Sampler,        // id, sg_sampler_desc
    View,           // id, sg_view_desc
    Shader,         // id, label
    Pipeline,       // id, sg_pipeline_desc, label
    BeginPass = 10, // sg_pass, label
    Viewport,       // x, y, width, height, origin top left
    Scissor,
    ApplyPipeline,  // id
    ApplyBindings,  // sg_bindings
    ApplyUniforms,  // slot, bytes
    Draw,           // base element, elements, instances, base vertex, base instance
    EndPass,
    End = 0xFFFF,
};

// The sizes of the descriptions in the file, checked at the read.
struct Sizes {
    std::uint32_t image = sizeof(sg_image_desc), buffer = sizeof(sg_buffer_desc), sampler = sizeof(sg_sampler_desc),
                  view = sizeof(sg_view_desc), pipeline = sizeof(sg_pipeline_desc), pass = sizeof(sg_pass),
                  bindings = sizeof(sg_bindings);
};

struct Image {
    std::uint32_t id = 0;
    sg_image_desc desc{};
    std::string label;
    std::vector<std::vector<std::uint8_t>> levels;   // empty: not read back
};
struct Buffer {
    std::uint32_t id = 0;
    sg_buffer_desc desc{};
    std::string label;
    std::vector<std::uint8_t> bytes;
};
struct Sampler {
    std::uint32_t id = 0;
    sg_sampler_desc desc{};
};
struct View {
    std::uint32_t id = 0;
    sg_view_desc desc{};
};
struct Shader {
    std::uint32_t id = 0;
    std::string label;
};
struct Pipeline {
    std::uint32_t id = 0;
    sg_pipeline_desc desc{};
    std::string label;
};
struct Command {
    Record kind = Record::End;
    sg_pass pass{};
    std::string label;
    int args[5] = {};
    bool flag = false;
    std::uint32_t id = 0;
    sg_bindings bindings{};
    std::vector<std::uint8_t> bytes;   // uniforms
};

struct Frame {
    std::vector<Image> images;
    std::vector<Buffer> buffers;
    std::vector<Sampler> samplers;
    std::vector<View> views;
    std::vector<Shader> shaders;
    std::vector<Pipeline> pipelines;
    std::vector<Command> commands;
};

// ---- Writing and reading -----------------------------------------------------------------------------------------

class Writer {
public:
    explicit Writer(std::FILE* f) : file(f) {}
    void Raw(const void* data, std::size_t bytes) { ok = ok && std::fwrite(data, 1, bytes, file) == bytes; }
    template <class T> void Value(const T& v) { Raw(&v, sizeof(T)); }
    void Text(const std::string& s) {
        Value(static_cast<std::uint32_t>(s.size()));
        Raw(s.data(), s.size());
    }
    void Bytes(const std::vector<std::uint8_t>& b) {
        Value(static_cast<std::uint64_t>(b.size()));
        Raw(b.data(), b.size());
    }
    bool ok = true;

private:
    std::FILE* file;
};

class Reader {
public:
    explicit Reader(std::FILE* f) : file(f) {}
    void Raw(void* data, std::size_t bytes) { ok = ok && std::fread(data, 1, bytes, file) == bytes; }
    template <class T> T Value() {
        T v{};
        Raw(&v, sizeof(T));
        return v;
    }
    std::string Text() {
        std::string s(Value<std::uint32_t>(), '\0');
        if (ok) Raw(s.data(), s.size());
        return s;
    }
    std::vector<std::uint8_t> Bytes() {
        std::uint64_t n = Value<std::uint64_t>();
        std::vector<std::uint8_t> b;
        if (!ok || n > (1ull << 32)) {
            ok = false;
            return b;
        }
        b.resize(n);
        Raw(b.data(), b.size());
        return b;
    }
    bool ok = true;

private:
    std::FILE* file;
};

bool WriteFrame(const char* path, const Frame& frame);
bool ReadFrame(const char* path, Frame* frame, std::string* error);

} // namespace gpu_spike
