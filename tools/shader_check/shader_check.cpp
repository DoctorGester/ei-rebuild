// The HLSL programs' check (docs/notes/renderer.md, "HLSL and SDL_shadercross"; tools/build_checks.py runs it on
// Windows, ctest on Linux). First every program's interface against sokol-shdc's description of the same GLSL program:
// the uniform blocks and textures per stage and the vertex inputs (gpu_shaders_sokol.h holds the blocks' layouts
// against sokol-shdc's structs, checked when this file compiles). Then, on each GPU driver SDL_GPU has here (Direct3D
// 12 and Vulkan on Windows, Vulkan on Linux: lavapipe without a GPU), every program's two shaders and a pipeline for
// each kind of target the renderer draws it into (kDraws).
//
// usage: ei_shader_check [--require]   (--require: a driver that makes no device fails; without it, it is skipped)

#include <SDL3/SDL.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "lib/vecmath.h"
#include "sokol_gfx.h"
#include "shaders/shaders.h"
#include "shaders/gpu_shaders_sokol.h"

namespace {

using gpu_shaders::kProgramCount;
using gpu_shaders::kPrograms;
using gpu_shaders::Program;
using gpu_shaders::Scalar;

int g_failures = 0;

void Fail(const char* format, ...) SDL_PRINTF_VARARG_FUNC(1);
void Fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::fputs("shader_check: ", stdout);
    std::vprintf(format, args);
    std::fputc('\n', stdout);
    va_end(args);
    ++g_failures;
}

// ---- Against sokol-shdc --------------------------------------------------------------------------------------------

#ifdef _WIN32
constexpr sg_backend kSokolBackend = SG_BACKEND_D3D11;
#else
constexpr sg_backend kSokolBackend = SG_BACKEND_GLCORE;
#endif

void CompareWithSokol() {
    for (int k = 0; k < kProgramCount; ++k) {
        const Program& p = kPrograms[k];
        const sg_shader_desc* d = gpu_shaders::kSokolDescs[k](kSokolBackend);
        if (!d) {
            Fail("%s: sokol-shdc has no description", p.name);
            continue;
        }
        int blocks[2] = {}, textures[2] = {}, inputs = 0;
        for (const sg_shader_uniform_block& b : d->uniform_blocks)
            if (b.stage != SG_SHADERSTAGE_NONE) ++blocks[b.stage == SG_SHADERSTAGE_VERTEX ? 0 : 1];
        for (const sg_shader_view& v : d->views)
            if (v.texture.stage != SG_SHADERSTAGE_NONE) ++textures[v.texture.stage == SG_SHADERSTAGE_VERTEX ? 0 : 1];
        for (const sg_shader_vertex_attr& a : d->attrs)
            if (a.base_type != SG_SHADERATTRBASETYPE_UNDEFINED || a.glsl_name || a.hlsl_sem_name) ++inputs;
        const gpu_shaders::Stage* stages[2] = {&p.vertex, &p.fragment};
        for (int s = 0; s < 2; ++s) {
            const char* stage = s == 0 ? "vertex" : "fragment";
            if (stages[s]->uniformBlocks != blocks[s])
                Fail("%s %s: %d uniform blocks, the GLSL %d", p.name, stage, stages[s]->uniformBlocks, blocks[s]);
            if (stages[s]->samplers != textures[s])
                Fail("%s %s: %d textures, the GLSL %d", p.name, stage, stages[s]->samplers, textures[s]);
        }
        if (p.inputCount != inputs) Fail("%s: %d vertex inputs, the GLSL %d", p.name, p.inputCount, inputs);
    }
}

// ---- On the GPU ----------------------------------------------------------------------------------------------------

constexpr SDL_GPUTextureFormat kNone = SDL_GPU_TEXTUREFORMAT_INVALID;

// A kind of target a program is drawn into (the renderer's pipelines): its colour format (kNone: depth only), whether
// it has the D32F depth, its sample count, and the first vertex input of the instance buffer (-1: none).
struct Draw {
    const char* program;
    SDL_GPUTextureFormat colour;
    bool depth;
    int samples;
    int instanceFrom = -1;
};
constexpr SDL_GPUTextureFormat kRgba8 = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat kBgra8 = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
const Draw kDraws[] = {
    // The frame target (render.cpp): colour and depth, single or multisampled (the smooth option).
    {"blit", kRgba8, true, 1},
    {"blit", kRgba8, true, 4},
    {"solid", kRgba8, true, 1},
    {"solid", kRgba8, true, 4},
    {"solid", kNone, true, 1},   // a point shadow face's clear
    {"terrain", kRgba8, true, 1},
    {"terrain", kRgba8, true, 4},
    {"mesh", kRgba8, true, 1},
    {"mesh", kRgba8, true, 4},
    {"into_multisampled", kRgba8, true, 4},
    {"depth_from_multisampled", kRgba8, true, 1},
    // The window's back buffer.
    {"present", kBgra8, false, 1},
    {"perf_overlay", kBgra8, false, 1},
    // Composed textures and the clouds' frames (render_compose.cpp).
    {"texture_compose", kRgba8, false, 1},
    {"texture_mip", kRgba8, false, 1},
    {"texture_clouds_frame", SDL_GPU_TEXTUREFORMAT_R8_UINT, false, 1},
    {"texture_clouds", kRgba8, false, 1},
    // The shadow maps and their range (render_shadows.cpp, render_point_shadows.cpp).
    {"shadow_caster", kNone, true, 1, 2},   // the caster's rows and cut per instance
    {"shadow_terrain", kNone, true, 1},
    {"shadow_reduce", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false, 1},
    {"shadow_dilate", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false, 1},
    // The interface canvases (render_ui.cpp): RGBA8, the glyphs' coverage R8 and the save's picture R16_UINT.
    {"ui_solid", kRgba8, false, 1},
    {"ui_glyph", SDL_GPU_TEXTUREFORMAT_R8_UNORM, false, 1},
    {"ui_compose", kRgba8, false, 1},
    {"ui_grey", kRgba8, false, 1},
    {"ui_copy", kRgba8, false, 1},
    {"ui_move", kRgba8, false, 1},
    {"ui_rgb565", SDL_GPU_TEXTUREFORMAT_R16_UINT, false, 1},
    {"ui_blit", kRgba8, false, 1},
};

int ProgramIndex(const char* name) {
    for (int k = 0; k < kProgramCount; ++k)
        if (std::strcmp(kPrograms[k].name, name) == 0) return k;
    return -1;
}

bool UintFormat(SDL_GPUTextureFormat f) {
    return f == SDL_GPU_TEXTUREFORMAT_R8_UINT || f == SDL_GPU_TEXTUREFORMAT_R16_UINT;
}

void CheckDrawTable() {
    for (const Draw& d : kDraws) {
        const int k = ProgramIndex(d.program);
        if (k < 0) {
            Fail("kDraws names %s, which is no program", d.program);
            continue;
        }
        const Program& p = kPrograms[k];
        if (d.colour != kNone && (p.outputCount != 1 || (p.outputs[0].scalar == Scalar::Uint) != UintFormat(d.colour)))
            Fail("%s: its colour output does not suit a draw's target", d.program);
    }
    for (int k = 0; k < kProgramCount; ++k) {
        bool drawn = false;
        for (const Draw& d : kDraws) drawn = drawn || std::strcmp(d.program, kPrograms[k].name) == 0;
        if (!drawn) Fail("%s: no draw in kDraws", kPrograms[k].name);
    }
}

SDL_GPUVertexElementFormat ElementFormat(const gpu_shaders::Value& v) {
    static constexpr SDL_GPUVertexElementFormat kFloat[] = {
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4};
    static constexpr SDL_GPUVertexElementFormat kInt[] = {
        SDL_GPU_VERTEXELEMENTFORMAT_INT, SDL_GPU_VERTEXELEMENTFORMAT_INT2, SDL_GPU_VERTEXELEMENTFORMAT_INT3,
        SDL_GPU_VERTEXELEMENTFORMAT_INT4};
    static constexpr SDL_GPUVertexElementFormat kUint[] = {
        SDL_GPU_VERTEXELEMENTFORMAT_UINT, SDL_GPU_VERTEXELEMENTFORMAT_UINT2, SDL_GPU_VERTEXELEMENTFORMAT_UINT3,
        SDL_GPU_VERTEXELEMENTFORMAT_UINT4};
    const SDL_GPUVertexElementFormat* row = v.scalar == Scalar::Float ? kFloat : v.scalar == Scalar::Int ? kInt : kUint;
    return row[v.components - 1];
}

SDL_GPUShader* MakeShader(SDL_GPUDevice* device, bool dxil, const Program& p, bool vertex) {
    const gpu_shaders::Stage& s = vertex ? p.vertex : p.fragment;
    const gpu_shaders::Code& code = dxil ? s.dxil : s.spirv;
    SDL_GPUShaderCreateInfo ci{};
    ci.code = code.bytes;
    ci.code_size = code.size;
    ci.entrypoint = s.entry;
    ci.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = static_cast<Uint32>(s.samplers);
    ci.num_uniform_buffers = static_cast<Uint32>(s.uniformBlocks);
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &ci);
    if (!shader) Fail("%s %s shader: %s", p.name, vertex ? "vertex" : "fragment", SDL_GetError());
    return shader;
}

SDL_GPUSampleCount SampleCount(int samples) {
    switch (samples) {
    case 2: return SDL_GPU_SAMPLECOUNT_2;
    case 4: return SDL_GPU_SAMPLECOUNT_4;
    case 8: return SDL_GPU_SAMPLECOUNT_8;
    default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

bool MakePipeline(SDL_GPUDevice* device, const Draw& d, const Program& p, SDL_GPUShader* vs, SDL_GPUShader* fs) {
    SDL_GPUVertexBufferDescription buffers[2]{};
    std::vector<SDL_GPUVertexAttribute> attrs;
    Uint32 strides[2] = {};
    for (int i = 0; i < p.inputCount; ++i) {
        const gpu_shaders::Value& v = p.inputs[i];
        const Uint32 slot = d.instanceFrom >= 0 && v.location >= d.instanceFrom ? 1 : 0;
        attrs.push_back({static_cast<Uint32>(v.location), slot, ElementFormat(v), strides[slot]});
        strides[slot] += static_cast<Uint32>(v.components) * 4;
    }
    Uint32 bufferCount = 0;
    for (Uint32 slot = 0; slot < 2; ++slot)
        if (strides[slot])
            buffers[bufferCount++] = {slot, strides[slot],
                                      slot == 1 ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.vertex_input_state = {buffers, bufferCount, attrs.data(), static_cast<Uint32>(attrs.size())};
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.enable_depth_clip = true;
    ci.multisample_state.sample_count = SampleCount(d.samples);
    SDL_GPUColorTargetDescription colour{};
    colour.format = d.colour;
    if (d.colour != kNone) {
        ci.target_info.color_target_descriptions = &colour;
        ci.target_info.num_color_targets = 1;
    }
    if (d.depth) {
        ci.target_info.has_depth_stencil_target = true;
        ci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        ci.depth_stencil_state.enable_depth_test = true;
        ci.depth_stencil_state.enable_depth_write = true;
        ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    }
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &ci);
    if (!pipeline) {
        Fail("%s pipeline (%d samples): %s", d.program, d.samples, SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    return true;
}

// Every program and pipeline on `driver`; false when it made no device.
bool CheckDriver(const char* driver) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#ifdef _WIN32
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
#endif
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
    SDL_GPUDevice* device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!device) {
        std::printf("shader_check: %s: no device (%s)\n", driver, SDL_GetError());
        return false;
    }
    const bool dxil = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, "?");
    const int failuresBefore = g_failures;
    std::vector<SDL_GPUShader*> vertex(kProgramCount), fragment(kProgramCount);
    for (int k = 0; k < kProgramCount; ++k) {
        vertex[k] = MakeShader(device, dxil, kPrograms[k], true);
        fragment[k] = MakeShader(device, dxil, kPrograms[k], false);
    }
    int pipelines = 0, unsupported = 0;
    for (const Draw& d : kDraws) {
        const int k = ProgramIndex(d.program);
        if (k < 0 || !vertex[k] || !fragment[k]) continue;
        const SDL_GPUSampleCount count = SampleCount(d.samples);
        if ((d.colour != kNone && !SDL_GPUTextureSupportsSampleCount(device, d.colour, count)) ||
            (d.depth && !SDL_GPUTextureSupportsSampleCount(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, count))) {
            ++unsupported;
            continue;
        }
        if (MakePipeline(device, d, kPrograms[k], vertex[k], fragment[k])) ++pipelines;
    }
    for (int k = 0; k < kProgramCount; ++k) {
        if (vertex[k]) SDL_ReleaseGPUShader(device, vertex[k]);
        if (fragment[k]) SDL_ReleaseGPUShader(device, fragment[k]);
    }
    std::printf("shader_check: %s (%s, %s): %d programs, %d pipelines%s%s\n", driver, name, dxil ? "DXIL" : "SPIR-V",
                kProgramCount, pipelines, unsupported ? " (sample counts it lacks: " : "",
                unsupported ? (std::to_string(unsupported) + " pipelines left out)").c_str() : "");
    if (g_failures != failuresBefore) std::printf("shader_check: %s: %d failures\n", driver, g_failures - failuresBefore);
    SDL_DestroyGPUDevice(device);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool require = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--require") == 0) {
            require = true;
        } else {
            std::printf("usage: ei_shader_check [--require]\n");
            return 2;
        }
    }
    CompareWithSokol();
    CheckDrawTable();
    // SDL_GPU's Vulkan driver loads the Vulkan library through the video subsystem.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Fail("SDL_Init: %s", SDL_GetError());
        return 1;
    }
#ifdef _WIN32
    const char* drivers[] = {"direct3d12", "vulkan"};
#else
    const char* drivers[] = {"vulkan"};
#endif
    for (const char* driver : drivers)
        if (!CheckDriver(driver) && require) Fail("%s: no device, and --require", driver);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
