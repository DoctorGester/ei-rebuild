// Meshes: each pass of an old-path device mesh draw becomes a batch with the old device's state of that pass.
// See docs/notes/renderer.md, "The renderer's source files".
#include "engine/scene/device.h"
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"
#include "render_state.h"

#include <algorithm>
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
    const Object3D* caster = nullptr;   // render::BeginCaster's object
    int casterSlot = -1;                // ... its place in ShadowFrame::casters
    bool figure = false;            // ... a unit's
    const Object3D* interfaceUnit = nullptr;   // render::BeginInterfaceUnit's unit, if any
    bool boxed = false;             // its positions' box in model space taken (a caster's: MeshPass)
    float box[6] = {};
    CachedMesh* cached = nullptr;   // kept on the GPU, else built into the frame's stream
    int lighting = -1;              // Frame::meshLights
    int placement = -1;             // Frame::models
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
static bool Build(Frame& f, Pending& pending) {
    const MeshDraw& mesh = pending.mesh;
    auto base = static_cast<std::uint32_t>(f.meshVertices.size());
    for (int i = 0; i < mesh.cornerCount; ++i) {
        // Without corners (the flat geometry): vertex i, one colour pair for all.
        const MeshCorner flat{static_cast<std::uint16_t>(i), 0, static_cast<std::uint16_t>(i)};
        const MeshCorner& corner = mesh.corners ? mesh.corners[i] : flat;
        int position = corner.position;
        if (position >= mesh.positionCount) {
            f.meshVertices.resize(base);
            return false;
        }
        Vertex vertex{};
        vertex.position = PacketVertex(mesh.positions, position);
        vertex.colour = mesh.colours ? mesh.colours[corner.normal] : mesh.colour;
        vertex.specular = mesh.colours ? mesh.speculars[corner.normal] : mesh.specular;
        vertex.normal = CornerNormal(mesh, corner.normal);
        vertex.u = mesh.uvs[corner.texCoord].u;
        vertex.v = mesh.uvs[corner.texCoord].v;
        if (mesh.moveUv) {
            vertex.u = mesh.uOffset + vertex.u;
            vertex.v = mesh.vOffset + vertex.v;
        }
        f.meshVertices.push_back(vertex);
    }
    pending.firstIndex = static_cast<int>(f.meshIndices.size());
    AppendTriangles(mesh.indices, mesh.indexCount, mesh.cornerCount, base, f.meshIndices);
    pending.indexCount = static_cast<int>(f.meshIndices.size()) - pending.firstIndex;
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

// The mesh pipelines by a packed key of what MeshDesc and MakePassPipeline read (the pass state's cull, depth and
// blend, lines, kept geometry, the pass's samples and depth copy), for the device they were made on: found without a
// description's hash per draw. A pipeline that could not be made is kept as none (MakePipeline does not try again).
struct KeyedPipeline {
    std::uint64_t key = 0;
    gpu::Pipeline pipeline;
};
static std::vector<KeyedPipeline> g_keyedPipelines;
static std::uint32_t g_keyedPipelinesDevice = 0;   // the gpu::DeviceSerial they were made on

// What of a pipeline the pass state, lines and kept geometry choose (Pipeline's key without the pass's part).
static std::uint64_t StateKey(const PassState& s, bool lines, bool cached) {
    auto bits = [](auto v) { return static_cast<std::uint64_t>(v); };
    return bits(s.cull) | bits(s.depthTest) << 8 | bits(s.depthFunc) << 9 | bits(s.depthWrite) << 17 | bits(s.blend) << 18 |
           bits(s.sourceBlend) << 19 | bits(s.destBlend) << 27 | bits(lines) << 35 | bits(cached) << 36;
}

static gpu::Pipeline Pipeline(const PassState& s, bool lines, bool cached) {
    const int samples = PassSampleCount();
    const bool depthCopy = PassDepthCopy();
    auto bits = [](auto v) { return static_cast<std::uint64_t>(v); };
    const std::uint64_t key =
        StateKey(s, lines, cached) | bits(depthCopy) << 37 | bits(static_cast<std::uint8_t>(samples)) << 38;
    if (!gpu::Started()) return MakePassPipeline(MeshDesc(s, lines, cached, samples), "ei-mesh");   // none
    if (g_keyedPipelinesDevice != gpu::DeviceSerial()) {
        g_keyedPipelines.clear();
        g_keyedPipelinesDevice = gpu::DeviceSerial();
    }
    for (const KeyedPipeline& k : g_keyedPipelines)
        if (k.key == key) return k.pipeline;
    const gpu::Pipeline made = MakePassPipeline(MeshDesc(s, lines, cached, samples), "ei-mesh");
    g_keyedPipelines.push_back({key, made});
    return made;
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
    constexpr Blend kSourceAlpha = Blend::SourceAlpha, kInvSourceAlpha = Blend::InvSourceAlpha;
    static constexpr Wanted kWanted[] = {
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, true, true},
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, true, false},
        {kCw, true, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {kNone, true, kSourceAlpha, kInvSourceAlpha, true, false, true},
        {kNone, true, kSourceAlpha, kInvSourceAlpha, true, false, false},
        {kNone, false, Blend::One, Blend::Zero, true, true, false},
        {kCw, true, kSourceAlpha, Blend::One, true, false, false},
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
    const RenderState& current = g_renderState;
    PassState pass;
    pass.blend = current.blend;
    pass.sourceBlend = current.sourceBlend;
    pass.destBlend = current.destBlend;
    pass.alphaTest = current.alphaTest;
    pass.alphaFunc = current.alphaFunc;
    pass.alphaReference = current.alphaReference;
    pass.depthTest = current.depthTest;
    pass.depthWrite = current.depthWrite;
    pass.depthFunc = current.depthFunc;
    pass.cull = current.cull;
    pass.specular = current.specular;
    pass.fog = current.fog;
    pass.fogColour = current.fogColour;
    pass.textureFactor = current.textureFactor;
    for (int stage = 0; stage < 2; ++stage) {
        const TextureStage& from = current.stages[stage];
        StageState& to = pass.stages[stage];
        if (stage == 1 && from.combiner.colourOp == TextureOp::Disable) break;   // stage 1 unused
        to.combiner = from.combiner;
        to.magFilter = from.magFilter;
        to.minFilter = from.minFilter;
        to.mipFilter = from.mipFilter;
        to.addressU = from.addressU;
        to.addressV = from.addressV;
        // The surface holder the game bound to the stage.
        if (!device) continue;
        const Surface* holder = device->stageSurfaces[stage];
        to.hasTexture = holder != nullptr;
        to.texture = SurfaceView(holder);
        if (stage == 0) pass.patch = PatchOf(holder);
    }
    ++g_stats.counts[FrameStats::Count::StateReads];
    return pass;
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

void ApplyPassState(gpu::PipelineDesc& desc, const PassState& state) {
    // D3DCULL_CW culls triangles clockwise on screen, D3DCULL_CCW counter-clockwise ones (screen y
    // down in both APIs).
    switch (state.cull) {
    case Cull::Clockwise:
        desc.cull = gpu::CullFace::Back;
        desc.front = gpu::Winding::CounterClockwise;
        break;
    case Cull::CounterClockwise:
        desc.cull = gpu::CullFace::Back;
        desc.front = gpu::Winding::Clockwise;
        break;
    default: desc.cull = gpu::CullFace::None; break;
    }
    desc.depth = kDepthFormat;
    desc.depthCompare = state.depthTest ? ToGpu(state.depthFunc) : gpu::Compare::Always;
    desc.depthWrite = state.depthTest && state.depthWrite;
    desc.colour = kColourFormat;
    // The old render target has no alpha channel: destination alpha reads as one, so the new
    // target's alpha stays at the clear value.
    desc.colourMask = gpu::ColourMask::Red | gpu::ColourMask::Green | gpu::ColourMask::Blue;
    if (state.blend) {
        gpu::BlendFactor source = ToGpu(state.sourceBlend), dest = ToGpu(state.destBlend);
        if (state.sourceBlend == Blend::BothSourceAlpha) {
            source = gpu::BlendFactor::SourceAlpha;
            dest = gpu::BlendFactor::OneMinusSourceAlpha;
        } else if (state.sourceBlend == Blend::BothInvSourceAlpha) {
            source = gpu::BlendFactor::OneMinusSourceAlpha;
            dest = gpu::BlendFactor::SourceAlpha;
        }
        desc.blend = {true, source, dest, source, dest};
    }
}

bool CreateMeshShader() {
    bool ok = static_cast<bool>(Pipeline(PassState{}, false, false));
    if (!ok) ei::Log("render: the mesh pipeline could not be created (see the gpu messages above)");
    return ok;
}

void MeshLayout(gpu::PipelineDesc& desc, bool cached, bool positionsOnly) {
    // The frame's stream: Vertex (position, colour, specular, texture coordinates, normal) in buffer 0. A
    // mesh kept on the GPU: position, texture coordinates and normal in buffer 0, colour and specular in
    // buffer 1. The mesh program's draws then take their MeshRecord's index per instance in the next buffer
    // (MeshDraws). The locations are the mesh program's (the casters': position 0, texture coordinates 1).
    static_assert(gpu_shaders::mesh::kAttrPos == 0 && gpu_shaders::mesh::kAttrCol == 1 && gpu_shaders::mesh::kAttrSpec == 2 &&
                  gpu_shaders::mesh::kAttrUv == 3 && gpu_shaders::mesh::kAttrNormal == 4 &&
                  gpu_shaders::mesh::kAttrRecord == 5 &&
                  gpu_shaders::shadow_caster::kAttrPos == 0 && gpu_shaders::shadow_caster::kAttrUv == 1);
    const std::uint8_t uv = positionsOnly ? 1 : 3;
    // A member's offset: in MeshVertex (or ColourPair, buffer 1) when kept, else in Vertex.
    auto at = [cached](std::size_t kept, std::size_t streamed) { return static_cast<std::uint16_t>(cached ? kept : streamed); };
    desc.bufferCount = cached && !positionsOnly ? 2 : 1;
    desc.buffers[0].stride = at(sizeof(MeshVertex), sizeof(Vertex));
    desc.attributes[0] = {0, 0, gpu::VertexFormat::Float3,
                          at(offsetof(MeshVertex, position), offsetof(Vertex, position))};
    desc.attributes[1] = {uv, 0, gpu::VertexFormat::Float2, at(offsetof(MeshVertex, u), offsetof(Vertex, u))};
    desc.attributeCount = 2;
    if (positionsOnly) return;
    const std::uint8_t colours = cached ? 1 : 0;
    desc.attributes[2] = {4, 0, gpu::VertexFormat::Float3, at(offsetof(MeshVertex, normal), offsetof(Vertex, normal))};
    desc.attributes[3] = {1, colours, gpu::VertexFormat::UByte4Norm,
                          at(offsetof(ColourPair, colour), offsetof(Vertex, colour))};
    desc.attributes[4] = {2, colours, gpu::VertexFormat::UByte4Norm,
                          at(offsetof(ColourPair, specular), offsetof(Vertex, specular))};
    if (cached) desc.buffers[1].stride = sizeof(ColourPair);
    const int records = desc.bufferCount++;
    desc.buffers[records] = {static_cast<std::uint16_t>(sizeof(std::uint32_t)), true};
    desc.attributes[5] = {5, static_cast<std::uint8_t>(records), gpu::VertexFormat::Uint, 0};
    desc.attributeCount = 6;
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

// A batch's own values (its MeshRecord) and what a run of batches shares (`fs`: the fragment block, and the shadow block
// the batch takes).
// The batch's record and run blocks; its lights in `part` (true when it has them).
static bool FillRecord(const Frame& f, const MeshBatch& batch, MeshRecord& r, PartLight& part, MeshFsParams& fs,
                       const ShadowParams** shadow) {
    r = MeshRecord{};
    bool lit = false;
    fs = MeshFsParams{};
    r.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    r.flags[0] = batch.unclipped ? 1 : 0;
    r.fog[0] = batch.fog.start;
    r.fog[1] = batch.fog.density;
    r.fog[2] = batch.fog.depthPerW;
    r.flags[1] = batch.horizon ? 1 : 0;
    r.fog[3] = batch.eyeHeight;
    r.place[0] = f.pixelShift[0];
    r.place[1] = f.pixelShift[1];
    r.place[2] = batch.zScale;
    r.light = batch.light >= 0 ? f.shadow.lights[static_cast<std::size_t>(batch.light)] : kIdentity;
    r.uvOffset[0] = batch.uOffset;
    r.uvOffset[1] = batch.vOffset;
    *shadow = &ShadowUniforms(f, batch.light >= 0, batch.light >= 0);
    r.place[3] = ShadowNormalOffset(f, batch.light >= 0);
    if (batch.lighting >= 0) {
        // In the world it takes the point lights and their shadows with the landscape's rows.
        if (!lit) part = PartLight{};
        lit = true;
        MeshLightRecord(f.meshLights[static_cast<std::size_t>(batch.lighting)], r, part);
        if (batch.landscape >= 0) {
            const Matrix4& m = f.models[static_cast<std::size_t>(batch.landscape)];
            std::memcpy(r.landscape, m.m, sizeof(r.landscape));
            r.kinds[0] = 1;
            r.kinds[1] = batch.figure ? 1 : 0;
            std::memcpy(r.pointFront, batch.pointFront, sizeof(r.pointFront));
        }
    }
    if (batch.decal) {   // a ground print: lit as the ground (its positions the landscape's), its normal up
        if (!lit) part = PartLight{};
        lit = true;
        MeshLightRecord(GroundDecalLight(f), r, part);
        for (int k = 0; k < 3; ++k) r.landscape[k][k] = 1.0f;
        r.flags[2] = 1;
        r.kinds[0] = 1;
    }
    if (batch.figure && batch.front <= 1.0f) {   // a unit's: no shadow of its own to keep off
        r.flags[3] = 1;
        r.uvOffset[2] = batch.front;
        r.place[3] = 0.0f;
    }
    // The occlusion's arguments are the frame's: a batch that does not take it never reads them.
    const OcclusionReceiver& occlusion = OcclusionOf(batch.occluded);
    r.kinds[3] = occlusion.on ? 1 : 0;
    r.lit[3] = occlusion.view ? 1 : 0;
    const OcclusionReceiver& frameOcclusion = OcclusionOf(true);
    fs.occlusionDepthOffset = frameOcclusion.depthOffset;
    fs.occlusionDepthScale = frameOcclusion.depthScale;
    for (int k = 0; k < 2; ++k) {
        fs.occlusionScale[k] = frameOcclusion.scale[k];
        fs.occlusionTexel[k] = frameOcclusion.texel[k];
    }
    // The weather's kinds in mesh.hlsl; the sun's direction and colour are the frame's (read only with weather).
    constexpr int kUnitWeather = 1;
    constexpr int kFigureWeather = 2;
    float weather[2] = {};
    MeshWeatherUniforms(&fs, weather);
    if (batch.light >= 0 && !batch.decal) {   // a world mesh
        r.weather[0] = weather[0];
        r.weather[1] = weather[1];
        if (batch.figure) {   // a unit's own
            r.weather[0] = batch.weather[0];
            r.weather[1] = batch.weather[1];
            r.kinds[2] = kUnitWeather;
        }
    } else if (batch.interfaceUnit) {   // an interface figure of a world unit: its weather, no map's space
        r.weather[0] = batch.weather[0];
        r.weather[1] = batch.weather[1];
        r.kinds[2] = kFigureWeather;
    }
    const PassState& state = batch.state;
    UnpackColour(state.fogColour, fs.fogColour);
    fs.fog = state.fog ? 1 : 0;
    UnpackColour(state.textureFactor, fs.factor);
    fs.factor[3] = ((state.textureFactor >> 24) & 0xFF) / 255.0f;
    // The shader takes render_state.h's values as they are.
    const StageCombiner& c0 = state.stages[0].combiner;
    const StageCombiner& c1 = state.stages[1].combiner;
    fs.colourStage0[0] = static_cast<int>(c0.colourOp);
    fs.colourStage0[1] = static_cast<int>(c0.colourArg1);
    fs.colourStage0[2] = static_cast<int>(c0.colourArg2);
    fs.colourStage1[0] = static_cast<int>(c0.colourOp != TextureOp::Disable ? c1.colourOp : TextureOp::Disable);
    fs.colourStage1[1] = static_cast<int>(c1.colourArg1);
    fs.colourStage1[2] = static_cast<int>(c1.colourArg2);
    fs.alphaStage0[0] = static_cast<int>(c0.alphaOp);
    fs.alphaStage0[1] = static_cast<int>(c0.alphaArg1);
    fs.alphaStage0[2] = static_cast<int>(c0.alphaArg2);
    fs.alphaStage1[0] = static_cast<int>(c1.alphaOp);
    fs.alphaStage1[1] = static_cast<int>(c1.alphaArg1);
    fs.alphaStage1[2] = static_cast<int>(c1.alphaArg2);
    fs.specular = state.specular ? 1 : 0;
    fs.alphaFunc = state.alphaTest ? static_cast<int>(state.alphaFunc) : 0;
    fs.alphaReference = state.alphaReference;
    if (state.patch.texture) {
        for (int k = 0; k < 4; ++k) {
            fs.patchTo[k] = state.patch.to[k];
            fs.patchInside[k] = state.patch.inside[k];
        }
        for (int k = 0; k < 2; ++k) {
            fs.patchScale[k] = state.patch.map[k];
            fs.patchOffset[k] = state.patch.map[2 + k];
        }
    }
    return lit;
}

static bool SameGeometry(const Geometry& a, const Geometry& b) {
    if (a.vertexBuffers != b.vertexBuffers || a.indices.buffer.buffer != b.indices.buffer.buffer ||
        a.indices.offset != b.indices.offset)
        return false;
    for (int k = 0; k < a.vertexBuffers; ++k)
        if (a.vertices[k].buffer.buffer != b.vertices[k].buffer.buffer || a.vertices[k].offset != b.vertices[k].offset)
            return false;
    return true;
}

static bool SameTextures(const gpu::TextureBinding (&a)[3], const gpu::TextureBinding (&b)[3]) {
    for (int k = 0; k < 3; ++k)
        if (a[k].texture.texture != b[k].texture.texture || a[k].sampler.sampler != b[k].sampler.sampler) return false;
    return true;
}

MeshDraws::MeshDraws(const char* newLabel)
    : records(gpu::BufferUse::Storage, newLabel), lights(gpu::BufferUse::Storage, newLabel),
      arguments(gpu::BufferUse::Indirect, newLabel), label(newLabel) {
    lightData.assign(1, PartLight{});
}

void MeshDraws::Clear() {
    recordData.clear();
    lightData.resize(1);
    commandData.clear();
    runs.clear();
    runStarting.clear();
    recordOffset = argumentOffset = -1;
}

bool MeshDraws::Add(const Frame& f, const MeshBatch& batch, int anisotropy, bool join) {
    Run run;
    int first = 0;
    if (batch.indexCount <= 0 || (batch.decal && !GroundDecalsDrawn()) || !BindMeshBatch(batch, run.geometry, &first))
        return false;
    const PassState& state = batch.state;
    run.state = state;
    run.lines = batch.lines;
    run.cached = batch.cached != nullptr;
    run.key = StateKey(state, batch.lines, run.cached);
    for (int t = 0; t < 2; ++t) {
        const StageState& st = state.stages[t];
        run.textures[t].texture = st.texture ? st.texture : SurfaceView(nullptr);
        run.textures[t].sampler =
            anisotropy > 1 ? AnisotropicSampler(st, static_cast<std::uint32_t>(anisotropy)) : StageSampler(st);
    }
    run.textures[2] = {state.patch.texture ? state.patch.texture : SurfaceView(nullptr), run.textures[0].sampler};   // sampled as stage 0
    MeshRecord& record = recordData.emplace_back();
    if (FillRecord(f, batch, record, light, run.fs, &run.shadow)) {
        // Lit as the batch before (the palms of one stand): its lights again.
        if (lightData.size() == 1 || std::memcmp(&lightData.back(), &light, sizeof(light)) != 0) lightData.push_back(light);
        record.lights[0] = static_cast<int>(lightData.size() - 1);
    }
    const auto draw = static_cast<std::uint32_t>(recordData.size() - 1);
    runStarting.push_back(-1);
    Run* last = runs.empty() ? nullptr : &runs.back();
    if (!join || !last || last->key != run.key || last->shadow != run.shadow || !SameGeometry(last->geometry, run.geometry) ||
        !SameTextures(last->textures, run.textures) || std::memcmp(&last->fs, &run.fs, sizeof(run.fs)) != 0) {
        run.command = static_cast<std::uint32_t>(commandData.size());
        runStarting.back() = static_cast<int>(runs.size());
        runs.push_back(run);
        last = &runs.back();
    }
    // The draws of a run with the same indices are one draw of consecutive instances (records).
    gpu::IndexedIndirect* previous = last->commands ? &commandData.back() : nullptr;
    if (previous && previous->first == static_cast<std::uint32_t>(first) &&
        previous->count == static_cast<std::uint32_t>(batch.indexCount)) {
        ++previous->instances;
    } else {
        commandData.push_back({static_cast<std::uint32_t>(batch.indexCount), 1, static_cast<std::uint32_t>(first), 0, draw});
        ++last->commands;
    }
    ++last->draws;
    return true;
}

bool MeshDraws::Upload() {
    recordOffset = argumentOffset = -1;
    if (recordData.empty()) return false;
    // The buffers are bound whole: the lights' indices count from the buffer's start.
    const int lightOffset = lights.Append(lightData.data(), lightData.size() * sizeof(PartLight));
    if (lightOffset < 0) return false;
    const auto lightBase = static_cast<int>(static_cast<std::size_t>(lightOffset) / sizeof(PartLight));
    for (MeshRecord& r : recordData) r.lights[0] += lightBase;
    g_stats.counts[FrameStats::Count::UniformBytes] += lightData.size() * sizeof(PartLight);   // the records': CountDraw
    recordOffset = records.Append(recordData.data(), recordData.size() * sizeof(MeshRecord));
    if (recordOffset < 0) return false;
    // A record's index in the buffer is its instance: the per-instance indices count up from 0 (`indices`).
    const auto base = static_cast<std::uint32_t>(static_cast<std::size_t>(recordOffset) / sizeof(MeshRecord));
    for (gpu::IndexedIndirect& c : commandData) c.firstInstance += base;
    argumentOffset = arguments.Append(commandData.data(), commandData.size() * sizeof(gpu::IndexedIndirect));
    const std::size_t needed = base + recordData.size();
    if (indices.size < needed * sizeof(std::uint32_t)) {
        const std::size_t count = std::max<std::size_t>(needed * 2, 4096);
        std::vector<std::uint32_t> values(count);
        for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<std::uint32_t>(i);
        gpu::Release(indices);
        indices = gpu::MakeBuffer(gpu::BufferUse::Vertex, static_cast<std::uint32_t>(count * sizeof(std::uint32_t)), label);
        if (!indices || !gpu::Write(indices, 0, values.data(), indices.size, gpu::Contents::Discard)) {
            gpu::Release(indices);
            argumentOffset = -1;
        }
    }
    return argumentOffset >= 0;
}

void MeshDraws::Draw(const Frame& f, std::size_t first) {
    if (first >= runStarting.size() || runStarting[first] < 0 || argumentOffset < 0) return;
    namespace program = gpu_shaders::mesh;
    const Run& run = runs[static_cast<std::size_t>(runStarting[first])];
    gpu::Pipeline pipeline = Pipeline(run.state, run.lines, run.cached);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    gpu::BufferBinding vertices[3];
    for (int k = 0; k < run.geometry.vertexBuffers; ++k) vertices[k] = run.geometry.vertices[k];
    vertices[run.geometry.vertexBuffers] = {indices, 0};
    gpu::BindVertexBuffers(0, {vertices, static_cast<std::size_t>(run.geometry.vertexBuffers + 1)});
    gpu::BindIndexBuffer(run.geometry.indices);
    // The run's three textures, then the pass's (the same for every draw of the pass: taken at its first), bound in one
    // call (the unchanged ones are left out).
    static_assert(program::kSamplerTex0 == 0 && program::kSamplerTex1 == 1 && program::kSamplerTex2 == 2 &&
                  program::kSamplerShadowMap == 3 && program::kSamplerShadowRange == 4 &&
                  program::kSamplerPointShadowMap == 5 && program::kSamplerCloudMap == 6 &&
                  program::kSamplerOcclusionMap == 7 && program::kSamplerOcclusionDepth == 8 &&
                  program::kSamplerRoofMap == 9);
    static gpu::TextureBinding textures[10];
    static std::uint64_t passTextures = 0;   // the gpu::PassSerial textures[3..9] were taken in
    if (passTextures != gpu::PassSerial()) {
        passTextures = gpu::PassSerial();
        ShadowMapBindings(f, &textures[3]);
        textures[5] = PointShadowBinding(f);
        textures[6] = CloudMapBinding();
        OcclusionBindings(&textures[7]);
        textures[9] = RoofMapBinding();
    }
    for (int t = 0; t < 3; ++t) textures[t] = run.textures[t];
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex0, textures);
    CountBindings(textures[0].texture);
    static_assert(program::kStoragePartLight == program::kStorageMeshRecord + 1 &&
                  program::kStorageLightWords == program::kStoragePointLight + 1);
    const gpu::Buffer storage[2] = {records.Buffer(), lights.Buffer()};
    gpu::BindStorageBuffers(gpu::Stage::Vertex, program::kStorageMeshRecord, {storage, 1});
    gpu::BindStorageBuffers(gpu::Stage::Fragment, program::kStorageMeshRecord, storage);
    BindPointLights(program::kStoragePointLight);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockMeshFsParams, run.fs);
    // The shadow's (one of the frame's ShadowUniforms keeps) and the point lights' blocks stay the same for the pass.
    gpu::PushPassUniforms(gpu::Stage::Fragment, program::kBlockShadowParams, *run.shadow);
    gpu::PushPassUniforms(gpu::Stage::Fragment, program::kBlockPointLightParams, f.shadow.point.params);
    if (run.commands == 1) {
        const gpu::IndexedIndirect& c = commandData[run.command];
        gpu::DrawIndexed(c.first, c.count, c.instances, c.firstInstance);
    } else {
        gpu::DrawIndexedIndirect(arguments.Buffer(),
                                 static_cast<std::uint32_t>(argumentOffset) +
                                     run.command * static_cast<std::uint32_t>(sizeof(gpu::IndexedIndirect)),
                                 run.commands);
    }
    CountDraw(run.draws * sizeof(MeshRecord) + sizeof(run.fs) + sizeof(ShadowParams) + sizeof(f.shadow.point.params),
              run.draws);
}

void MeshDraws::DrawAll(const Frame& f) {
    for (std::size_t k = 0; k < runStarting.size(); ++k) Draw(f, k);
}

// The mesh pending for its passes under `clip` (its model's matrix multiplied in here); `world`: drawn in the world,
// else a caster alone (CasterMesh: the normals kept, as the part's world draw keeps them).
static void Pend(const MeshDraw& mesh, Matrix4 clip, bool world) {
    Frame& f = g_frame;
    if (f.meshVertices.size() > 4'000'000) {   // no present for a long time: drop the rest
        LogFirst(g_dropped, 1, "render: too many meshes in one frame; the rest are dropped");
        return;
    }
    if (mesh.model) {
        clip = Product(clip, *mesh.model);
        f.models.push_back(*mesh.model);
        g_pending.placement = static_cast<int>(f.models.size()) - 1;
    }
    g_pending.active = true;
    g_pending.mesh = mesh;
    g_pending.matrix = AddMatrix(clip);
    g_pending.caster = CurrentCaster();
    g_pending.casterSlot = CurrentCasterSlot();
    // A caster's box (its point shadows), and a unit's: taken in AddPass (a kept mesh keeps it).
    g_pending.boxed = g_pending.caster && mesh.positions && mesh.positionCount > 0;
    g_pending.figure = CurrentCasterFigure() && g_pending.boxed;
    g_pending.interfaceUnit = g_interfaceUnitWeather ? CurrentInterfaceUnit() : nullptr;
    // A figure part lit on the GPU. Only its normals are read (the other mesh draws' callers may pass anything there).
    if (mesh.lighting) {
        f.meshLights.push_back(FigureLight(*mesh.lighting));
        g_pending.lighting = static_cast<int>(f.meshLights.size()) - 1;
    } else if (world && !g_pending.interfaceUnit) {
        g_pending.mesh.normals = nullptr;   // an interface figure with weather keeps them (its snow settles by them)
        g_pending.mesh.words = nullptr;
    }
}

void BeginMesh(const TextureDevice* device, const MeshDraw& mesh) {
    g_pending = Pending{};
    if (!device || !mesh.matrix) return;
    Start();
    Timer timer(FrameStats::Time::HandOverMs);
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
    Pend(mesh, clip, true);
    g_pending.device = device;
}

// The pending mesh's pass in `state` as a batch (built or kept the first time); `world`: drawn in the world (a command),
// else in the shadow maps alone (MeshBatch::casterOnly).
static void AddPass(const PassState& state, bool world) {
    Frame& f = g_frame;
    if (!g_pending.built) {
        g_pending.built = true;
        ++g_stats.counts[FrameStats::Count::Meshes];
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
                ++g_stats.counts[FrameStats::Count::FlatMeshes];
                g_stats.counts[FrameStats::Count::FlatVertexBytes] += (f.meshVertices.size() - first) * sizeof(Vertex);
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
        batch.state = state;
        if (g_pending.mesh.fog == MeshFog::Distance) batch.fog = g_renderState.distanceFog;
        batch.horizon = g_pending.mesh.fog == MeshFog::Horizon;
        batch.eyeHeight = g_pending.mesh.eyeHeight;
        batch.zScale = g_pending.mesh.zScale != 0.0f ? g_pending.mesh.zScale : 1.0f;
        batch.model = true;
        batch.flat = !g_pending.mesh.corners;
        batch.decal = batch.flat && g_pending.mesh.groundDecal;
        batch.caster = g_pending.caster;
        batch.casterSlot = g_pending.casterSlot;
        batch.casterOnly = !world;
        batch.figure = g_pending.figure;
        batch.interfaceUnit = g_pending.interfaceUnit;
        batch.boxed = g_pending.boxed;
        std::memcpy(batch.box, g_pending.box, sizeof(batch.box));
        batch.cached = g_pending.cached;
        batch.lighting = g_pending.lighting;
        batch.placement = g_pending.placement;
        if (batch.cached && g_pending.mesh.moveUv) {   // the stream has them added (Build)
            batch.uOffset = g_pending.mesh.uOffset;
            batch.vOffset = g_pending.mesh.vOffset;
        }
        ++g_stats.counts[FrameStats::Count::MeshPasses];
        f.meshBatches.push_back(batch);
        if (world) f.commands.push_back(Command{Command::Kind::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
}

void MeshPass(const TextureDevice* device) {
    // Not handed over (BeginMesh logged why) or given up after a failed Build.
    if (!g_pending.active || device != g_pending.device) return;
    Timer timer(FrameStats::Time::HandOverMs);
    AddPass(ReadPassState(device), true);
}

void CasterMesh(const MeshDraw& mesh, const Surface* texture) {
    g_pending = Pending{};
    Pend(mesh, kIdentity, false);
    if (g_pending.active) AddPass(OutsideCasterState(texture), false);
    g_pending = Pending{};
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
    Timer timer(FrameStats::Time::HandOverMs);
    ++g_stats.counts[FrameStats::Count::TransformedDraws];
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
        f.commands.push_back(Command{Command::Kind::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    } else {
        f.meshVertices.resize(base);
    }
}

void ScreenDraw(const TextureDevice* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount) {
    if (!device || !vertices || count <= 0) return;   // nothing to draw
    Start();
    Timer timer(FrameStats::Time::HandOverMs);
    ++g_stats.counts[FrameStats::Count::TransformedDraws];
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
        f.commands.push_back(Command{Command::Kind::Mesh, static_cast<int>(f.meshBatches.size()) - 1});
    }
}

} // namespace render
