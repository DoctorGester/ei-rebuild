// The terrain in the new renderer. The game's terrain passes (006C6230 ground pages, 006C6330 water
// pages) call DrawSectorPage (006BFF40, src/game/terrain_draw.cpp) for every visible sector page;
// it hands the page here. The page's tiles are turned into world-space triangles at the sector's
// level of detail (the same tile entries and index patterns as the old path), with the vertex
// colours the game's lighting wrote into the sector's vertex records, the tile texture coordinates
// and the page's texture; the clip matrix is the one the old path transforms the same vertices with
// (the pipeline's stack at +0x24). The GPU clips; no software clipping or projection here.
//
// The pages stay on the GPU (R16c, docs/notes/renderer.md section 13): each sector page at a level of
// detail and seam variant is built once into a pool of vertex and index buffers (positions and
// texture coordinates in one stream, the lit colours in another) and built again only when what it
// was built from changes (the sector's tile lists and records: SectorData::Build, render::
// TerrainChanged). When the game only moved vertices or texture coordinates (the waves, the water
// levels, the animated tiles: TerrainChanged) the page reads them again and uploads those that
// changed. The colours of the page's visible quarters are compared with the records every frame and
// only the changed ones uploaded. A frame keeps the visible quarters of each page handed over (TerrainPiece).
#include "render.h"
#include "render_internal.h"

#include "../game/terrain_sector.h"
#include "../hook/log.h"

#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace render {

namespace {

using namespace detail;
using terrain::SectorData;
using terrain::SectorVertex;
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

// The game's changes to a sector (render::TerrainChanged): how often it was built again (tile lists,
// records) and how often vertices or texture coordinates moved.
struct SectorChanges {
    std::uint32_t rebuilt = 0, moved = 0;
};
std::unordered_map<const SectorData*, SectorChanges> g_changes;

// --- Pages kept on the GPU (R16c) ----------------------------------------------------------------------

// A page vertex's geometry; its colours (colour, specular) are a stream of their own.
struct PageVertex {
    Vector3 position;
    float u, v;
};
static_assert(sizeof(PageVertex) == 20);

// What a page was built from: when any of it differs, the page is built again.
struct PageSource {
    const SectorVertex* records = nullptr;
    int recordCount = 0;
    const void* positions = nullptr;
    int positionCount = 0;
    const TileVertex* lists[4] = {};
    int listSizes[4] = {};
    const std::uint8_t* edges[4] = {};
    SectorChanges changes;

    // The same tiles of the same records: at most vertices and texture coordinates moved.
    bool SameTiles(const PageSource& o) const {
        for (int q = 0; q < 4; ++q)
            if (lists[q] != o.lists[q] || listSizes[q] != o.listSizes[q] || edges[q] != o.edges[q]) return false;
        return records == o.records && recordCount == o.recordCount && positions == o.positions &&
               positionCount == o.positionCount && changes.rebuilt == o.changes.rebuilt;
    }
};

PageSource SourceOf(const SectorData* s, int page) {
    PageSource src;
    src.records = s->records.data;
    src.recordCount = s->records.size;
    src.positions = s->vertices.data;
    src.positionCount = s->vertices.size;
    for (int q = 0; q < 4; ++q) {
        if (s->tiles[q].data && page < s->tiles[q].size) {
            src.lists[q] = s->tiles[q].data[page].data;
            src.listSizes[q] = s->tiles[q].data[page].size;
        }
        if (s->tileFlags[q].data && page < s->tileFlags[q].size) src.edges[q] = s->tileFlags[q].data[page].data;
    }
    auto it = g_changes.find(s);
    if (it != g_changes.end()) src.changes = it->second;
    return src;
}

// A quarter's tiles of a page: its vertices and indices within the page's.
struct QuarterRange {
    std::uint32_t firstVertex = 0, vertexCount = 0, firstIndex = 0, indexCount = 0;
};

} // namespace

namespace detail {

// A sector page at one level of detail and seam variant, as built and as placed in the pool.
struct CachedPage {
    PageSource source;
    std::vector<PageVertex> vertices;
    std::vector<std::uint32_t> colours;    // colour, specular per vertex
    std::vector<std::uint16_t> records;    // the record each vertex reads its position and colours from
    std::vector<const TileVertex*> entries;   // the tile entry each vertex reads its texture coordinates from
    std::vector<std::uint32_t> indices;    // relative to the page's first vertex
    QuarterRange quarters[4];
    PoolPlace place;                                 // in the pool
    bool geometryDirty = true;                       // all of it to upload
    std::uint32_t vertexFrom = 0, vertexTo = 0;      // vertices whose positions or texture coordinates to upload
    std::uint32_t colourFrom = 0, colourTo = 0;      // vertices whose colours to upload
    bool pending = false;                            // in g_pending
    std::uint32_t lastUsed = 0;                      // FrameNumber() of its last hand-over
    std::uint32_t mark = 0;                          // Restart's
};

} // namespace detail

namespace {

// The tiles of `page` at level of detail `lod` (with the seams' variants when `seams`), every quarter's
// (culled or not), as DrawSectorPage builds them: positions, texture coordinates, the record of each
// vertex and the triangles.
void BuildPage(const SectorData* s, int page, int lod, bool seams, CachedPage& out) {
    out.vertices.clear();
    out.records.clear();
    out.entries.clear();
    out.indices.clear();
    const int* entries = lod == 0 ? kLod0Entries : lod == 1 ? kLod1Entries : kLod2Entries;
    int perTile = lod == 0 ? 9 : lod == 1 ? 6 : 4;
    for (int quarter = 0; quarter < 4; ++quarter) {
        QuarterRange& range = out.quarters[quarter];
        range.firstVertex = static_cast<std::uint32_t>(out.vertices.size());
        range.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        const auto& lists = s->tiles[quarter];
        if (lists.data && page < lists.size && lists.data[page].data) {
            const terrain::TileList& list = lists.data[page];
            int tiles = list.size / 9;
            const std::uint8_t* edges = nullptr;
            if (seams && s->tileFlags[quarter].data && page < s->tileFlags[quarter].size)
                edges = s->tileFlags[quarter].data[page].data;
            for (int t = 0; t < tiles; ++t) {
                const TileVertex* tile = list.data + t * 9;
                auto base = static_cast<std::uint32_t>(out.vertices.size());
                bool ok = true;
                for (int k = 0; k < perTile; ++k) {
                    const TileVertex& e = tile[entries[k]];
                    std::ptrdiff_t index = e.vertex - s->records.data;
                    if (index < 0 || index >= s->records.size || index >= s->vertices.size) {
                        ok = false;
                        break;
                    }
                    const Vector3& position = s->vertices.data[index];
                    out.vertices.push_back(PageVertex{position, e.u, e.v});
                    out.records.push_back(static_cast<std::uint16_t>(index));
                    out.entries.push_back(&e);
                }
                if (!ok) {
                    out.vertices.resize(base);
                    out.records.resize(base);
                    out.entries.resize(base);
                    continue;
                }
                TileTriangles(out.indices, base, lod, seams, s, edges ? edges[t] : 0);
            }
        }
        range.vertexCount = static_cast<std::uint32_t>(out.vertices.size()) - range.firstVertex;
        range.indexCount = static_cast<std::uint32_t>(out.indices.size()) - range.firstIndex;
    }
}

bool SameGeometry(const CachedPage& a, const CachedPage& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(PageVertex)) == 0 &&
           std::memcmp(a.records.data(), b.records.data(), a.records.size() * sizeof(std::uint16_t)) == 0 &&
           std::memcmp(a.entries.data(), b.entries.data(), a.entries.size() * sizeof(const TileVertex*)) == 0 &&
           std::memcmp(a.indices.data(), b.indices.data(), a.indices.size() * sizeof(std::uint32_t)) == 0;
}

// Vertex `i` into the range [from, to) of vertices to upload.
void MarkVertex(std::uint32_t i, std::uint32_t& from, std::uint32_t& to) {
    if (to <= from) {
        from = i;
        to = i + 1;
    } else {
        if (i < from) from = i;
        if (i + 1 > to) to = i + 1;
    }
}

// Reads the colours of vertices [from, to) of `p` from the sector's records; the changed ones are
// marked for upload. True when any changed.
bool ReadColours(const SectorData* s, CachedPage& p, std::uint32_t from, std::uint32_t to) {
    bool changed = false;
    for (std::uint32_t i = from; i < to; ++i) {
        const SectorVertex& record = s->records.data[p.records[i]];
        std::uint32_t colour = record.colour, specular = record.specular;
        std::uint32_t* kept = &p.colours[2 * static_cast<std::size_t>(i)];
        if (kept[0] == colour && kept[1] == specular) continue;
        kept[0] = colour;
        kept[1] = specular;
        MarkVertex(i, p.colourFrom, p.colourTo);
        changed = true;
    }
    return changed;
}

// Reads every vertex's position and texture coordinates of `p` again (the game moved some: the
// tiles are the same); the changed ones are marked for upload. True when any changed.
bool ReadVertices(const SectorData* s, CachedPage& p) {
    bool changed = false;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(p.vertices.size()); ++i) {
        const Vector3& position = s->vertices.data[p.records[i]];
        const TileVertex& e = *p.entries[i];
        PageVertex& kept = p.vertices[i];
        PageVertex now{position, e.u, e.v};
        if (std::memcmp(&kept, &now, sizeof(PageVertex)) == 0) continue;
        kept = now;
        MarkVertex(i, p.vertexFrom, p.vertexTo);
        changed = true;
    }
    return changed;
}

struct PageKey {
    const SectorData* sector;
    int page;
    int variant;   // the level of detail | 4 seam before | 8 seam after
    bool operator==(const PageKey& o) const { return sector == o.sector && page == o.page && variant == o.variant; }
};
struct PageKeyHash {
    std::size_t operator()(const PageKey& k) const {
        return std::hash<const void*>()(k.sector) ^ (static_cast<std::size_t>(k.page) * 16 + k.variant) * 0x9E3779B9u;
    }
};
std::unordered_map<PageKey, CachedPage, PageKeyHash> g_pages;
std::vector<CachedPage*> g_pending;   // pages with something to upload
// A page not handed over for this many frames is dropped (its pool room comes back at the next Restart).
constexpr std::uint32_t kPageLife = 600;

#if defined(EI_RENDER_VALIDATION)
bool g_cacheOn = true;
std::uint64_t g_checks = 0, g_stale = 0;
// Every this many frames each kept page is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
#endif

// The pool: the pages' vertices (geometry and colours) and indices.
GpuPool g_pool(sizeof(PageVertex), 1u << 17, 3u << 17, "ei-terrain");

// --- GPU side ------------------------------------------------------------------------------------------

struct Params {
    Matrix4 matrix;
    float fog[4];
    float flags[4];
    Matrix4 light;
    ShadowParams shadow;
};
static_assert(sizeof(Params) == 224);

sg_shader g_shader{};
std::unordered_map<std::uint32_t, sg_pipeline> g_pipelines;   // by PassState::PipelineKey

// The pipeline drawing terrain as the old device does with `s`: culling, depth and blending from the
// state the old path draws the page with. The ground pass (006C6230) runs under the terrain's state
// block 007AEDDC with what the sky left (CULLMODE 2, ZWRITEENABLE 1; the world draw 00569E40), the
// water pass (006C6330) under the scene's 007AEDD0; both with ALPHABLENDENABLE on (SRCALPHA /
// INVSRCALPHA in the harness runs; the verify build logs each new pipeline's state).
sg_pipeline Pipeline(const PassState& s, [[maybe_unused]] bool water) {   // water: for the verify build's log
    int samples = PassSampleCount();
    std::uint32_t key = s.PipelineKey() | (static_cast<std::uint32_t>(samples) << 24);
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end()) return it->second;
    sg_pipeline_desc p{};
    p.shader = g_shader;
    // Buffer 0 the pool's geometry (position, texture coordinates), buffer 1 its colours.
    p.layout.buffers[0].stride = sizeof(PageVertex);
    p.layout.buffers[1].stride = kColourPairBytes;
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[0].offset = 0;
    p.layout.attrs[1].buffer_index = 1;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[1].offset = 0;
    p.layout.attrs[2].buffer_index = 1;
    p.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    p.layout.attrs[2].offset = 4;
    p.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[3].offset = 12;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    ApplyPassState(p, s);
    p.sample_count = samples;
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

// --- The pool -------------------------------------------------------------------------------------------

// Room for `p` in the pool (all of it to upload when placed anew). False when the pool is full.
bool Place(CachedPage& p) {
    bool anew = false;
    if (!g_pool.Place(p.place, static_cast<std::uint32_t>(p.vertices.size()), static_cast<std::uint32_t>(p.indices.size()),
                      &anew))
        return false;
    if (anew) p.geometryDirty = true;
    return true;
}

// The pool starts again with the pages of frame `f` (and the pending ones): they are placed again
// from its start. It is made larger when they need more than half of it.
void Restart(const Frame& f) {
    static std::uint32_t stamp = 0;
    ++stamp;
    std::uint64_t vertices = 0, indices = 0;
    std::vector<CachedPage*> pages;
    auto take = [&](CachedPage* p) {
        if (p->mark == stamp) return;
        p->mark = stamp;
        vertices += p->vertices.size();
        indices += p->indices.size();
        pages.push_back(p);
    };
    for (const TerrainPiece& piece : f.terrainPieces) take(piece.page);
    for (CachedPage* p : g_pending) take(p);
    g_pool.Restart(vertices, indices);
    for (CachedPage* p : pages) {
        if (!p->pending) {
            p->pending = true;
            g_pending.push_back(p);
        }
    }
}

// The page's changes into its room in the pool: all of it, or the vertices and colours that changed.
void Write(CachedPage& p) {
    FrameStats& stats = Stats();
    if (p.geometryDirty) {
        auto count = static_cast<std::uint32_t>(p.vertices.size());
        g_pool.WriteGeometry(p.place, 0, count, p.vertices.data());
        g_pool.WriteColours(p.place, 0, count, p.colours.data());
        g_pool.WriteIndices(p.place, p.indices.data(), p.indices.size());
        stats.terrainVertexBytes += p.vertices.size() * (sizeof(PageVertex) + kColourPairBytes);
        stats.terrainIndexBytes += p.indices.size() * sizeof(std::uint32_t);
    } else {
        if (p.vertexTo > p.vertexFrom) {
            g_pool.WriteGeometry(p.place, p.vertexFrom, p.vertexTo - p.vertexFrom, &p.vertices[p.vertexFrom]);
            stats.terrainVertexBytes += (p.vertexTo - p.vertexFrom) * sizeof(PageVertex);
        }
        if (p.colourTo > p.colourFrom) {
            g_pool.WriteColours(p.place, p.colourFrom, p.colourTo - p.colourFrom,
                                &p.colours[2 * static_cast<std::size_t>(p.colourFrom)]);
            stats.terrainVertexBytes += (p.colourTo - p.colourFrom) * kColourPairBytes;
        }
    }
    p.geometryDirty = false;
    p.vertexFrom = p.vertexTo = 0;
    p.colourFrom = p.colourTo = 0;
}

// Builds `p` again; true when its geometry differs from what it had (it then has the new one).
CachedPage g_scratch;
bool BuildAgain(const SectorData* s, int page, int lod, bool seams, CachedPage& p) {
    BuildPage(s, page, lod, seams, g_scratch);
    if (SameGeometry(g_scratch, p)) return false;
    p.vertices.swap(g_scratch.vertices);
    p.records.swap(g_scratch.records);
    p.entries.swap(g_scratch.entries);
    p.indices.swap(g_scratch.indices);
    for (int q = 0; q < 4; ++q) p.quarters[q] = g_scratch.quarters[q];
    return true;
}

} // namespace

namespace detail {

bool CreateTerrainShader() {
    sg_shader_desc s{};
    SetBytecode(s, kTerrainShader);
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
    DescribeShadowMap(s, 1, 1);
    s.label = "ei-terrain";
    g_shader = sg_make_shader(&s);
    bool ok = sg_query_shader_state(g_shader) == SG_RESOURCESTATE_VALID;
    if (!ok) ei::Log("render: the terrain shader could not be created (see the sokol messages above)");
    return ok;
}

void UploadTerrain(const Frame& f) {
    if (g_pending.empty()) return;
    StatsTimer timer(&FrameStats::terrainMs);
    bool placed = true;
    for (CachedPage* p : g_pending) {
        if (!Place(*p)) {
            placed = false;
            break;
        }
    }
    if (!placed) {
        Restart(f);
        for (CachedPage* p : g_pending) Place(*p);   // fits: the pool holds twice the frame's pages
    }
    for (CachedPage* p : g_pending) {
        if (g_pool.Placed(p->place)) Write(*p);
        p->pending = false;
    }
    g_pending.clear();
}

void DrawTerrainBatch(const Frame& f, const TerrainBatch& batch) {
    if (batch.pieceCount <= 0) return;
    sg_bindings b{};
    g_pool.Bind(b);
    if (!b.vertex_buffers[0].id) return;
    sg_pipeline pipeline = Pipeline(batch.state, batch.water);
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) return;
    sg_apply_pipeline(pipeline);
    CountPipeline(pipeline);
    b.views[0] = TextureView(batch.texture);
    b.samplers[0] = AnisotropicSampler(batch.state.stages[0]);
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
    BindShadowMap(b, 1, 1);
    sg_apply_bindings(&b);
    CountBindings(b);
    Params params{};
    // The ground receives the objects' shadows (R16j); the water does not (the ground under it does).
    bool receives = !batch.water && mode < 1.5f && !f.shadow.lights.empty();
    params.light = receives ? f.shadow.lights[0] : f.matrices[static_cast<std::size_t>(batch.matrix)];
    ShadowUniforms(f, receives, &params.shadow);
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
    // The pieces' triangles in their order; ranges that follow each other in the pool in one draw.
    std::size_t uniformBytes = 2 * sizeof(params);
    std::uint32_t first = 0, count = 0;
    auto draw = [&] {
        if (count == 0) return;
        sg_draw(static_cast<int>(first), static_cast<int>(count), 1);
        CountDraw(uniformBytes);
        uniformBytes = 0;
    };
    for (int k = batch.firstPiece; k < batch.firstPiece + batch.pieceCount; ++k) {
        const TerrainPiece& piece = f.terrainPieces[static_cast<std::size_t>(k)];
        const CachedPage& p = *piece.page;
        const QuarterRange& q = p.quarters[piece.quarter];
        if (!g_pool.Placed(p.place) || q.indexCount == 0) continue;
        std::uint32_t start = p.place.firstIndex + q.firstIndex;
        if (count > 0 && first + count == start) {
            count += q.indexCount;
        } else {
            draw();
            first = start;
            count = q.indexCount;
        }
    }
    draw();
}

void TerrainPositions(const Frame& f, std::size_t step, std::vector<Vector3>* out) {
    out->clear();
    std::size_t at = 0, next = 0;   // the vertices passed, the next one taken
    for (const TerrainPiece& piece : f.terrainPieces) {
        const CachedPage& p = *piece.page;
        const QuarterRange& q = p.quarters[piece.quarter];
        for (; next < at + q.vertexCount; next += step) out->push_back(p.vertices[q.firstVertex + (next - at)].position);
        at += q.vertexCount;
    }
}

void TerrainFrameDone() {
    std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (auto it = g_pages.begin(); it != g_pages.end();) {
        if (!it->second.pending && now - it->second.lastUsed > kPageLife) it = g_pages.erase(it);
        else ++it;
    }
}

void DestroyTerrain() {
#if defined(EI_RENDER_VALIDATION)
    DropLevelImages();
#endif
    // The pages keep what they were built from; they are placed in a new pool at their next draw.
    g_pool.Drop();
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

void TerrainChanged(const terrain::SectorData* sector, bool rebuilt) {
    if (!sector) return;
    SectorChanges& c = g_changes[sector];
    ++(rebuilt ? c.rebuilt : c.moved);
}

#if defined(EI_RENDER_VALIDATION)
void SetTerrainCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: terrain pages %s", on ? "kept on the GPU" : "built and uploaded every frame");
    g_cacheOn = on;
}

TerrainCacheInfo TerrainCacheState() {
    TerrainCacheInfo info;
    info.on = g_cacheOn;
    info.pages = g_pages.size();
    info.checks = g_checks;
    info.stale = g_stale;
    return info;
}
#endif

void TerrainPage(const SectorData* s, void* renderer, int page) {
    if (!s || !renderer || !Active()) return;
    StatsTimer timer(&FrameStats::terrainMs);
    ++Stats().terrainPages;
    Frame& f = CurrentFrame();
    if (f.terrainPieces.size() > 1'000'000) return;   // no present for a long time: drop the rest
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
    bool seams = lod != 2 && (s->seamBefore != 0 || s->seamAfter != 0);
    bool inView = false;
    for (int q = 0; q < 4 && !inView; ++q) {
        const auto& lists = s->tiles[q];
        inView = s->quarterMode[q] != 2 && lists.data && page < lists.size && lists.data[page].data &&
                 lists.data[page].size >= 9;
    }
    if (!inView) return;

    // The page as kept: built when new or when its tiles changed, its vertices read again when the game
    // moved some.
    PageKey key{s, page, lod | (seams && s->seamBefore != 0 ? 4 : 0) | (seams && s->seamAfter != 0 ? 8 : 0)};
    auto [found, made] = g_pages.try_emplace(key);
    CachedPage& p = found->second;
    PageSource source = SourceOf(s, page);
    bool moved = false, upload = false;
    if (made) {
        BuildPage(s, page, lod, seams, p);
        upload = true;
    } else if (!p.source.SameTiles(source)) {
        moved = upload = BuildAgain(s, page, lod, seams, p);
    } else if (p.source.changes.moved != source.changes.moved) {
        moved = ReadVertices(s, p);
#if defined(EI_RENDER_VALIDATION)
    } else if (!g_cacheOn) {
        moved = BuildAgain(s, page, lod, seams, p);   // off: all of it uploaded again, as before R16c
        upload = true;
    } else if (FrameNumber() % kCheckEvery == 0) {
        ++g_checks;
        if (BuildAgain(s, page, lod, seams, p)) {
            if (++g_stale <= 20)
                ei::Log("render: a terrain page kept on the GPU was stale: sector (%d, %d) page %d, detail %d, seams %d / %d",
                        s->sx, s->sy, page, lod, s->seamBefore, s->seamAfter);
            moved = upload = true;
        }
#endif
    }
    p.source = source;
    if (upload) {
        p.geometryDirty = true;
        p.colours.resize(2 * p.vertices.size());
        for (std::size_t i = 0; i < p.vertices.size(); ++i) {
            const SectorVertex& record = s->records.data[p.records[i]];
            p.colours[2 * i] = record.colour;
            p.colours[2 * i + 1] = record.specular;
        }
        p.colourFrom = p.colourTo = 0;
    }
    if (!g_pool.Placed(p.place)) p.geometryDirty = true;

    // The quarters in view: their colours read again, their tiles drawn.
    bool relit = false;
    std::size_t firstPiece = f.terrainPieces.size();
    for (int q = 0; q < 4; ++q) {
        const QuarterRange& range = p.quarters[q];
        if (s->quarterMode[q] == 2 || range.indexCount == 0) continue;
        if (!upload && ReadColours(s, p, range.firstVertex, range.firstVertex + range.vertexCount)) relit = true;
        f.terrainPieces.push_back(TerrainPiece{&p, q});
    }
    p.lastUsed = FrameNumber();
    if ((p.geometryDirty || p.vertexTo > p.vertexFrom || p.colourTo > p.colourFrom) && !p.pending) {
        p.pending = true;
        g_pending.push_back(&p);
    }
    auto count = static_cast<int>(f.terrainPieces.size() - firstPiece);
    if (count <= 0) return;
    ++Stats().terrainReuse[made ? FrameStats::New : moved ? FrameStats::Moved : relit ? FrameStats::Relit : FrameStats::Same];
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
            last.state.PipelineKey() == state.PipelineKey() &&
            static_cast<std::size_t>(last.firstPiece) + static_cast<std::size_t>(last.pieceCount) == firstPiece) {
            last.pieceCount += count;
            return;
        }
    }
    ++Stats().terrainBatches;
    f.terrainBatches.push_back(TerrainBatch{texture, water, matrix, static_cast<int>(firstPiece), count, state});
    f.commands.push_back(Command{Command::Terrain, static_cast<int>(f.terrainBatches.size()) - 1});
}

} // namespace render
