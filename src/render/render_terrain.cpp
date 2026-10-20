// The terrain in the new renderer. The game's terrain passes (006C6230 ground pages, 006C6330 water
// pages) call DrawSectorPage (006BFF40, src/game/terrain_draw.cpp) for every visible sector page;
// it hands the page here. The page's tiles are turned into world-space triangles at the sector's
// level of detail (the same tile entries and index patterns as the old path), with the vertex
// colours the game's lighting wrote into the sector's vertex records, the tile texture coordinates
// and the page's texture; the clip matrix is the one the old path transforms the same vertices with
// (the pipeline's stack at +0x24). The GPU clips; no software clipping or projection here.
#include "render.h"
#include "render_internal.h"

#include "../game/terrain_sector.h"
#include "../hook/log.h"

#include <cstring>
#include <unordered_map>

namespace render {

namespace {

using namespace detail;
using terrain::SectorData;
using terrain::TileVertex;

// --- Tile patterns (as DrawSectorPage builds them) -----------------------------------------------------

// The entries of a tile (3x3, row by row) each level of detail uses.
constexpr int kLod0Entries[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr int kLod1Entries[6] = {0, 2, 3, 5, 6, 8};
constexpr int kLod2Entries[4] = {0, 2, 6, 8};
// Triangles per tile, relative to the tile's first emitted vertex; the variants leave out the
// triangles along an edge to a coarser neighbour (tile edge flags, see terrain_draw.cpp).
constexpr std::uint16_t kLod0[24] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};
constexpr std::uint16_t kLod0Edge1[21] = {0, 2, 4, 0, 4, 3, 2, 5, 4, 3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};
constexpr std::uint16_t kLod0Edge2[21] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 3, 4, 6, 4, 8, 6, 4, 5, 8};
constexpr std::uint16_t kLod1[12] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
constexpr std::uint16_t kLod1Edge4[9] = {0, 1, 3, 0, 3, 4, 3, 5, 4};
constexpr std::uint16_t kLod1Edge8[9] = {0, 1, 2, 1, 5, 2, 2, 5, 4};
constexpr std::uint16_t kLod2[6] = {0, 1, 3, 0, 3, 2};

template <int N>
void PutTriangles(std::vector<std::uint32_t>& out, std::uint32_t base, const std::uint16_t (&pattern)[N]) {
    for (int k = 0; k < N; ++k) out.push_back(base + pattern[k]);
}

// The triangles of one tile, as FillIndices (terrain_draw.cpp) picks them.
void TileTriangles(std::vector<std::uint32_t>& out, std::uint32_t base, int lod, bool seams, const SectorData* s,
                   std::uint8_t edge) {
    if (lod == 2) {
        PutTriangles(out, base, kLod2);
    } else if (lod == 1) {
        if (seams && s->seamBefore != 0 && (edge & 4)) PutTriangles(out, base, kLod1Edge4);
        else if (seams && s->seamAfter != 0 && (edge & 8)) PutTriangles(out, base, kLod1Edge8);
        else PutTriangles(out, base, kLod1);
    } else {
        if (seams && s->seamBefore != 0 && (edge & 1)) PutTriangles(out, base, kLod0Edge1);
        else if (seams && s->seamAfter != 0 && (edge & 2)) PutTriangles(out, base, kLod0Edge2);
        else PutTriangles(out, base, kLod0);
    }
}

// --- Game memory -------------------------------------------------------------------------------------

// The landscape's page textures: Terrain::pageTextures (+0xD8), an array of CTexture*, indexed
// page % (pages + 2) (006C6460 binds them so before each page).
const void* PageTexture(const SectorData* s, int page) {
    const char* owner = reinterpret_cast<const char*>(s->owner);
    if (!owner || s->pages + 2 <= 0) return nullptr;
    void* const* textures = *reinterpret_cast<void* const* const*>(owner + 0xD8);
    return textures ? textures[page % (s->pages + 2)] : nullptr;
}

// --- GPU side ------------------------------------------------------------------------------------------

const char* const kTerrainSource = R"(
cbuffer params : register(b0) {
    float4 row0; float4 row1; float4 row2; float4 row3;
    float4 fog;     // rgb: the fog colour
    float4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels),
                    // y: fog on; zw: the half-pixel shift
};
struct vs_in { float3 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; };
struct vs_out { float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.pos.xy += flags.zw * o.pos.w;   // Direct3D 7 pixel centres (render_internal.h)
    o.col = i.col.zyxw;     // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
float4 ps_main(vs_out i) : SV_Target0 {
    float4 t = tex.Sample(smp, i.uv);
    if (flags.x > 1.5) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    float3 c = saturate(t.rgb * i.col.rgb + i.spec.rgb);
    float a = flags.x > 0.5 ? i.col.a : t.a;
    if (flags.y > 0.5) c = lerp(fog.rgb, c, i.spec.a);
    return float4(c, a);
}
)";

struct Params {
    Matrix4 matrix;
    float fog[4];
    float flags[4];
};
static_assert(sizeof(Params) == 96);

sg_shader g_shader{};
std::unordered_map<std::uint32_t, sg_pipeline> g_pipelines;   // by PassState::PipelineKey
GeometryStream g_geometry("ei-terrain");
std::vector<sg_buffer> g_retiredBuffers;

// The pipeline drawing terrain as the old device does with `s`: culling, depth and blending from the
// state the old path draws the page with. The ground pass (006C6230) runs under the terrain's state
// block 007AEDDC with what the sky left (CULLMODE 2, ZWRITEENABLE 1; the world draw 00569E40), the
// water pass (006C6330) under the scene's 007AEDD0; both with ALPHABLENDENABLE on (SRCALPHA /
// INVSRCALPHA in the harness runs; the verify build logs each new pipeline's state).
sg_pipeline Pipeline(const PassState& s, [[maybe_unused]] bool water) {   // water: for the verify build's log
    std::uint32_t key = s.PipelineKey();
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end()) return it->second;
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.layout.buffers[0].stride = sizeof(TerrainVertex);
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[0].offset = 0;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[1].offset = 12;
    p.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[2].offset = 16;
    p.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[3].offset = 20;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    ApplyPassState(p, s);
    p.sample_count = 1;
    p.label = "ei-terrain";
    sg_pipeline pipeline = sg_make_pipeline(&p);
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID)
        ei::Log("render: a terrain pipeline could not be created (state key %08X; see the sokol messages above)", key);
#if defined(EI_RENDER_VALIDATION)
    ei::Log("render: terrain pipeline (first for a %s page): blend %d (%u, %u), depth test %d write %d func %u, cull %u",
            water ? "water" : "ground", s.blend ? 1 : 0, s.sourceBlend, s.destBlend, s.depthTest ? 1 : 0, s.depthWrite ? 1 : 0, s.depthFunc, s.cull);
#endif
    g_pipelines.emplace(key, pipeline);
    return pipeline;
}

#if defined(EI_RENDER_VALIDATION)
// --- The level map (render::TerrainLevels, harness build) ---------------------------------------------

TerrainLevelMode g_levelMode = TerrainLevelMode::Off;

// A texture of a page texture's size and levels, level k all grey k * 32 (at most 255).
struct LevelImage {
    int width, height, levels;
    sg_image image;
    sg_view view;
};
std::vector<LevelImage> g_levelImages;

sg_view LevelView(sg_view page) {
    sg_image image = sg_query_view_image(page);
    if (!image.id) return sg_view{};
    int width = sg_query_image_width(image), height = sg_query_image_height(image);
    int levels = sg_query_image_num_mipmaps(image);
    for (const LevelImage& l : g_levelImages)
        if (l.width == width && l.height == height && l.levels == levels) return l.view;
    if (width <= 0 || height <= 0 || levels <= 0 || levels > SG_MAX_MIPMAPS) return sg_view{};
    std::vector<std::vector<std::uint8_t>> pixels(static_cast<std::size_t>(levels));
    sg_image_desc d{};
    d.width = width;
    d.height = height;
    d.num_mipmaps = levels;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.label = "ei-terrain-levels";
    for (int k = 0; k < levels; ++k) {
        int w = (width >> k) > 0 ? width >> k : 1, h = (height >> k) > 0 ? height >> k : 1;
        auto grey = static_cast<std::uint8_t>(k * 32 < 255 ? k * 32 : 255);
        auto& level = pixels[static_cast<std::size_t>(k)];
        level.assign(static_cast<std::size_t>(w) * h * 4, grey);
        for (std::size_t i = 3; i < level.size(); i += 4) level[i] = 255;
        d.data.mip_levels[k] = sg_range{level.data(), level.size()};
    }
    LevelImage l{width, height, levels, sg_make_image(&d), sg_view{}};
    sg_view_desc v{};
    v.texture.image = l.image;
    l.view = sg_make_view(&v);
    g_levelImages.push_back(l);
    return l.view;
}

void DropLevelImages() {
    for (const LevelImage& l : g_levelImages) {
        sg_destroy_view(l.view);
        sg_destroy_image(l.image);
    }
    g_levelImages.clear();
}
#endif

} // namespace

namespace detail {

int StreamBuffer::Append(const void* data, std::size_t bytes) {
    if (bytes == 0) return 0;
    std::size_t rounded = (bytes + 3) & ~static_cast<std::size_t>(3);   // sokol appends at multiples of 4
    if (commit_ != CommitNumber()) {   // sokol starts appending from the start again after a commit
        commit_ = CommitNumber();
        used_ = 0;
    }
    if (!buffer_.id || used_ + rounded > capacity_) {
        // Too small for this frame's data: a larger buffer; the old one is destroyed after the
        // commit (passes drawn earlier in the frame still use it).
        if (buffer_.id) g_retiredBuffers.push_back(buffer_);
        buffer_ = sg_buffer{};
        capacity_ = (capacity_ > rounded ? capacity_ : rounded) * 2 + 4096;
        sg_buffer_desc d{};
        d.size = capacity_;
        d.usage.vertex_buffer = !index_;
        d.usage.index_buffer = index_;
        d.usage.immutable = false;
        d.usage.dynamic_update = true;
        d.label = label_;
        buffer_ = sg_make_buffer(&d);
        used_ = 0;
        if (sg_query_buffer_state(buffer_) != SG_RESOURCESTATE_VALID) {
            if (buffer_.id) sg_destroy_buffer(buffer_);
            buffer_ = sg_buffer{};
            capacity_ = 0;
            return -1;
        }
    }
    sg_range range{data, bytes};
    int offset = sg_append_buffer(buffer_, &range);
    used_ += rounded;
    return sg_query_buffer_overflow(buffer_) ? -1 : offset;
}

void StreamBuffer::Destroy() {
    if (buffer_.id) sg_destroy_buffer(buffer_);
    buffer_ = sg_buffer{};
    capacity_ = 0;
}

void RetireBuffers() {
    for (sg_buffer b : g_retiredBuffers) sg_destroy_buffer(b);
    g_retiredBuffers.clear();
}

bool GeometryStream::Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    if (frame_ == FrameNumber() && vertexCount_ == vertices.size() && indexCount_ == indices.size())
        return vertexOffset_ >= 0 && indexOffset_ >= 0;
    frame_ = FrameNumber();
    vertexCount_ = vertices.size();
    indexCount_ = indices.size();
    vertexOffset_ = indexOffset_ = -1;
    if (vertices.empty() || indices.empty()) return false;
    vertexOffset_ = vertices_.Append(vertices.data(), vertices.size() * sizeof(Vertex));
    indexOffset_ = indices_.Append(indices.data(), indices.size() * sizeof(std::uint32_t));
    return vertexOffset_ >= 0 && indexOffset_ >= 0;
}

bool GeometryStream::Bind(sg_bindings& b) const {
    if (frame_ != FrameNumber() || vertexOffset_ < 0 || indexOffset_ < 0) return false;
    b.vertex_buffers[0] = vertices_.Buffer();
    b.vertex_buffer_offsets[0] = vertexOffset_;
    b.index_buffer = indices_.Buffer();
    b.index_buffer_offset = indexOffset_;
    return true;
}

void GeometryStream::Destroy() {
    vertices_.Destroy();
    indices_.Destroy();
    frame_ = 0xFFFFFFFFu;
    vertexOffset_ = indexOffset_ = -1;
}

bool CreateTerrainShader() {
    sg_shader_desc s{};
    s.vertex_func.source = kTerrainSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kTerrainSource;
    s.fragment_func.entry = "ps_main";
    const char* const semantics[4] = {"POSITION", "COLOR", "COLOR", "TEXCOORD"};
    const std::uint8_t indices[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; ++i) {
        s.attrs[i].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[i].hlsl_sem_name = semantics[i];
        s.attrs[i].hlsl_sem_index = indices[i];
    }
    // The same constant buffer in both stages (register b0 of each): uniform slots 0 and 1.
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(Params);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = sizeof(Params);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = "ei-terrain";
    g_shader = sg_make_shader(&s);
    bool ok = sg_query_shader_state(g_shader) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the terrain shader could not be created (see the sokol messages above)");
    return ok;
}

void UploadTerrain(const Frame& f) {
    if (f.terrainBatches.empty()) return;
    g_geometry.Upload(f.terrainVertices, f.terrainIndices);
}

void DrawTerrainBatch(const Frame& f, const TerrainBatch& batch) {
    sg_bindings b{};
    if (!g_geometry.Bind(b)) return;
    sg_pipeline pipeline = Pipeline(batch.state, batch.water);
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) return;
    sg_apply_pipeline(pipeline);
    b.views[0] = TextureView(batch.texture);
    b.samplers[0] = TerrainSampler(batch.state.stages[0]);
    float mode = batch.water ? 1.0f : 0.0f;
#if defined(EI_RENDER_VALIDATION)
    if (g_levelMode != TerrainLevelMode::Off && !batch.water) {
        sg_view levels = LevelView(b.views[0]);
        if (levels.id) {
            b.views[0] = levels;
            if (g_levelMode == TerrainLevelMode::PointMips) b.samplers[0] = LinearSampler();
            mode = 2.0f;
        }
    }
#endif
    sg_apply_bindings(&b);
    Params params{};
    params.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    params.fog[0] = static_cast<float>((f.fogColour >> 16) & 0xFF) / 255.0f;
    params.fog[1] = static_cast<float>((f.fogColour >> 8) & 0xFF) / 255.0f;
    params.fog[2] = static_cast<float>(f.fogColour & 0xFF) / 255.0f;
    params.flags[0] = mode;
    params.flags[1] = f.fogEnabled ? 1.0f : 0.0f;
    params.flags[2] = f.pixelShift[0];
    params.flags[3] = f.pixelShift[1];
    sg_range u{&params, sizeof(params)};
    sg_apply_uniforms(0, &u);
    sg_apply_uniforms(1, &u);
    sg_draw(batch.firstIndex, batch.indexCount, 1);
}

void DestroyTerrain() {
#if defined(EI_RENDER_VALIDATION)
    DropLevelImages();
#endif
    g_geometry.Destroy();
    RetireBuffers();
    for (auto& p : g_pipelines) sg_destroy_pipeline(p.second);
    g_pipelines.clear();
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_shader = sg_shader{};
}

} // namespace detail

#if defined(EI_RENDER_VALIDATION)
void TerrainLevels(TerrainLevelMode mode) {
    if (mode != g_levelMode)
        ei::Log("render: terrain level map %s",
                mode == TerrainLevelMode::Off ? "off" : mode == TerrainLevelMode::Levels ? "on" : "on, through point mips");
    g_levelMode = mode;
}
#endif

void TerrainPage(const SectorData* s, void* renderer, int page) {
    if (!s || !renderer || !Active()) return;
    Frame& f = CurrentFrame();
    if (f.terrainVertices.size() > 8'000'000) return;   // no present for a long time: drop the rest
    // The clip matrix: the top of the transform pipeline's stack at +0x24 (renderer +8), which the
    // old path's 004F29F0 transforms the sector's vertices with (the landscape's model matrix
    // pushed by 006C6230 / 006C6330 on top of the view).
    const char* pipeline = *reinterpret_cast<char* const*>(static_cast<const char*>(renderer) + 8);
    if (!pipeline) return;
    const Matrix4* stack = *reinterpret_cast<const Matrix4* const*>(pipeline + 0x24);
    int depth = *reinterpret_cast<const int*>(pipeline + 0x28);
    if (!stack || depth <= 0) return;
    int lod = s->detail;
    if (lod < 0 || lod > 2) return;
    if (!f.haveFog) ReadFog(f);
    int matrix = AddMatrix(stack[depth - 1]);
    const void* texture = PageTexture(s, page);
    bool water = page >= s->pages + 2;
    const int* entries = lod == 0 ? kLod0Entries : lod == 1 ? kLod1Entries : kLod2Entries;
    int perTile = lod == 0 ? 9 : lod == 1 ? 6 : 4;
    bool seams = lod != 2 && (s->seamBefore != 0 || s->seamAfter != 0);

    int firstIndex = static_cast<int>(f.terrainIndices.size());
    for (int quarter = 0; quarter < 4; ++quarter) {
        if (s->quarterMode[quarter] == 2) continue;   // outside the view
        const auto& lists = s->tiles[quarter];
        if (!lists.data || page >= lists.size) continue;
        const terrain::TileList& list = lists.data[page];
        int tiles = list.size / 9;
        if (tiles <= 0 || !list.data) continue;
        const std::uint8_t* edges = nullptr;
        if (seams && s->tileFlags[quarter].data && page < s->tileFlags[quarter].size)
            edges = s->tileFlags[quarter].data[page].data;
        for (int t = 0; t < tiles; ++t) {
            const TileVertex* tile = list.data + t * 9;
            auto base = static_cast<std::uint32_t>(f.terrainVertices.size());
            bool ok = true;
            for (int k = 0; k < perTile && ok; ++k) {
                const TileVertex& e = tile[entries[k]];
                std::ptrdiff_t index = e.vertex - s->records.data;
                if (index < 0 || index >= s->records.size || index >= s->vertices.size) {
                    ok = false;
                    break;
                }
                const Vector3& position = s->vertices.data[index];
                const auto* record = reinterpret_cast<const std::uint8_t*>(e.vertex);
                TerrainVertex v{};
                v.position.x = position.x;
                v.position.y = position.y;
                v.position.z = position.z;
                std::memcpy(&v.colour, record + 0x11, 4);
                std::memcpy(&v.specular, record + 0x15, 4);
                v.u = e.u;
                v.v = e.v;
                f.terrainVertices.push_back(v);
            }
            if (!ok) {
                f.terrainVertices.resize(base);
                continue;
            }
            TileTriangles(f.terrainIndices, base, lod, seams, s, edges ? edges[t] : 0);
        }
    }
    int count = static_cast<int>(f.terrainIndices.size()) - firstIndex;
    if (count <= 0) return;
    // The old device's state the page is drawn with; DrawPage changes none of what the pipeline
    // takes before its draws (it turns SPECULARENABLE on around each, which the shader always adds).
    PassState state;
    {
        FpuGuard fpu;
        state = ReadPassState(nullptr);
    }
    if (!f.commands.empty() && f.commands.back().kind == Command::Terrain) {
        TerrainBatch& last = f.terrainBatches[static_cast<std::size_t>(f.commands.back().index)];
        if (last.texture == texture && last.water == water && last.matrix == matrix &&
            last.state.PipelineKey() == state.PipelineKey() && last.firstIndex + last.indexCount == firstIndex) {
            last.indexCount += count;
            return;
        }
    }
    f.terrainBatches.push_back(TerrainBatch{texture, water, matrix, firstIndex, count, state});
    f.commands.push_back(Command{Command::Terrain, static_cast<int>(f.terrainBatches.size()) - 1});
}

} // namespace render
