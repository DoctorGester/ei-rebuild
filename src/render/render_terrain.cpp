// The terrain: the game's sector pages as world-space triangles, kept on the GPU and built again only where the
// game says they changed. See docs/notes/renderer.md, "The renderer's source files".
#include "../game/model.h"
#include "../game/terrain_sector.h"
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include "lib/containers.h"
#include "lib/float_select.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

using terrain::SectorData;
using terrain::SectorPosition;
using terrain::SectorVertex;
using terrain::TileVertex;

// --- Tile patterns (as DrawSectorPage builds them) -----------------------------------------------------

// Triangles per tile (3x3 vertices, row by row), relative to the tile's first emitted vertex.
constexpr int kTileVertices = 9;
constexpr std::uint16_t kTileTriangles[24] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};

// --- Game memory -------------------------------------------------------------------------------------

// The landscape's page texture of `page` (Terrain::pageTextures, indexed page % LayerPages(); they are bound
// so before each page).
static const Texture* PageTexture(const SectorData* s, int page) {
    if (!s->owner || s->owner->LayerPages() <= 0) return nullptr;
    const std::vector<HeldRef<Texture>>& textures = s->owner->pageTextures;
    const int index = page % s->owner->LayerPages();
    return index < std::ssize(textures) ? textures[index].Get() : nullptr;
}

// The game's changes to a sector (render::TerrainChanged): how often it was built again (tile lists,
// records), how often vertices or texture coordinates moved, and how often only the water layer's
// positions moved.
struct SectorChanges {
    std::uint32_t rebuilt = 0, moved = 0, waterMoved = 0;
};
constexpr int kWaterRecords = terrain::kSectorVertices;
// Light lost under water per squared depth and absorption (1 / 15): the dimming of the ground under water.
constexpr float kWaterFalloff = 0.06666667f;
static std::unordered_map<const SectorData*, SectorChanges> g_changes;

// --- Pages kept on the GPU ----------------------------------------------------------------------

// A page vertex's geometry; its colours (colour, specular) are a stream of their own. The record's
// normal and, on the ground layer, the light lost under the water (-1 on the water layer); the GPU lights both layers.
// On the water layer the depth over the ground (the ground record's: negative where the ground is above the water); on
// the ground layer the same where the record's water is a swamp (terrain.hlsl's muddy rim), else -1. `shore`: on the
// ground layer 1 where it lies under water that is not a swamp (no muddy rim where a swamp meets it), else 0; on the
// water layer the slope of the ground under it (GroundSlope: a swamp's distance from its shore line).
// `extra`: on the ground layer the ground's own occlusion of the ambient light (GroundOcclusion), on the water layer the
// water's kind (WaterKind).
struct PageVertex {
    Vector3 position;
    float u = 0.0f, v = 0.0f;
    Vector3 normal;
    float loss = 0.0f;
    float depth = 0.0f;
    float shore = 0.0f;
    float extra = 0.0f;
};
static_assert(sizeof(PageVertex) == 48);

// The ground record `index` lies under the sector's water (a water material above it, the depth not negative):
// its water record, else null.
static const SectorVertex* WaterAbove(const SectorData* s, int index) {
    if (!s->hasWater || index >= kWaterRecords || index + kWaterRecords >= std::ssize(s->records)) return nullptr;
    const SectorVertex& water = s->records[index + kWaterRecords];
    if (water.waterMaterial == SectorVertex::kNoWaterMaterial || SignBit(s->records[index].depth)) return nullptr;
    return &water;
}

// The kind of water a material is (terrain.hlsl's Water): 0 water, 1 a swamp, 2 lava. The map's materials tell them
// apart only by their colour: the water's blue, the swamps' green and the lava's red (which also lights itself).
static float WaterKind(const terrain::TerrainMaterial& m) {
    const Vector3& c = m.waveColour;
    if (c.x > c.y && c.x > c.z) return 2.0f;
    return c.y > c.z ? 1.0f : 0.0f;
}

// The depth of the swamp over ground record `index`, -1 .. 1 (negative where the ground is above it), or -1 where its
// water record is not a swamp's.
static float SwampDepth(const SectorData* s, int index) {
    if (!s->hasWater || index + kWaterRecords >= std::ssize(s->records) || !s->owner) return -1.0f;
    const SectorVertex& water = s->records[index + kWaterRecords];
    const terrain::Terrain& owner = *s->owner;
    if (water.waterMaterial == SectorVertex::kNoWaterMaterial || water.waterMaterial >= std::ssize(owner.materials) ||
        WaterKind(owner.materials[water.waterMaterial]) != 1.0f)
        return -1.0f;
    return std::clamp(s->records[index].depth, -1.0f, 1.0f);
}

// The rise of the ground per landscape unit at a ground record, from its normal (the map's, smooth across the triangles:
// terrain.hlsl's shore_distance divides the depth by it, which a slope taken per triangle broke into steps).
static float GroundSlope(const SectorVertex& ground) {
    const Vector3& n = ground.normal;
    return std::min(std::sqrt(LengthSquared2D(n.x, n.y)) / std::max(n.z, 0.05f), 20.0f);
}

// `occlusion`: the sector's ground occlusion per ground record (GroundOcclusion).
static PageVertex MakeVertex(const SectorData* s, int index, const TileVertex& e, const float* occlusion) {
    const SectorVertex& record = s->records[index];
    PageVertex v{s->vertices[index], e.u, e.v, record.normal, -1.0f, 0.0f, 0.0f, 0.0f};
    if (index >= kWaterRecords) {
        v.depth = s->records[index - kWaterRecords].depth;
        v.shore = GroundSlope(s->records[index - kWaterRecords]);
        const terrain::Terrain* owner = s->owner;
        if (owner && record.waterMaterial < std::ssize(owner->materials))
            v.extra = WaterKind(owner->materials[record.waterMaterial]);
    } else {
        v.loss = 0.0f;
        v.depth = SwampDepth(s, index);
        v.extra = occlusion[index];
        if (const SectorVertex* water = WaterAbove(s, index)) {
            const terrain::Terrain* owner = s->owner;
            if (owner && water->waterMaterial < std::ssize(owner->materials)) {
                const terrain::TerrainMaterial& m = owner->materials[water->waterMaterial];
                v.loss = kWaterFalloff * m.absorption * record.depth * record.depth;
                v.shore = WaterKind(m) != 1.0f ? 1.0f : 0.0f;
            }
        }
    }
    return v;
}

static std::uint32_t Byte(float v) {
    float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    return static_cast<std::uint32_t>(c * 255.0f + 0.5f);
}

// The colour word a page vertex of record `index` is drawn with (lit on the GPU): the material
// colour the sun's light starts from (on the ground the water's above it, else the record's own), alpha the
// material byte (the water's opacity).
static std::uint32_t ColourWord(const SectorData* s, int index) {
    const SectorVertex& record = s->records[index];
    Vector3 m = record.materialColour;
    if (const SectorVertex* water = WaterAbove(s, index)) {
        m = Max(water->materialColour, m);
    }
    return static_cast<std::uint32_t>(record.materialByte) << 24 | Byte(m.x) << 16 | Byte(m.y) << 8 | Byte(m.z);
}

// What a page was built from: when any of it differs, the page is built again.
struct PageSource {
    const SectorVertex* records = nullptr;
    int recordCount = 0;
    const void* positions = nullptr;
    int positionCount = 0;
    const TileVertex* lists[4] = {};
    int listSizes[4] = {};
    const terrain::AnimatedTile* animated = nullptr;
    int animatedCount = 0;
    SectorChanges changes;
    std::uint32_t occlusion = 0;   // the ground occlusion's version (SectorOcclusion::version)

    // The same tiles of the same records: at most vertices and texture coordinates moved.
    bool SameTiles(const PageSource& o) const {
        for (int q = 0; q < 4; ++q)
            if (lists[q] != o.lists[q] || listSizes[q] != o.listSizes[q]) return false;
        return records == o.records && recordCount == o.recordCount && positions == o.positions &&
               positionCount == o.positionCount && animated == o.animated && animatedCount == o.animatedCount &&
               changes.rebuilt == o.changes.rebuilt;
    }
};

static PageSource SourceOf(const SectorData* s, int page) {
    PageSource src;
    src.records = s->records.data();
    src.recordCount = lib::Count(s->records);
    src.positions = s->vertices.data();
    src.positionCount = lib::Count(s->vertices);
    for (int q = 0; q < 4; ++q) {
        if (page < std::ssize(s->tiles[q])) {
            src.lists[q] = s->tiles[q][page].data();
            src.listSizes[q] = lib::Count(s->tiles[q][page]);
        }
    }
    src.animated = s->animated.data();
    src.animatedCount = lib::Count(s->animated);
    auto it = g_changes.find(s);
    if (it != g_changes.end()) src.changes = it->second;
    return src;
}

// A quarter's tiles of a page: its vertices, indices and runs of vertices (CachedPage::groups) within the page's,
// and all its vertices as one run.
struct QuarterRange {
    std::uint32_t firstVertex = 0, vertexCount = 0, firstIndex = 0, indexCount = 0;
    std::uint32_t firstGroup = 0, groupCount = 0;
    TerrainGroup whole{};
};

// A sector's ground layer as a caster: its 33 x 33 vertices with normals from their neighbours, kept while
// the sector holds the same vertices (the same array, place and builds).
struct SectorCaster {
    const void* positions = nullptr;
    int sx = -1, sy = -1;
    std::uint32_t rebuilt = 0;
    std::vector<TerrainCasterVertex> vertices;
    ShadowCasterSphere sphere{};
    PoolPlace place;
    bool dirty = true;
    std::uint32_t seen = 0;
    bool cast = false;
};
static std::unordered_map<const SectorData*, SectorCaster> g_sectorCasters;
static std::vector<SectorCaster*> g_frameCasters;
static std::vector<std::uint32_t> g_gridIndices;
static GpuPool g_casterPool(sizeof(TerrainCasterVertex), 1u << 15, 3u << 16, "ei-terrain-casters");

static void BuildCaster(const SectorData* s, SectorCaster& c) {
    const Vector3* v = s->vertices.data();
    c.vertices.resize(static_cast<std::size_t>(kWaterRecords));
    Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    using terrain::kLastVertex, terrain::kSectorRow;
    auto at = [&](int x, int y) { return v[std::clamp(y, 0, kLastVertex) * kSectorRow + std::clamp(x, 0, kLastVertex)]; };
    for (int y = 0; y < kSectorRow; ++y)
        for (int x = 0; x < kSectorRow; ++x) {
            Vector3 p = at(x, y);
            Vector3 n = Cross(at(x + 1, y) - at(x - 1, y), at(x, y + 1) - at(x, y - 1));
            float length = Length(n);
            if (length > 1e-12f) {
                float k = (n.z < 0.0f ? -1.0f : 1.0f) / length;   // turned up
                n *= k;
            } else {
                n = Vector3{0.0f, 0.0f, 1.0f};
            }
            c.vertices[static_cast<std::size_t>(y * kSectorRow + x)] = TerrainCasterVertex{p, n};
            lo = Min(lo, p);
            hi = Max(hi, p);
        }
    const Vector3 centre = (lo + hi) * 0.5f;
    c.sphere = ShadowCasterSphere{centre.x, centre.y, centre.z, Length((hi - lo) * 0.5f)};
    if (g_gridIndices.empty()) {
        for (int y = 0; y < terrain::kSectorCells; ++y)
            for (int x = 0; x < terrain::kSectorCells; ++x) {
                std::uint32_t a = static_cast<std::uint32_t>(y * kSectorRow + x), b = a + 1, d = a + kSectorRow, e = d + 1;
                for (std::uint32_t i : {a, b, e, a, e, d}) g_gridIndices.push_back(i);
            }
    }
}

// --- The ground's own ambient occlusion ------------------------------------------------------------------

// How much of the sky a ground vertex's surroundings hide, from the height field (the ground records' heights, a
// cell a unit): in each of the grid's eight directions the highest rise of the ground within kOcclusionReach cells
// above the vertex's own tangent plane, as a sine less the plane's own, faded with the distance (1 - (d / reach)^2);
// the mean of the eight is how much is hidden, and 1 less kGroundOcclusion times that reaches the vertex. The heights
// around the sector come from its neighbours the landscape holds (Terrain::CachedSector: nothing is loaded for it); a
// neighbour it does not hold continues the sector's edge, and the occlusion is worked out again once it is held. Small
// shapes (a unit, a step, the ground's facets) are the screen's (render_occlusion.cpp).
constexpr int kOcclusionReach = 8;
constexpr float kGroundOcclusion = 0.7f;
struct SectorOcclusion {
    const void* positions = nullptr;   // what it was worked out for: the sector's vertices, place and builds
    int sx = -1, sy = -1;
    std::uint32_t rebuilt = 0;
    std::uint8_t neighbours = 0;       // the neighbours whose heights it read (bit k: kNeighbours[k])
    std::uint32_t version = 0;         // a new one each time it is worked out
    std::uint32_t seen = 0;            // FrameNumber() of its last use
    std::vector<float> occlusion;      // per ground record
};
static std::unordered_map<const SectorData*, SectorOcclusion> g_occlusion;
static std::uint32_t g_occlusionVersion = 0;   // the last given out; also how often one was worked out
constexpr int kNeighbours[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

// The ground vertices' positions of the sector's neighbour `k`, if the landscape holds it.
static const SectorPosition* NeighbourVertices(const SectorData* s, int k) {
    const terrain::Terrain* owner = s->owner;
    if (!owner) return nullptr;
    const int nx = s->sx + kNeighbours[k][0], ny = s->sy + kNeighbours[k][1];
    if (nx < 0 || ny < 0 || nx >= owner->sectorsX || ny >= owner->sectorsY) return nullptr;
    const SectorData* n = owner->CachedSector(nx, ny);
    if (!n || !lib::Has(n->built, terrain::SectorStage::Loaded) || std::ssize(n->vertices) < kWaterRecords) return nullptr;
    return n->vertices.data();
}

static std::uint8_t HeldNeighbours(const SectorData* s) {
    std::uint8_t held = 0;
    for (int k = 0; k < 8; ++k)
        if (NeighbourVertices(s, k)) held |= static_cast<std::uint8_t>(1u << k);
    return held;
}

static void WorkOutOcclusion(const SectorData* s, SectorOcclusion& o) {
    using terrain::kLastVertex, terrain::kSectorCells, terrain::kSectorRow;
    constexpr int kReach = kOcclusionReach, kRow = kSectorRow + 2 * kReach;
    o.version = ++g_occlusionVersion;
    if (std::ssize(s->vertices) < kWaterRecords) {
        o.occlusion.assign(static_cast<std::size_t>(kWaterRecords), 1.0f);
        return;
    }
    // The heights from kReach cells before the sector's first vertex to kReach after its last.
    static std::vector<float> heights;
    heights.assign(static_cast<std::size_t>(kRow) * kRow, 0.0f);
    const SectorPosition* own = s->vertices.data();
    const SectorPosition* around[8];
    for (int k = 0; k < 8; ++k) around[k] = (o.neighbours >> k) & 1 ? NeighbourVertices(s, k) : nullptr;
    for (int y = -kReach; y <= kLastVertex + kReach; ++y)
        for (int x = -kReach; x <= kLastVertex + kReach; ++x) {
            const int sideX = x < 0 ? -1 : x > kLastVertex ? 1 : 0, sideY = y < 0 ? -1 : y > kLastVertex ? 1 : 0;
            const SectorPosition* from = own;
            int lx = x, ly = y;
            if (sideX != 0 || sideY != 0) {
                int k = 0;
                while (kNeighbours[k][0] != sideX || kNeighbours[k][1] != sideY) ++k;
                if (around[k]) {
                    from = around[k];
                    lx -= sideX * kSectorCells;
                    ly -= sideY * kSectorCells;
                } else {
                    lx = std::clamp(x, 0, kLastVertex);
                    ly = std::clamp(y, 0, kLastVertex);
                }
            }
            heights[static_cast<std::size_t>((y + kReach) * kRow + x + kReach)] = from[ly * kSectorRow + lx].z;
        }
    auto height = [&](int x, int y) { return heights[static_cast<std::size_t>((y + kReach) * kRow + x + kReach)]; };
    o.occlusion.resize(static_cast<std::size_t>(kWaterRecords));
    constexpr float kReachSquared = static_cast<float>(kReach * kReach);
    for (int y = 0; y < kSectorRow; ++y)
        for (int x = 0; x < kSectorRow; ++x) {
            const float h = height(x, y);
            const float slopeX = (height(x + 1, y) - height(x - 1, y)) * 0.5f;
            const float slopeY = (height(x, y + 1) - height(x, y - 1)) * 0.5f;
            float hidden = 0.0f;
            for (const auto& d : kNeighbours) {
                const float step = d[0] != 0 && d[1] != 0 ? 1.41421356f : 1.0f;
                // The tangent plane's rise along the direction, as a sine.
                const float plane = Dot2D(Vector2{slopeX, slopeY}, Vector2{static_cast<float>(d[0]), static_cast<float>(d[1])}) / step;
                const float planeSine = plane / std::sqrt(1.0f + plane * plane);
                float horizon = 0.0f;
                for (int k = 1; k * step <= static_cast<float>(kReach); ++k) {
                    const float distance = k * step;
                    const float rise = (height(x + k * d[0], y + k * d[1]) - h) / distance;
                    const float sine = rise / std::sqrt(1.0f + rise * rise);
                    horizon = std::max(horizon, (sine - planeSine) * (1.0f - distance * distance / kReachSquared));
                }
                hidden += horizon;
            }
            o.occlusion[static_cast<std::size_t>(y * kSectorRow + x)] = 1.0f - kGroundOcclusion * std::min(1.0f, hidden / 8.0f);
        }
}

// The sector's ground occlusion, worked out when it is new for the sector or a neighbour it continued is held now.
static const SectorOcclusion& GroundOcclusion(const SectorData* s) {
    std::uint32_t rebuilt = 0;
    if (auto it = g_changes.find(s); it != g_changes.end()) rebuilt = it->second.rebuilt;
    SectorOcclusion& o = g_occlusion[s];
    o.seen = FrameNumber();
    const bool same = o.positions == s->vertices.data() && o.sx == s->sx && o.sy == s->sy && o.rebuilt == rebuilt &&
                      !o.occlusion.empty();
    const std::uint8_t held = HeldNeighbours(s);
    if (same && (held & ~o.neighbours) == 0) return o;
    o.positions = s->vertices.data();
    o.sx = s->sx;
    o.sy = s->sy;
    o.rebuilt = rebuilt;
    o.neighbours = same ? static_cast<std::uint8_t>(o.neighbours | held) : held;
    WorkOutOcclusion(s, o);
    return o;
}

// A sector page as built and as placed in the pool.
struct CachedPage {
    PageSource source;
    std::vector<PageVertex> vertices;
    std::vector<std::uint32_t> colours;
    std::vector<std::uint16_t> records;    // the record each vertex reads its position and colours from
    std::vector<const TileVertex*> entries;
    std::vector<std::uint32_t> indices;
    bool readsWater = false;
    bool animatedTiles = false;
    QuarterRange quarters[4];
    std::vector<TerrainGroup> groups;      // the quarters' vertices in runs of kGroupVertices, with their boxes
    PoolPlace place;
    bool geometryDirty = true;
    std::uint32_t vertexFrom = 0, vertexTo = 0;      // vertices whose positions or texture coordinates to upload
    std::uint32_t colourFrom = 0, colourTo = 0;      // vertices whose colours to upload
    bool pending = false;
    std::uint32_t lastUsed = 0;
    std::uint32_t mark = 0;                          // Restart's
};

// The quarters' vertices in runs with their boxes, after they were built or moved: a run is a few tiles in a row
// (8 tiles), small enough that its box's top stays close to its ground.
constexpr std::uint32_t kGroupVertices = 72;
static void MakeGroups(CachedPage& p) {
    p.groups.clear();
    for (QuarterRange& q : p.quarters) {
        q.firstGroup = static_cast<std::uint32_t>(p.groups.size());
        q.whole = TerrainGroup{Box{}, q.firstVertex, q.vertexCount};
        for (std::uint32_t first = q.firstVertex; first < q.firstVertex + q.vertexCount; first += kGroupVertices) {
            TerrainGroup g{Box{}, first, (std::min)(kGroupVertices, q.firstVertex + q.vertexCount - first)};
            for (std::uint32_t i = first; i < first + g.count; ++i) {
                const Vector3& at = p.vertices[i].position;
                g.box.Extend(at);
            }
            p.groups.push_back(g);
            q.whole.box.low = Min(q.whole.box.low, g.box.low);
            q.whole.box.high = Max(q.whole.box.high, g.box.high);
        }
        q.groupCount = static_cast<std::uint32_t>(p.groups.size()) - q.firstGroup;
    }
}

// The tiles of `page`, every quarter's (culled or not), as DrawSectorPage builds them: positions, texture coordinates, the record of each
// vertex and the triangles; `occlusion` is the sector's GroundOcclusion.
static void BuildPage(const SectorData* s, int page, CachedPage& out, const float* occlusion) {
    out.vertices.clear();
    out.records.clear();
    out.entries.clear();
    out.indices.clear();
    out.animatedTiles = false;
    // The sector's animated tiles (their 9 entries in the tile lists), for animatedTiles.
    static std::vector<const TileVertex*> animated;
    animated.clear();
    for (int i = 0; i < std::ssize(s->animated); ++i) animated.push_back(s->animated[i].vertices);
    std::sort(animated.begin(), animated.end());
    for (int quarter = 0; quarter < 4; ++quarter) {
        QuarterRange& range = out.quarters[quarter];
        range.firstVertex = static_cast<std::uint32_t>(out.vertices.size());
        range.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        const auto& lists = s->tiles[quarter];
        if (page < std::ssize(lists) && !lists[page].empty()) {
            const std::vector<terrain::TileVertex>& list = lists[page];
            int tiles = lib::Count(list) / 9;
            for (int t = 0; t < tiles; ++t) {
                const TileVertex* tile = list.data() + t * 9;
                if (std::binary_search(animated.begin(), animated.end(), tile)) out.animatedTiles = true;
                auto base = static_cast<std::uint32_t>(out.vertices.size());
                bool ok = true;
                for (int k = 0; k < kTileVertices; ++k) {
                    const TileVertex& e = tile[k];
                    std::ptrdiff_t index = e.vertex - s->records.data();
                    if (index < 0 || index >= std::ssize(s->records) || index >= std::ssize(s->vertices)) {
                        ok = false;
                        break;
                    }
                    out.vertices.push_back(MakeVertex(s, static_cast<int>(index), e, occlusion));
                    out.records.push_back(static_cast<std::uint16_t>(index));
                    out.entries.push_back(&e);
                }
                if (!ok) {
                    out.vertices.resize(base);
                    out.records.resize(base);
                    out.entries.resize(base);
                    continue;
                }
                for (std::uint16_t corner : kTileTriangles) out.indices.push_back(base + corner);
            }
        }
        range.vertexCount = static_cast<std::uint32_t>(out.vertices.size()) - range.firstVertex;
        range.indexCount = static_cast<std::uint32_t>(out.indices.size()) - range.firstIndex;
    }
    out.readsWater = std::any_of(out.records.begin(), out.records.end(), [](std::uint16_t r) { return r >= kWaterRecords; });
    MakeGroups(out);
}

static bool SameGeometry(const CachedPage& a, const CachedPage& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(PageVertex)) == 0 &&
           std::memcmp(a.records.data(), b.records.data(), a.records.size() * sizeof(std::uint16_t)) == 0 &&
           std::memcmp(a.entries.data(), b.entries.data(), a.entries.size() * sizeof(const TileVertex*)) == 0 &&
           std::memcmp(a.indices.data(), b.indices.data(), a.indices.size() * sizeof(std::uint32_t)) == 0;
}

static void MarkVertex(std::uint32_t i, std::uint32_t& from, std::uint32_t& to) {
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
static bool ReadColours(const SectorData* s, CachedPage& p, std::uint32_t from, std::uint32_t to) {
    bool changed = false;
    for (std::uint32_t i = from; i < to; ++i) {
        // The colour word (the material) is the one the page was built with; the game writes the
        // specular word (the edge fade; on the water the wave crests).
        const SectorVertex& record = s->records[p.records[i]];
        std::uint32_t* kept = &p.colours[2 * static_cast<std::size_t>(i)];
        if (kept[1] == record.specular) continue;
        kept[1] = record.specular;
        MarkVertex(i, p.colourFrom, p.colourTo);
        changed = true;
    }
    return changed;
}

// Reads every vertex's position, texture coordinates and occlusion of `p` again (the game moved some, or the
// occlusion was worked out again: the tiles are the same); the changed ones are marked for upload. True when any
// changed.
static bool ReadVertices(const SectorData* s, CachedPage& p, const float* occlusion) {
    bool changed = false;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(p.vertices.size()); ++i) {
        PageVertex& kept = p.vertices[i];
        PageVertex now = MakeVertex(s, p.records[i], *p.entries[i], occlusion);
        if (std::memcmp(&kept, &now, sizeof(PageVertex)) == 0) continue;
        kept = now;
        MarkVertex(i, p.vertexFrom, p.vertexTo);
        changed = true;
    }
    if (changed) MakeGroups(p);
    return changed;
}

struct PageKey {
    const SectorData* sector = nullptr;
    int page = 0;
    bool operator==(const PageKey& o) const { return sector == o.sector && page == o.page; }
};
struct PageKeyHash {
    std::size_t operator()(const PageKey& k) const {
        return std::hash<const void*>()(k.sector) ^ static_cast<std::size_t>(k.page) * 0x9E3779B9u;
    }
};
static std::unordered_map<PageKey, CachedPage, PageKeyHash> g_pages;
static std::vector<CachedPage*> g_pending;   // pages with something to upload
constexpr std::uint32_t kPageLife = 600;

#if defined(EI_RENDER_VALIDATION)
static bool g_cacheOn = true;
static std::uint64_t g_checks = 0, g_stale = 0;
// Every this many frames each kept page is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
#endif

static GpuPool g_pool(sizeof(PageVertex), 1u << 17, 3u << 17, "ei-terrain");

// --- GPU side ------------------------------------------------------------------------------------------

// The pipeline drawing terrain as the old device does with `s`: culling, depth and blending from the
// state the old path draws the page with. The ground pass runs under the terrain's state
// block with what the sky left (CULLMODE 2, ZWRITEENABLE 1; the world draw), the
// water pass under the scene's; both with ALPHABLENDENABLE on (SRCALPHA /
// INVSRCALPHA in the harness runs; the verify build logs each new pipeline's state).
static gpu::PipelineDesc TerrainDesc(const PassState& s, int samples) {
    namespace program = gpu_shaders::terrain;
    gpu::PipelineDesc p;
    p.program = program::kIndex;
    p.bufferCount = 2;
    p.buffers[0].stride = sizeof(PageVertex);
    p.buffers[1].stride = sizeof(ColourPair);
    p.attributeCount = 8;
    p.attributes[0] = {program::kAttrPos, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, position)};
    p.attributes[1] = {program::kAttrCol, 1, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, colour)};
    p.attributes[2] = {program::kAttrSpec, 1, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, specular)};
    p.attributes[3] = {program::kAttrUv, 0, gpu::VertexFormat::Float2, offsetof(PageVertex, u)};
    p.attributes[4] = {program::kAttrNormal, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, normal)};
    p.attributes[5] = {program::kAttrLoss, 0, gpu::VertexFormat::Float, offsetof(PageVertex, loss)};
    p.attributes[6] = {program::kAttrDepth, 0, gpu::VertexFormat::Float2, offsetof(PageVertex, depth)};   // depth, shore
    p.attributes[7] = {program::kAttrExtra, 0, gpu::VertexFormat::Float, offsetof(PageVertex, extra)};
    ApplyPassState(p, s);
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

// TerrainDesc's pipeline (gpu.h keeps them by description).
static gpu::Pipeline Pipeline(const PassState& s, [[maybe_unused]] bool water) {   // water: for the verify build's log
    const int samples = PassSampleCount();
#if defined(EI_RENDER_VALIDATION)
    const int known = gpu::PipelineCount();
#endif
    gpu::Pipeline pipeline = gpu::MakePipeline(TerrainDesc(s, samples), "ei-terrain");
    static int failures = 0;
    if (!pipeline)
        LogFirst(failures, 10,
                 "render: a terrain pipeline could not be created (state key %08X; see the gpu messages above)",
                 s.PipelineKey());
#if defined(EI_RENDER_VALIDATION)
    if (gpu::PipelineCount() != known)
        ei::Log("render: terrain pipeline (first for a %s page): blend %d (%u, %u), depth test %d write %d func %u, "
                "cull %u",
                water ? "water" : "ground", s.blend ? 1 : 0, static_cast<unsigned>(s.sourceBlend), static_cast<unsigned>(s.destBlend),
                s.depthTest ? 1 : 0, s.depthWrite ? 1 : 0, static_cast<unsigned>(s.depthFunc), static_cast<unsigned>(s.cull));
#endif
    return pipeline;
}

#if defined(EI_RENDER_VALIDATION)
// --- The level map (render::TerrainLevels, harness build) ---------------------------------------------

static TerrainLevelMode g_levelMode = TerrainLevelMode::Off;

// A texture of the page texture's size and levels, each level one grey (level k: 32 k).
static std::vector<gpu::Texture> g_levelImages;

static gpu::Texture LevelView(const gpu::Texture& page) {
    if (!page) return gpu::Texture{};
    const int width = page.width, height = page.height, levels = page.levels;
    for (const gpu::Texture& l : g_levelImages)
        if (l.width == width && l.height == height && l.levels == levels) return l;
    if (width <= 0 || height <= 0 || levels <= 0) return gpu::Texture{};
    gpu::Texture t = gpu::MakeTexture({width, height, levels, 1, gpu::Format::Rgba8, gpu::kSampled, "ei-terrain-levels"});
    if (!t) return t;
    std::vector<std::uint8_t> level;
    for (int k = 0; k < levels; ++k) {
        int w = (width >> k) > 0 ? width >> k : 1, h = (height >> k) > 0 ? height >> k : 1;
        auto grey = static_cast<std::uint8_t>(k * 32 < 255 ? k * 32 : 255);
        level.assign(static_cast<std::size_t>(w) * h * 4, grey);
        for (std::size_t i = 3; i < level.size(); i += 4) level[i] = 255;
        gpu::Write(t, k, {0, 0, w, h}, level.data(), static_cast<std::size_t>(w) * 4);
    }
    g_levelImages.push_back(t);
    return t;
}
#endif

// The index runs of `batch`'s pieces placed in the pool, pieces that follow each other in it as one run:
// `draw(first, count)` per run.
template <class Draw>
static void ForPieceRuns(const Frame& f, const TerrainBatch& batch, Draw draw) {
    std::uint32_t first = 0, count = 0;
    for (int k = batch.firstPiece; k < batch.firstPiece + batch.pieceCount; ++k) {
        const TerrainPiece& piece = f.terrainPieces[static_cast<std::size_t>(k)];
        const CachedPage& p = *piece.page;
        const QuarterRange& q = p.quarters[piece.quarter];
        if (!g_pool.Placed(p.place) || q.indexCount == 0) continue;
        std::uint32_t start = p.place.firstIndex + q.firstIndex;
        if (count > 0 && first + count == start) {
            count += q.indexCount;
        } else {
            if (count > 0) draw(first, count);
            first = start;
            count = q.indexCount;
        }
    }
    if (count > 0) draw(first, count);
}

// --- The pool -------------------------------------------------------------------------------------------

static bool Place(CachedPage& p) {
    bool anew = false;
    if (!g_pool.Place(p.place, static_cast<std::uint32_t>(p.vertices.size()), static_cast<std::uint32_t>(p.indices.size()),
                      &anew))
        return false;
    if (anew) p.geometryDirty = true;
    return true;
}

// The pool starts again with the pages of frame `f` (and the pending ones): they are placed again
// from its start. It is made larger when they need more than half of it.
static void Restart(const Frame& f) {
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

static void Write(CachedPage& p) {
    FrameStats& stats = g_stats;
    if (p.geometryDirty) {
        auto count = static_cast<std::uint32_t>(p.vertices.size());
        g_pool.WriteGeometry(p.place, 0, count, p.vertices.data());
        g_pool.WriteColours(p.place, 0, count, p.colours.data());
        g_pool.WriteIndices(p.place, p.indices.data(), p.indices.size());
        stats.terrainVertexBytes += p.vertices.size() * (sizeof(PageVertex) + sizeof(ColourPair));
        stats.terrainIndexBytes += p.indices.size() * sizeof(std::uint32_t);
    } else {
        if (p.vertexTo > p.vertexFrom) {
            g_pool.WriteGeometry(p.place, p.vertexFrom, p.vertexTo - p.vertexFrom, &p.vertices[p.vertexFrom]);
            stats.terrainVertexBytes += (p.vertexTo - p.vertexFrom) * sizeof(PageVertex);
        }
        if (p.colourTo > p.colourFrom) {
            g_pool.WriteColours(p.place, p.colourFrom, p.colourTo - p.colourFrom,
                                &p.colours[2 * static_cast<std::size_t>(p.colourFrom)]);
            stats.terrainVertexBytes += (p.colourTo - p.colourFrom) * sizeof(ColourPair);
        }
    }
    p.geometryDirty = false;
    p.vertexFrom = p.vertexTo = 0;
    p.colourFrom = p.colourTo = 0;
}

static CachedPage g_scratch;
static bool BuildAgain(const SectorData* s, int page, CachedPage& p, const float* occlusion) {
    BuildPage(s, page, g_scratch, occlusion);
    if (SameGeometry(g_scratch, p)) return false;
    p.vertices.swap(g_scratch.vertices);
    p.records.swap(g_scratch.records);
    p.entries.swap(g_scratch.entries);
    p.indices.swap(g_scratch.indices);
    p.readsWater = g_scratch.readsWater;
    p.animatedTiles = g_scratch.animatedTiles;
    for (int q = 0; q < 4; ++q) p.quarters[q] = g_scratch.quarters[q];
    p.groups.swap(g_scratch.groups);
    return true;
}

// The pass states the ground and the water pages are drawn with in every battle (the pipelines a run makes, logged).
void ListTerrainPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    PassState ground;   // alpha-blended, clockwise triangles culled
    ground.cull = Cull::Clockwise;
    ground.blend = true;
    ground.sourceBlend = Blend::SourceAlpha;
    ground.destBlend = Blend::InvSourceAlpha;
    out.push_back(TerrainDesc(ground, samples));
    PassState water;   // opaque, nothing culled
    water.blend = false;
    out.push_back(TerrainDesc(water, samples));
}

// The program checked at the start: a pipeline of it made (gpu.h keeps it).
bool CreateTerrainShader() {
    bool ok = static_cast<bool>(gpu::MakePipeline(TerrainDesc(PassState{}, 1), "ei-terrain"));
    if (!ok) ei::Log("render: the terrain pipeline could not be created (see the gpu messages above)");
    return ok;
}

void UploadTerrain(const Frame& f) {
#if defined(EI_RENDER_VALIDATION)
    // The level maps are written here, before the scene's pass (gpu.h refuses writes inside one).
    if (g_levelMode != TerrainLevelMode::Off)
        for (const TerrainBatch& batch : f.terrainBatches)
            if (!batch.water) LevelView(TextureView(batch.texture));
#endif
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
    namespace program = gpu_shaders::terrain;
    if (batch.pieceCount <= 0) return;
    Geometry g;
    g_pool.Bind(g);
    if (!g.vertices[0].buffer) return;
    gpu::Pipeline pipeline = Pipeline(batch.state, batch.water);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    g.Bind();
    gpu::TextureBinding texture{TextureView(batch.texture), AnisotropicSampler(batch.state.stages[0], kMostAnisotropy)};
    float mode = batch.water ? 1.0f : 0.0f;
#if defined(EI_RENDER_VALIDATION)
    if (g_levelMode != TerrainLevelMode::Off && !batch.water) {
        gpu::Texture levels = LevelView(texture.texture);
        if (levels) {
            texture.texture = levels;
            if (g_levelMode == TerrainLevelMode::PointMips) texture.sampler = LinearSampler();
            mode = 2.0f;
        }
    }
#endif
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex, {&texture, 1});
    BindShadowMap(program::kSamplerShadowMap);
    BindPointShadows(program::kSamplerPointShadowMap);
    const gpu::TextureBinding ripples = RippleMap();
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerRipples, {&ripples, 1});
    BindCloudMap(program::kSamplerCloudMap);
    BindOcclusion(program::kSamplerOcclusionMap);
    BindRoofMap(program::kSamplerRoofMap);
    CountBindings(texture.texture);
    // The ground receives the objects' shadows; the water does not (the ground under it does). Both take the clouds'.
    const bool clouded = mode < 1.5f && !f.shadow.lights.empty();
    const bool receives = clouded && !batch.water;
    const ShadowParams& shadow = ShadowUniforms(f, receives, clouded);
    TerrainVsParams vs{};
    vs.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    vs.light = clouded ? f.shadow.lights[0] : vs.matrix;
    vs.shift[0] = f.pixelShift[0];
    vs.shift[1] = f.pixelShift[1];
    vs.normalOffset[0] = ShadowNormalOffset(shadow);
    TerrainFsParams fs{};
    UnpackColour(f.fogColour, fs.fog);
    fs.flags[0] = mode;
    fs.flags[1] = f.fogEnabled ? 1.0f : 0.0f;
    RippleOffsets(fs.ripples, fs.slowRipples);
    SurfaceUniforms(fs.weather, fs.rain);
    OcclusionUniforms(batch.occluded && mode < 1.5f, fs.occlusion, fs.occlusionMap);
    SceneLightParams light;
    LightUniforms(&light);
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockTerrainVsParams, vs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockTerrainFsParams, fs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowParams, shadow);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockSceneLightParams, light);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPointShadowParams, f.shadow.point.params);
    std::size_t uniformBytes = sizeof(vs) + sizeof(fs) + sizeof(shadow) + sizeof(light) + sizeof(f.shadow.point.params);
    ForPieceRuns(f, batch, [&](std::uint32_t first, std::uint32_t count) {
        gpu::DrawIndexed(first, count);
        CountDraw(uniformBytes);
        uniformBytes = 0;
    });
}

gpu::PipelineDesc TerrainDepthDesc() {
    namespace program = gpu_shaders::shadow_terrain;
    gpu::PipelineDesc p;
    p.program = program::kIndex;
    p.bufferCount = 1;
    p.buffers[0].stride = sizeof(PageVertex);
    p.attributeCount = 2;
    p.attributes[0] = {program::kAttrPos, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, position)};
    p.attributes[1] = {program::kAttrNormal, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, normal)};
    p.colour = gpu::Format::None;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::LessEqual;
    p.depthWrite = true;
    return p;
}

// shadow_terrain.hlsl with the batch's clip matrix for the light and no sun (no face left out).
int DrawTerrainDepth(const Frame& f, gpu::Pipeline pipeline) {
    namespace program = gpu_shaders::shadow_terrain;
    Geometry g;
    g_pool.Bind(g);
    g.vertexBuffers = 1;   // the colours: not read
    if (!g.vertices[0].buffer) return 0;
    gpu::ApplyPipeline(pipeline);
    g.Bind();
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowTerrainFsParams, ShadowTerrainFsParams{});
    int draws = 0;
    for (const TerrainBatch& batch : f.terrainBatches) {
        if (!batch.occluded || batch.pieceCount <= 0) continue;
        gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockShadowTerrainVsParams,
                          ShadowTerrainVsParams{f.matrices[static_cast<std::size_t>(batch.matrix)]});
        ForPieceRuns(f, batch, [&](std::uint32_t first, std::uint32_t count) {
            gpu::DrawIndexed(first, count);
            ++draws;
        });
    }
    return draws;
}

void GroundOcclusionCounts(int* sectors, int* made) {
    *sectors = static_cast<int>(g_occlusion.size());
    *made = static_cast<int>(g_occlusionVersion);
}

std::span<const TerrainGroup> TerrainPieceGroups(const TerrainPiece& piece) {
    const QuarterRange& q = piece.page->quarters[piece.quarter];
    return std::span<const TerrainGroup>(piece.page->groups.data() + q.firstGroup, q.groupCount);
}

const TerrainGroup& TerrainPieceWhole(const TerrainPiece& piece) { return piece.page->quarters[piece.quarter].whole; }

void TerrainGroupPositions(const TerrainPiece& piece, const TerrainGroup& group, std::uint32_t step,
                           std::vector<Vector3>* out) {
    out->clear();
    for (std::uint32_t i = 0; i < group.count; i += step) out->push_back(piece.page->vertices[group.first + i].position);
}

void TerrainFrameDone() {
    std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (auto it = g_pages.begin(); it != g_pages.end();) {
        if (!it->second.pending && now - it->second.lastUsed > kPageLife) it = g_pages.erase(it);
        else ++it;
    }
    for (auto it = g_occlusion.begin(); it != g_occlusion.end();) {
        if (now - it->second.seen > kPageLife) it = g_occlusion.erase(it);
        else ++it;
    }
}

#if defined(EI_RENDER_VALIDATION)
void TerrainLevels(TerrainLevelMode mode) {
    if (mode != g_levelMode)
        ei::Log("render: terrain level map %s",
                mode == TerrainLevelMode::Off ? "off" : mode == TerrainLevelMode::Levels ? "on" : "on, through point mips");
    g_levelMode = mode;
}
#endif

void TerrainChanged(const terrain::SectorData* sector, TerrainChange change) {
    if (!sector) return;
    SectorChanges& c = g_changes[sector];
    switch (change) {
    case TerrainChange::Rebuilt:
        ++c.rebuilt;
        break;
    case TerrainChange::Moved:
        ++c.moved;
        break;
    default:
        ++c.waterMoved;
        break;
    }
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

void TerrainPage(const SectorData* s, const SceneRenderer* renderer, int page) {
    if (!s || !renderer) return;
    StatsTimer timer(&FrameStats::terrainMs);
    ++g_stats.terrainPages;
    Frame& f = g_frame;
    if (f.terrainPieces.size() > 1'000'000) return;   // no present for a long time: drop the rest
    // The clip matrix: the top of the transform pipeline's stack, which the old path transforms the sector's
    // vertices with (the landscape's model matrix pushed by the terrain passes on top of the view).
    const TransformPipeline* pipeline = renderer->pipeline;
    if (!pipeline) return;
    const MatrixStack& clipStack = pipeline->stacks[TransformPipeline::kClipStack];
    const int depth = clipStack.count;
    if (depth <= 0) return;
    if (!f.haveFog) ReadFog(f);
    int matrix = AddMatrix(clipStack.matrices[depth - 1]);
    const Texture* texture = PageTexture(s, page);
    bool water = page >= s->owner->LayerPages();
    bool inView = false;
    for (int q = 0; q < 4 && !inView; ++q) {
        const auto& lists = s->tiles[q];
        inView = s->quarterMode[q] != TransformPipeline::kClipOutside && page < std::ssize(lists) && !lists[page].empty() &&
                 std::ssize(lists[page]) >= 9;
    }
    if (!inView) return;

    PageKey key{s, page};
    auto [found, made] = g_pages.try_emplace(key);
    CachedPage& p = found->second;
    PageSource source = SourceOf(s, page);
    const SectorOcclusion& occlusion = GroundOcclusion(s);
    source.occlusion = occlusion.version;
    const float* occluded = occlusion.occlusion.data();
    bool moved = false, upload = false;
    if (made) {
        BuildPage(s, page, p, occluded);
        upload = true;
#if defined(EI_RENDER_VALIDATION)
    } else if (!g_cacheOn) {
        moved = BuildAgain(s, page, p, occluded);   // off: all of it uploaded again, as before
        upload = true;
#endif
    } else if (!p.source.SameTiles(source)) {
        moved = upload = BuildAgain(s, page, p, occluded);
    } else if ((p.animatedTiles && p.source.changes.moved != source.changes.moved) ||
               (p.readsWater && p.source.changes.waterMoved != source.changes.waterMoved) ||
               p.source.occlusion != source.occlusion) {
        moved = ReadVertices(s, p, occluded);
#if defined(EI_RENDER_VALIDATION)
    } else if (FrameNumber() % kCheckEvery == 0) {
        ++g_checks;
        if (BuildAgain(s, page, p, occluded)) {
            LogFirst(g_stale, 20, "render: a terrain page kept on the GPU was stale: sector (%d, %d) page %d", s->sx,
                     s->sy, page);
            moved = upload = true;
        }
#endif
    }
    p.source = source;
    if (upload) {
        p.geometryDirty = true;
        p.colours.resize(2 * p.vertices.size());
        for (std::size_t i = 0; i < p.vertices.size(); ++i) {
            p.colours[2 * i] = ColourWord(s, p.records[i]);
            p.colours[2 * i + 1] = s->records[p.records[i]].specular;
        }
        p.colourFrom = p.colourTo = 0;
    }
    if (!g_pool.Placed(p.place)) p.geometryDirty = true;

    bool relit = false;
    std::size_t firstPiece = f.terrainPieces.size();
    for (int q = 0; q < 4; ++q) {
        const QuarterRange& range = p.quarters[q];
        if (s->quarterMode[q] == TransformPipeline::kClipOutside || range.indexCount == 0) continue;
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
    ++g_stats.terrainReuse[made ? FrameStats::New : moved ? FrameStats::Moved : relit ? FrameStats::Relit : FrameStats::Same];
    // The old device's state the page is drawn with; DrawPage changes none of what the pipeline
    // takes before its draws (it turns SPECULARENABLE on around each, which the shader always adds).
    PassState state;
    {
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
    ++g_stats.terrainBatches;
    f.terrainBatches.push_back(TerrainBatch{texture, water, matrix, static_cast<int>(firstPiece), count, state});
    f.commands.push_back(Command{Command::Terrain, static_cast<int>(f.terrainBatches.size()) - 1});
}

// --- The ground's casters into the shadow map ------------------------------------------------------

void ShadowTerrainSector(const SectorData* s) {
    if (!s || !lib::Has(s->built, terrain::SectorStage::Loaded) || s->vertices.empty() || std::ssize(s->vertices) < kWaterRecords) return;
    StatsTimer timer(&FrameStats::shadowMs);
    std::uint32_t rebuilt = 0;
    if (auto it = g_changes.find(s); it != g_changes.end()) rebuilt = it->second.rebuilt;
    SectorCaster& c = g_sectorCasters[s];
    if (c.seen == FrameNumber() && !c.vertices.empty()) return;
    c.seen = FrameNumber();
    c.cast = false;
    if (c.positions != s->vertices.data() || c.sx != s->sx || c.sy != s->sy || c.rebuilt != rebuilt || c.vertices.empty()) {
        BuildCaster(s, c);
        c.positions = s->vertices.data();
        c.sx = s->sx;
        c.sy = s->sy;
        c.rebuilt = rebuilt;
        c.dirty = true;
    }
    if (!ShadowCasterReaches(c.sphere)) return;
    c.cast = true;
    g_frameCasters.push_back(&c);
}

std::size_t TerrainCasterCount() { return g_frameCasters.size(); }

void UploadTerrainCasters() {
    if (g_frameCasters.empty()) return;
    const auto vertices = static_cast<std::uint32_t>(kWaterRecords);
    const auto indices = static_cast<std::uint32_t>(g_gridIndices.size());
    bool placed = true;
    for (SectorCaster* c : g_frameCasters) {
        bool anew = false;
        if (!g_casterPool.Place(c->place, vertices, indices, &anew)) {
            placed = false;
            break;
        }
        if (anew) c->dirty = true;
    }
    if (!placed) {   // the pool starts again with this frame's sectors
        g_casterPool.Restart(static_cast<std::uint64_t>(vertices) * g_frameCasters.size(),
                             static_cast<std::uint64_t>(indices) * g_frameCasters.size());
        for (SectorCaster* c : g_frameCasters) {
            bool anew = false;
            g_casterPool.Place(c->place, vertices, indices, &anew);
            c->dirty = true;
        }
    }
    for (SectorCaster* c : g_frameCasters) {
        if (!c->dirty || !g_casterPool.Placed(c->place)) continue;
        g_casterPool.WriteGeometry(c->place, 0, vertices, c->vertices.data());
        g_casterPool.WriteIndices(c->place, g_gridIndices.data(), g_gridIndices.size());
        g_stats.terrainVertexBytes += c->vertices.size() * sizeof(TerrainCasterVertex);
        c->dirty = false;
    }
}

int DrawTerrainCasters(gpu::Pipeline pipeline) {
    if (g_frameCasters.empty()) return 0;
    Geometry g;
    g_casterPool.Bind(g);
    g.vertexBuffers = 1;   // the colours: not read
    if (!g.vertices[0].buffer) return 0;
    gpu::ApplyPipeline(pipeline);
    g.Bind();
    int draws = 0;
    for (const SectorCaster* c : g_frameCasters) {
        if (!g_casterPool.Placed(c->place)) continue;
        gpu::DrawIndexed(c->place.firstIndex, static_cast<std::uint32_t>(g_gridIndices.size()));
        ++draws;
    }
    return draws;
}

void TerrainCastersDone() {
    g_frameCasters.clear();
    std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (auto it = g_sectorCasters.begin(); it != g_sectorCasters.end();) {
        if (now - it->second.seen > kPageLife) it = g_sectorCasters.erase(it);
        else ++it;
    }
}

} // namespace render
