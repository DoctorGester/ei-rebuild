// ei_gpu_spike's SDL_GPU path (replay.h): the plan drawn through SDL3's GPU API (Direct3D 12 or Vulkan) with the
// HLSL programs SDL_shadercross compiled (tools/gpu_spike/shaders): sokol's descriptions translated one by one.
#include "replay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "mesh_fragment_spirv.h"
#include "mesh_vertex_spirv.h"
#include "shadow_caster_fragment_spirv.h"
#include "shadow_caster_vertex_spirv.h"
#include "shadow_dilate_fragment_spirv.h"
#include "shadow_dilate_vertex_spirv.h"
#include "shadow_reduce_fragment_spirv.h"
#include "shadow_reduce_vertex_spirv.h"
#include "shadow_terrain_fragment_spirv.h"
#include "shadow_terrain_vertex_spirv.h"
#include "solid_fragment_spirv.h"
#include "solid_vertex_spirv.h"
#include "terrain_fragment_spirv.h"
#include "terrain_vertex_spirv.h"
#ifdef _WIN32
#include "mesh_fragment_dxil.h"
#include "mesh_vertex_dxil.h"
#include "shadow_caster_fragment_dxil.h"
#include "shadow_caster_vertex_dxil.h"
#include "shadow_dilate_fragment_dxil.h"
#include "shadow_dilate_vertex_dxil.h"
#include "shadow_reduce_fragment_dxil.h"
#include "shadow_reduce_vertex_dxil.h"
#include "shadow_terrain_fragment_dxil.h"
#include "shadow_terrain_vertex_dxil.h"
#include "solid_fragment_dxil.h"
#include "solid_vertex_dxil.h"
#include "terrain_fragment_dxil.h"
#include "terrain_vertex_dxil.h"
#endif

namespace gpu_spike {

namespace {

// ---- The programs' code and bindings ------------------------------------------------------------------------

struct Code {
    const unsigned char* bytes;
    std::size_t size;
};
#define EI_CODE(name) Code{name, sizeof(name)}
#ifdef _WIN32
#define EI_DXIL(name) EI_CODE(name)
#else
#define EI_DXIL(name) Code{nullptr, 0}
#endif

// Where sokol's flat slots go in SDL_GPU's stages: a uniform block slot to (stage, slot), a view / sampler slot to
// the fragment stage's sampler slot; -1 none.
struct Bindings {
    Code vertex[2], fragment[2];   // SPIR-V, DXIL
    int uniformStage[SG_MAX_UNIFORMBLOCK_BINDSLOTS];   // 0 vertex, 1 fragment
    int uniformSlot[SG_MAX_UNIFORMBLOCK_BINDSLOTS];
    int samplerSlot[SG_MAX_VIEW_BINDSLOTS];
    int vertexUniforms, fragmentUniforms, fragmentSamplers;
};

#define EI_PROGRAM(p) {EI_CODE(k_##p##_vertex_spirv), EI_DXIL(k_##p##_vertex_dxil)}, \
                      {EI_CODE(k_##p##_fragment_spirv), EI_DXIL(k_##p##_fragment_dxil)}
constexpr int N = -1;
const Bindings kBindings[kPrograms] = {
    // terrain: blocks 0 vs, 1..4 fs; views 0 tex, 3 shadow map, 4 range, 5 point shadows
    {EI_PROGRAM(terrain), {0, 1, 1, 1, 1, N, N, N}, {0, 0, 1, 2, 3, N, N, N},
     {0, N, N, 1, 2, 3, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 1, 4, 4},
    // mesh: blocks 0 vs, 1..4 fs; views 0..2 the stages, 3 shadow map, 4 range, 5 point shadows
    {EI_PROGRAM(mesh), {0, 1, 1, 1, 1, N, N, N}, {0, 0, 1, 2, 3, N, N, N},
     {0, 1, 2, 3, 4, 5, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 1, 4, 6},
    {EI_PROGRAM(solid), {0, N, N, N, N, N, N, N}, {0, N, N, N, N, N, N, N},
     {N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 1, 0, 0},
    {EI_PROGRAM(shadow_caster), {N, N, N, N, N, N, N, N}, {N, N, N, N, N, N, N, N},
     {0, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 0, 0, 1},
    {EI_PROGRAM(shadow_terrain), {0, 1, N, N, N, N, N, N}, {0, 0, N, N, N, N, N, N},
     {N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 1, 1, 0},
    {EI_PROGRAM(shadow_reduce), {N, N, N, N, N, N, N, N}, {N, N, N, N, N, N, N, N},
     {0, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 0, 0, 1},
    {EI_PROGRAM(shadow_dilate), {1, N, N, N, N, N, N, N}, {0, N, N, N, N, N, N, N},
     {0, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N}, 0, 1, 1},
};
static_assert(SG_MAX_UNIFORMBLOCK_BINDSLOTS == 8 && SG_MAX_VIEW_BINDSLOTS == 32);

// ---- sokol's values in SDL_GPU's ----------------------------------------------------------------------------

SDL_GPUTextureFormat Format(sg_pixel_format f) {
    switch (f) {
    case SG_PIXELFORMAT_RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case SG_PIXELFORMAT_BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case SG_PIXELFORMAT_R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case SG_PIXELFORMAT_R8UI: return SDL_GPU_TEXTUREFORMAT_R8_UINT;
    case SG_PIXELFORMAT_R16UI: return SDL_GPU_TEXTUREFORMAT_R16_UINT;
    case SG_PIXELFORMAT_R16F: return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    case SG_PIXELFORMAT_R32F: return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case SG_PIXELFORMAT_RG32F: return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    case SG_PIXELFORMAT_RGBA16F: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    case SG_PIXELFORMAT_RGBA32F: return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    case SG_PIXELFORMAT_RGB10A2: return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
    case SG_PIXELFORMAT_DEPTH: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case SG_PIXELFORMAT_DEPTH_STENCIL: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    case SG_PIXELFORMAT_BC1_RGBA: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case SG_PIXELFORMAT_BC2_RGBA: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case SG_PIXELFORMAT_BC3_RGBA: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}
bool IsDepth(sg_pixel_format f) { return f == SG_PIXELFORMAT_DEPTH || f == SG_PIXELFORMAT_DEPTH_STENCIL; }

SDL_GPUVertexElementFormat VertexFormat(sg_vertex_format f) {
    switch (f) {
    case SG_VERTEXFORMAT_FLOAT: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case SG_VERTEXFORMAT_FLOAT2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case SG_VERTEXFORMAT_FLOAT3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case SG_VERTEXFORMAT_FLOAT4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case SG_VERTEXFORMAT_INT: return SDL_GPU_VERTEXELEMENTFORMAT_INT;
    case SG_VERTEXFORMAT_UINT: return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    case SG_VERTEXFORMAT_BYTE4N: return SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM;
    case SG_VERTEXFORMAT_UBYTE4: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
    case SG_VERTEXFORMAT_UBYTE4N: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    case SG_VERTEXFORMAT_SHORT2: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
    case SG_VERTEXFORMAT_SHORT2N: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
    case SG_VERTEXFORMAT_SHORT4: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
    case SG_VERTEXFORMAT_SHORT4N: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM;
    case SG_VERTEXFORMAT_USHORT2N: return SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM;
    case SG_VERTEXFORMAT_USHORT4N: return SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM;
    case SG_VERTEXFORMAT_HALF2: return SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
    case SG_VERTEXFORMAT_HALF4: return SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
    default: return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    }
}

SDL_GPUBlendFactor BlendFactor(sg_blend_factor f) {
    switch (f) {
    case SG_BLENDFACTOR_ZERO: return SDL_GPU_BLENDFACTOR_ZERO;
    case SG_BLENDFACTOR_ONE: return SDL_GPU_BLENDFACTOR_ONE;
    case SG_BLENDFACTOR_SRC_COLOR: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case SG_BLENDFACTOR_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case SG_BLENDFACTOR_DST_COLOR: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case SG_BLENDFACTOR_ONE_MINUS_DST_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case SG_BLENDFACTOR_DST_ALPHA: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case SG_BLENDFACTOR_SRC_ALPHA_SATURATED: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    case SG_BLENDFACTOR_BLEND_COLOR: return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
    case SG_BLENDFACTOR_ONE_MINUS_BLEND_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}
SDL_GPUBlendOp BlendOp(sg_blend_op o) {
    switch (o) {
    case SG_BLENDOP_SUBTRACT: return SDL_GPU_BLENDOP_SUBTRACT;
    case SG_BLENDOP_REVERSE_SUBTRACT: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    case SG_BLENDOP_MIN: return SDL_GPU_BLENDOP_MIN;
    case SG_BLENDOP_MAX: return SDL_GPU_BLENDOP_MAX;
    default: return SDL_GPU_BLENDOP_ADD;
    }
}
SDL_GPUCompareOp Compare(sg_compare_func c) {
    switch (c) {
    case SG_COMPAREFUNC_NEVER: return SDL_GPU_COMPAREOP_NEVER;
    case SG_COMPAREFUNC_LESS: return SDL_GPU_COMPAREOP_LESS;
    case SG_COMPAREFUNC_EQUAL: return SDL_GPU_COMPAREOP_EQUAL;
    case SG_COMPAREFUNC_LESS_EQUAL: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case SG_COMPAREFUNC_GREATER: return SDL_GPU_COMPAREOP_GREATER;
    case SG_COMPAREFUNC_NOT_EQUAL: return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case SG_COMPAREFUNC_GREATER_EQUAL: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    default: return SDL_GPU_COMPAREOP_ALWAYS;
    }
}
SDL_GPUSamplerAddressMode Address(sg_wrap w) {
    switch (w) {
    case SG_WRAP_CLAMP_TO_EDGE: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    case SG_WRAP_MIRRORED_REPEAT: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case SG_WRAP_CLAMP_TO_BORDER: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;   // SDL_GPU has no border
    default: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}
SDL_GPULoadOp Load(sg_load_action a) {
    if (a == SG_LOADACTION_LOAD) return SDL_GPU_LOADOP_LOAD;
    if (a == SG_LOADACTION_DONTCARE) return SDL_GPU_LOADOP_DONT_CARE;
    return SDL_GPU_LOADOP_CLEAR;
}

// ---- The scene on the device --------------------------------------------------------------------------------

struct Scene {
    SDL_GPUDevice* device = nullptr;
    std::map<std::uint32_t, SDL_GPUTexture*> textures;   // by image
    std::map<std::uint32_t, SDL_GPUBuffer*> buffers;
    std::map<std::uint32_t, SDL_GPUSampler*> samplers;
    std::map<std::uint32_t, SDL_GPUGraphicsPipeline*> pipelines;
    // By pipeline: sokol's vertex buffer slot to SDL's. SDL wants the slots a pipeline uses packed from 0: its
    // D3D12 backend looks a buffer's description up by slot number (D3D12_INTERNAL_ConvertVertexInputState), so
    // the casters' slots 0 and 2 read past the two descriptions and the instance rows lost their per-instance step.
    std::map<std::uint32_t, std::array<int, SG_MAX_VERTEXBUFFER_BINDSLOTS>> slots;
    SDL_GPUShader* vertex[kPrograms] = {};
    SDL_GPUShader* fragment[kPrograms] = {};
    SDL_GPUTexture* blank = nullptr;        // a texture slot the draw leaves empty
    SDL_GPUSampler* blankSampler = nullptr;
    std::vector<SDL_GPUTransferBuffer*> uploads;
    std::string missing;                    // formats SDL_GPU has no counterpart for
};

bool Upload(Scene& s, SDL_GPUCopyPass* copy, const Image& image, SDL_GPUTexture* texture) {
    std::size_t total = 0;
    for (const auto& level : image.levels) total += level.size();
    SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(total), 0};
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(s.device, &ti);
    if (!tb) return false;
    auto* p = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(s.device, tb, false));
    std::size_t at = 0;
    for (const auto& level : image.levels) {
        std::memcpy(p + at, level.data(), level.size());
        at += level.size();
    }
    SDL_UnmapGPUTransferBuffer(s.device, tb);
    const int slices = image.desc.num_slices > 0 ? image.desc.num_slices : 1;
    at = 0;
    for (std::size_t k = 0; k < image.levels.size(); ++k) {
        const std::size_t perSlice = image.levels[k].size() / static_cast<std::size_t>(slices);
        for (int slice = 0; slice < slices; ++slice) {
            SDL_GPUTextureTransferInfo src{tb, static_cast<Uint32>(at + perSlice * slice), 0, 0};
            SDL_GPUTextureRegion dst{};
            dst.texture = texture;
            dst.mip_level = static_cast<Uint32>(k);
            dst.layer = static_cast<Uint32>(slice);
            dst.w = static_cast<Uint32>(std::max(1, image.desc.width >> k));
            dst.h = static_cast<Uint32>(std::max(1, image.desc.height >> k));
            dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
        }
        at += image.levels[k].size();
    }
    s.uploads.push_back(tb);
    return true;
}

bool MakeResources(const Plan& plan, Scene& s, std::string* error) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(s.device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    for (const auto& [id, image] : plan.images) {
        const sg_image_desc& d = image->desc;
        SDL_GPUTextureCreateInfo ci{};
        ci.type = d.type == SG_IMAGETYPE_ARRAY ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D;
        ci.format = Format(d.pixel_format);
        if (ci.format == SDL_GPU_TEXTUREFORMAT_INVALID) {
            s.missing += " " + std::to_string(d.pixel_format);
            continue;
        }
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = static_cast<Uint32>(d.width);
        ci.height = static_cast<Uint32>(d.height);
        ci.layer_count_or_depth = static_cast<Uint32>(d.num_slices > 0 ? d.num_slices : 1);
        ci.num_levels = static_cast<Uint32>(d.num_mipmaps > 0 ? d.num_mipmaps : 1);
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        const bool attachment = plan.attachments.count(id) != 0 || IsDepth(d.pixel_format) || image->levels.empty();
        if (attachment) {
            ci.usage |= IsDepth(d.pixel_format) ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                                : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            ci.num_levels = 1;
            if (image == plan.colour || image == plan.depth) {
                ci.width = static_cast<Uint32>(plan.width);
                ci.height = static_cast<Uint32>(plan.height);
            }
        }
        SDL_GPUTexture* t = SDL_CreateGPUTexture(s.device, &ci);
        if (!t) {
            *error = std::string("SDL_CreateGPUTexture (") + image->label + "): " + SDL_GetError();
            return false;
        }
        s.textures[id] = t;
        if (!attachment) {
            if (!Upload(s, copy, *image, t)) {
                *error = std::string("upload: ") + SDL_GetError();
                return false;
            }
        }
    }
    for (const auto& [id, buffer] : plan.buffers) {
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = (buffer->desc.usage.index_buffer ? SDL_GPU_BUFFERUSAGE_INDEX : 0) |
                   (buffer->desc.usage.vertex_buffer ? SDL_GPU_BUFFERUSAGE_VERTEX : 0);
        bi.size = static_cast<Uint32>((buffer->bytes.size() + 3) & ~std::size_t{3});
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(s.device, &bi);
        if (!b) {
            *error = std::string("SDL_CreateGPUBuffer: ") + SDL_GetError();
            return false;
        }
        s.buffers[id] = b;
        SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bi.size, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(s.device, &ti);
        auto* p = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(s.device, tb, false));
        std::memcpy(p, buffer->bytes.data(), buffer->bytes.size());
        SDL_UnmapGPUTransferBuffer(s.device, tb);
        SDL_GPUTransferBufferLocation src{tb, 0};
        SDL_GPUBufferRegion dst{b, 0, bi.size};
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        s.uploads.push_back(tb);
    }
    for (const auto& [id, sampler] : plan.samplers) {
        const sg_sampler_desc& d = sampler->desc;
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = d.min_filter == SG_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        si.mag_filter = d.mag_filter == SG_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = d.mipmap_filter == SG_FILTER_LINEAR ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                                             : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = Address(d.wrap_u);
        si.address_mode_v = Address(d.wrap_v);
        si.address_mode_w = Address(d.wrap_w);
        si.min_lod = d.min_lod;
        si.max_lod = d.max_lod;
        si.enable_anisotropy = d.max_anisotropy > 1;
        si.max_anisotropy = static_cast<float>(d.max_anisotropy);
        si.enable_compare = d.compare != SG_COMPAREFUNC_NEVER && d.compare != _SG_COMPAREFUNC_DEFAULT;
        si.compare_op = Compare(d.compare);
        s.samplers[id] = SDL_CreateGPUSampler(s.device, &si);
    }
    SDL_GPUTextureCreateInfo blank{};
    blank.type = SDL_GPU_TEXTURETYPE_2D;
    blank.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    blank.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    blank.width = blank.height = blank.layer_count_or_depth = blank.num_levels = 1;
    s.blank = SDL_CreateGPUTexture(s.device, &blank);
    Image white;
    white.desc.width = white.desc.height = 1;
    white.levels.push_back({255, 255, 255, 255});
    Upload(s, copy, white, s.blank);
    SDL_GPUSamplerCreateInfo bs{};
    s.blankSampler = SDL_CreateGPUSampler(s.device, &bs);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(s.device, true, &fence, 1);
    SDL_ReleaseGPUFence(s.device, fence);
    for (SDL_GPUTransferBuffer* tb : s.uploads) SDL_ReleaseGPUTransferBuffer(s.device, tb);
    s.uploads.clear();
    return true;
}

bool MakePipelines(const Plan& plan, Scene& s, std::string* error) {
    const bool dxil = (SDL_GetGPUShaderFormats(s.device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    for (int k = 0; k < kPrograms; ++k) {
        const Bindings& b = kBindings[k];
        for (int stage = 0; stage < 2; ++stage) {
            const Code& code = stage == 0 ? b.vertex[dxil ? 1 : 0] : b.fragment[dxil ? 1 : 0];
            SDL_GPUShaderCreateInfo ci{};
            ci.code = code.bytes;
            ci.code_size = code.size;
            ci.entrypoint = stage == 0 ? "vs_main" : "ps_main";
            ci.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
            ci.stage = stage == 0 ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            ci.num_samplers = stage == 0 ? 0 : static_cast<Uint32>(b.fragmentSamplers);
            ci.num_uniform_buffers = static_cast<Uint32>(stage == 0 ? b.vertexUniforms : b.fragmentUniforms);
            SDL_GPUShader* shader = SDL_CreateGPUShader(s.device, &ci);
            if (!shader) {
                *error = std::string("SDL_CreateGPUShader (") + kProgramNames[k] + "): " + SDL_GetError();
                return false;
            }
            (stage == 0 ? s.vertex : s.fragment)[k] = shader;
        }
    }
    for (const PlannedPass& pass : plan.passes)
        for (const PlannedDraw& d : pass.draws) {
            if (s.pipelines.count(d.pipeline->id)) continue;
            const sg_pipeline_desc& p = d.pipeline->desc;
            SDL_GPUGraphicsPipelineCreateInfo ci{};
            ci.vertex_shader = s.vertex[static_cast<int>(d.program)];
            ci.fragment_shader = s.fragment[static_cast<int>(d.program)];
            SDL_GPUVertexBufferDescription buffers[SG_MAX_VERTEXBUFFER_BINDSLOTS];
            SDL_GPUVertexAttribute attrs[SG_MAX_VERTEX_ATTRIBUTES];
            std::array<int, SG_MAX_VERTEXBUFFER_BINDSLOTS>& slot = s.slots[d.pipeline->id];
            slot.fill(-1);
            Uint32 attrCount = 0, bufferCount = 0;
            for (int a = 0; a < SG_MAX_VERTEX_ATTRIBUTES; ++a)
                if (p.layout.attrs[a].format != SG_VERTEXFORMAT_INVALID) slot[p.layout.attrs[a].buffer_index] = 0;
            for (int k = 0; k < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++k) {
                if (slot[k] < 0) continue;
                slot[k] = static_cast<int>(bufferCount);
                const sg_vertex_buffer_layout_state& l = p.layout.buffers[k];
                buffers[bufferCount++] = SDL_GPUVertexBufferDescription{
                    static_cast<Uint32>(slot[k]), static_cast<Uint32>(l.stride),
                    l.step_func == SG_VERTEXSTEP_PER_INSTANCE ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                                              : SDL_GPU_VERTEXINPUTRATE_VERTEX,
                    0};
            }
            for (int a = 0; a < SG_MAX_VERTEX_ATTRIBUTES; ++a) {
                const sg_vertex_attr_state& at = p.layout.attrs[a];
                if (at.format == SG_VERTEXFORMAT_INVALID) continue;
                attrs[attrCount++] = SDL_GPUVertexAttribute{static_cast<Uint32>(a),
                                                            static_cast<Uint32>(slot[at.buffer_index]),
                                                            VertexFormat(at.format), static_cast<Uint32>(at.offset)};
            }
            ci.vertex_input_state = SDL_GPUVertexInputState{buffers, bufferCount, attrs, attrCount};
            switch (p.primitive_type) {
            case SG_PRIMITIVETYPE_TRIANGLE_STRIP: ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
            case SG_PRIMITIVETYPE_LINES: ci.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
            case SG_PRIMITIVETYPE_LINE_STRIP: ci.primitive_type = SDL_GPU_PRIMITIVETYPE_LINESTRIP; break;
            case SG_PRIMITIVETYPE_POINTS: ci.primitive_type = SDL_GPU_PRIMITIVETYPE_POINTLIST; break;
            default: ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
            }
            ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            ci.rasterizer_state.cull_mode = p.cull_mode == SG_CULLMODE_FRONT  ? SDL_GPU_CULLMODE_FRONT
                                            : p.cull_mode == SG_CULLMODE_BACK ? SDL_GPU_CULLMODE_BACK
                                                                              : SDL_GPU_CULLMODE_NONE;
            ci.rasterizer_state.front_face = p.face_winding == SG_FACEWINDING_CCW
                                                 ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
                                                 : SDL_GPU_FRONTFACE_CLOCKWISE;
            ci.rasterizer_state.depth_bias_constant_factor = p.depth.bias;
            ci.rasterizer_state.depth_bias_slope_factor = p.depth.bias_slope_scale;
            ci.rasterizer_state.depth_bias_clamp = p.depth.bias_clamp;
            ci.rasterizer_state.enable_depth_bias = p.depth.bias != 0.0f || p.depth.bias_slope_scale != 0.0f;
            ci.rasterizer_state.enable_depth_clip = true;
            ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUColorTargetDescription colours[1]{};
            if (p.color_count > 0 && p.colors[0].pixel_format != SG_PIXELFORMAT_NONE) {
                const sg_color_target_state& c = p.colors[0];
                colours[0].format = Format(c.pixel_format);
                SDL_GPUColorTargetBlendState& bl = colours[0].blend_state;
                bl.enable_blend = c.blend.enabled;
                bl.src_color_blendfactor = BlendFactor(c.blend.src_factor_rgb);
                bl.dst_color_blendfactor = BlendFactor(c.blend.dst_factor_rgb);
                bl.color_blend_op = BlendOp(c.blend.op_rgb);
                bl.src_alpha_blendfactor = BlendFactor(c.blend.src_factor_alpha);
                bl.dst_alpha_blendfactor = BlendFactor(c.blend.dst_factor_alpha);
                bl.alpha_blend_op = BlendOp(c.blend.op_alpha);
                bl.enable_color_write_mask = true;
                bl.color_write_mask = static_cast<SDL_GPUColorComponentFlags>(
                    c.write_mask == SG_COLORMASK_NONE ? 0 : static_cast<int>(c.write_mask) & 15);
                ci.target_info.color_target_descriptions = colours;
                ci.target_info.num_color_targets = 1;
            }
            if (p.depth.pixel_format != SG_PIXELFORMAT_NONE) {
                ci.target_info.has_depth_stencil_target = true;
                ci.target_info.depth_stencil_format = Format(p.depth.pixel_format);
                ci.depth_stencil_state.enable_depth_test = true;
                ci.depth_stencil_state.enable_depth_write = p.depth.write_enabled;
                ci.depth_stencil_state.compare_op = Compare(p.depth.compare);
            }
            SDL_GPUGraphicsPipeline* made = SDL_CreateGPUGraphicsPipeline(s.device, &ci);
            if (!made) {
                *error = std::string("SDL_CreateGPUGraphicsPipeline (") + kProgramNames[static_cast<int>(d.program)] +
                         "): " + SDL_GetError();
                return false;
            }
            s.pipelines[d.pipeline->id] = made;
        }
    return true;
}

void DrawPasses(const Plan& plan, Scene& s, SDL_GPUCommandBuffer* cmd) {
    for (const PlannedPass& pass : plan.passes) {
        SDL_GPUColorTargetInfo colour{};
        SDL_GPUDepthStencilTargetInfo depth{};
        if (pass.colour) {
            colour.texture = s.textures[pass.colour->id];
            const sg_color& c = pass.action.colors[0].clear_value;
            colour.clear_color = SDL_FColor{c.r, c.g, c.b, c.a};
            colour.load_op = Load(pass.action.colors[0].load_action);
            colour.store_op = SDL_GPU_STOREOP_STORE;
        }
        if (pass.depth) {
            depth.texture = s.textures[pass.depth->id];
            depth.clear_depth = pass.action.depth.clear_value;
            depth.load_op = Load(pass.action.depth.load_action);
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        }
        SDL_GPURenderPass* rp =
            SDL_BeginGPURenderPass(cmd, pass.colour ? &colour : nullptr, pass.colour ? 1 : 0, pass.depth ? &depth : nullptr);
        std::uint32_t pipeline = 0;
        for (const PlannedDraw& d : pass.draws) {
            const Bindings& b = kBindings[static_cast<int>(d.program)];
            if (d.pipeline->id != pipeline) {
                pipeline = d.pipeline->id;
                SDL_BindGPUGraphicsPipeline(rp, s.pipelines[pipeline]);
            }
            int r[4];
            ScaleRect(plan, pass, d.viewport, r);
            SDL_GPUViewport vp{static_cast<float>(r[0]), static_cast<float>(r[1]), static_cast<float>(r[2]),
                               static_cast<float>(r[3]), 0.0f, 1.0f};
            SDL_SetGPUViewport(rp, &vp);
            if (d.scissored) ScaleRect(plan, pass, d.scissor, r);
            else r[0] = r[1] = 0, r[2] = pass.width, r[3] = pass.height;
            SDL_Rect sc{r[0], r[1], r[2], r[3]};
            SDL_SetGPUScissor(rp, &sc);
            const sg_pipeline_desc& p = d.pipeline->desc;
            const std::array<int, SG_MAX_VERTEXBUFFER_BINDSLOTS>& slot = s.slots[pipeline];
            for (int k = 0; k < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++k) {
                if (slot[k] < 0 || !d.bindings.vertex_buffers[k].id) continue;
                SDL_GPUBufferBinding vb{s.buffers[d.bindings.vertex_buffers[k].id],
                                        static_cast<Uint32>(d.bindings.vertex_buffer_offsets[k])};
                SDL_BindGPUVertexBuffers(rp, static_cast<Uint32>(slot[k]), &vb, 1);
            }
            if (d.bindings.index_buffer.id) {
                SDL_GPUBufferBinding ib{s.buffers[d.bindings.index_buffer.id],
                                        static_cast<Uint32>(d.bindings.index_buffer_offset)};
                SDL_BindGPUIndexBuffer(rp, &ib, p.index_type == SG_INDEXTYPE_UINT32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                                                                   : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            }
            if (b.fragmentSamplers > 0) {
                SDL_GPUTextureSamplerBinding ts[8];
                for (int k = 0; k < b.fragmentSamplers; ++k) ts[k] = SDL_GPUTextureSamplerBinding{s.blank, s.blankSampler};
                for (int k = 0; k < SG_MAX_VIEW_BINDSLOTS; ++k) {
                    const int at = b.samplerSlot[k];
                    if (at < 0 || !d.bindings.views[k].id) continue;
                    auto view = plan.views.find(d.bindings.views[k].id);
                    if (view == plan.views.end()) continue;
                    auto texture = s.textures.find(view->second->desc.texture.image.id);
                    auto sampler = s.samplers.find(d.bindings.samplers[k].id);
                    if (texture != s.textures.end()) ts[at].texture = texture->second;
                    if (sampler != s.samplers.end()) ts[at].sampler = sampler->second;
                }
                SDL_BindGPUFragmentSamplers(rp, 0, ts, static_cast<Uint32>(b.fragmentSamplers));
            }
            for (int k = 0; k < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++k) {
                if (!d.uniforms[k] || b.uniformSlot[k] < 0) continue;
                const auto& u = *d.uniforms[k];
                if (b.uniformStage[k] == 0)
                    SDL_PushGPUVertexUniformData(cmd, static_cast<Uint32>(b.uniformSlot[k]), u.data(),
                                                 static_cast<Uint32>(u.size()));
                else
                    SDL_PushGPUFragmentUniformData(cmd, static_cast<Uint32>(b.uniformSlot[k]), u.data(),
                                                   static_cast<Uint32>(u.size()));
            }
            if (p.index_type != SG_INDEXTYPE_NONE)
                SDL_DrawGPUIndexedPrimitives(rp, static_cast<Uint32>(d.count), static_cast<Uint32>(d.instances),
                                             static_cast<Uint32>(d.base), d.baseVertex,
                                             static_cast<Uint32>(d.baseInstance));
            else
                SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(d.count), static_cast<Uint32>(d.instances),
                                      static_cast<Uint32>(d.base), static_cast<Uint32>(d.baseInstance));
        }
        SDL_EndGPURenderPass(rp);
    }
}

bool Present(const Plan& plan, Scene& s, SDL_Window* window, SDL_GPUCommandBuffer* cmd) {
    SDL_GPUTexture* swap = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h)) return false;
    if (swap) {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = s.textures[plan.colour->id];
        blit.source.w = static_cast<Uint32>(plan.width);
        blit.source.h = static_cast<Uint32>(plan.height);
        blit.destination.texture = swap;
        blit.destination.w = w;
        blit.destination.h = h;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
    }
    return SDL_SubmitGPUCommandBuffer(cmd);
}

// A texture's first level to the CPU: a download in `cmd`, waited for.
void Download(Scene& s, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* texture, int width, int height, int texelBytes,
              std::vector<std::uint8_t>* out) {
    const Uint32 bytes = static_cast<Uint32>(width) * static_cast<Uint32>(height) * static_cast<Uint32>(texelBytes);
    SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(s.device, &ti);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = texture;
    src.w = static_cast<Uint32>(width);
    src.h = static_cast<Uint32>(height);
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{tb, 0, 0, 0};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(s.device, true, &fence, 1);
    SDL_ReleaseGPUFence(s.device, fence);
    const auto* p = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(s.device, tb, false));
    out->assign(p, p + bytes);
    SDL_UnmapGPUTransferBuffer(s.device, tb);
    SDL_ReleaseGPUTransferBuffer(s.device, tb);
}

void ReadFrame(const Plan& plan, Scene& s, SDL_GPUCommandBuffer* cmd, std::vector<std::uint8_t>* rgba) {
    Download(s, cmd, s.textures[plan.colour->id], plan.width, plan.height, 4, rgba);
}

// Debug mode: each attachment of the passes (the shadow map, its range, the point shadows) against the dump's own
// read-back of it, 32-bit texels compared as floats.
void CheckAttachments(const Plan& plan, Scene& s) {
    for (const auto& [id, made] : plan.attachments) {
        const Image* image = plan.images.at(id);
        if (image == plan.colour || image == plan.depth || image->levels.empty()) continue;
        const sg_pixel_format f = image->desc.pixel_format;
        const int texel = f == SG_PIXELFORMAT_RGBA32F ? 16 : 4;
        std::vector<std::uint8_t> got;
        Download(s, SDL_AcquireGPUCommandBuffer(s.device), s.textures[id], image->desc.width, image->desc.height, texel,
                 &got);
        const std::vector<std::uint8_t>& want = image->levels[0];
        std::size_t off = 0, n = std::min(got.size(), want.size()) / 4;
        float worst = 0.0f;
        std::size_t nearer = 0, cleared = 0;
        int lo[2] = {1 << 30, 1 << 30}, hi[2] = {-1, -1};
        const int perRow = image->desc.width * texel / 4;
        for (std::size_t k = 0; k < n; ++k) {
            float a, b;
            std::memcpy(&a, &got[k * 4], 4);
            std::memcpy(&b, &want[k * 4], 4);
            if (f == SG_PIXELFORMAT_RGBA8 ? got[k * 4] != want[k * 4] : std::fabs(a - b) > 1e-4f) {
                ++off;
                worst = std::max(worst, std::fabs(a - b));
                nearer += a < b;
                cleared += a == 1.0f;
                const int x = static_cast<int>(k % perRow) * 4 / texel, y = static_cast<int>(k / perRow);
                lo[0] = std::min(lo[0], x), lo[1] = std::min(lo[1], y);
                hi[0] = std::max(hi[0], x), hi[1] = std::max(hi[1], y);
            }
        }
        std::printf("check %s %dx%d format %d: %zu of %zu values differ (largest %g; ours nearer %zu, ours clear %zu; "
                    "in %d,%d..%d,%d)\n",
                    image->label.c_str(), image->desc.width, image->desc.height, static_cast<int>(f), off, n, worst,
                    nearer, cleared, lo[0], lo[1], hi[0], hi[1]);
    }
}

void Release(Scene& s, SDL_Window* window) {
    SDL_WaitForGPUIdle(s.device);
    for (auto& [id, t] : s.textures) SDL_ReleaseGPUTexture(s.device, t);
    for (auto& [id, b] : s.buffers) SDL_ReleaseGPUBuffer(s.device, b);
    for (auto& [id, smp] : s.samplers) SDL_ReleaseGPUSampler(s.device, smp);
    for (auto& [id, p] : s.pipelines) SDL_ReleaseGPUGraphicsPipeline(s.device, p);
    for (int k = 0; k < kPrograms; ++k) {
        if (s.vertex[k]) SDL_ReleaseGPUShader(s.device, s.vertex[k]);
        if (s.fragment[k]) SDL_ReleaseGPUShader(s.device, s.fragment[k]);
    }
    if (s.blank) SDL_ReleaseGPUTexture(s.device, s.blank);
    if (s.blankSampler) SDL_ReleaseGPUSampler(s.device, s.blankSampler);
    SDL_ReleaseWindowFromGPUDevice(s.device, window);
    SDL_DestroyGPUDevice(s.device);
}

} // namespace

bool RunSdlGpu(const Plan& plan, const Options& o, SDL_Window* window, Timings* t, std::string* error) {
    const Clock::time_point start = Clock::now();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#ifdef _WIN32
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
#endif
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, o.debug);
    if (!o.driver.empty()) SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, o.driver.c_str());
    Scene s;
    s.device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!s.device) {
        *error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(s.device, window)) {
        *error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
        SDL_DestroyGPUDevice(s.device);
        return false;
    }
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!o.vsync) {
        if (SDL_WindowSupportsGPUPresentMode(s.device, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        else if (SDL_WindowSupportsGPUPresentMode(s.device, window, SDL_GPU_PRESENTMODE_MAILBOX))
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
    }
    SDL_SetGPUSwapchainParameters(s.device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    t->device = SDL_GetGPUDeviceDriver(s.device);
    t->device += mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? ", immediate" : mode == SDL_GPU_PRESENTMODE_MAILBOX ? ", mailbox" : ", vsync";
    SDL_PropertiesID info = SDL_GetGPUDeviceProperties(s.device);
    if (const char* name = SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr)) t->device += std::string(", ") + name;
    if (const char* driver = SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr))
        t->device += std::string(", ") + driver;
    if (o.debug) std::printf("sdlgpu: device made\n");
    const Clock::time_point device = Clock::now();
    t->deviceMs = Ms(start, device);
    if (!MakeResources(plan, s, error)) {
        Release(s, window);
        return false;
    }
    if (o.debug) std::printf("sdlgpu: resources made\n");
    if (!s.missing.empty()) std::printf("sdlgpu: no format for sokol's%s (those images left out)\n", s.missing.c_str());
    const Clock::time_point resources = Clock::now();
    t->resourcesMs = Ms(device, resources);
    if (!MakePipelines(plan, s, error)) {
        Release(s, window);
        return false;
    }
    if (o.debug) std::printf("sdlgpu: pipelines made\n");
    const Clock::time_point pipelines = Clock::now();
    t->pipelinesMs = Ms(resources, pipelines);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(s.device);
    DrawPasses(plan, s, cmd);
    Present(plan, s, window, cmd);
    SDL_WaitForGPUIdle(s.device);
    const Clock::time_point first = Clock::now();
    t->firstFrameMs = Ms(pipelines, first);
    t->startupMs = Ms(start, first);
    Clock::time_point last = Clock::now();
    for (int f = 0; f < o.frames && Ms(start, last) < 40000.0; ++f) {
        SDL_PumpEvents();
        const Clock::time_point begin = Clock::now();
        cmd = SDL_AcquireGPUCommandBuffer(s.device);
        DrawPasses(plan, s, cmd);
        if (!Present(plan, s, window, cmd)) break;
        const Clock::time_point now = Clock::now();
        t->cpuMs.push_back(Ms(begin, now));
        t->frameMs.push_back(Ms(last, now));
        last = now;
    }
    SDL_WaitForGPUIdle(s.device);
    std::vector<std::uint8_t> scratch;
    Clock::time_point a = Clock::now();
    ReadFrame(plan, s, SDL_AcquireGPUCommandBuffer(s.device), &t->rgba);
    t->readbackMs = Ms(a, Clock::now());
    a = Clock::now();
    cmd = SDL_AcquireGPUCommandBuffer(s.device);
    DrawPasses(plan, s, cmd);
    ReadFrame(plan, s, cmd, &scratch);
    t->readbackFrameMs = Ms(a, Clock::now());
    if (o.debug) CheckAttachments(plan, s);
    Release(s, window);
    return true;
}

} // namespace gpu_spike
