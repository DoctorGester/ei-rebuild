// Meshes: each pass of an old-path device mesh draw becomes a batch with the old device's state of that pass.
// See docs/notes/renderer.md, "The renderer's source files".
#include "../game/model.h"   // TransformPipeline
#include "../game/texture.h"
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"
#include "render_state.h"

#include <cstring>
#include <optional>
#include <unordered_map>

namespace render {

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
    const Object3D* caster = nullptr;   // render::BeginCaster's object
    bool figure = false;            // ... a unit's
    const Object3D* interfaceUnit = nullptr;   // render::BeginInterfaceUnit's unit, if any
    bool boxed = false;             // its positions' box in model space taken (a caster's: MeshPass)
    float box[6] = {};
    CachedMesh* cached = nullptr;   // kept on the GPU, else built into the frame's stream
    int lighting = -1;              // Frame::meshLights
};
static Pending g_pending;
static int g_dropped = 0;
static bool g_groundDecals = true;           // SetGroundDecals
static bool g_weatherParticles = true;       // SetWeatherParticles
static bool g_interfaceUnitWeather = true;   // SetInterfaceUnitWeather

// A draw the renderer does not take is lost (there is no other way to draw): each reason logged the first time.
enum Refusal { NoTargetSize, BadCorner, OtherFvf, VertexLimit, RefusalCount };
static bool g_refused[RefusalCount] = {};

static void Refuse(Refusal reason, const char* what) {
    if (g_refused[reason]) return;
    g_refused[reason] = true;
    ei::Log("render: %s; the draw is lost", what);
}

// A matrix giving screen pixels after the divide (sx = row0.p / row3.p, sy = row1.p / row3.p) as a
// clip-space one: ndc x = 2 sx / width - 1, ndc y = 1 - 2 sy / height; z and w unchanged.
static Matrix4 ScreenToClip(const Matrix4& screen, Size target) {
    float kx = 2.0f / target.w;
    float ky = 2.0f / target.h;
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

static const Matrix4 kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

// The mesh's corners as vertices and its triangles, once per mesh (the passes share them).
static bool Build(Frame& f, Pending& p) {
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
        Vertex v{};
        v.position = PacketVertex(m.positions, position);
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
    AppendTriangles(m.indices, m.indexCount, m.cornerCount, base, f.meshIndices);
    p.indexCount = static_cast<int>(f.meshIndices.size()) - p.firstIndex;
    return true;
}

void AppendTriangles(const std::uint16_t* indices, int indexCount, int cornerCount, std::uint32_t base,
                     std::vector<std::uint32_t>& out) {
    for (int k = 0; k + 2 < indexCount; k += 3) {
        if (indices[k] >= cornerCount || indices[k + 1] >= cornerCount || indices[k + 2] >= cornerCount) continue;
        for (int j = 0; j < 3; ++j) out.push_back(base + indices[k + j]);
    }
}

// --- GPU side ------------------------------------------------------------------------------------------------

static GeometryStream g_geometry("ei-mesh", false);

static gpu::PipelineDesc MeshDesc(const PassState& s, bool lines, bool cached, int samples) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::mesh::kIndex;
    MeshLayout(p, cached, false);
    p.primitive = lines ? gpu::Primitive::Lines : gpu::Primitive::Triangles;
    ApplyPassState(p, s);
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

static gpu::Pipeline Pipeline(const PassState& s, bool lines, bool cached) {
    return gpu::MakePipeline(MeshDesc(s, lines, cached, PassSampleCount()), "ei-mesh");
}

// The pass states every battle and menu draws meshes with (the pipelines a run makes, logged; the rare ones are made at
// their first draw).
void ListMeshPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    struct Wanted {
        Cull cull = Cull::None;
        bool blend = false;
        Blend sourceBlend = Blend::One, destBlend = Blend::Zero;
        bool depthTest = false, depthWrite = false;
        bool cached = false;
    };
    constexpr Cull kCw = Cull::Clockwise, kNone = Cull::None;
    constexpr Blend kSourceAlpha = Blend::SourceAlpha, kInvSourceAlpha = Blend::InvSourceAlpha, kOne = Blend::One;
    static constexpr Wanted kWanted[] = {
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, true, true},
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, true, false},
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {kNone, true, kSourceAlpha, kInvSourceAlpha, true, false, true},
        {kNone, true, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {kNone, false, kOne, Blend::Zero, true, true, false},
        {kCw, true, kSourceAlpha, kOne, true, false, false},
        {kCw, true, kSourceAlpha, kInvSourceAlpha, false, false, false},
    };
    for (const Wanted& w : kWanted) {
        PassState s;
        s.cull = w.cull;
        s.blend = w.blend;
        s.sourceBlend = w.sourceBlend;
        s.destBlend = w.destBlend;
        s.depthTest = w.depthTest;
        s.depthWrite = w.depthWrite;
        out.push_back(MeshDesc(s, false, w.cached, samples));
    }
}

PassState ReadPassState(const TextureDevice* device) {
    const RenderState& d = g_renderState;
    PassState s;
    s.blend = d.blend;
    s.sourceBlend = d.sourceBlend;
    s.destBlend = d.destBlend;
    s.alphaTest = d.alphaTest;
    s.alphaFunc = d.alphaFunc;
    s.alphaReference = d.alphaReference;
    s.depthTest = d.depthTest;
    s.depthWrite = d.depthWrite;
    s.depthFunc = d.depthFunc;
    s.cull = d.cull;
    s.specular = d.specular;
    s.fog = d.fog;
    s.fogColour = d.fogColour;
    s.textureFactor = d.textureFactor;
    for (int stage = 0; stage < 2; ++stage) {
        const TextureStage& from = d.stages[stage];
        StageState& st = s.stages[stage];
        if (stage == 1 && from.combiner.colourOp == TextureOp::Disable) break;   // stage 1 unused
        st.combiner = from.combiner;
        st.magFilter = from.magFilter;
        st.minFilter = from.minFilter;
        st.mipFilter = from.mipFilter;
        st.addressU = from.addressU;
        st.addressV = from.addressV;
        // The surface holder the game bound to the stage.
        if (!device) continue;
        const Surface* holder = device->stageSurfaces[stage];
        st.hasTexture = holder != nullptr;
        st.texture = SurfaceView(holder);
        if (stage == 0) s.patch = PatchOf(holder);
    }
    ++g_stats.counts[FrameStats::StateReads];
    return s;
}

static gpu::BlendFactor ToGpu(Blend factor) {
    switch (factor) {
    case Blend::Zero: return gpu::BlendFactor::Zero;
    case Blend::One: return gpu::BlendFactor::One;
    case Blend::SourceColour: return gpu::BlendFactor::SourceColour;
    case Blend::InvSourceColour: return gpu::BlendFactor::OneMinusSourceColour;
    case Blend::SourceAlpha: return gpu::BlendFactor::SourceAlpha;
    case Blend::InvSourceAlpha: return gpu::BlendFactor::OneMinusSourceAlpha;
    case Blend::DestAlpha: return gpu::BlendFactor::DestAlpha;
    case Blend::InvDestAlpha: return gpu::BlendFactor::OneMinusDestAlpha;
    case Blend::DestColour: return gpu::BlendFactor::DestColour;
    case Blend::InvDestColour: return gpu::BlendFactor::OneMinusDestColour;
    case Blend::SourceAlphaSaturate: return gpu::BlendFactor::SourceAlphaSaturate;
    default: return gpu::BlendFactor::One;   // BothSourceAlpha, BothInvSourceAlpha: ApplyPassState sets both factors
    }
}

static gpu::Compare ToGpu(Compare function) {
    switch (function) {
    case Compare::Never: return gpu::Compare::Never;
    case Compare::Less: return gpu::Compare::Less;
    case Compare::Equal: return gpu::Compare::Equal;
    case Compare::LessEqual: return gpu::Compare::LessEqual;
    case Compare::Greater: return gpu::Compare::Greater;
    case Compare::NotEqual: return gpu::Compare::NotEqual;
    case Compare::GreaterEqual: return gpu::Compare::GreaterEqual;
    default: return gpu::Compare::Always;
    }
}

void ApplyPassState(gpu::PipelineDesc& p, const PassState& s) {
    // D3DCULL_CW culls triangles clockwise on screen, D3DCULL_CCW counter-clockwise ones (screen y
    // down in both APIs).
    switch (s.cull) {
    case Cull::Clockwise:
        p.cull = gpu::CullFace::Back;
        p.front = gpu::Winding::CounterClockwise;
        break;
    case Cull::CounterClockwise:
        p.cull = gpu::CullFace::Back;
        p.front = gpu::Winding::Clockwise;
        break;
    default: p.cull = gpu::CullFace::None; break;
    }
    p.depth = kDepthFormat;
    p.depthCompare = s.depthTest ? ToGpu(s.depthFunc) : gpu::Compare::Always;
    p.depthWrite = s.depthTest && s.depthWrite;
    p.colour = kColourFormat;
    // The old render target has no alpha channel: destination alpha reads as one, so the new
    // target's alpha stays at the clear value.
    p.colourMask = gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    if (s.blend) {
        gpu::BlendFactor source = ToGpu(s.sourceBlend), dest = ToGpu(s.destBlend);
        if (s.sourceBlend == Blend::BothSourceAlpha) {
            source = gpu::BlendFactor::SourceAlpha;
            dest = gpu::BlendFactor::OneMinusSourceAlpha;
        } else if (s.sourceBlend == Blend::BothInvSourceAlpha) {
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
    // A member's offset: in MeshVertex (or ColourPair, buffer 1) when kept, else in Vertex.
    auto at = [cached](std::size_t kept, std::size_t streamed) { return static_cast<std::uint16_t>(cached ? kept : streamed); };
    p.bufferCount = cached && !positionsOnly ? 2 : 1;
    p.buffers[0].stride = at(sizeof(MeshVertex), sizeof(Vertex));
    p.attributes[0] = {0, 0, gpu::VertexFormat::Float3, at(offsetof(MeshVertex, position), offsetof(Vertex, position))};
    p.attributes[1] = {uv, 0, gpu::VertexFormat::Float2, at(offsetof(MeshVertex, u), offsetof(Vertex, u))};
    p.attributeCount = 2;
    if (positionsOnly) return;
    const std::uint8_t colours = cached ? 1 : 0;
    p.attributes[2] = {4, 0, gpu::VertexFormat::Float3, at(offsetof(MeshVertex, normal), offsetof(Vertex, normal))};
    p.attributes[3] = {1, colours, gpu::VertexFormat::UByte4Norm, at(offsetof(ColourPair, colour), offsetof(Vertex, colour))};
    p.attributes[4] = {2, colours, gpu::VertexFormat::UByte4Norm, at(offsetof(ColourPair, specular), offsetof(Vertex, specular))};
    p.attributeCount = 5;
    if (cached) p.buffers[1].stride = sizeof(ColourPair);
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
    BindShadowMap(f, program::kSamplerShadowMap);
    BindPointShadows(f, program::kSamplerPointShadowMap);
    BindCloudMap(program::kSamplerCloudMap);
    BindOcclusion(program::kSamplerOcclusionMap);
    BindRoofMap(program::kSamplerRoofMap);
    CountBindings(textures[0].texture);
    MeshVsParams vs{};
    vs.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    if (batch.distanceFog && batch.fogScale > 0.0f) {
        vs.distanceFog = 1;
        vs.fogStart = batch.fogNear;
        vs.fogScale = batch.fogScale / 255.0f;
    }
    vs.unclipped = batch.unclipped ? 1 : 0;
    vs.shift[0] = f.pixelShift[0];
    vs.shift[1] = f.pixelShift[1];
    vs.depthScale = batch.zScale;
    vs.light = batch.light >= 0 ? f.shadow.lights[static_cast<std::size_t>(batch.light)] : kIdentity;
    vs.uvOffset[0] = batch.uOffset;
    vs.uvOffset[1] = batch.vOffset;
    MeshFsParams fs{};
    const ShadowParams& shadow = ShadowUniforms(f, batch.light >= 0, batch.light >= 0);
    vs.normalOffset = ShadowNormalOffset(f, batch.light >= 0);
    MeshLightParams lighting{};   // unlit
    if (batch.lighting >= 0) {
        // Its lights take their point shadows with the landscape's rows.
        lighting = MeshLightUniforms(f, f.meshLights[static_cast<std::size_t>(batch.lighting)]);
        if (batch.landscape >= 0) {
            const Matrix4& m = f.shadow.point.models[static_cast<std::size_t>(batch.landscape)];
            std::memcpy(vs.landscape, m.m, sizeof(vs.landscape));
            fs.pointShadows = 1;
            fs.pointUnit = batch.figure ? 1 : 0;
            std::memcpy(fs.pointFront, batch.pointFront, sizeof(fs.pointFront));
        }
    }
    if (batch.decal) {   // a ground print: lit as the ground (its positions the landscape's), its normal up
        lighting = MeshLightUniforms(f, GroundDecalLight(f, batch.box));
        for (int k = 0; k < 3; ++k) vs.landscape[k][k] = 1.0f;
        vs.normalUp = 1;
        fs.pointShadows = 1;
    }
    if (batch.figure && batch.front <= 1.0f) {   // a unit's: no shadow of its own to keep off
        fs.unitShadow = 1;
        fs.unitFront = batch.front;
        vs.normalOffset = 0.0f;
    }
    OcclusionUniforms(batch.occluded, fs.occlusion, fs.occlusionMap);
    // The weather's kinds in mesh.hlsl.
    constexpr int kUnitWeather = 1;
    constexpr int kFigureWeather = 2;
    if (batch.light >= 0 && !batch.decal) {   // a world mesh
        MeshWeatherUniforms(&fs);
        if (batch.figure) {   // a unit's own
            fs.wet = batch.weather[0];
            fs.snow = batch.weather[1];
            fs.weatherKind = kUnitWeather;
        }
    } else if (batch.interfaceUnit) {   // an interface figure of a world unit: its weather, no map's space
        fs.wet = batch.weather[0];
        fs.snow = batch.weather[1];
        fs.weatherKind = kFigureWeather;
    }
    UnpackColour(s.fogColour, fs.fogColour);
    fs.fog = s.fog ? 1 : 0;
    UnpackColour(s.textureFactor, fs.factor);
    fs.factor[3] = ((s.textureFactor >> 24) & 0xFF) / 255.0f;
    // The shader takes render_state.h's values as they are.
    const StageCombiner& c0 = s.stages[0].combiner;
    const StageCombiner& c1 = s.stages[1].combiner;
    fs.colourStage0[0] = static_cast<int>(c0.colourOp);
    fs.colourStage0[1] = c0.colourArg1;
    fs.colourStage0[2] = c0.colourArg2;
    fs.colourStage1[0] = static_cast<int>(c0.colourOp != TextureOp::Disable ? c1.colourOp : TextureOp::Disable);
    fs.colourStage1[1] = c1.colourArg1;
    fs.colourStage1[2] = c1.colourArg2;
    fs.alphaStage0[0] = static_cast<int>(c0.alphaOp);
    fs.alphaStage0[1] = c0.alphaArg1;
    fs.alphaStage0[2] = c0.alphaArg2;
    fs.alphaStage1[0] = static_cast<int>(c1.alphaOp);
    fs.alphaStage1[1] = c1.alphaArg1;
    fs.alphaStage1[2] = c1.alphaArg2;
    fs.specular = s.specular ? 1 : 0;
    fs.alphaFunc = s.alphaTest ? static_cast<int>(s.alphaFunc) : 0;
    fs.alphaReference = s.alphaReference;
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

void BeginMesh(const TextureDevice* device, const MeshDraw& mesh) {
    g_pending = Pending{};
    NoteMeshDraw(mesh);
    if (!device || !mesh.matrix) return;
    Start();
    Timer timer(FrameStats::HandOverMs);
    Frame& f = g_frame;
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time: drop the rest
        LogFirst(g_dropped, 1, "render: too many meshes in one frame; the rest are dropped");
        return;
    }
    Matrix4 clip;
    if (mesh.screenMatrix) {
        const std::optional<Size> target = GameTargetSize();
        if (!target) {
            Refuse(NoTargetSize, "a mesh draw with no render target size");
            return;
        }
        clip = ScreenToClip(*mesh.matrix, *target);
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
    g_pending.interfaceUnit = g_interfaceUnitWeather ? CurrentInterfaceUnit() : nullptr;
    // A figure part lit on the GPU, unless the draw set its colours to the device's. Only its
    // normals are read (the other mesh draws' callers may pass anything there).
    if (const MeshLighting* lighting = MeshSourceLighting(); lighting && !mesh.coloursWritten) {
        f.meshLights.push_back(FigureLight(*lighting));
        g_pending.lighting = static_cast<int>(f.meshLights.size()) - 1;
    } else if (!g_pending.interfaceUnit) {
        g_pending.mesh.normals = nullptr;   // an interface figure with weather keeps them (its snow settles by them)
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
    Timer timer(FrameStats::HandOverMs);
    Frame& f = g_frame;
    if (!g_pending.built) {
        g_pending.built = true;
        ++g_stats.counts[FrameStats::Meshes];
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
                ++g_stats.counts[FrameStats::FlatMeshes];
                g_stats.counts[FrameStats::FlatVertexBytes] += (f.meshVertices.size() - first) * sizeof(Vertex);
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
        batch.interfaceUnit = g_pending.interfaceUnit;
        batch.boxed = g_pending.boxed;
        std::memcpy(batch.box, g_pending.box, sizeof(batch.box));
        batch.cached = g_pending.cached;
        batch.lighting = g_pending.lighting;
        if (batch.cached && g_pending.mesh.moveUv) {   // the stream has them added (Build)
            batch.uOffset = g_pending.mesh.uOffset;
            batch.vOffset = g_pending.mesh.vOffset;
        }
        ++g_stats.counts[FrameStats::MeshPasses];
        f.meshBatches.push_back(batch);
        f.commands.push_back(Command{Command::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
}

void EndMesh() { g_pending = Pending{}; }

void SetGroundDecals(bool on) {
    if (on != g_groundDecals) ei::Log("render: ground prints %s", on ? "drawn" : "left out");
    g_groundDecals = on;
}

bool GroundDecalsDrawn() { return g_groundDecals; }

void SetWeatherParticles(bool on) {
    if (on != g_weatherParticles) ei::Log("render: rain and snow particles %s", on ? "drawn" : "left out");
    g_weatherParticles = on;
}

bool WeatherParticlesDrawn() { return g_weatherParticles; }

void SetInterfaceUnitWeather(bool on) { g_interfaceUnitWeather = on; }

void TlDraw(const TextureDevice* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            TlPrimitive primitive) {
    if (!device || !vertices || vertexCount <= 0) return;   // nothing to draw
    Start();
    if (fvf != kFvfTextured && fvf != kFvfUntextured) {   // not a layout the game uses for these
        Refuse(OtherFvf, "a pre-transformed draw in another vertex format");
        return;
    }
    Timer timer(FrameStats::HandOverMs);
    ++g_stats.counts[FrameStats::TransformedDraws];
    const std::optional<Size> target = GameTargetSize();
    if (!target) {
        Refuse(NoTargetSize, "a pre-transformed draw with no render target size");
        return;
    }
    Frame& f = g_frame;
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time
        Refuse(VertexLimit, "too many pre-transformed vertices in one frame");
        return;
    }
    Matrix4 clip = ScreenToClip(kIdentity, *target);
    // kFvfUntextured's vertex is a ScreenVertex; kFvfTextured's has the texture coordinates after it.
    struct TexturedVertex {
        ScreenVertex screen;
        float u = 0.0f, v = 0.0f;
    };
    const std::size_t stride = fvf == kFvfTextured ? sizeof(TexturedVertex) : sizeof(ScreenVertex);
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < vertexCount; ++i) {
        TexturedVertex t;
        std::memcpy(&t, static_cast<const std::uint8_t*>(vertices) + static_cast<std::size_t>(i) * stride, stride);
        const ScreenVertex& s = t.screen;   // rhw taken as 1: affine interpolation
        f.meshVertices.push_back(Vertex{s.x, s.y, s.z, s.colour, s.specular, t.u, t.v});
    }
    // The primitive as a list over absolute indices.
    int count = indices ? indexCount : vertexCount;
    auto at = [&](int k) { return base + (indices ? indices[k] : static_cast<std::uint16_t>(k)); };
    auto valid = [&](int k) { return (indices ? indices[k] : k) < vertexCount; };
    int firstIndex = static_cast<int>(f.meshIndices.size());
    const bool lines = primitive == TlPrimitive::LineStrip;
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
    Timer timer(FrameStats::HandOverMs);
    ++g_stats.counts[FrameStats::TransformedDraws];
    const std::optional<Size> target = GameTargetSize();
    if (!target) {
        Refuse(NoTargetSize, "a screen draw with no render target size");
        return;
    }
    Frame& f = g_frame;
    Matrix4 clip = ScreenToClip(kIdentity, *target);
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
        AppendTriangles(indices, indexCount, count, base, f.meshIndices);
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
