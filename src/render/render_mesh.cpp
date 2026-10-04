// Meshes in the new renderer: everything the old path draws through the device's mesh draws
// (004E9010 / 004E9A50, src/game/device_mesh.cpp): figures and map objects, the sky, and the
// interface's sprites (CUiMesh) and 3D previews. The game hands the mesh over in model space with the
// matrix it projects it with (BeginMesh); each pass the old path then draws (004EB550 / 004EB370,
// src/game/device_draw.cpp) becomes a batch with the old device's state of that moment (MeshPass):
// blend, alpha test, depth, culling, fog, the two texture stages' operations, filters and textures.
// The material's state blocks are opaque to us, so the state is read back from the old device
// rather than decoded; later the materials will be read from the game's data.
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#include <cstring>
#include <unordered_map>

namespace render {

namespace {

using namespace detail;

// --- The pending mesh ------------------------------------------------------------------------------------

struct Pending {
    bool active = false;
    bool built = false;
    MeshDraw mesh{};
    char* device = nullptr;
    int matrix = 0;
    int firstIndex = 0;
    int indexCount = 0;
    bool distanceFog = false;
    float fogNear = 0.0f;
    float fogScale = 0.0f;
};
Pending g_pending;
int g_dropped = 0;

// A matrix giving screen pixels after the divide (sx = row0.p / row3.p, sy = row1.p / row3.p) as a
// clip-space one: ndc x = 2 sx / width - 1, ndc y = 1 - 2 sy / height; z and w unchanged.
void ScreenToClip(const float* s, float* c, int width, int height) {
    float kx = 2.0f / static_cast<float>(width);
    float ky = 2.0f / static_cast<float>(height);
    for (int k = 0; k < 4; ++k) {
        c[k] = kx * s[k] - s[12 + k];
        c[4 + k] = s[12 + k] - ky * s[4 + k];
        c[8 + k] = s[8 + k];
        c[12 + k] = s[12 + k];
    }
}

// The mesh's corners as vertices and its triangles, once per mesh (the passes share them).
bool Build(Frame& f, Pending& p) {
    const MeshDraw& m = p.mesh;
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < m.cornerCount; ++i) {
        // Without corners (the flat geometry): vertex i, one colour pair for all.
        const std::uint16_t flat[3] = {static_cast<std::uint16_t>(i), 0, static_cast<std::uint16_t>(i)};
        const std::uint16_t* c = m.corners ? m.corners + i * 3 : flat;
        int position = c[0];
        if (position >= m.positionCount) {
            f.meshVertices.resize(base);
            return false;
        }
        const float* block = m.positions + (position / 4) * 16;
        int lane = position % 4;
        Vertex v{};
        v.x = block[lane];
        v.y = block[4 + lane];
        v.z = block[8 + lane];
        v.colour = m.colours[c[1]];
        v.specular = m.speculars[c[1]];
        v.u = m.uvs[c[2] * 2];
        v.v = m.uvs[c[2] * 2 + 1];
        if (m.moveUv) {
            v.u = m.uOffset + v.u;
            v.v = m.vOffset + v.v;
        }
        f.meshVertices.push_back(v);
    }
    p.firstIndex = static_cast<int>(f.meshIndices.size());
    for (int k = 0; k + 2 < m.indexCount; k += 3) {
        if (m.indices[k] >= m.cornerCount || m.indices[k + 1] >= m.cornerCount || m.indices[k + 2] >= m.cornerCount) continue;
        for (int j = 0; j < 3; ++j) f.meshIndices.push_back(base + m.indices[k + j]);
    }
    p.indexCount = static_cast<int>(f.meshIndices.size()) - p.firstIndex;
    return true;
}

// The old device's state for the pass about to be drawn.
PassState ReadPassState(char* device) {
    static const std::uint32_t kStates[] = {
        27,   // ALPHABLENDENABLE
        19,   // SRCBLEND
        20,   // DESTBLEND
        15,   // ALPHATESTENABLE
        25,   // ALPHAFUNC
        24,   // ALPHAREF
        7,    // ZENABLE
        14,   // ZWRITEENABLE
        23,   // ZFUNC
        22,   // CULLMODE
        29,   // SPECULARENABLE
        28,   // FOGENABLE
        34,   // FOGCOLOR
        60,   // TEXTUREFACTOR
    };
    constexpr int kStateCount = static_cast<int>(sizeof(kStates) / sizeof(kStates[0]));
    std::uint32_t v[kStateCount];
    OldRenderStates(kStates, v, kStateCount);
    PassState s;
    s.blend = v[0] != 0;
    s.sourceBlend = static_cast<std::uint8_t>(v[1]);
    s.destBlend = static_cast<std::uint8_t>(v[2]);
    s.alphaTest = v[3] != 0;
    s.alphaFunc = static_cast<std::uint8_t>(v[4]);
    s.alphaReference = static_cast<std::uint8_t>(v[5]);
    s.depthTest = v[6] != 0;
    s.depthWrite = v[7] != 0;
    s.depthFunc = static_cast<std::uint8_t>(v[8]);
    s.cull = static_cast<std::uint8_t>(v[9]);
    s.specular = v[10] != 0;
    s.fog = v[11] != 0;
    s.fogColour = v[12];
    s.textureFactor = v[13];

    static const std::uint32_t kStageStates[] = {
        1, 2, 3,      // COLOROP, COLORARG1, COLORARG2
        4, 5, 6,      // ALPHAOP, ALPHAARG1, ALPHAARG2
        16, 17, 18,   // MAGFILTER, MINFILTER, MIPFILTER
        13, 14,       // ADDRESSU, ADDRESSV
    };
    constexpr int kStageCount = static_cast<int>(sizeof(kStageStates) / sizeof(kStageStates[0]));
    for (int stage = 0; stage < 2; ++stage) {
        std::uint32_t t[kStageCount];
        OldStageStates(stage, kStageStates, t, kStageCount);
        StageState& st = s.stages[stage];
        st.colourOp = static_cast<std::uint8_t>(t[0]);
        st.colourArg1 = static_cast<std::uint8_t>(t[1]);
        st.colourArg2 = static_cast<std::uint8_t>(t[2]);
        st.alphaOp = static_cast<std::uint8_t>(t[3]);
        st.alphaArg1 = static_cast<std::uint8_t>(t[4]);
        st.alphaArg2 = static_cast<std::uint8_t>(t[5]);
        st.magFilter = static_cast<std::uint8_t>(t[6]);
        st.minFilter = static_cast<std::uint8_t>(t[7]);
        st.mipFilter = static_cast<std::uint8_t>(t[8]);
        st.addressU = static_cast<std::uint8_t>(t[9]);
        st.addressV = static_cast<std::uint8_t>(t[10]);
        if (stage == 1 && st.colourOp <= 1) break;   // D3DTOP_DISABLE: stage 1 and later unused
        // The surface holder the old path bound to the stage (device +0x12C + stage * 4, set by
        // 004EB300 / 004EBAF0).
        const void* holder = *reinterpret_cast<void* const*>(device + 0x12C + stage * 4);
        st.hasTexture = holder != nullptr;
        st.texture = SurfaceView(holder);
    }
    return s;
}

// --- GPU side ------------------------------------------------------------------------------------------------

// The fixed-function texture stages of the old device as the game sets them (D3DTEXTUREOP and
// D3DTA values), evaluated per pixel; then specular, fog and the alpha test.
const char* const kMeshSource = R"(
cbuffer vs_params : register(b0) {
    float4 row0; float4 row1; float4 row2; float4 row3;
    float4 distance_fog;   // x: on, y: start, z: 1 / (end - start); w: keep z inside the depth range
    float4 shift;          // xy: the half-pixel shift (render_internal.h); z: depth scale
};
cbuffer fs_params : register(b0) {
    float4 fog_colour;     // rgb
    float4 factor;         // TEXTUREFACTOR
    int4 colour_ops;       // stage 0: op, arg1, arg2; w: stage 1 op
    int4 colour_ops1;      // stage 1: arg1, arg2
    int4 alpha_ops;        // stage 0: op, arg1, arg2; w: stage 1 op
    int4 alpha_ops1;       // stage 1: arg1, arg2
    int4 switches;         // x: specular, y: fog, z: alpha test func (0 off), w: alpha reference
};
struct vs_in { float3 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; };
struct vs_out { float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    // Meshes the old path projects to the screen without clipping (the interface's) are not cut at
    // the near and far planes there either.
    if (distance_fog.w > 0.5) o.pos.z = clamp(o.pos.z, 0.0, o.pos.w);
    o.pos.z *= shift.z;   // the flat geometry's depth scale (shadows drawn just in front)
    o.pos.xy += shift.xy * o.pos.w;
    o.col = i.col.zyxw;
    o.spec = i.spec.zyxw;
    // The old path's vertex fog by distance (3DfpFPU slot 35): the fog factor in the specular
    // alpha falls from the start of the range, the smaller of it and the vertex's own is kept.
    if (distance_fog.x > 0.5 && o.pos.w >= distance_fog.y)
        o.spec.a = min(o.spec.a, 1.0 - min((o.pos.w - distance_fog.y) * distance_fog.z, 1.0));
    o.uv = i.uv;
    return o;
}
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
SamplerState smp0 : register(s0);
SamplerState smp1 : register(s1);
float4 arg(int a, float4 current, float4 diffuse, float4 texel, float4 spec) {
    int sel = a & 15;
    float4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? factor : sel == 4 ? spec : current;
    if (a & 16) v = 1.0 - v;
    if (a & 32) v = v.aaaa;
    return v;
}
float4 combine(int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 texel) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return lerp(a2, a1, diffuse.a);
    if (op == 13) return lerp(a2, a1, texel.a);
    if (op == 14) return lerp(a2, a1, factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return lerp(a2, a1, current.a);
    if (op == 18) return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return float4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return float4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0).xxxx;
    return a1 * a2;   // 4 MODULATE and anything not handled
}
bool passes(int func, float a, int reference) {
    int v = (int)round(saturate(a) * 255.0);
    if (func == 1) return false;
    if (func == 2) return v < reference;
    if (func == 3) return v == reference;
    if (func == 4) return v <= reference;
    if (func == 5) return v > reference;
    if (func == 6) return v != reference;
    if (func == 7) return v >= reference;
    return true;
}
float4 ps_main(vs_out i) : SV_Target0 {
    float4 diffuse = i.col;
    float4 current = diffuse;
    float4 t0 = tex0.Sample(smp0, i.uv);
    if (colour_ops.x > 1) {
        float3 c = combine(colour_ops.x, arg(colour_ops.y, current, diffuse, t0, i.spec),
                           arg(colour_ops.z, current, diffuse, t0, i.spec), current, diffuse, t0).rgb;
        float a = current.a;
        if (alpha_ops.x > 1)
            a = combine(alpha_ops.x, arg(alpha_ops.y, current, diffuse, t0, i.spec),
                        arg(alpha_ops.z, current, diffuse, t0, i.spec), current, diffuse, t0).a;
        current = saturate(float4(c, a));
        if (colour_ops.w > 1) {
            float4 t1 = tex1.Sample(smp1, i.uv);
            float3 c1 = combine(colour_ops.w, arg(colour_ops1.x, current, diffuse, t1, i.spec),
                                arg(colour_ops1.y, current, diffuse, t1, i.spec), current, diffuse, t1).rgb;
            float a1 = current.a;
            if (alpha_ops.w > 1)
                a1 = combine(alpha_ops.w, arg(alpha_ops1.x, current, diffuse, t1, i.spec),
                             arg(alpha_ops1.y, current, diffuse, t1, i.spec), current, diffuse, t1).a;
            current = saturate(float4(c1, a1));
        }
    }
    if (switches.z != 0 && !passes(switches.z, current.a, switches.w)) discard;
    float3 rgb = current.rgb;
    if (switches.x != 0) rgb = saturate(rgb + i.spec.rgb);
    if (switches.y != 0) rgb = lerp(fog_colour.rgb, rgb, i.spec.a);
    return float4(rgb, current.a);
}
)";

struct VsParams {
    float rows[16];
    float distanceFog[4];
    float shift[4];
};
static_assert(sizeof(VsParams) == 96);
struct FsParams {
    float fogColour[4];
    float factor[4];
    std::int32_t colourOps[4];
    std::int32_t colourOps1[4];
    std::int32_t alphaOps[4];
    std::int32_t alphaOps1[4];
    std::int32_t switches[4];
};
static_assert(sizeof(FsParams) == 112);

sg_shader g_shader{};
std::unordered_map<std::uint32_t, sg_pipeline> g_pipelines;
GeometryStream g_geometry("ei-mesh");

sg_blend_factor Factor(std::uint8_t d3d) {
    switch (d3d) {   // D3DBLEND
    case 1: return SG_BLENDFACTOR_ZERO;
    case 2: return SG_BLENDFACTOR_ONE;
    case 3: return SG_BLENDFACTOR_SRC_COLOR;
    case 4: return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return SG_BLENDFACTOR_SRC_ALPHA;
    case 6: return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return SG_BLENDFACTOR_DST_ALPHA;
    case 8: return SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return SG_BLENDFACTOR_DST_COLOR;
    case 10: return SG_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case 11: return SG_BLENDFACTOR_SRC_ALPHA_SATURATED;
    default: return SG_BLENDFACTOR_ONE;
    }
}

sg_compare_func Compare(std::uint8_t d3d) {
    switch (d3d) {   // D3DCMPFUNC
    case 1: return SG_COMPAREFUNC_NEVER;
    case 2: return SG_COMPAREFUNC_LESS;
    case 3: return SG_COMPAREFUNC_EQUAL;
    case 4: return SG_COMPAREFUNC_LESS_EQUAL;
    case 5: return SG_COMPAREFUNC_GREATER;
    case 6: return SG_COMPAREFUNC_NOT_EQUAL;
    case 7: return SG_COMPAREFUNC_GREATER_EQUAL;
    default: return SG_COMPAREFUNC_ALWAYS;
    }
}

sg_pipeline Pipeline(const PassState& s, bool lines) {
    std::uint32_t key = s.PipelineKey() | (lines ? 1u << 20 : 0u);
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end()) return it->second;
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.layout.buffers[0].stride = sizeof(Vertex);
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[0].offset = 0;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[1].offset = 12;
    p.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[2].offset = 16;
    p.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[3].offset = 20;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.primitive_type = lines ? SG_PRIMITIVETYPE_LINES : SG_PRIMITIVETYPE_TRIANGLES;
    // D3DCULL_CW culls triangles clockwise on screen, D3DCULL_CCW counter-clockwise ones (screen y
    // down in both APIs).
    if (s.cull == 2) {
        p.cull_mode = SG_CULLMODE_BACK;
        p.face_winding = SG_FACEWINDING_CCW;
    } else if (s.cull == 3) {
        p.cull_mode = SG_CULLMODE_BACK;
        p.face_winding = SG_FACEWINDING_CW;
    } else {
        p.cull_mode = SG_CULLMODE_NONE;
    }
    p.depth.pixel_format = kDepthFormat;
    p.depth.compare = s.depthTest ? Compare(s.depthFunc) : SG_COMPAREFUNC_ALWAYS;
    p.depth.write_enabled = s.depthTest && s.depthWrite;
    p.colors[0].pixel_format = kColourFormat;
    // The old render target has no alpha channel: destination alpha reads as one, so the new
    // target's alpha stays at the clear value.
    p.colors[0].write_mask = SG_COLORMASK_RGB;
    if (s.blend) {
        sg_blend_factor source = Factor(s.sourceBlend), dest = Factor(s.destBlend);
        if (s.sourceBlend == 12) {   // D3DBLEND_BOTHSRCALPHA
            source = SG_BLENDFACTOR_SRC_ALPHA;
            dest = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        } else if (s.sourceBlend == 13) {   // D3DBLEND_BOTHINVSRCALPHA
            source = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            dest = SG_BLENDFACTOR_SRC_ALPHA;
        }
        p.colors[0].blend.enabled = true;
        p.colors[0].blend.src_factor_rgb = source;
        p.colors[0].blend.dst_factor_rgb = dest;
        p.colors[0].blend.src_factor_alpha = source;
        p.colors[0].blend.dst_factor_alpha = dest;
    }
    p.sample_count = 1;
    p.label = "ei-mesh";
    sg_pipeline pipeline = sg_make_pipeline(&p);
    g_pipelines.emplace(key, pipeline);
    return pipeline;
}

} // namespace

namespace detail {

bool CreateMeshShader() {
    sg_shader_desc s{};
    s.vertex_func.source = kMeshSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kMeshSource;
    s.fragment_func.entry = "ps_main";
    const char* const semantics[4] = {"POSITION", "COLOR", "COLOR", "TEXCOORD"};
    const std::uint8_t indices[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; ++i) {
        s.attrs[i].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[i].hlsl_sem_name = semantics[i];
        s.attrs[i].hlsl_sem_index = indices[i];
    }
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(VsParams);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(FsParams);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    for (int t = 0; t < 2; ++t) {
        s.views[t].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        s.views[t].texture.image_type = SG_IMAGETYPE_2D;
        s.views[t].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        s.views[t].texture.hlsl_register_t_n = static_cast<std::uint8_t>(t);
        s.samplers[t].stage = SG_SHADERSTAGE_FRAGMENT;
        s.samplers[t].sampler_type = SG_SAMPLERTYPE_FILTERING;
        s.samplers[t].hlsl_register_s_n = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].stage = SG_SHADERSTAGE_FRAGMENT;
        s.texture_sampler_pairs[t].view_slot = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].sampler_slot = static_cast<std::uint8_t>(t);
    }
    s.label = "ei-mesh";
    g_shader = sg_make_shader(&s);
    bool ok = sg_query_shader_state(g_shader) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the mesh shader could not be created (see the sokol messages above)");
    return ok;
}

void UploadMeshes(const Frame& f) {
    if (f.meshBatches.empty()) return;
    g_geometry.Upload(f.meshVertices, f.meshIndices);
}

void DrawMeshBatch(const Frame& f, const MeshBatch& batch) {
    sg_bindings b{};
    if (batch.indexCount <= 0 || !g_geometry.Bind(b)) return;
    const PassState& s = batch.state;
    sg_pipeline pipeline = Pipeline(s, batch.lines);
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) return;
    sg_apply_pipeline(pipeline);
    for (int t = 0; t < 2; ++t) {
        const StageState& st = s.stages[t];
        b.views[t] = st.texture.id ? st.texture : SurfaceView(nullptr);
        b.samplers[t] = StageSampler(st);
    }
    sg_apply_bindings(&b);
    VsParams vs{};
    std::memcpy(vs.rows, f.matrices[static_cast<std::size_t>(batch.matrix)].m, sizeof(vs.rows));
    if (batch.distanceFog && batch.fogScale > 0.0f) {
        vs.distanceFog[0] = 1.0f;
        vs.distanceFog[1] = batch.fogNear;
        vs.distanceFog[2] = batch.fogScale / 255.0f;
    }
    vs.distanceFog[3] = batch.unclipped ? 1.0f : 0.0f;
    vs.shift[0] = f.pixelShift[0];
    vs.shift[1] = f.pixelShift[1];
    vs.shift[2] = batch.zScale;
    FsParams fs{};
    fs.fogColour[0] = static_cast<float>((s.fogColour >> 16) & 0xFF) / 255.0f;
    fs.fogColour[1] = static_cast<float>((s.fogColour >> 8) & 0xFF) / 255.0f;
    fs.fogColour[2] = static_cast<float>(s.fogColour & 0xFF) / 255.0f;
    fs.factor[0] = static_cast<float>((s.textureFactor >> 16) & 0xFF) / 255.0f;
    fs.factor[1] = static_cast<float>((s.textureFactor >> 8) & 0xFF) / 255.0f;
    fs.factor[2] = static_cast<float>(s.textureFactor & 0xFF) / 255.0f;
    fs.factor[3] = static_cast<float>((s.textureFactor >> 24) & 0xFF) / 255.0f;
    const StageState& s0 = s.stages[0];
    const StageState& s1 = s.stages[1];
    fs.colourOps[0] = s0.colourOp;
    fs.colourOps[1] = s0.colourArg1;
    fs.colourOps[2] = s0.colourArg2;
    fs.colourOps[3] = s0.colourOp > 1 ? s1.colourOp : 1;
    fs.colourOps1[0] = s1.colourArg1;
    fs.colourOps1[1] = s1.colourArg2;
    fs.alphaOps[0] = s0.alphaOp;
    fs.alphaOps[1] = s0.alphaArg1;
    fs.alphaOps[2] = s0.alphaArg2;
    fs.alphaOps[3] = s1.alphaOp;
    fs.alphaOps1[0] = s1.alphaArg1;
    fs.alphaOps1[1] = s1.alphaArg2;
    fs.switches[0] = s.specular ? 1 : 0;
    fs.switches[1] = s.fog ? 1 : 0;
    fs.switches[2] = s.alphaTest ? s.alphaFunc : 0;
    fs.switches[3] = s.alphaReference;
    sg_range vr{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vr);
    sg_range fr{&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fr);
    sg_draw(batch.firstIndex, batch.indexCount, 1);
}

void DestroyMeshes() {
    g_geometry.Destroy();
    for (auto& p : g_pipelines) sg_destroy_pipeline(p.second);
    g_pipelines.clear();
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_shader = sg_shader{};
}

} // namespace detail

void BeginMesh(void* device, const MeshDraw& mesh) {
    g_pending = Pending{};
    if (!device || !mesh.matrix || !Active() || !Started()) return;
    Frame& f = CurrentFrame();
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time: drop the rest
        if (g_dropped++ == 0) ei::Log("render: too many meshes in one frame; the rest are dropped");
        return;
    }
    float clip[16];
    if (mesh.screenMatrix) {
        int width = 0, height = 0;
        if (!GameTargetSize(&width, &height)) return;
        ScreenToClip(mesh.matrix, clip, width, height);
    } else {
        std::memcpy(clip, mesh.matrix, sizeof(clip));
    }
    g_pending.active = true;
    g_pending.mesh = mesh;
    g_pending.device = static_cast<char*>(device);
    g_pending.matrix = AddMatrix(clip);
    // The old path fogs the projected vertices by distance (slot 35 of the transform object at the
    // pipeline's +0xC: range start +0x34, 255 / length +0x3C).
    g_pending.distanceFog = mesh.distanceFog;
    const char* pipeline = *reinterpret_cast<char* const*>(g_pending.device + 0xAC);
    const char* transform = pipeline ? *reinterpret_cast<char* const*>(pipeline + 0xC) : nullptr;
    if (transform) {
        std::memcpy(&g_pending.fogNear, transform + 0x34, 4);
        std::memcpy(&g_pending.fogScale, transform + 0x3C, 4);
    } else {
        g_pending.distanceFog = false;
    }
}

bool MeshPass(void* device) {
    if (!g_pending.active || device != g_pending.device) return true;
    Frame& f = CurrentFrame();
    if (!g_pending.built) {
        g_pending.built = true;
        if (!Build(f, g_pending)) {
            g_pending.active = false;
            return true;
        }
    }
    if (g_pending.indexCount > 0) {
        FpuGuard fpu;
        MeshBatch batch{};
        batch.matrix = g_pending.matrix;
        batch.firstIndex = g_pending.firstIndex;
        batch.indexCount = g_pending.indexCount;
        batch.state = ReadPassState(g_pending.device);
        batch.distanceFog = g_pending.distanceFog;
        batch.fogNear = g_pending.fogNear;
        batch.fogScale = g_pending.fogScale;
        batch.unclipped = !g_pending.mesh.clipped;
        batch.zScale = g_pending.mesh.zScale != 0.0f ? g_pending.mesh.zScale : 1.0f;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
    return DrawOldMeshes();
}

void EndMesh() { g_pending = Pending{}; }

bool TlDraw(void* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            int primitive) {
    if (!device || !vertices || vertexCount <= 0 || !Active() || !Started()) return true;
    if (fvf != 0x1C4 && fvf != 0xC4) return true;   // not a layout the game uses for these
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height)) return true;
    Frame& f = CurrentFrame();
    if (f.meshVertices.size() > 4'000'000) return true;
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float clip[16];
    ScreenToClip(kIdentity, clip, width, height);
    int stride = fvf == 0x1C4 ? 32 : 24;
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < vertexCount; ++i) {
        const auto* p = static_cast<const std::uint8_t*>(vertices) + static_cast<std::size_t>(i) * stride;
        Vertex v{};
        std::memcpy(&v.x, p, 12);   // x, y, z (rhw taken as 1: affine interpolation)
        std::memcpy(&v.colour, p + 16, 8);
        if (stride == 32) std::memcpy(&v.u, p + 24, 8);
        f.meshVertices.push_back(v);
    }
    // The primitive as a list over absolute indices.
    int count = indices ? indexCount : vertexCount;
    auto at = [&](int k) { return base + (indices ? indices[k] : static_cast<std::uint16_t>(k)); };
    auto valid = [&](int k) { return (indices ? indices[k] : k) < vertexCount; };
    int firstIndex = static_cast<int>(f.meshIndices.size());
    bool lines = primitive == 2 || primitive == 3;
    for (int k = 0; k < count; ++k) {
        switch (primitive) {
        case 2:   // line list
            if (k % 2 == 1 && valid(k - 1) && valid(k)) { f.meshIndices.push_back(at(k - 1)); f.meshIndices.push_back(at(k)); }
            break;
        case 3:   // line strip
            if (k >= 1 && valid(k - 1) && valid(k)) { f.meshIndices.push_back(at(k - 1)); f.meshIndices.push_back(at(k)); }
            break;
        case 4:   // triangle list
            if (k % 3 == 2 && valid(k - 2) && valid(k - 1) && valid(k))
                for (int j = 2; j >= 0; --j) f.meshIndices.push_back(at(k - j));
            break;
        case 5:   // strip: every second triangle reversed to keep the winding
            if (k >= 2 && valid(k - 2) && valid(k - 1) && valid(k)) {
                if (k % 2 == 0) { f.meshIndices.push_back(at(k - 2)); f.meshIndices.push_back(at(k - 1)); }
                else { f.meshIndices.push_back(at(k - 1)); f.meshIndices.push_back(at(k - 2)); }
                f.meshIndices.push_back(at(k));
            }
            break;
        case 6:   // fan
            if (k >= 2 && valid(0) && valid(k - 1) && valid(k)) {
                f.meshIndices.push_back(at(0));
                f.meshIndices.push_back(at(k - 1));
                f.meshIndices.push_back(at(k));
            }
            break;
        default: break;
        }
    }
    int drawn = static_cast<int>(f.meshIndices.size()) - firstIndex;
    if (drawn > 0) {
        FpuGuard fpu;
        MeshBatch batch{};
        batch.matrix = AddMatrix(clip);
        batch.firstIndex = firstIndex;
        batch.indexCount = drawn;
        batch.state = ReadPassState(static_cast<char*>(device));
        batch.unclipped = true;
        batch.zScale = 1.0f;
        batch.lines = lines;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    } else {
        f.meshVertices.resize(base);
    }
    return DrawOldMeshes();
}

bool ScreenDraw(void* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount) {
    if (!device || !vertices || count <= 0 || !Active() || !Started()) return true;
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height)) return true;
    Frame& f = CurrentFrame();
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float clip[16];
    ScreenToClip(kIdentity, clip, width, height);
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < count; ++i) {
        const ScreenVertex& s = vertices[i];
        f.meshVertices.push_back(Vertex{s.x, s.y, s.z, s.colour, s.specular, 0.0f, 0.0f});
    }
    int firstIndex = static_cast<int>(f.meshIndices.size());
    bool lines = indices == nullptr;
    if (lines) {   // the strip as a list of segments
        for (int i = 0; i + 1 < count; ++i) {
            f.meshIndices.push_back(base + static_cast<std::uint32_t>(i));
            f.meshIndices.push_back(base + static_cast<std::uint32_t>(i + 1));
        }
    } else {
        for (int k = 0; k + 2 < indexCount; k += 3) {
            if (indices[k] >= count || indices[k + 1] >= count || indices[k + 2] >= count) continue;
            for (int j = 0; j < 3; ++j) f.meshIndices.push_back(base + indices[k + j]);
        }
    }
    int drawn = static_cast<int>(f.meshIndices.size()) - firstIndex;
    if (drawn > 0) {
        FpuGuard fpu;
        MeshBatch batch{};
        batch.matrix = AddMatrix(clip);
        batch.firstIndex = firstIndex;
        batch.indexCount = drawn;
        batch.state = ReadPassState(static_cast<char*>(device));
        batch.unclipped = true;
        batch.zScale = 1.0f;
        batch.lines = lines;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
    return DrawOldMeshes();
}

} // namespace render
