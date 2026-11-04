// Meshes: each pass of an old-path device mesh draw becomes a batch with the old device's state of that pass.
// See docs/notes/renderer.md, "The renderer's source files".
#include "../game/model.h"   // TransformPipeline
#include "../game/texture.h"
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"
#include "render_state.h"

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
    const TextureDevice* device = nullptr;
    int matrix = 0;
    int firstIndex = 0;
    int indexCount = 0;
    bool distanceFog = false;
    float fogNear = 0.0f;
    float fogScale = 0.0f;
    const void* caster = nullptr;   // render::BeginCaster's object
    bool figure = false;            // ... a unit's
    bool boxed = false;             // its positions' box in model space taken (a caster's: MeshPass)
    float box[6] = {};
    CachedMesh* cached = nullptr;   // kept on the GPU, else built into the frame's stream
    int lighting = -1;              // Frame::meshLights
};
Pending g_pending;
int g_dropped = 0;
bool g_groundDecals = true;   // SetGroundDecals

// A draw the renderer does not take is lost (there is no other way to draw): each reason logged the first time.
enum Refusal { NoTargetSize, BadCorner, OtherFvf, VertexLimit, RefusalCount };
bool g_refused[RefusalCount] = {};

void Refuse(Refusal reason, const char* what) {
    if (g_refused[reason]) return;
    g_refused[reason] = true;
    ei::Log("render: %s; the draw is lost", what);
}

// A matrix giving screen pixels after the divide (sx = row0.p / row3.p, sy = row1.p / row3.p) as a
// clip-space one: ndc x = 2 sx / width - 1, ndc y = 1 - 2 sy / height; z and w unchanged.
Matrix4 ScreenToClip(const Matrix4& screen, int width, int height) {
    float kx = 2.0f / width;
    float ky = 2.0f / height;
    const float* s = screen.m;
    Matrix4 clip;
    float* c = clip.m;
    for (int k = 0; k < 4; ++k) {
        c[k] = kx * s[k] - s[12 + k];
        c[4 + k] = s[12 + k] - ky * s[4 + k];
        c[8 + k] = s[8 + k];
        c[12 + k] = s[12 + k];
    }
    return clip;
}

const Matrix4 kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

// The mesh's corners as vertices and its triangles, once per mesh (the passes share them).
bool Build(Frame& f, Pending& p) {
    const MeshDraw& m = p.mesh;
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < m.cornerCount; ++i) {
        // Without corners (the flat geometry): vertex i, one colour pair for all.
        const MeshCorner flat{static_cast<std::uint16_t>(i), 0, static_cast<std::uint16_t>(i)};
        const MeshCorner& c = m.corners ? m.corners[i] : flat;
        int position = c.position;
        if (position >= m.positionCount) {
            f.meshVertices.resize(base);
            return false;
        }
        const VertexPacket& packet = m.positions[position / 4];
        int lane = position % 4;
        Vertex v{};
        v.position.x = packet.x()[lane];
        v.position.y = packet.y()[lane];
        v.position.z = packet.z()[lane];
        v.colour = m.colours[c.normal];
        v.specular = m.speculars[c.normal];
        v.normal = CornerNormal(m, c.normal);
        v.u = m.uvs[c.texCoord].u;
        v.v = m.uvs[c.texCoord].v;
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

// --- GPU side ------------------------------------------------------------------------------------------------

// The pipelines by their pass state's key (gpu.h keeps them by description; this skips building one per draw).
std::unordered_map<std::uint32_t, gpu::Pipeline> g_pipelines;
GeometryStream g_geometry("ei-mesh", false);

gpu::PipelineDesc MeshDesc(const PassState& s, bool lines, bool cached, int samples) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::mesh::kIndex;
    MeshLayout(p, cached, false);
    p.primitive = lines ? gpu::Primitive::Lines : gpu::Primitive::Triangles;
    ApplyPassState(p, s);
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

gpu::Pipeline Pipeline(const PassState& s, bool lines, bool cached) {
    int samples = PassSampleCount();
    std::uint32_t key = s.PipelineKey() | (lines ? 1u << 20 : 0u) | (cached ? 1u << 21 : 0u) |
                        (static_cast<std::uint32_t>(samples) << 24);
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end()) return it->second;
    gpu::Pipeline pipeline = gpu::MakePipeline(MeshDesc(s, lines, cached, samples), "ei-mesh");
    g_pipelines.emplace(key, pipeline);
    return pipeline;
}

} // namespace

namespace detail {

// The pass states every battle and menu draws meshes with (the pipelines a run makes, logged; the rare ones are made at
// their first draw): D3DCULL, D3DBLEND, D3DCMP numbers as PassState keeps them.
void ListMeshPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    struct Wanted {
        std::uint8_t cull;
        std::uint8_t sourceBlend, destBlend;   // 0: blending off
        bool depthTest, depthWrite;
        bool cached;
    };
    constexpr std::uint8_t kSourceAlpha = 5, kInvSourceAlpha = 6, kOne = 2;
    static constexpr Wanted kWanted[] = {
        {2, kSourceAlpha, kInvSourceAlpha, true, true, true},
        {2, kSourceAlpha, kInvSourceAlpha, true, true, false},
        {2, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {1, kSourceAlpha, kInvSourceAlpha, true, false, true},
        {1, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {1, 0, 0, true, true, false},
        {2, kSourceAlpha, kOne, true, false, false},
        {2, kSourceAlpha, kInvSourceAlpha, false, false, false},
    };
    for (const Wanted& w : kWanted) {
        PassState s;
        s.cull = w.cull;
        s.blend = w.sourceBlend != 0;
        if (s.blend) s.sourceBlend = w.sourceBlend, s.destBlend = w.destBlend;
        s.depthTest = w.depthTest;
        s.depthWrite = w.depthWrite;
        out.push_back(MeshDesc(s, false, w.cached, samples));
    }
}

PassState ReadPassState(const TextureDevice* device) {
    const RenderState& d = g_renderState;
    PassState s;
    s.blend = d.blend;
    s.sourceBlend = static_cast<std::uint8_t>(d.sourceBlend);
    s.destBlend = static_cast<std::uint8_t>(d.destBlend);
    s.alphaTest = d.alphaTest;
    s.alphaFunc = static_cast<std::uint8_t>(d.alphaFunc);
    s.alphaReference = d.alphaReference;
    s.depthTest = d.depthTest;
    s.depthWrite = d.depthWrite;
    s.depthFunc = static_cast<std::uint8_t>(d.depthFunc);
    s.cull = static_cast<std::uint8_t>(d.cull);
    s.specular = d.specular;
    s.fog = d.fog;
    s.fogColour = d.fogColour;
    s.textureFactor = d.textureFactor;
    for (int stage = 0; stage < 2; ++stage) {
        const TextureStage& from = d.stages[stage];
        StageState& st = s.stages[stage];
        st.colourOp = static_cast<std::uint8_t>(from.combiner.colourOp);
        if (stage == 1 && st.colourOp <= 1) break;   // TextureOp::Disable: stage 1 unused
        st.colourArg1 = from.combiner.colourArg1;
        st.colourArg2 = from.combiner.colourArg2;
        st.alphaOp = static_cast<std::uint8_t>(from.combiner.alphaOp);
        st.alphaArg1 = from.combiner.alphaArg1;
        st.alphaArg2 = from.combiner.alphaArg2;
        st.magFilter = static_cast<std::uint8_t>(from.magFilter);
        st.minFilter = static_cast<std::uint8_t>(from.minFilter);
        st.mipFilter = static_cast<std::uint8_t>(from.mipFilter);
        st.addressU = static_cast<std::uint8_t>(from.addressU);
        st.addressV = static_cast<std::uint8_t>(from.addressV);
        // The surface holder the game bound to the stage.
        if (!device) continue;
        const void* holder = device->stageSurfaces[stage];
        st.hasTexture = holder != nullptr;
        st.texture = SurfaceView(holder);
        if (stage == 0) s.patch = PatchOf(holder);
    }
    ++g_stats.stateReads;
    return s;
}

namespace {

gpu::BlendFactor Factor(std::uint8_t d3d) {
    switch (d3d) {   // D3DBLEND
    case 1: return gpu::BlendFactor::Zero;
    case 2: return gpu::BlendFactor::One;
    case 3: return gpu::BlendFactor::SourceColour;
    case 4: return gpu::BlendFactor::OneMinusSourceColour;
    case 5: return gpu::BlendFactor::SourceAlpha;
    case 6: return gpu::BlendFactor::OneMinusSourceAlpha;
    case 7: return gpu::BlendFactor::DestAlpha;
    case 8: return gpu::BlendFactor::OneMinusDestAlpha;
    case 9: return gpu::BlendFactor::DestColour;
    case 10: return gpu::BlendFactor::OneMinusDestColour;
    case 11: return gpu::BlendFactor::SourceAlphaSaturate;
    default: return gpu::BlendFactor::One;
    }
}

gpu::Compare Compare(std::uint8_t d3d) {
    switch (d3d) {   // D3DCMPFUNC
    case 1: return gpu::Compare::Never;
    case 2: return gpu::Compare::Less;
    case 3: return gpu::Compare::Equal;
    case 4: return gpu::Compare::LessEqual;
    case 5: return gpu::Compare::Greater;
    case 6: return gpu::Compare::NotEqual;
    case 7: return gpu::Compare::GreaterEqual;
    default: return gpu::Compare::Always;
    }
}

} // namespace

void ApplyPassState(gpu::PipelineDesc& p, const PassState& s) {
    // D3DCULL_CW culls triangles clockwise on screen, D3DCULL_CCW counter-clockwise ones (screen y
    // down in both APIs).
    if (s.cull == 2) {
        p.cull = gpu::Cull::Back;
        p.front = gpu::Winding::CounterClockwise;
    } else if (s.cull == 3) {
        p.cull = gpu::Cull::Back;
        p.front = gpu::Winding::Clockwise;
    } else {
        p.cull = gpu::Cull::None;
    }
    p.depth = kDepthFormat;
    p.depthCompare = s.depthTest ? Compare(s.depthFunc) : gpu::Compare::Always;
    p.depthWrite = s.depthTest && s.depthWrite;
    p.colour = kColourFormat;
    // The old render target has no alpha channel: destination alpha reads as one, so the new
    // target's alpha stays at the clear value.
    p.colourMask = gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    if (s.blend) {
        gpu::BlendFactor source = Factor(s.sourceBlend), dest = Factor(s.destBlend);
        if (s.sourceBlend == 12) {   // D3DBLEND_BOTHSRCALPHA
            source = gpu::BlendFactor::SourceAlpha;
            dest = gpu::BlendFactor::OneMinusSourceAlpha;
        } else if (s.sourceBlend == 13) {   // D3DBLEND_BOTHINVSRCALPHA
            source = gpu::BlendFactor::OneMinusSourceAlpha;
            dest = gpu::BlendFactor::SourceAlpha;
        }
        p.blend = {true, source, dest, source, dest};
    }
}

bool CreateMeshShader() {
    bool ok = static_cast<bool>(Pipeline(PassState{}, false, false));
    if (!ok) ei::Log("render: the mesh pipeline could not be created (see the gpu messages above)");
    return ok;
}

void MeshLayout(gpu::PipelineDesc& p, bool cached, bool positionsOnly) {
    // The frame's stream: Vertex (position, colour, specular, texture coordinates, normal) in buffer 0. A
    // mesh kept on the GPU: position, texture coordinates and normal in buffer 0, colour and specular in
    // buffer 1. The locations are the mesh program's (the casters': position 0, texture coordinates 1).
    static_assert(gpu_shaders::mesh::kAttrPos == 0 && gpu_shaders::mesh::kAttrCol == 1 && gpu_shaders::mesh::kAttrSpec == 2 &&
                  gpu_shaders::mesh::kAttrUv == 3 && gpu_shaders::mesh::kAttrNormal == 4 &&
                  gpu_shaders::shadow_caster::kAttrPos == 0 && gpu_shaders::shadow_caster::kAttrUv == 1);
    const std::uint8_t uv = positionsOnly ? 1 : 3;
    p.bufferCount = cached && !positionsOnly ? 2 : 1;
    p.buffers[0].stride = cached ? static_cast<std::uint16_t>(kCachedVertexBytes) : static_cast<std::uint16_t>(sizeof(Vertex));
    p.attributes[0] = {0, 0, gpu::VertexFormat::Float3, 0};
    p.attributes[1] = {uv, 0, gpu::VertexFormat::Float2, static_cast<std::uint16_t>(cached ? 12 : 20)};
    p.attributeCount = 2;
    if (positionsOnly) return;
    p.attributes[2] = {4, 0, gpu::VertexFormat::Float3, static_cast<std::uint16_t>(cached ? 20 : 28)};
    for (std::uint8_t k = 1; k <= 2; ++k) {
        p.attributes[2 + k] = {k, static_cast<std::uint8_t>(cached ? 1 : 0), gpu::VertexFormat::UByte4Norm,
                               static_cast<std::uint16_t>(cached ? (k - 1) * 4 : 12 + (k - 1) * 4)};
    }
    p.attributeCount = 5;
    if (cached) p.buffers[1].stride = static_cast<std::uint16_t>(kColourPairBytes);
}

void UploadMeshes(const Frame& f) {
    UploadCachedMeshes(f);   // the casters outside the view too
    if (f.meshBatches.empty()) return;
    if (!f.meshVertices.empty()) g_geometry.Upload(f.meshVertices, f.meshIndices);
}

bool BindMeshBatch(const MeshBatch& batch, Geometry& g, int* firstIndex) {
    if (batch.cached) {
        if (!BindCachedMesh(*batch.cached, g, firstIndex)) return false;
        *firstIndex += batch.firstIndex;
        return true;
    }
    *firstIndex = batch.firstIndex;
    return g_geometry.Bind(g);
}

void DrawMeshBatch(const Frame& f, const MeshBatch& batch, int anisotropy) {
    namespace program = gpu_shaders::mesh;
    Geometry g;
    int first = 0;
    if (batch.indexCount <= 0 || (batch.decal && !GroundDecalsDrawn()) || !BindMeshBatch(batch, g, &first)) return;
    const PassState& s = batch.state;
    gpu::Pipeline pipeline = Pipeline(s, batch.lines, batch.cached != nullptr);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    g.Bind();
    gpu::TextureBinding textures[3];
    for (int t = 0; t < 2; ++t) {
        const StageState& st = s.stages[t];
        textures[t].texture = st.texture ? st.texture : SurfaceView(nullptr);
        textures[t].sampler = anisotropy > 1 ? AnisotropicSampler(st, static_cast<std::uint32_t>(anisotropy)) : StageSampler(st);
    }
    textures[2] = {s.patch.texture ? s.patch.texture : SurfaceView(nullptr), textures[0].sampler};   // sampled as stage 0
    static_assert(program::kSamplerTex0 == 0 && program::kSamplerTex1 == 1 && program::kSamplerTex2 == 2);
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex0, textures);
    BindShadowMap(program::kSamplerShadowMap);
    BindPointShadows(program::kSamplerPointShadowMap);
    BindCloudMap(program::kSamplerCloudMap);
    BindOcclusion(program::kSamplerOcclusionMap);
    CountBindings(textures[0].texture);
    MeshVsParams vs{};
    vs.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    if (batch.distanceFog && batch.fogScale > 0.0f) {
        vs.distanceFog[0] = 1.0f;
        vs.distanceFog[1] = batch.fogNear;
        vs.distanceFog[2] = batch.fogScale / 255.0f;
    }
    vs.distanceFog[3] = batch.unclipped ? 1.0f : 0.0f;
    vs.shift[0] = f.pixelShift[0];
    vs.shift[1] = f.pixelShift[1];
    vs.shift[2] = batch.zScale;
    vs.light = batch.light >= 0 ? f.shadow.lights[static_cast<std::size_t>(batch.light)] : kIdentity;
    vs.uvOffset[0] = batch.uOffset;
    vs.uvOffset[1] = batch.vOffset;
    MeshFsParams fs{};
    const ShadowParams& shadow = ShadowUniforms(f, batch.light >= 0, batch.light >= 0);
    vs.normalOffset[0] = ShadowNormalOffset(shadow);
    MeshLightParams lighting{};   // unlit: on[0] 0
    if (batch.lighting >= 0) {
        lighting = f.meshLights[static_cast<std::size_t>(batch.lighting)];
        // Its lights' scene light becomes their point shadow, taken with the landscape's rows.
        for (int k = 0; k < kMostMeshLights; ++k) {
            float& w = lighting.pointColour[k][3];
            w = static_cast<float>(PointShadowSlot(f, static_cast<int>(w) - 1) + 1);
        }
        if (batch.landscape >= 0) {
            const Matrix4& m = f.shadow.point.models[static_cast<std::size_t>(batch.landscape)];
            std::memcpy(vs.landscape, m.m, sizeof(vs.landscape));
            fs.pointOn[0] = 1.0f;
            fs.pointOn[1] = batch.figure ? 1.0f : 0.0f;
            std::memcpy(fs.pointFront, batch.pointFront, sizeof(fs.pointFront));
        }
    }
    if (batch.decal) {   // a ground print: lit as the ground (its positions the landscape's), its normal up
        GroundDecalLight(f, batch.box, &lighting);
        for (int k = 0; k < 3; ++k) vs.landscape[k][k] = 1.0f;
        vs.normalOffset[1] = 1.0f;
        fs.pointOn[0] = 1.0f;
    }
    if (batch.figure && batch.front <= 1.0f) {   // a unit's: no shadow of its own to keep off
        fs.shadowFigure[0] = 1.0f;
        fs.shadowFigure[1] = batch.front;
        vs.normalOffset[0] = 0.0f;
    }
    OcclusionUniforms(batch.occluded, fs.occlusion, fs.occlusionMap);
    if (batch.light >= 0 && !batch.decal) {   // a world mesh
        MeshWeatherUniforms(fs.weather, fs.weatherSun, fs.weatherSunColour);
        if (batch.figure) {   // a unit's own
            fs.weather[0] = batch.weather[0];
            fs.weather[1] = batch.weather[1];
        }
        fs.weather[2] = batch.figure ? 1.0f : 0.0f;
    }
    fs.fogColour[0] = ((s.fogColour >> 16) & 0xFF) / 255.0f;
    fs.fogColour[1] = ((s.fogColour >> 8) & 0xFF) / 255.0f;
    fs.fogColour[2] = (s.fogColour & 0xFF) / 255.0f;
    fs.factor[0] = ((s.textureFactor >> 16) & 0xFF) / 255.0f;
    fs.factor[1] = ((s.textureFactor >> 8) & 0xFF) / 255.0f;
    fs.factor[2] = (s.textureFactor & 0xFF) / 255.0f;
    fs.factor[3] = ((s.textureFactor >> 24) & 0xFF) / 255.0f;
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
    if (s.patch.texture) {
        for (int k = 0; k < 4; ++k) {
            fs.patchTo[k] = s.patch.to[k];
            fs.patchMap[k] = s.patch.map[k];
            fs.patchInside[k] = s.patch.inside[k];
        }
    }
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockMeshVsParams, vs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockMeshFsParams, fs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowParams, shadow);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockMeshLightParams, lighting);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPointShadowParams, f.shadow.point.params);
    gpu::DrawIndexed(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(batch.indexCount));
    CountDraw(sizeof(vs) + sizeof(fs) + sizeof(shadow) + sizeof(lighting) + sizeof(f.shadow.point.params));
}

} // namespace detail

void BeginMesh(const TextureDevice* device, const MeshDraw& mesh) {
    g_pending = Pending{};
    NoteMeshDraw(mesh);
    if (!device || !mesh.matrix) return;
    Start();
    StatsTimer timer(&FrameStats::handOverMs);
    Frame& f = g_frame;
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time: drop the rest
        if (g_dropped++ == 0) ei::Log("render: too many meshes in one frame; the rest are dropped");
        return;
    }
    Matrix4 clip;
    if (mesh.screenMatrix) {
        int width = 0, height = 0;
        if (!GameTargetSize(&width, &height)) {
            Refuse(NoTargetSize, "a mesh draw with no render target size");
            return;
        }
        clip = ScreenToClip(*mesh.matrix, width, height);
    } else {
        clip = *mesh.matrix;
    }
    g_pending.active = true;
    g_pending.mesh = mesh;
    g_pending.device = device;
    g_pending.matrix = AddMatrix(clip);
    g_pending.caster = CurrentCaster();
    // A caster's box (its point shadows), and a unit's: taken in MeshPass (a kept mesh keeps it).
    g_pending.boxed = g_pending.caster && mesh.positions && mesh.positionCount > 0;
    g_pending.figure = CurrentCasterFigure() && g_pending.boxed;
    // A figure part lit on the GPU, unless the draw set its colours to the device's. Only its
    // normals are read (the other mesh draws' callers may pass anything there).
    if (const MeshLighting* lighting = MeshSourceLighting(); lighting && !mesh.coloursWritten) {
        MeshLightParams params;
        MeshLightUniforms(*lighting, &params);
        f.meshLights.push_back(params);
        g_pending.lighting = static_cast<int>(f.meshLights.size()) - 1;
    } else {
        g_pending.mesh.normals = nullptr;
        g_pending.mesh.words = nullptr;
    }
    // The distance fog's range: the pipeline's (its start and 255 / length).
    g_pending.distanceFog = mesh.distanceFog;
    if (const TransformPipeline* pipeline = g_pending.device->pipeline) {
        g_pending.fogNear = pipeline->fogStart;
        g_pending.fogScale = pipeline->fogScale;
    } else {
        g_pending.distanceFog = false;
    }
}

void MeshPass(const TextureDevice* device) {
    // Not handed over (BeginMesh logged why) or given up after a failed Build.
    if (!g_pending.active || device != g_pending.device) return;
    StatsTimer timer(&FrameStats::handOverMs);
    Frame& f = g_frame;
    if (!g_pending.built) {
        g_pending.built = true;
        ++g_stats.meshes;
        if (g_pending.caster) NoteCasterPart(MeshSourceOwner());
        // A figure mesh kept on the GPU, else its vertices into the frame's stream.
        g_pending.cached = KeepMesh(g_pending.mesh);
        if (g_pending.cached) {
            g_pending.firstIndex = 0;
            g_pending.indexCount = CachedIndexCount(*g_pending.cached);
        } else {
            std::size_t first = f.meshVertices.size();
            if (!Build(f, g_pending)) {
                Refuse(BadCorner, "a mesh corner past its positions");
                g_pending.active = false;
                return;
            }
            CountReuse(false, g_pending.mesh.positions, 0, f.meshVertices.data() + first, f.meshVertices.size() - first);
            if (!g_pending.mesh.corners) {
                ++g_stats.flatMeshes;
                g_stats.flatVertexBytes += (f.meshVertices.size() - first) * sizeof(Vertex);
            }
        }
        if (g_pending.boxed) {
            if (g_pending.cached) CachedMeshBox(*g_pending.cached, g_pending.mesh, g_pending.box);
            else PositionBox(g_pending.mesh, g_pending.box);
        } else if (g_pending.mesh.groundDecal) {
            PositionBox(g_pending.mesh, g_pending.box);
        }
    }
    if (g_pending.indexCount > 0) {
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
        batch.model = true;
        batch.flat = !g_pending.mesh.corners;
        batch.decal = batch.flat && g_pending.mesh.groundDecal;
        batch.caster = g_pending.caster;
        batch.figure = g_pending.figure;
        batch.boxed = g_pending.boxed;
        std::memcpy(batch.box, g_pending.box, sizeof(batch.box));
        batch.cached = g_pending.cached;
        batch.lighting = g_pending.lighting;
        if (batch.cached && g_pending.mesh.moveUv) {   // the stream has them added (Build)
            batch.uOffset = g_pending.mesh.uOffset;
            batch.vOffset = g_pending.mesh.vOffset;
        }
        ++g_stats.meshPasses;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
}

void EndMesh() { g_pending = Pending{}; }

void SetGroundDecals(bool on) {
    if (on != g_groundDecals) ei::Log("render: ground prints %s", on ? "drawn" : "left out");
    g_groundDecals = on;
}

namespace detail {
bool GroundDecalsDrawn() { return g_groundDecals; }
} // namespace detail

void TlDraw(const TextureDevice* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            Primitive primitive) {
    if (!device || !vertices || vertexCount <= 0) return;   // nothing to draw
    Start();
    if (fvf != kFvfTextured && fvf != kFvfUntextured) {   // not a layout the game uses for these
        Refuse(OtherFvf, "a pre-transformed draw in another vertex format");
        return;
    }
    StatsTimer timer(&FrameStats::handOverMs);
    ++g_stats.transformedDraws;
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height)) {
        Refuse(NoTargetSize, "a pre-transformed draw with no render target size");
        return;
    }
    Frame& f = g_frame;
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time
        Refuse(VertexLimit, "too many pre-transformed vertices in one frame");
        return;
    }
    Matrix4 clip = ScreenToClip(kIdentity, width, height);
    int stride = fvf == kFvfTextured ? 32 : 24;
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < vertexCount; ++i) {
        const auto* p = static_cast<const std::uint8_t*>(vertices) + static_cast<std::size_t>(i) * stride;
        Vertex v{};
        std::memcpy(&v.position, p, 12);   // x, y, z (rhw taken as 1: affine interpolation)
        std::memcpy(&v.colour, p + 16, 8);
        if (stride == 32) std::memcpy(&v.u, p + 24, 8);
        f.meshVertices.push_back(v);
    }
    // The primitive as a list over absolute indices.
    int count = indices ? indexCount : vertexCount;
    auto at = [&](int k) { return base + (indices ? indices[k] : static_cast<std::uint16_t>(k)); };
    auto valid = [&](int k) { return (indices ? indices[k] : k) < vertexCount; };
    int firstIndex = static_cast<int>(f.meshIndices.size());
    const bool lines = primitive == Primitive::LineStrip;
    for (int k = 0; k < count; ++k) {
        if (lines) {
            if (k >= 1 && valid(k - 1) && valid(k)) { f.meshIndices.push_back(at(k - 1)); f.meshIndices.push_back(at(k)); }
        } else if (k % 3 == 2 && valid(k - 2) && valid(k - 1) && valid(k)) {
            for (int j = 2; j >= 0; --j) f.meshIndices.push_back(at(k - j));
        }
    }
    int drawn = static_cast<int>(f.meshIndices.size()) - firstIndex;
    if (drawn > 0) {
        MeshBatch batch{};
        batch.matrix = AddMatrix(clip);
        batch.firstIndex = firstIndex;
        batch.indexCount = drawn;
        batch.state = ReadPassState(device);
        batch.unclipped = true;
        batch.zScale = 1.0f;
        batch.lines = lines;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    } else {
        f.meshVertices.resize(base);
    }
}

void ScreenDraw(const TextureDevice* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount) {
    if (!device || !vertices || count <= 0) return;   // nothing to draw
    Start();
    StatsTimer timer(&FrameStats::handOverMs);
    ++g_stats.transformedDraws;
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height)) {
        Refuse(NoTargetSize, "a screen draw with no render target size");
        return;
    }
    Frame& f = g_frame;
    Matrix4 clip = ScreenToClip(kIdentity, width, height);
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
        MeshBatch batch{};
        batch.matrix = AddMatrix(clip);
        batch.firstIndex = firstIndex;
        batch.indexCount = drawn;
        batch.state = ReadPassState(device);
        batch.unclipped = true;
        batch.zScale = 1.0f;
        batch.lines = lines;
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
}

} // namespace render
