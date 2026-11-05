// The terrain: the game's sector pages as world-space triangles, kept on the GPU and built again only where the
// game says they changed. See docs/notes/renderer.md, "The renderer's source files".
#include "game/model.h"
#include "game/terrain_sector.h"
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
static const Texture* PageTexture(const SectorData* sector, int page) {
    if (!sector->owner || sector->owner->LayerPages() <= 0) return nullptr;
    const std::vector<HeldRef<Texture>>& textures = sector->owner->pageTextures;
    const int index = page % sector->owner->LayerPages();
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
static const SectorVertex* WaterAbove(const SectorData* sector, int index) {
    if (!sector->hasWater || index >= kWaterRecords || index + kWaterRecords >= std::ssize(sector->records))
        return nullptr;
    const SectorVertex& water = sector->records[index + kWaterRecords];
    if (water.waterMaterial == SectorVertex::kNoWaterMaterial || SignBit(sector->records[index].depth)) return nullptr;
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
static float SwampDepth(const SectorData* sector, int index) {
    if (!sector->hasWater || index + kWaterRecords >= std::ssize(sector->records) || !sector->owner) return -1.0f;
    const SectorVertex& water = sector->records[index + kWaterRecords];
    const terrain::Terrain& owner = *sector->owner;
    if (water.waterMaterial == SectorVertex::kNoWaterMaterial || water.waterMaterial >= std::ssize(owner.materials) ||
        WaterKind(owner.materials[water.waterMaterial]) != 1.0f)
        return -1.0f;
    return std::clamp(sector->records[index].depth, -1.0f, 1.0f);
}

// The rise of the ground per landscape unit at a ground record, from its normal (the map's, smooth across the triangles:
// terrain.hlsl's shore_distance divides the depth by it, which a slope taken per triangle broke into steps).
static float GroundSlope(const SectorVertex& ground) {
    const Vector3& n = ground.normal;
    return std::min(std::sqrt(LengthSquared2D(n.x, n.y)) / std::max(n.z, 0.05f), 20.0f);
}

// `occlusion`: the sector's ground occlusion per ground record (GroundOcclusion).
static PageVertex MakeVertex(const SectorData* sector, int index, const TileVertex& entry, const float* occlusion) {
    const SectorVertex& record = sector->records[index];
    PageVertex vertex{sector->vertices[index], entry.u, entry.v, record.normal, -1.0f, 0.0f, 0.0f, 0.0f};
    if (index >= kWaterRecords) {
        vertex.depth = sector->records[index - kWaterRecords].depth;
        vertex.shore = GroundSlope(sector->records[index - kWaterRecords]);
        const terrain::Terrain* owner = sector->owner;
        if (owner && record.waterMaterial < std::ssize(owner->materials))
            vertex.extra = WaterKind(owner->materials[record.waterMaterial]);
    } else {
        vertex.loss = 0.0f;
        vertex.depth = SwampDepth(sector, index);
        vertex.extra = occlusion[index];
        if (const SectorVertex* water = WaterAbove(sector, index)) {
            const terrain::Terrain* owner = sector->owner;
            if (owner && water->waterMaterial < std::ssize(owner->materials)) {
                const terrain::TerrainMaterial& material = owner->materials[water->waterMaterial];
                vertex.loss = kWaterFalloff * material.absorption * record.depth * record.depth;
                vertex.shore = WaterKind(material) != 1.0f ? 1.0f : 0.0f;
            }
        }
    }
    return vertex;
}

static std::uint32_t Byte(float v) {
    float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    return static_cast<std::uint32_t>(c * 255.0f + 0.5f);
}

// The colour word a page vertex of record `index` is drawn with (lit on the GPU): the material
// colour the sun's light starts from (on the ground the water's above it, else the record's own), alpha the
// material byte (the water's opacity).
static std::uint32_t ColourWord(const SectorData* sector, int index) {
    const SectorVertex& record = sector->records[index];
    Vector3 colour = record.materialColour;
    if (const SectorVertex* water = WaterAbove(sector, index)) {
        colour = Max(water->materialColour, colour);
    }
    return static_cast<std::uint32_t>(record.materialByte) << 24 | Byte(colour.x) << 16 | Byte(colour.y) << 8 |
           Byte(colour.z);
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

static PageSource SourceOf(const SectorData* sector, int page) {
    PageSource src;
    src.records = sector->records.data();
    src.recordCount = lib::Count(sector->records);
    src.positions = sector->vertices.data();
    src.positionCount = lib::Count(sector->vertices);
    for (int q = 0; q < 4; ++q) {
        if (page < std::ssize(sector->tiles[q])) {
            src.lists[q] = sector->tiles[q][page].data();
            src.listSizes[q] = lib::Count(sector->tiles[q][page]);
        }
    }
    src.animated = sector->animated.data();
    src.animatedCount = lib::Count(sector->animated);
    auto it = g_changes.find(sector);
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
struct SectorCaster : KeptRanges {
    const void* positions = nullptr;
    int sx = -1, sy = -1;
    std::uint32_t rebuilt = 0;
    std::vector<TerrainCasterVertex> vertices;
    ShadowCasterSphere sphere{};
    bool cast = false;
};
static std::unordered_map<const SectorData*, SectorCaster> g_sectorCasters;
static std::vector<SectorCaster*> g_frameCasters;
static std::vector<std::uint32_t> g_gridIndices;
static GpuPool g_casterPool(sizeof(TerrainCasterVertex), 1u << 15, 3u << 16, "ei-terrain-casters");

static void BuildCaster(const SectorData* sector, SectorCaster& caster) {
    const Vector3* positions = sector->vertices.data();
    caster.vertices.resize(static_cast<std::size_t>(kWaterRecords));
    Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    using terrain::kLastVertex, terrain::kSectorRow;
    auto at = [&](int x, int y) {
        return positions[std::clamp(y, 0, kLastVertex) * kSectorRow + std::clamp(x, 0, kLastVertex)];
    };
    for (int y = 0; y < kSectorRow; ++y)
        for (int x = 0; x < kSectorRow; ++x) {
            Vector3 position = at(x, y);
            Vector3 normal = Cross(at(x + 1, y) - at(x - 1, y), at(x, y + 1) - at(x, y - 1));
            float length = Length(normal);
            if (length > 1e-12f) {
                float k = (normal.z < 0.0f ? -1.0f : 1.0f) / length;   // turned up
                normal *= k;
            } else {
                normal = Vector3{0.0f, 0.0f, 1.0f};
            }
            caster.vertices[static_cast<std::size_t>(y * kSectorRow + x)] = TerrainCasterVertex{position, normal};
            lo = Min(lo, position);
            hi = Max(hi, position);
        }
    const Vector3 centre = (lo + hi) * 0.5f;
    caster.sphere = ShadowCasterSphere{centre.x, centre.y, centre.z, Length((hi - lo) * 0.5f)};
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
static const SectorPosition* NeighbourVertices(const SectorData* sector, int k) {
    const terrain::Terrain* owner = sector->owner;
    if (!owner) return nullptr;
    const int nx = sector->sx + kNeighbours[k][0], ny = sector->sy + kNeighbours[k][1];
    if (nx < 0 || ny < 0 || nx >= owner->sectorsX || ny >= owner->sectorsY) return nullptr;
    const SectorData* neighbour = owner->CachedSector(nx, ny);
    if (!neighbour || !lib::Has(neighbour->built, terrain::SectorStage::Loaded) ||
        std::ssize(neighbour->vertices) < kWaterRecords)
        return nullptr;
    return neighbour->vertices.data();
}

static std::uint8_t HeldNeighbours(const SectorData* sector) {
    std::uint8_t held = 0;
    for (int k = 0; k < 8; ++k)
        if (NeighbourVertices(sector, k)) held |= static_cast<std::uint8_t>(1u << k);
    return held;
}

static void WorkOutOcclusion(const SectorData* sector, SectorOcclusion& entry) {
    using terrain::kLastVertex, terrain::kSectorCells, terrain::kSectorRow;
    constexpr int kReach = kOcclusionReach, kRow = kSectorRow + 2 * kReach;
    entry.version = ++g_occlusionVersion;
    if (std::ssize(sector->vertices) < kWaterRecords) {
        entry.occlusion.assign(static_cast<std::size_t>(kWaterRecords), 1.0f);
        return;
    }
    // The heights from kReach cells before the sector's first vertex to kReach after its last.
    std::vector<float> heights(static_cast<std::size_t>(kRow) * kRow, 0.0f);
    const SectorPosition* own = sector->vertices.data();
    const SectorPosition* around[8];
    for (int k = 0; k < 8; ++k) around[k] = (entry.neighbours >> k) & 1 ? NeighbourVertices(sector, k) : nullptr;
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
    entry.occlusion.resize(static_cast<std::size_t>(kWaterRecords));
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
            entry.occlusion[static_cast<std::size_t>(y * kSectorRow + x)] =
                1.0f - kGroundOcclusion * std::min(1.0f, hidden / 8.0f);
        }
}

// The sector's ground occlusion, worked out when it is new for the sector or a neighbour it continued is held now.
static const SectorOcclusion& GroundOcclusion(const SectorData* sector) {
    std::uint32_t rebuilt = 0;
    if (auto it = g_changes.find(sector); it != g_changes.end()) rebuilt = it->second.rebuilt;
    SectorOcclusion& entry = g_occlusion[sector];
    entry.seen = FrameNumber();
    const bool same = entry.positions == sector->vertices.data() && entry.sx == sector->sx && entry.sy == sector->sy &&
                      entry.rebuilt == rebuilt && !entry.occlusion.empty();
    const std::uint8_t held = HeldNeighbours(sector);
    if (same && (held & ~entry.neighbours) == 0) return entry;
    entry.positions = sector->vertices.data();
    entry.sx = sector->sx;
    entry.sy = sector->sy;
    entry.rebuilt = rebuilt;
    entry.neighbours = same ? static_cast<std::uint8_t>(entry.neighbours | held) : held;
    WorkOutOcclusion(sector, entry);
    return entry;
}

// A sector page as built and as placed in the pool.
struct CachedPage : KeptRanges {
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
};

// The quarters' vertices in runs with their boxes, after they were built or moved: a run is a few tiles in a row
// (8 tiles), small enough that its box's top stays close to its ground.
constexpr std::uint32_t kGroupVertices = 72;
static void MakeGroups(CachedPage& cached) {
    cached.groups.clear();
    for (QuarterRange& quarter : cached.quarters) {
        quarter.firstGroup = static_cast<std::uint32_t>(cached.groups.size());
        quarter.whole = TerrainGroup{Box{}, quarter.firstVertex, quarter.vertexCount};
        const std::uint32_t end = quarter.firstVertex + quarter.vertexCount;
        for (std::uint32_t first = quarter.firstVertex; first < end; first += kGroupVertices) {
            TerrainGroup group{Box{}, first, (std::min)(kGroupVertices, end - first)};
            for (std::uint32_t i = first; i < first + group.count; ++i) {
                const Vector3& at = cached.vertices[i].position;
                group.box.Extend(at);
            }
            cached.groups.push_back(group);
            quarter.whole.box.low = Min(quarter.whole.box.low, group.box.low);
            quarter.whole.box.high = Max(quarter.whole.box.high, group.box.high);
        }
        quarter.groupCount = static_cast<std::uint32_t>(cached.groups.size()) - quarter.firstGroup;
    }
}

// The tiles of `page`, every quarter's (culled or not), as DrawSectorPage builds them: positions, texture coordinates, the record of each
// vertex and the triangles; `occlusion` is the sector's GroundOcclusion.
static void BuildPage(const SectorData* sector, int page, CachedPage& out, const float* occlusion) {
    out.vertices.clear();
    out.records.clear();
    out.entries.clear();
    out.indices.clear();
    out.animatedTiles = false;
    // The sector's animated tiles (their 9 entries in the tile lists), for animatedTiles.
    std::vector<const TileVertex*> animated;
    for (int i = 0; i < std::ssize(sector->animated); ++i) animated.push_back(sector->animated[i].vertices);
    std::sort(animated.begin(), animated.end());
    for (int quarter = 0; quarter < 4; ++quarter) {
        QuarterRange& range = out.quarters[quarter];
        range.firstVertex = static_cast<std::uint32_t>(out.vertices.size());
        range.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        const auto& lists = sector->tiles[quarter];
        if (page < std::ssize(lists) && !lists[page].empty()) {
            const std::vector<terrain::TileVertex>& list = lists[page];
            int tiles = lib::Count(list) / 9;
            for (int t = 0; t < tiles; ++t) {
                const TileVertex* tile = list.data() + t * 9;
                if (std::binary_search(animated.begin(), animated.end(), tile)) out.animatedTiles = true;
                auto base = static_cast<std::uint32_t>(out.vertices.size());
                bool ok = true;
                for (int k = 0; k < kTileVertices; ++k) {
                    const TileVertex& entry = tile[k];
                    std::ptrdiff_t index = entry.vertex - sector->records.data();
                    if (index < 0 || index >= std::ssize(sector->records) || index >= std::ssize(sector->vertices)) {
                        ok = false;
                        break;
                    }
                    out.vertices.push_back(MakeVertex(sector, static_cast<int>(index), entry, occlusion));
                    out.records.push_back(static_cast<std::uint16_t>(index));
                    out.entries.push_back(&entry);
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

// Reads the colours of vertices [from, to) of `cached` from the sector's records; the changed ones are
// marked for upload. True when any changed.
static bool ReadColours(const SectorData* sector, CachedPage& cached, std::uint32_t from, std::uint32_t to) {
    bool changed = false;
    for (std::uint32_t i = from; i < to; ++i) {
        // The colour word (the material) is the one the page was built with; the game writes the
        // specular word (the edge fade; on the water the wave crests).
        const SectorVertex& record = sector->records[cached.records[i]];
        std::uint32_t* kept = &cached.colours[2 * static_cast<std::size_t>(i)];
        if (kept[1] == record.specular) continue;
        kept[1] = record.specular;
        cached.MarkColour(i);
        changed = true;
    }
    return changed;
}

// Reads every vertex's position, texture coordinates and occlusion of `cached` again (the game moved some, or the
// occlusion was worked out again: the tiles are the same); the changed ones are marked for upload. True when any
// changed.
static bool ReadVertices(const SectorData* sector, CachedPage& cached, const float* occlusion) {
    bool changed = false;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(cached.vertices.size()); ++i) {
        PageVertex& kept = cached.vertices[i];
        PageVertex now = MakeVertex(sector, cached.records[i], *cached.entries[i], occlusion);
        if (std::memcmp(&kept, &now, sizeof(PageVertex)) == 0) continue;
        kept = now;
        cached.MarkVertex(i);
        changed = true;
    }
    if (changed) MakeGroups(cached);
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

static bool g_cacheOn = true;   // SetTerrainCache
static std::uint64_t g_checks = 0, g_stale = 0;
#if defined(EI_RENDER_VALIDATION)
// Every this many frames each kept page is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
#endif

static GpuPool g_pool(sizeof(PageVertex), 1u << 17, 3u << 17, "ei-terrain");

// --- GPU side ------------------------------------------------------------------------------------------

// The pipeline drawing terrain as the old device does with `state`: culling, depth and blending from the
// state the old path draws the page with. The ground pass runs under the terrain's state
// block with what the sky left (CULLMODE 2, ZWRITEENABLE 1; the world draw), the
// water pass under the scene's; both with ALPHABLENDENABLE on (SRCALPHA /
// INVSRCALPHA in the harness runs; the verify build logs each new pipeline's state).
static gpu::PipelineDesc TerrainDesc(const PassState& state, int samples) {
    namespace program = gpu_shaders::terrain;
    gpu::PipelineDesc desc;
    desc.program = program::kIndex;
    desc.bufferCount = 2;
    desc.buffers[0].stride = sizeof(PageVertex);
    desc.buffers[1].stride = sizeof(ColourPair);
    desc.attributeCount = 8;
    desc.attributes[0] = {program::kAttrPos, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, position)};
    desc.attributes[1] = {program::kAttrCol, 1, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, colour)};
    desc.attributes[2] = {program::kAttrSpec, 1, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, specular)};
    desc.attributes[3] = {program::kAttrUv, 0, gpu::VertexFormat::Float2, offsetof(PageVertex, u)};
    desc.attributes[4] = {program::kAttrNormal, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, normal)};
    desc.attributes[5] = {program::kAttrLoss, 0, gpu::VertexFormat::Float, offsetof(PageVertex, loss)};
    // depth and shore
    desc.attributes[6] = {program::kAttrDepth, 0, gpu::VertexFormat::Float2, offsetof(PageVertex, depth)};
    desc.attributes[7] = {program::kAttrExtra, 0, gpu::VertexFormat::Float, offsetof(PageVertex, extra)};
    ApplyPassState(desc, state);
    desc.samples = samples;
    AddDepthCopy(desc);
    return desc;
}

// TerrainDesc's pipeline (gpu.h keeps them by description); the pipelines that could not be made, logged.
static int g_pipelineFailures = 0;
// `water`: for the verify build's log.
static gpu::Pipeline Pipeline(const PassState& state, [[maybe_unused]] bool water) {
    const int samples = PassSampleCount();
#if defined(EI_RENDER_VALIDATION)
    const int known = gpu::PipelineCount();
#endif
    gpu::Pipeline pipeline = MakePassPipeline(TerrainDesc(state, samples), "ei-terrain");
    if (!pipeline)
        LogFirst(g_pipelineFailures, 10,
                 "render: a terrain pipeline could not be created (state key %08X; see the gpu messages above)",
                 state.PipelineKey());
#if defined(EI_RENDER_VALIDATION)
    if (gpu::PipelineCount() != known)
        ei::Log("render: terrain pipeline (first for a %s page): blend %d (%u, %u), depth test %d write %d func %u, "
                "cull %u",
                water ? "water" : "ground", state.blend ? 1 : 0, static_cast<unsigned>(state.sourceBlend),
                static_cast<unsigned>(state.destBlend), state.depthTest ? 1 : 0, state.depthWrite ? 1 : 0,
                static_cast<unsigned>(state.depthFunc), static_cast<unsigned>(state.cull));
#endif
    return pipeline;
}

// --- The level map (render::TerrainLevels, the harness's) ---------------------------------------------

static TerrainLevelMode g_levelMode = TerrainLevelMode::Off;

// A texture of the page texture's size and levels, each level one grey (level k: 32 k).
static std::vector<gpu::Texture> g_levelImages;

static gpu::Texture LevelView(const gpu::Texture& page) {
    if (!page) return gpu::Texture{};
    const int width = page.width, height = page.height, levels = page.levels;
    for (const gpu::Texture& made : g_levelImages)
        if (made.width == width && made.height == height && made.levels == levels) return made;
    if (width <= 0 || height <= 0 || levels <= 0) return gpu::Texture{};
    gpu::Texture image =
        gpu::MakeTexture({width, height, levels, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-terrain-levels"});
    if (!image) return image;
    std::vector<std::uint8_t> level;
    for (int k = 0; k < levels; ++k) {
        int w = (width >> k) > 0 ? width >> k : 1, h = (height >> k) > 0 ? height >> k : 1;
        auto grey = static_cast<std::uint8_t>(k * 32 < 255 ? k * 32 : 255);
        level.assign(static_cast<std::size_t>(w) * h * 4, grey);
        for (std::size_t i = 3; i < level.size(); i += 4) level[i] = 255;
        gpu::Write(image, k, {0, 0, w, h}, level.data(), static_cast<std::size_t>(w) * 4);
    }
    g_levelImages.push_back(image);
    return image;
}

// The index runs of `batch`'s pieces placed in the pool, pieces that follow each other in it as one run:
// `draw(first, count)` per run.
template <class Draw>
static void ForPieceRuns(const Frame& f, const TerrainBatch& batch, Draw draw) {
    std::uint32_t first = 0, count = 0;
    for (int k = batch.firstPiece; k < batch.firstPiece + batch.pieceCount; ++k) {
        const TerrainPiece& piece = f.terrainPieces[static_cast<std::size_t>(k)];
        const CachedPage& cached = *piece.page;
        const QuarterRange& quarter = cached.quarters[piece.quarter];
        if (!g_pool.Placed(cached.place) || quarter.indexCount == 0) continue;
        std::uint32_t start = cached.place.firstIndex + quarter.firstIndex;
        if (count > 0 && first + count == start) {
            count += quarter.indexCount;
        } else {
            if (count > 0) draw(first, count);
            first = start;
            count = quarter.indexCount;
        }
    }
    if (count > 0) draw(first, count);
}

static CachedPage g_scratch;
static bool BuildAgain(const SectorData* sector, int page, CachedPage& cached, const float* occlusion) {
    BuildPage(sector, page, g_scratch, occlusion);
    if (SameGeometry(g_scratch, cached)) return false;
    cached.vertices.swap(g_scratch.vertices);
    cached.records.swap(g_scratch.records);
    cached.entries.swap(g_scratch.entries);
    cached.indices.swap(g_scratch.indices);
    cached.readsWater = g_scratch.readsWater;
    cached.animatedTiles = g_scratch.animatedTiles;
    for (int q = 0; q < 4; ++q) cached.quarters[q] = g_scratch.quarters[q];
    cached.groups.swap(g_scratch.groups);
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
    // The level maps are written here, before the scene's pass (gpu.h refuses writes inside one).
    if (g_levelMode != TerrainLevelMode::Off)
        for (const TerrainBatch& batch : f.terrainBatches)
            if (!batch.water) LevelView(TextureView(batch.texture));
    if (g_pending.empty()) return;
    Timer timer(FrameStats::Time::TerrainMs);
    auto framePages = [&](auto add) {
        for (const TerrainPiece& piece : f.terrainPieces) add(*piece.page);
    };
    UploadKept(g_pool, g_pending, framePages, FrameStats::Count::TerrainVertexBytes, FrameStats::Count::TerrainIndexBytes);
}

void DrawTerrainBatch(const Frame& f, const TerrainBatch& batch) {
    namespace program = gpu_shaders::terrain;
    if (batch.pieceCount <= 0) return;
    Geometry geometry;
    g_pool.Bind(geometry);
    if (!geometry.vertices[0].buffer) return;
    gpu::Pipeline pipeline = Pipeline(batch.state, batch.water);
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    geometry.Bind();
    gpu::TextureBinding texture{TextureView(batch.texture), AnisotropicSampler(batch.state.stages[0], kMostAnisotropy)};
    // terrain.hlsl's mode: the ground, the water or the level map.
    constexpr int kGround = 0, kWater = 1, kLevels = 2;
    int mode = batch.water ? kWater : kGround;
    if (g_levelMode != TerrainLevelMode::Off && !batch.water) {
        gpu::Texture levels = LevelView(texture.texture);
        if (levels) {
            texture.texture = levels;
            if (g_levelMode == TerrainLevelMode::PointMips) texture.sampler = LinearSampler();
            mode = kLevels;
        }
    }
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerTex, {&texture, 1});
    BindShadowMap(f, program::kSamplerShadowMap);
    BindPointShadows(f, program::kSamplerPointShadowMap);
    const gpu::TextureBinding ripples = RippleMap();
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerRipples, {&ripples, 1});
    BindCloudMap(program::kSamplerCloudMap);
    BindOcclusion(program::kSamplerOcclusionMap);
    BindRoofMap(program::kSamplerRoofMap);
    CountBindings(texture.texture);
    // The ground and the water receive the objects' shadows and the clouds'.
    const bool receives = mode != kLevels && !f.shadow.lights.empty();
    const ShadowParams& shadow = ShadowUniforms(f, receives, receives);
    TerrainVsParams vs{};
    vs.matrix = f.matrices[static_cast<std::size_t>(batch.matrix)];
    vs.light = receives ? f.shadow.lights[0] : vs.matrix;
    vs.shift[0] = f.pixelShift[0];
    vs.shift[1] = f.pixelShift[1];
    vs.normalOffset = ShadowNormalOffset(f, receives);
    vs.fogStart = f.distanceFog.start;
    vs.fogDensity = f.distanceFog.density;
    vs.fogDepthPerW = f.distanceFog.depthPerW;
    vs.mapWidth = f.mapEdge.width;
    vs.mapHeight = f.mapEdge.height;
    vs.edgeBand = f.mapEdge.band;
    TerrainFsParams fs{};
    UnpackColour(f.fogColour, fs.fog);
    fs.mode = mode;
    fs.fogOn = f.fogEnabled ? 1 : 0;
    RippleOffsets(fs.ripples, fs.slowRipples);
    SurfaceUniforms(&fs);
    OcclusionUniforms(batch.occluded && mode != kLevels, fs);
    BindWaterReflections(vs.matrix, &fs);
    const SceneLightParams light = SceneLightUniforms(f);
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
    gpu::PipelineDesc desc;
    desc.program = program::kIndex;
    desc.bufferCount = 1;
    desc.buffers[0].stride = sizeof(PageVertex);
    desc.attributeCount = 2;
    desc.attributes[0] = {program::kAttrPos, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, position)};
    desc.attributes[1] = {program::kAttrNormal, 0, gpu::VertexFormat::Float3, offsetof(PageVertex, normal)};
    desc.colour = gpu::Format::None;
    desc.depth = kDepthFormat;
    desc.depthCompare = gpu::Compare::LessEqual;
    desc.depthWrite = true;
    return desc;
}

// shadow_terrain.hlsl with the batch's clip matrix for the light and no sun (no face left out).
int DrawTerrainDepth(const Frame& f, gpu::Pipeline pipeline) {
    namespace program = gpu_shaders::shadow_terrain;
    Geometry geometry;
    g_pool.Bind(geometry);
    geometry.vertexBuffers = 1;   // the colours: not read
    if (!geometry.vertices[0].buffer) return 0;
    gpu::ApplyPipeline(pipeline);
    geometry.Bind();
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

GroundOcclusionCount GroundOcclusionCounts() {
    return {static_cast<int>(g_occlusion.size()), static_cast<int>(g_occlusionVersion)};
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
    const std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    std::erase_if(g_pages, [now](const auto& entry) {
        return !entry.second.pending && now - entry.second.lastUsed > kPageLife;
    });
    std::erase_if(g_occlusion, [now](const auto& entry) { return now - entry.second.seen > kPageLife; });
}

void TerrainLevels(TerrainLevelMode mode) {
    if (mode != g_levelMode)
        ei::Log("render: terrain level map %s",
                mode == TerrainLevelMode::Off ? "off" : mode == TerrainLevelMode::Levels ? "on" : "on, through point mips");
    g_levelMode = mode;
}

void TerrainChanged(const terrain::SectorData* sector, TerrainChange change) {
    if (!sector) return;
    SectorChanges& changes = g_changes[sector];
    switch (change) {
    case TerrainChange::Rebuilt:
        ++changes.rebuilt;
        break;
    case TerrainChange::Moved:
        ++changes.moved;
        break;
    default:
        ++changes.waterMoved;
        break;
    }
}

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

void TerrainPage(const SectorData* sector, const SceneRenderer* renderer, int page) {
    if (!sector || !renderer) return;
    Timer timer(FrameStats::Time::TerrainMs);
    ++g_stats.counts[FrameStats::Count::TerrainPages];
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
    const Texture* texture = PageTexture(sector, page);
    bool water = page >= sector->owner->LayerPages();
    bool inView = false;
    for (int q = 0; q < 4 && !inView; ++q) {
        const auto& lists = sector->tiles[q];
        inView = sector->quarterMode[q] != TransformPipeline::ClipResult::Outside && page < std::ssize(lists) &&
                 !lists[page].empty() && std::ssize(lists[page]) >= 9;
    }
    if (!inView) return;

    PageKey key{sector, page};
    auto [found, made] = g_pages.try_emplace(key);
    CachedPage& cached = found->second;
    PageSource source = SourceOf(sector, page);
    const SectorOcclusion& occlusion = GroundOcclusion(sector);
    source.occlusion = occlusion.version;
    const float* occluded = occlusion.occlusion.data();
    bool moved = false, upload = false;
    if (made) {
        BuildPage(sector, page, cached, occluded);
        upload = true;
    } else if (!g_cacheOn) {
        moved = BuildAgain(sector, page, cached, occluded);   // off: all of it uploaded again, as before
        upload = true;
    } else if (!cached.source.SameTiles(source)) {
        moved = upload = BuildAgain(sector, page, cached, occluded);
    } else if ((cached.animatedTiles && cached.source.changes.moved != source.changes.moved) ||
               (cached.readsWater && cached.source.changes.waterMoved != source.changes.waterMoved) ||
               cached.source.occlusion != source.occlusion) {
        moved = ReadVertices(sector, cached, occluded);
#if defined(EI_RENDER_VALIDATION)
    } else if (FrameNumber() % kCheckEvery == 0) {
        ++g_checks;
        if (BuildAgain(sector, page, cached, occluded)) {
            LogFirst(g_stale, 20, "render: a terrain page kept on the GPU was stale: sector (%d, %d) page %d",
                     sector->sx, sector->sy, page);
            moved = upload = true;
        }
#endif
    }
    cached.source = source;
    if (upload) {
        cached.WriteAll();
        cached.colours.resize(2 * cached.vertices.size());
        for (std::size_t i = 0; i < cached.vertices.size(); ++i) {
            cached.colours[2 * i] = ColourWord(sector, cached.records[i]);
            cached.colours[2 * i + 1] = sector->records[cached.records[i]].specular;
        }
    }
    if (!g_pool.Placed(cached.place)) cached.dirty = true;

    bool relit = false;
    std::size_t firstPiece = f.terrainPieces.size();
    for (int q = 0; q < 4; ++q) {
        const QuarterRange& range = cached.quarters[q];
        if (sector->quarterMode[q] == TransformPipeline::ClipResult::Outside || range.indexCount == 0) continue;
        if (!upload && ReadColours(sector, cached, range.firstVertex, range.firstVertex + range.vertexCount))
            relit = true;
        f.terrainPieces.push_back(TerrainPiece{&cached, q});
    }
    cached.lastUsed = FrameNumber();
    if (cached.HasWrites()) AddPending(g_pending, cached);
    auto count = static_cast<int>(f.terrainPieces.size() - firstPiece);
    if (count <= 0) return;
    ++g_stats.terrainReuse[made ? FrameStats::Reuse::New : moved ? FrameStats::Reuse::Moved : relit ? FrameStats::Reuse::Relit : FrameStats::Reuse::Same];
    // The old device's state the page is drawn with; DrawPage changes none of what the pipeline
    // takes before its draws (it turns SPECULARENABLE on around each, which the shader always adds).
    const PassState state = ReadPassState(nullptr);
    if (!f.commands.empty() && f.commands.back().kind == Command::Kind::Terrain) {
        TerrainBatch& last = f.terrainBatches[static_cast<std::size_t>(f.commands.back().index)];
        if (last.texture == texture && last.water == water && last.matrix == matrix &&
            last.state.PipelineKey() == state.PipelineKey() &&
            static_cast<std::size_t>(last.firstPiece) + static_cast<std::size_t>(last.pieceCount) == firstPiece) {
            last.pieceCount += count;
            return;
        }
    }
    ++g_stats.counts[FrameStats::Count::TerrainBatches];
    f.terrainBatches.push_back(TerrainBatch{texture, water, matrix, static_cast<int>(firstPiece), count, state});
    f.commands.push_back(Command{Command::Kind::Terrain, static_cast<int>(f.terrainBatches.size()) - 1});
}

// --- The ground's casters into the shadow map ------------------------------------------------------

void ShadowTerrainSector(const SectorData* sector) {
    if (!sector || !lib::Has(sector->built, terrain::SectorStage::Loaded) || sector->vertices.empty() ||
        std::ssize(sector->vertices) < kWaterRecords)
        return;
    Timer timer(FrameStats::Time::ShadowMs);
    std::uint32_t rebuilt = 0;
    if (auto it = g_changes.find(sector); it != g_changes.end()) rebuilt = it->second.rebuilt;
    SectorCaster& caster = g_sectorCasters[sector];
    if (caster.lastUsed == FrameNumber() && !caster.vertices.empty()) return;
    caster.lastUsed = FrameNumber();
    caster.cast = false;
    if (caster.positions != sector->vertices.data() || caster.sx != sector->sx || caster.sy != sector->sy ||
        caster.rebuilt != rebuilt || caster.vertices.empty()) {
        BuildCaster(sector, caster);
        caster.positions = sector->vertices.data();
        caster.sx = sector->sx;
        caster.sy = sector->sy;
        caster.rebuilt = rebuilt;
        caster.dirty = true;
    }
    if (!ShadowCasterReaches(caster.sphere)) return;
    caster.cast = true;
    g_frameCasters.push_back(&caster);
}

std::size_t TerrainCasterCount() { return g_frameCasters.size(); }

void UploadTerrainCasters() {
    if (g_frameCasters.empty()) return;
    const auto vertices = static_cast<std::uint32_t>(kWaterRecords);
    const auto indices = static_cast<std::uint32_t>(g_gridIndices.size());
    bool placed = true;
    for (SectorCaster* c : g_frameCasters) {
        if (!g_casterPool.Place(*c, vertices, indices)) {
            placed = false;
            break;
        }
    }
    if (!placed) {   // the pool starts again with this frame's sectors
        g_casterPool.Restart(static_cast<std::uint64_t>(vertices) * g_frameCasters.size(),
                             static_cast<std::uint64_t>(indices) * g_frameCasters.size());
        for (SectorCaster* c : g_frameCasters) {
            g_casterPool.Place(*c, vertices, indices);
            c->dirty = true;
        }
    }
    for (SectorCaster* c : g_frameCasters) {
        if (!c->dirty || !g_casterPool.Placed(c->place)) continue;
        g_casterPool.WriteGeometry(c->place, 0, vertices, c->vertices.data());
        g_casterPool.WriteIndices(c->place, g_gridIndices.data(), g_gridIndices.size());
        g_stats.counts[FrameStats::Count::TerrainVertexBytes] += c->vertices.size() * sizeof(TerrainCasterVertex);
        c->dirty = false;
    }
}

int DrawTerrainCasters(gpu::Pipeline pipeline) {
    if (g_frameCasters.empty()) return 0;
    Geometry geometry;
    g_casterPool.Bind(geometry);
    geometry.vertexBuffers = 1;   // the colours: not read
    if (!geometry.vertices[0].buffer) return 0;
    gpu::ApplyPipeline(pipeline);
    geometry.Bind();
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
    const std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    std::erase_if(g_sectorCasters, [now](const auto& entry) { return now - entry.second.lastUsed > kPageLife; });
}

} // namespace render
