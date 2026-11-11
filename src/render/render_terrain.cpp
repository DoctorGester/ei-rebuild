// The terrain: the zone's ground and water as two meshes on the GPU, each sector's tiles in a place of their own, built
// when the game builds the sector and written again only where the game says it changed; a pass draws its layer's mesh
// whole. See docs/notes/renderer.md, "The renderer's source files".
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/models/texture.h"
#include "game/terrain/terrain_sector.h"
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include "lib/containers.h"
#include "lib/float_select.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace render {

using terrain::SectorData;
using terrain::SectorPosition;
using terrain::SectorVertex;
using terrain::TileVertex;

// --- Tile patterns (as the game's tile lists hold them) --------------------------------------------------

// Triangles per tile (3x3 vertices, row by row), relative to the tile's first vertex.
constexpr int kTileVertices = 9;
constexpr int kTileIndices = 24;
constexpr std::uint16_t kTileTriangles[kTileIndices] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4,
                                                        3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};
constexpr int kQuarters = 4;
constexpr int kLayers = 2;   // the ground, the water

constexpr int kWaterRecords = terrain::kSectorVertices;
// Light lost under water per squared depth and absorption (1 / 15): the dimming of the ground under water.
constexpr float kWaterFalloff = 0.06666667f;

// --- A vertex -------------------------------------------------------------------------------------------

// A vertex is four streams: its position (the water's at rest: terrain_water.hlsl moves it with the waves), the rest of
// what is built with it, its colour pair (on the water the specular word's colour bytes are its material, column and
// row in its sector, which the waves read; terrain_water.hlsl puts the crests there) and its texture coordinates with its
// page's layer in the page textures' array and its animation (the GPU works the animated tiles' frames out: `base`,
// `frames`, `seed`, `rate`, terrain_common.hlsli).
// The depth-only passes read the first two.
// `normal` is the record's; `loss` on the ground layer the light lost under the water, on the water layer the vertex's
// wave phase offset (Terrain::wavePhases); the GPU lights both layers. `depth`: on the water layer the depth over the ground (the ground record's: negative where the
// ground is above the water); on the ground layer the same where the record's water is a swamp (terrain_ground.hlsl's muddy
// rim), else -1. `shore`: on the ground layer 1 where it lies under water that is not a swamp (no muddy rim where a swamp
// meets it), else 0; on the water layer the slope of the ground under it (GroundSlope: a swamp's distance from its
// shore line). `extra`: on the ground layer the ground's own occlusion of the ambient light (GroundOcclusion), on the
// water layer the water's kind (WaterKind).
struct VertexRest {
    Vector3 normal;
    float loss = 0.0f;
    float depth = 0.0f;
    float shore = 0.0f;
    float extra = 0.0f;
};
static_assert(sizeof(VertexRest) == 28);
struct VertexUv {
    float u = 0.0f, v = 0.0f, layer = 0.0f;
    float base = 0.0f, frames = 0.0f, seed = 0.0f, rate = 0.0f;   // an animated tile's first frame in its page, frames, seed, rate (1 steps every time); no frames: static
};
// The vertex streams (buffer slots) of the meshes.
constexpr int kPositions = 0, kRest = 1, kColours = 2, kUvs = 3, kStreams = 4;
constexpr std::uint32_t kStrides[kStreams] = {sizeof(Vector3), sizeof(VertexRest), sizeof(ColourPair), sizeof(VertexUv)};

// The ground record `index` lies under the sector's water (a water material above it, the depth not negative):
// its water record, else null.
static const SectorVertex* WaterAbove(const SectorData* sector, int index) {
    if (!sector->hasWater || index >= kWaterRecords || index + kWaterRecords >= std::ssize(sector->records))
        return nullptr;
    const SectorVertex& water = sector->records[index + kWaterRecords];
    if (water.waterMaterial == SectorVertex::kNoWaterMaterial || SignBit(sector->records[index].depth)) return nullptr;
    return &water;
}

// The kind of water a material is (terrain_water.hlsl's Water): 0 water, 1 a swamp, 2 lava. The map's materials tell them
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
// the shaders' shore_distance divides the depth by it, which a slope taken per triangle broke into steps).
static float GroundSlope(const SectorVertex& ground) {
    const Vector3& n = ground.normal;
    return std::min(std::sqrt(LengthSquared2D(n.x, n.y)) / std::max(n.z, 0.05f), 20.0f);
}

// `occlusion`: the sector's ground occlusion per ground record (GroundOcclusion).
static VertexRest MakeRest(const SectorData* sector, int index, const float* occlusion) {
    const SectorVertex& record = sector->records[index];
    VertexRest vertex{record.normal, 0.0f, 0.0f, 0.0f, 0.0f};
    if (index >= kWaterRecords) {
        vertex.depth = sector->records[index - kWaterRecords].depth;
        vertex.shore = GroundSlope(sector->records[index - kWaterRecords]);
        const terrain::Terrain* owner = sector->owner;
        if (owner && index - kWaterRecords < std::ssize(owner->wavePhases))
            vertex.loss = owner->wavePhases[static_cast<std::size_t>(index - kWaterRecords)];
        if (owner && record.waterMaterial < std::ssize(owner->materials))
            vertex.extra = WaterKind(owner->materials[record.waterMaterial]);
    } else {
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

// The colour pair a vertex of record `index` is drawn with (lit on the GPU): the material colour the sun's light
// starts from (on the ground the water's above it, else the record's own), alpha the material byte (the water's
// opacity); the specular word as the game keeps it (the edge fade), on the water with its material (255: none or past
// the waves' table) and its column and row in the sector in the colour bytes (terrain_water.hlsl's `waved`).
static ColourPair Colours(const SectorData* sector, int index) {
    const SectorVertex& record = sector->records[index];
    Vector3 colour = record.materialColour;
    if (const SectorVertex* water = WaterAbove(sector, index)) colour = Max(water->materialColour, colour);
    std::uint32_t specular = record.specular;
    if (index >= kWaterRecords) {
        const int vertex = index - kWaterRecords;
        const std::uint32_t material = record.waterMaterial < terrain::Terrain::kWaveMaterials ? record.waterMaterial : 0xFF;
        specular = (specular & 0xFF000000u) | material << 16 | static_cast<std::uint32_t>(vertex % terrain::kSectorRow) << 8 |
                   static_cast<std::uint32_t>(vertex / terrain::kSectorRow);
    }
    return ColourPair{static_cast<std::uint32_t>(record.materialByte) << 24 | Byte(colour.x) << 16 |
                          Byte(colour.y) << 8 | Byte(colour.z),
                      specular};
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
constexpr int kNeighbours[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
static std::uint32_t g_occlusionMade = 0;   // how often one was worked out

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

// The occlusion per ground record into `out`, reading the neighbours of `neighbours` (bit k: kNeighbours[k]) that are
// held.
static void WorkOutOcclusion(const SectorData* sector, std::uint8_t neighbours, std::vector<float>& out) {
    using terrain::kLastVertex, terrain::kSectorCells, terrain::kSectorRow;
    constexpr int kReach = kOcclusionReach, kRow = kSectorRow + 2 * kReach;
    ++g_occlusionMade;
    if (std::ssize(sector->vertices) < kWaterRecords) {
        out.assign(static_cast<std::size_t>(kWaterRecords), 1.0f);
        return;
    }
    // The heights from kReach cells before the sector's first vertex to kReach after its last.
    std::vector<float> heights(static_cast<std::size_t>(kRow) * kRow, 0.0f);
    const SectorPosition* own = sector->vertices.data();
    const SectorPosition* around[8];
    for (int k = 0; k < 8; ++k) around[k] = (neighbours >> k) & 1 ? NeighbourVertices(sector, k) : nullptr;
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
    out.resize(static_cast<std::size_t>(kWaterRecords));
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
            out[static_cast<std::size_t>(y * kSectorRow + x)] = 1.0f - kGroundOcclusion * std::min(1.0f, hidden / 8.0f);
        }
}

// --- The zone -------------------------------------------------------------------------------------------

// One page's tiles in one quarter of a sector's layer: its vertices within the layer's, in runs of kGroupVertices (8
// tiles in a row, small enough that a run's box stays close to its ground) with their boxes, and all of them as one.
constexpr std::uint32_t kGroupVertices = 72;
struct Run {
    std::uint32_t first = 0, count = 0;
    std::uint32_t firstGroup = 0, groupCount = 0;
    TerrainGroup whole{};
};

// A sector's ground or water layer in the zone's mesh of it: page by page (with a texture), each page's quarters, each
// quarter's tiles in the game's list order.
struct SlotLayer {
    std::uint32_t first = 0;                 // its first vertex in the mesh
    std::vector<std::uint16_t> records;      // per vertex: the record it reads its position, normal and colours from
    std::vector<VertexUv> uvs;               // per vertex
    std::vector<Run> runs;                   // [page * kQuarters + quarter]
    std::vector<TerrainGroup> groups;
    std::uint8_t quarters = 0;               // bit q: quarter q has tiles
    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
};

// What UploadTerrain writes of a slot: all of it (its meshes' ranges and its caster), the water's positions (a level).
constexpr std::uint8_t kWriteAll = 1, kWriteWater = 2;

// A place of the sector grid (Terrain::GridIndex: sx * sectorsY + sy) in the zone's meshes.
struct Slot {
    const SectorData* held = nullptr;     // the grid's sector it was built from
    const SectorData* sector = nullptr;   // the same when built (it has tile lists), else null
    bool stale = true;                    // to build again (TerrainChanged: Rebuilt, Dropped)
    SlotLayer layers[kLayers];
    // Its ground as a caster: 33 x 33 vertices with normals from their neighbours, until written; the sphere around
    // them; whether it has them.
    std::vector<TerrainCasterVertex> caster;
    ShadowCasterSphere sphere{};
    bool casts = false;
    std::vector<float> occlusion;   // per ground record
    std::uint8_t neighbours = 0;    // the neighbours whose heights it read (bit k: kNeighbours[k])
    std::uint8_t writes = 0;        // Write
    bool moved = false;             // WaterMoved: the water level changed since it was read
};

// A layer's mesh: the slots' vertices one after the other (four streams), the indices page by page, each page's in the
// order the game draws the sectors (by rows of the grid), each sector's by quarter: the order the pages were drawn in.
struct Mesh {
    gpu::Buffer streams[kStreams];
    gpu::Buffer indices;
    std::uint32_t vertexCount = 0, indexCount = 0;
};

struct Zone {
    const terrain::Terrain* terrain = nullptr;
    int sectorsX = 0, sectorsY = 0;
    int layerPages = 0;   // Terrain::LayerPages
    int pages = 0;        // of them, those with a texture
    std::vector<Slot> slots;
    std::unordered_map<const SectorData*, int> slotOf;   // the held sectors' slots
    std::vector<int> pending;    // slots with writes
    std::vector<int> moved;      // slots the game moved something in
    bool laid = false;           // the slots have their places in the meshes
    bool remake = true;          // the meshes' buffers to make again (laid out again)
    Mesh meshes[kLayers];
    gpu::Buffer casterVertices, casterIndices;
    std::uint32_t casterIndexCount = 0;
    // The page textures: as the passes read them, their images' serials, and the array made from them.
    std::vector<gpu::Texture> pageViews;
    std::vector<std::uint32_t> pageSerials;
    bool arrayStale = true;
    gpu::Texture array;
};
static Zone g_zone;
static bool g_cacheOn = true;   // SetTerrainCache
static std::uint32_t g_built = 0, g_movedSlots = 0;   // this frame's (FrameStats::terrainReuse)

// The casters of this frame (bit k: slot k; ShadowTerrainSector) and their count.
constexpr int kMaskedSectors = 4096;   // shadow_terrain.hlsl's kMaskWords * 32
static std::vector<std::uint32_t> g_casting;
static std::size_t g_casters = 0;
static std::vector<std::uint32_t> g_gridIndices;   // a sector's 32 x 32 cells as triangles

static int SlotIndex(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= g_zone.sectorsX || sy >= g_zone.sectorsY) return -1;
    return sx * g_zone.sectorsY + sy;
}

// The meshes' buffers released (their counts kept: Layout's).
static void ReleaseMeshes() {
    for (Mesh& mesh : g_zone.meshes) {
        for (gpu::Buffer& b : mesh.streams) gpu::Release(b);
        gpu::Release(mesh.indices);
    }
    gpu::Release(g_zone.casterVertices);
    gpu::Release(g_zone.casterIndices);
    g_zone.casterIndexCount = 0;
}

// A new landscape, or one of another size: every slot empty and stale.
static void Reset(const terrain::Terrain& t, int layerPages, int pages) {
    ReleaseMeshes();
    g_zone.terrain = &t;
    g_zone.sectorsX = t.sectorsX;
    g_zone.sectorsY = t.sectorsY;
    g_zone.layerPages = layerPages;
    g_zone.pages = pages;
    g_zone.slots.assign(static_cast<std::size_t>(t.sectorsX) * static_cast<std::size_t>(t.sectorsY), Slot{});
    g_zone.slotOf.clear();
    g_zone.pending.clear();
    g_zone.moved.clear();
    g_zone.laid = false;
    g_zone.remake = true;
    g_casting.assign((g_zone.slots.size() + 31) / 32, 0);
    g_casters = 0;
}

static void AddPending(int k) {
    Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    if (slot.writes == 0) g_zone.pending.push_back(k);
}

// The runs' groups and boxes of a layer, from its positions now.
static void LayerBoxes(const SectorData* sector, SlotLayer& layer) {
    for (Run& run : layer.runs) {
        run.whole.box = Box{};
        for (std::uint32_t g = run.firstGroup; g < run.firstGroup + run.groupCount; ++g) {
            TerrainGroup& group = layer.groups[g];
            group.box = Box{};
            for (std::uint32_t i = group.first; i < group.first + group.count; ++i)
                group.box.Extend(sector->vertices[layer.records[i]]);
            run.whole.box.low = Min(run.whole.box.low, group.box.low);
            run.whole.box.high = Max(run.whole.box.high, group.box.high);
        }
    }
}

// Layer `layer` of `sector`: the tiles of its pages with a texture, quarter by quarter, as the game's lists hold them (a
// tile with a vertex past the records left out).
static void BuildLayer(const SectorData* sector, int layer, SlotLayer& out) {
    out.records.clear();
    out.uvs.clear();
    out.runs.assign(static_cast<std::size_t>(g_zone.pages * kQuarters), Run{});
    out.groups.clear();
    out.quarters = 0;
    if (!sector) return;
    // The sector's animated tiles (their 9 entries in the tile lists), by their entries.
    static std::vector<const terrain::AnimatedTile*> animated;
    animated.clear();
    for (const terrain::AnimatedTile& tile : sector->animated) animated.push_back(&tile);
    std::sort(animated.begin(), animated.end(),
              [](const terrain::AnimatedTile* a, const terrain::AnimatedTile* b) { return a->vertices < b->vertices; });
    for (int pageInLayer = 0; pageInLayer < g_zone.pages; ++pageInLayer) {
        const int page = layer * g_zone.layerPages + pageInLayer;
        for (int quarter = 0; quarter < kQuarters; ++quarter) {
            Run& run = out.runs[static_cast<std::size_t>(pageInLayer * kQuarters + quarter)];
            run.first = out.Count();
            const auto& lists = sector->tiles[quarter];
            if (page < std::ssize(lists)) {
                const std::vector<TileVertex>& list = lists[static_cast<std::size_t>(page)];
                const int tiles = lib::Count(list) / kTileVertices;
                for (int t = 0; t < tiles; ++t) {
                    const TileVertex* tile = list.data() + t * kTileVertices;
                    bool ok = true;
                    for (int k = 0; k < kTileVertices && ok; ++k) {
                        const std::ptrdiff_t index = tile[k].vertex - sector->records.data();
                        ok = index >= 0 && index < std::ssize(sector->records) && index < std::ssize(sector->vertices);
                    }
                    if (!ok) continue;
                    const auto found = std::lower_bound(animated.begin(), animated.end(), tile,
                                                        [](const terrain::AnimatedTile* a, const TileVertex* b) { return a->vertices < b; });
                    const terrain::AnimatedTile* steps = found != animated.end() && (*found)->vertices == tile ? *found : nullptr;
                    for (int k = 0; k < kTileVertices; ++k) {
                        VertexUv uv{tile[k].u, tile[k].v, static_cast<float>(pageInLayer)};
                        if (steps) {
                            // Its own within the page's first tile and what the shader needs to find its frame.
                            const terrain::TileUv& local = sector->owner->uvLocal[static_cast<std::size_t>(steps->rotation)];
                            uv = VertexUv{local.u[k], local.v[k], uv.layer, static_cast<float>(steps->frameBase),
                                          static_cast<float>(steps->frames), static_cast<float>(steps->seed),
                                          static_cast<float>(steps->speed) / static_cast<float>(terrain::kAlwaysSteps)};
                        }
                        out.records.push_back(static_cast<std::uint16_t>(tile[k].vertex - sector->records.data()));
                        out.uvs.push_back(uv);
                    }
                }
            }
            run.count = out.Count() - run.first;
            if (run.count != 0) out.quarters |= static_cast<std::uint8_t>(1u << quarter);
            run.firstGroup = static_cast<std::uint32_t>(out.groups.size());
            run.whole = TerrainGroup{Box{}, run.first, run.count};
            for (std::uint32_t first = run.first; first < run.first + run.count; first += kGroupVertices)
                out.groups.push_back(TerrainGroup{Box{}, first, std::min(kGroupVertices, run.first + run.count - first)});
            run.groupCount = static_cast<std::uint32_t>(out.groups.size()) - run.firstGroup;
        }
    }
    LayerBoxes(sector, out);
}

// The ground layer's 33 x 33 vertices as a caster, normals from their neighbours (turned up), and its sphere.
static void BuildCaster(const SectorData* sector, Slot& slot) {
    slot.casts = sector && lib::Has(sector->built, terrain::SectorStage::Loaded) &&
                 std::ssize(sector->vertices) >= kWaterRecords;
    slot.caster.clear();
    if (!slot.casts) return;
    const Vector3* positions = sector->vertices.data();
    slot.caster.resize(static_cast<std::size_t>(kWaterRecords));
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
            slot.caster[static_cast<std::size_t>(y * kSectorRow + x)] = TerrainCasterVertex{position, normal};
            lo = Min(lo, position);
            hi = Max(hi, position);
        }
    const Vector3 centre = (lo + hi) * 0.5f;
    slot.sphere = ShadowCasterSphere{centre.x, centre.y, centre.z, Length((hi - lo) * 0.5f)};
}

// Slot `k` built again from `held`, the sector the grid holds there now (null: none). The meshes are laid out again
// when one of its runs changed its size.
static void BuildSlot(int k, const SectorData* held) {
    Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    if (slot.held && slot.held != held) {
        auto it = g_zone.slotOf.find(slot.held);
        if (it != g_zone.slotOf.end() && it->second == k) g_zone.slotOf.erase(it);
    }
    slot.held = held;
    slot.stale = false;
    if (held) g_zone.slotOf[held] = k;
    slot.sector = held && lib::Has(held->built, terrain::SectorStage::Built) ? held : nullptr;
    for (int layer = 0; layer < kLayers; ++layer) {
        SlotLayer& out = slot.layers[layer];
        static std::vector<std::uint32_t> counts;
        counts.clear();
        for (const Run& run : out.runs) counts.push_back(run.count);
        BuildLayer(slot.sector, layer, out);
        bool same = counts.size() == out.runs.size();
        for (std::size_t r = 0; same && r < counts.size(); ++r) same = counts[r] == out.runs[r].count;
        if (!same) g_zone.laid = false;
    }
    BuildCaster(held, slot);
    slot.moved = false;
    slot.occlusion.clear();
    AddPending(k);
    slot.writes = kWriteAll;
    ++g_built;
}

// The slots' places in the meshes, one after the other; all of them to write into the meshes made again.
static void Layout() {
    for (int layer = 0; layer < kLayers; ++layer) {
        std::uint32_t at = 0;
        for (Slot& slot : g_zone.slots) {
            slot.layers[layer].first = at;
            at += slot.layers[layer].Count();
        }
        g_zone.meshes[layer].vertexCount = at;
        g_zone.meshes[layer].indexCount = at / kTileVertices * kTileIndices;
    }
    for (int k = 0; k < std::ssize(g_zone.slots); ++k) {
        AddPending(k);
        g_zone.slots[static_cast<std::size_t>(k)].writes = kWriteAll;
    }
    g_zone.laid = true;
    g_zone.remake = true;
}

// What the game moved in a slot since it was read: the water's positions at a new level (the boxes of its runs made
// again).
static void ReadMoved(int k) {
    Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    const bool moved = std::exchange(slot.moved, false);
    if (!slot.sector || !moved) return;
    if (slot.layers[1].Count() != 0) {
        LayerBoxes(slot.sector, slot.layers[1]);
        AddPending(k);
        slot.writes |= kWriteWater;
    }
    ++g_movedSlots;
}

// The zone brought up to what the game holds: a new landscape reset; the page textures' images looked at; every slot
// whose grid sector is another or that the game rebuilt built again, the ground occlusion worked out for them and
// again for their neighbours that read fewer neighbours than are held now; what the game moved read again.
static void Refresh(const terrain::Terrain& t) {
    const int layerPages = t.LayerPages();
    const int pages = std::min(lib::Count(t.pageTextures), layerPages);
    if (g_zone.terrain != &t || g_zone.sectorsX != t.sectorsX || g_zone.sectorsY != t.sectorsY ||
        g_zone.layerPages != layerPages || g_zone.pages != pages)
        Reset(t, layerPages, pages);
    g_zone.pageViews.resize(static_cast<std::size_t>(pages));
    g_zone.pageSerials.resize(static_cast<std::size_t>(pages), 0);
    for (int i = 0; i < pages; ++i) {
        const ::Texture* texture = t.pageTextures[static_cast<std::size_t>(i)].Get();
        g_zone.pageViews[static_cast<std::size_t>(i)] = TextureView(texture);
        const std::uint32_t serial = TextureSerial(texture);
        if (serial != g_zone.pageSerials[static_cast<std::size_t>(i)]) g_zone.arrayStale = true;
        g_zone.pageSerials[static_cast<std::size_t>(i)] = serial;
    }

    static std::vector<int> built;
    built.clear();
    for (int k = 0; k < std::ssize(g_zone.slots); ++k) {
        Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
        const SectorData* held = t.sectorGrid[static_cast<std::size_t>(k)];
        if (!g_cacheOn) slot.stale = true;
        if (held == slot.held && !slot.stale) continue;
        BuildSlot(k, held);
        built.push_back(k);
    }
    for (int k : built) {
        Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
        if (!slot.sector) continue;
        slot.neighbours = HeldNeighbours(slot.sector);
        WorkOutOcclusion(slot.sector, slot.neighbours, slot.occlusion);
    }
    for (int k : built) {
        const int sx = k / g_zone.sectorsY, sy = k % g_zone.sectorsY;
        for (const auto& d : kNeighbours) {
            const int n = SlotIndex(sx + d[0], sy + d[1]);
            if (n < 0) continue;
            Slot& slot = g_zone.slots[static_cast<std::size_t>(n)];
            if (!slot.sector || slot.occlusion.empty()) continue;
            const std::uint8_t held = HeldNeighbours(slot.sector);
            if ((held & ~slot.neighbours) == 0) continue;
            slot.neighbours |= held;
            WorkOutOcclusion(slot.sector, slot.neighbours, slot.occlusion);
            AddPending(n);
            slot.writes |= kWriteAll;
        }
    }
    for (int k : g_zone.moved) ReadMoved(k);
    g_zone.moved.clear();
    if (!g_zone.laid) Layout();
}

// --- GPU side ------------------------------------------------------------------------------------------

// The pipeline drawing terrain as the old device does with `state`: culling, depth and blending from the
// state the old path draws the page with. The ground pass runs under the terrain's state
// block with what the sky left (CULLMODE 2, ZWRITEENABLE 1; the world draw), the
// water pass under the scene's; both with ALPHABLENDENABLE on (SRCALPHA /
// INVSRCALPHA in the harness runs; the verify build logs each new pipeline's state).
static gpu::PipelineDesc TerrainDesc(const PassState& state, int samples, bool water) {
    namespace program = gpu_shaders::terrain_ground;   // the vertex layout is terrain_common.hlsli's: the water's slots match
    static_assert(gpu_shaders::terrain_water::kAttrPos == program::kAttrPos &&
                  gpu_shaders::terrain_water::kAttrNormal == program::kAttrNormal &&
                  gpu_shaders::terrain_water::kAttrRest == program::kAttrRest &&
                  gpu_shaders::terrain_water::kAttrCol == program::kAttrCol &&
                  gpu_shaders::terrain_water::kAttrSpec == program::kAttrSpec &&
                  gpu_shaders::terrain_water::kAttrUv == program::kAttrUv &&
                  gpu_shaders::terrain_water::kAttrAnim == program::kAttrAnim);
    gpu::PipelineDesc desc;
    desc.program = water ? gpu_shaders::terrain_water::kIndex : program::kIndex;
    desc.bufferCount = kStreams;
    for (int s = 0; s < kStreams; ++s) desc.buffers[s].stride = static_cast<std::uint16_t>(kStrides[s]);
    desc.attributeCount = 7;
    desc.attributes[0] = {program::kAttrPos, kPositions, gpu::VertexFormat::Float3, 0};
    desc.attributes[1] = {program::kAttrNormal, kRest, gpu::VertexFormat::Float3, offsetof(VertexRest, normal)};
    // loss, depth, shore and extra: the rest of VertexRest after the normal
    static_assert(offsetof(VertexRest, extra) == offsetof(VertexRest, loss) + 3 * sizeof(float));
    desc.attributes[2] = {program::kAttrRest, kRest, gpu::VertexFormat::Float4, offsetof(VertexRest, loss)};
    desc.attributes[3] = {program::kAttrCol, kColours, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, colour)};
    desc.attributes[4] = {program::kAttrSpec, kColours, gpu::VertexFormat::UByte4Norm, offsetof(ColourPair, specular)};
    desc.attributes[5] = {program::kAttrUv, kUvs, gpu::VertexFormat::Float3, offsetof(VertexUv, u)};   // u, v, the page's layer
    desc.attributes[6] = {program::kAttrAnim, kUvs, gpu::VertexFormat::Float4, offsetof(VertexUv, base)};
    ApplyPassState(desc, state);
    desc.samples = samples;
    AddDepthCopy(desc);
    return desc;
}

// TerrainDesc's pipeline (gpu.h keeps them by description); the pipelines that could not be made, logged.
static int g_pipelineFailures = 0;
static gpu::Pipeline Pipeline(const PassState& state, bool water) {
    const int samples = PassSampleCount();
#if defined(EI_RENDER_VALIDATION)
    const int known = gpu::PipelineCount();
#endif
    gpu::Pipeline pipeline = MakePassPipeline(TerrainDesc(state, samples, water), water ? "ei-terrain-water" : "ei-terrain-ground");
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

// The first `streams` of the mesh's vertex buffers and its indices bound.
static void BindMesh(const Mesh& mesh, int streams) {
    gpu::BufferBinding vertices[kStreams];
    for (int s = 0; s < streams; ++s) vertices[s] = gpu::BufferBinding{mesh.streams[s], 0};
    gpu::BindVertexBuffers(0, {vertices, static_cast<std::size_t>(streams)});
    gpu::BindIndexBuffer(gpu::BufferBinding{mesh.indices, 0});
}

static gpu::Buffer MakeBuffer(gpu::BufferUse use, std::uint64_t bytes, const char* label) {
    if (bytes == 0) return gpu::Buffer{};
    if (bytes > 0xFFFFFFFFull) Fatal("Can't draw the terrain: its mesh needs %llu bytes.", static_cast<unsigned long long>(bytes));
    gpu::Buffer b = gpu::MakeBuffer(use, static_cast<std::uint32_t>(bytes), label);
    if (!b) Fatal("Can't draw the terrain: its mesh's buffer (%llu bytes) could not be made.", static_cast<unsigned long long>(bytes));
    return b;
}

static void WriteBytes(const gpu::Buffer& buffer, std::uint64_t offset, const void* data, std::size_t bytes,
                       FrameStats::Count count) {
    if (bytes == 0) return;
    gpu::Write(buffer, static_cast<std::uint32_t>(offset), data, static_cast<std::uint32_t>(bytes));
    g_stats.counts[count] += bytes;
}

// The meshes' buffers made for the layout, with their indices, and the casters' for the grid.
static void Remake() {
    g_zone.remake = false;
    ReleaseMeshes();
    for (int layer = 0; layer < kLayers; ++layer) {
        Mesh& mesh = g_zone.meshes[layer];
        static const char* const kLabels[kLayers] = {"ei-terrain-ground", "ei-terrain-water"};
        for (int s = 0; s < kStreams; ++s)
            mesh.streams[s] = MakeBuffer(gpu::BufferUse::Vertex, std::uint64_t{mesh.vertexCount} * kStrides[s], kLabels[layer]);
        mesh.indices = MakeBuffer(gpu::BufferUse::Index, std::uint64_t{mesh.indexCount} * sizeof(std::uint32_t), kLabels[layer]);
        static std::vector<std::uint32_t> indices;
        indices.clear();
        indices.reserve(mesh.indexCount);
        for (int page = 0; page < g_zone.pages; ++page)
            for (int sy = 0; sy < g_zone.sectorsY; ++sy)
                for (int sx = 0; sx < g_zone.sectorsX; ++sx) {
                    const SlotLayer& l = g_zone.slots[static_cast<std::size_t>(SlotIndex(sx, sy))].layers[layer];
                    if (l.runs.empty()) continue;
                    for (int q = 0; q < kQuarters; ++q) {
                        const Run& run = l.runs[static_cast<std::size_t>(page * kQuarters + q)];
                        for (std::uint32_t t = 0; t < run.count; t += kTileVertices)
                            for (std::uint16_t corner : kTileTriangles) indices.push_back(l.first + run.first + t + corner);
                    }
                }
        WriteBytes(mesh.indices, 0, indices.data(), indices.size() * sizeof(std::uint32_t), FrameStats::Count::TerrainIndexBytes);
    }
    // The casters: slot k's vertices from k * 33 * 33.
    if (g_gridIndices.empty()) {
        using terrain::kSectorCells, terrain::kSectorRow;
        for (int y = 0; y < kSectorCells; ++y)
            for (int x = 0; x < kSectorCells; ++x) {
                std::uint32_t a = static_cast<std::uint32_t>(y * kSectorRow + x), b = a + 1, d = a + kSectorRow, e = d + 1;
                for (std::uint32_t i : {a, b, e, a, e, d}) g_gridIndices.push_back(i);
            }
    }
    const std::uint64_t slots = g_zone.slots.size();
    g_zone.casterVertices =
        MakeBuffer(gpu::BufferUse::Vertex, slots * kWaterRecords * sizeof(TerrainCasterVertex), "ei-terrain-casters");
    g_zone.casterIndices = MakeBuffer(gpu::BufferUse::Index, slots * g_gridIndices.size() * sizeof(std::uint32_t), "ei-terrain-casters");
    static std::vector<std::uint32_t> indices;
    indices.clear();
    for (std::uint32_t k = 0; k < slots; ++k)
        for (std::uint32_t i : g_gridIndices) indices.push_back(k * kWaterRecords + i);
    g_zone.casterIndexCount = static_cast<std::uint32_t>(indices.size());
    WriteBytes(g_zone.casterIndices, 0, indices.data(), indices.size() * sizeof(std::uint32_t), FrameStats::Count::TerrainIndexBytes);
}

// What slot `k` has to write into the meshes (kWrite*).
static void WriteSlot(int k) {
    Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    const std::uint8_t writes = std::exchange(slot.writes, std::uint8_t{0});
    const SectorData* sector = slot.sector;
    if (!sector) return;
    constexpr auto kBytes = FrameStats::Count::TerrainVertexBytes;
    static std::vector<Vector3> positions;
    static std::vector<VertexRest> rest;
    static std::vector<ColourPair> colours;
    for (int layer = 0; layer < kLayers; ++layer) {
        SlotLayer& l = slot.layers[layer];
        const Mesh& mesh = g_zone.meshes[layer];
        const std::uint32_t n = l.Count();
        if (n == 0) continue;
        auto at = [&](int s, std::uint32_t i) { return std::uint64_t{l.first + i} * kStrides[s]; };
        const bool all = writes & kWriteAll;
        if (all || (layer == 1 && (writes & kWriteWater))) {
            positions.resize(n);
            for (std::uint32_t i = 0; i < n; ++i) positions[i] = sector->vertices[l.records[i]];
            WriteBytes(mesh.streams[kPositions], at(kPositions, 0), positions.data(), n * sizeof(Vector3), kBytes);
        }
        if (all) {
            colours.resize(n);
            for (std::uint32_t i = 0; i < n; ++i) colours[i] = Colours(sector, l.records[i]);
            WriteBytes(mesh.streams[kColours], at(kColours, 0), colours.data(), n * sizeof(ColourPair), kBytes);
            rest.resize(n);
            for (std::uint32_t i = 0; i < n; ++i) rest[i] = MakeRest(sector, l.records[i], slot.occlusion.data());
            WriteBytes(mesh.streams[kRest], at(kRest, 0), rest.data(), n * sizeof(VertexRest), kBytes);
            WriteBytes(mesh.streams[kUvs], at(kUvs, 0), l.uvs.data(), n * sizeof(VertexUv), kBytes);
        }
    }
    if ((writes & kWriteAll) && !slot.caster.empty()) {
        WriteBytes(g_zone.casterVertices, std::uint64_t{static_cast<std::uint32_t>(k)} * kWaterRecords * sizeof(TerrainCasterVertex),
                   slot.caster.data(), slot.caster.size() * sizeof(TerrainCasterVertex), kBytes);
        slot.caster.clear();
        slot.caster.shrink_to_fit();
    }
}

// --- The page textures' array --------------------------------------------------------------------------

static gpu::Texture g_whiteArray;   // one white texel: the array without page textures
static int g_arrayFailures = 0;

// The array made again from the page textures: the first one's size, levels and format; a page of another shape is
// lost (logged; its layer left as made).
static void MakeArray() {
    g_zone.arrayStale = false;
    ReleaseAfterPresent(g_zone.array);
    g_zone.array = gpu::Texture{};
    const std::vector<gpu::Texture>& views = g_zone.pageViews;
    if (views.empty()) return;
    const gpu::Texture& first = views.front();
    gpu::Texture array = gpu::MakeTexture({first.width, first.height, first.levels, 1, first.format,
                                           gpu::TextureUse::Sampled, "ei-terrain-pages", static_cast<int>(views.size())});
    if (!array) Fatal("Can't draw the terrain: its page textures' array could not be made (%s).", gpu::Error());
    for (int layer = 0; layer < std::ssize(views); ++layer) {
        const gpu::Texture& page = views[static_cast<std::size_t>(layer)];
        if (page.width != first.width || page.height != first.height || page.levels != first.levels ||
            page.format != first.format) {
            LogFirst(g_arrayFailures, 10, "render: terrain page %d is %dx%d with %d levels, not as page 0 (%dx%d, %d); its tiles are lost",
                     layer, page.width, page.height, page.levels, first.width, first.height, first.levels);
            continue;
        }
        for (int level = 0; level < first.levels; ++level) {
            const int w = std::max(first.width >> level, 1), h = std::max(first.height >> level, 1);
            gpu::Copy(page, level, Rect{0, 0, w, h}, array, level, 0, 0, layer);
        }
    }
    g_zone.array = array;
}

static void MakeWhiteArray() {
    if (g_whiteArray) return;
    g_whiteArray = gpu::MakeTexture({1, 1, 1, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-terrain-white", 1});
    if (!g_whiteArray) Fatal("Can't draw the terrain: its white texture could not be made (%s).", gpu::Error());
    const std::uint32_t white = 0xFFFFFFFFu;
    gpu::Write(g_whiteArray, 0, Rect{0, 0, 1, 1}, &white, 4);
}

// --- The level map (render::TerrainLevels, the harness's) ---------------------------------------------

static TerrainLevelMode g_levelMode = TerrainLevelMode::Off;

// An array of one layer of the page textures' size and levels, each level one grey (level k: 32 k); sampled with the
// pages' layers, which the GPU clamps to its one.
static gpu::Texture g_levelArray;

static void MakeLevelArray() {
    const gpu::Texture& pages = g_zone.array;
    if (!pages || (g_levelArray && g_levelArray.width == pages.width && g_levelArray.height == pages.height &&
                   g_levelArray.levels == pages.levels))
        return;
    ReleaseAfterPresent(g_levelArray);
    const int width = pages.width, height = pages.height, levels = pages.levels;
    g_levelArray = gpu::MakeTexture(
        {width, height, levels, 1, gpu::Format::Rgba8, gpu::TextureUse::Sampled, "ei-terrain-levels", 1});
    if (!g_levelArray) return;
    std::vector<std::uint8_t> level;
    for (int k = 0; k < levels; ++k) {
        int w = (width >> k) > 0 ? width >> k : 1, h = (height >> k) > 0 ? height >> k : 1;
        auto grey = static_cast<std::uint8_t>(k * 32 < 255 ? k * 32 : 255);
        level.assign(static_cast<std::size_t>(w) * h * 4, grey);
        for (std::size_t i = 3; i < level.size(); i += 4) level[i] = 255;
        gpu::Write(g_levelArray, k, {0, 0, w, h}, level.data(), static_cast<std::size_t>(w) * 4);
    }
}

// --- The hand-over -------------------------------------------------------------------------------------

// The pass states the ground and the water pages are drawn with in every battle (the pipelines a run makes, logged).
void ListTerrainPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    PassState ground;   // alpha-blended, clockwise triangles culled
    ground.cull = Cull::Clockwise;
    ground.blend = true;
    ground.sourceBlend = Blend::SourceAlpha;
    ground.destBlend = Blend::InvSourceAlpha;
    out.push_back(TerrainDesc(ground, samples, false));
    PassState water;   // opaque, nothing culled
    water.blend = false;
    out.push_back(TerrainDesc(water, samples, true));
}

// The programs checked at the start: a pipeline of each made (gpu.h keeps it).
bool CreateTerrainShader() {
    bool ok = static_cast<bool>(gpu::MakePipeline(TerrainDesc(PassState{}, 1, false), "ei-terrain-ground"));
    ok = static_cast<bool>(gpu::MakePipeline(TerrainDesc(PassState{}, 1, true), "ei-terrain-water")) && ok;
    if (!ok) ei::Log("render: a terrain pipeline could not be created (see the gpu messages above)");
    return ok;
}

// The water's waves as the water pass hands them over (Terrain::WavesOf, at the moment Terrain::WaterVertex answers
// for), for terrain_water.hlsl's `waved`: off without LandscapeFlag::Waves.
struct WaterWaves {
    bool on = false;
    float direction[2] = {};
    float time = 0.0f;
    float materials[terrain::Terrain::kWaveMaterials][4] = {};   // turn, lift, keeps the ground's x, y
    float crests[terrain::Terrain::kWaveMaterials][4] = {};
};
static WaterWaves g_waves;

// The animated tiles' clock as the passes of this frame draw them (terrain_common.hlsli's TERRAIN_VS_MEMBERS).
struct TileClock {
    int steps = 0;
    int perRow = 1;
    float side = 0.0f;
};
static TileClock g_tiles;

static void TakeWaves(const terrain::Terrain& t) {
    g_waves = WaterWaves{};
    g_waves.on = lib::Has(t.landscapeFlags, terrain::LandscapeFlag::Waves);
    if (!g_waves.on) return;
    g_waves.direction[0] = t.waveDirection.x;
    g_waves.direction[1] = t.waveDirection.y;
    g_waves.time = t.waveTime;
    const int count = std::min(lib::Count(t.materials), terrain::Terrain::kWaveMaterials);
    for (int m = 0; m < count; ++m) {
        const terrain::Terrain::MaterialWaves w = t.WavesOf(m);
        float* material = g_waves.materials[m];
        material[0] = w.turn;
        material[1] = w.lift;
        material[2] = w.keepsGroundXY ? 1.0f : 0.0f;
        float* crest = g_waves.crests[m];
        crest[0] = w.crest.x;
        crest[1] = w.crest.y;
        crest[2] = w.crest.z;
    }
}

void TerrainPass(const terrain::Terrain* terrain, const SceneRenderer* renderer, int firstPage, int lastPage,
                 std::span<const SectorData* const> drawn) {
    if (!terrain || !renderer || terrain->sectorsX <= 0 || terrain->sectorsY <= 0 || firstPage > lastPage) return;
    Timer timer(FrameStats::Time::TerrainMs);
    // The clip matrix: the top of the transform pipeline's stack (the landscape's model matrix pushed by the terrain
    // passes on top of the view).
    const TransformPipeline* pipeline = renderer->pipeline;
    if (!pipeline) return;
    const MatrixStack& clipStack = pipeline->stacks[TransformPipeline::kClipStack];
    const int depth = clipStack.count;
    if (depth <= 0) return;
    Frame& f = g_frame;
    if (!f.haveFog) ReadFog(f);
    Refresh(*terrain);
    const int layer = firstPage >= terrain->LayerPages() ? 1 : 0;
    // The sectors drawn with their quarters not culled; the pass is drawn when one of them shows tiles of the layer.
    const std::size_t firstSector = f.terrainSectors.size();
    bool shown = false;
    for (const SectorData* sector : drawn) {
        const int k = sector ? SlotIndex(sector->sx, sector->sy) : -1;
        if (k < 0) continue;
        std::uint8_t quarters = 0;
        for (int q = 0; q < kQuarters; ++q)
            if (sector->quarterMode[q] != TransformPipeline::ClipResult::Outside) quarters |= static_cast<std::uint8_t>(1u << q);
        f.terrainSectors.push_back(TerrainDrawn{k, quarters});
        shown = shown || (quarters & g_zone.slots[static_cast<std::size_t>(k)].layers[layer].quarters) != 0;
    }
    g_stats.counts[FrameStats::Count::TerrainSectors] += drawn.size();
    if (!shown) {
        f.terrainSectors.resize(firstSector);
        return;
    }
    if (layer == 1) TakeWaves(*terrain);
    g_tiles = TileClock{terrain->tileSteps, std::max(terrain->tilesPerRow, 1), terrain->tileSide};
    const int matrix = AddMatrix(clipStack.matrices[depth - 1]);
    // The old device's state the pass is drawn with (the shader always adds the specular colour the old pass turned on
    // around each page).
    const PassState state = ReadPassState(nullptr);
    ++g_stats.counts[FrameStats::Count::TerrainBatches];
    f.terrainBatches.push_back(TerrainBatch{layer == 1, matrix, static_cast<int>(firstSector),
                                            static_cast<int>(f.terrainSectors.size() - firstSector), state});
    f.commands.push_back(Command{Command::Kind::Terrain, static_cast<int>(f.terrainBatches.size()) - 1});
}

void TerrainChanged(const SectorData* sector, TerrainChange change) {
    if (!sector) return;
    auto it = g_zone.slotOf.find(sector);
    if (it == g_zone.slotOf.end()) return;   // built into the zone when the grid holds it
    const int k = it->second;
    Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    switch (change) {
    case TerrainChange::Rebuilt: slot.stale = true; break;
    case TerrainChange::Dropped:
        slot.held = slot.sector = nullptr;
        slot.stale = true;
        g_zone.slotOf.erase(it);
        break;
    case TerrainChange::WaterMoved:
        if (!slot.moved) g_zone.moved.push_back(k);
        slot.moved = true;
        break;
    }
}

void UploadTerrain() {
    Timer timer(FrameStats::Time::TerrainMs);
    MakeWhiteArray();
    if (g_zone.arrayStale) MakeArray();
    if (g_levelMode != TerrainLevelMode::Off) MakeLevelArray();
    if (g_zone.remake && g_zone.laid) Remake();
    for (int k : g_zone.pending) WriteSlot(k);
    g_zone.pending.clear();
}

// The vertex block's members both programs share (terrain_common.hlsli's TERRAIN_VS_MEMBERS) and the fragment block's
// (TERRAIN_FS_MEMBERS), the same in the ground's and the water's blocks.
template <class Vs>
static void CommonVs(Vs& vs, const Frame& f, const TerrainBatch& batch, bool receives) {
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
    vs.tileSteps = g_tiles.steps;
    vs.tilesPerRow = g_tiles.perRow;
    vs.tileSide = g_tiles.side;
}

template <class Fs>
static void CommonFs(Fs& fs, const Frame& f, const SurfaceWeather& weather, const WaterReflections& sky) {
    UnpackColour(f.fogColour, fs.fog);
    fs.fogOn = f.fogEnabled ? 1 : 0;
    fs.rain = weather.rain;
    fs.rainClock = weather.rainClock;
    fs.skyView = sky.skyView;
    fs.dome[0] = sky.dome.x;
    fs.dome[1] = sky.dome.y;
    fs.dome[2] = sky.dome.z;
    fs.domeRadius = sky.domeRadius;
    fs.domeHeight = sky.domeHeight;
}

// The program's pipeline applied and the mesh and the page textures bound (`texture`: the page array or the level map).
static bool BeginTerrainDraw(const TerrainBatch& batch, const Mesh& mesh, int slotTex, gpu::TextureBinding texture) {
    gpu::Pipeline pipeline = Pipeline(batch.state, batch.water);
    if (!pipeline) return false;
    gpu::ApplyPipeline(pipeline);
    CountPipeline(pipeline);
    BindMesh(mesh, kStreams);
    gpu::BindTextures(gpu::Stage::Fragment, slotTex, {&texture, 1});
    CountBindings(texture.texture);
    return true;
}

static gpu::TextureBinding PageTextures(const TerrainBatch& batch) {
    return {g_zone.array ? g_zone.array : g_whiteArray, AnisotropicSampler(batch.state.stages[0], kMostAnisotropy)};
}

static void DrawGroundBatch(const Frame& f, const TerrainBatch& batch, const Mesh& mesh) {
    namespace program = gpu_shaders::terrain_ground;
    gpu::TextureBinding texture = PageTextures(batch);
    // The harness's level map: the page textures' grey per level (no receiving of shadows).
    const bool levels = g_levelMode != TerrainLevelMode::Off && g_levelArray;
    if (levels) {
        texture.texture = g_levelArray;
        if (g_levelMode == TerrainLevelMode::PointMips) texture.sampler = LinearSampler();
    }
    if (!BeginTerrainDraw(batch, mesh, program::kSamplerTex, texture)) return;
    BindShadowMap(f, program::kSamplerShadowMap);
    BindPointShadows(f, program::kSamplerPointShadowMap);
    BindPointLights(program::kStoragePointLight);
    BindCloudMap(program::kSamplerCloudMap);
    BindOcclusion(program::kSamplerOcclusionMap);
    BindRoofMap(program::kSamplerRoofMap);
    // The ground receives the objects' shadows and the clouds'.
    const bool receives = !levels && !f.shadow.lights.empty();
    const ShadowParams& shadow = ShadowUniforms(f, receives, receives);
    TerrainGroundVsParams vs{};
    CommonVs(vs, f, batch, receives);
    TerrainGroundFsParams fs{};
    const SurfaceWeather weather = SurfaceUniforms();
    CommonFs(fs, f, weather, BindWaterReflections(vs.matrix, program::kSamplerSkyImage, false));
    fs.levels = levels ? 1 : 0;
    fs.wet = weather.wet;
    fs.puddles = weather.puddles;
    fs.snow = weather.snow;
    OcclusionUniforms(batch.occluded && !levels, fs);
    const SceneLightParams light = SceneLightUniforms(f);
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockTerrainGroundVsParams, vs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockTerrainGroundFsParams, fs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowParams, shadow);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockSceneLightParams, light);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPointLightParams, f.shadow.point.params);
    gpu::DrawIndexed(0, mesh.indexCount);
    CountDraw(sizeof(vs) + sizeof(fs) + sizeof(shadow) + sizeof(light) + sizeof(f.shadow.point.params));
}

static void DrawWaterBatch(const Frame& f, const TerrainBatch& batch, const Mesh& mesh) {
    namespace program = gpu_shaders::terrain_water;
    if (!BeginTerrainDraw(batch, mesh, program::kSamplerTex, PageTextures(batch))) return;
    BindShadowMap(f, program::kSamplerShadowMap);
    BindPointShadows(f, program::kSamplerPointShadowMap);
    BindPointLights(program::kStoragePointLight);
    const gpu::TextureBinding ripples = RippleMap();
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerRipples, {&ripples, 1});
    BindCloudMap(program::kSamplerCloudMap);
    gpu::TextureBinding occlusion[2];
    OcclusionBindings(occlusion);   // the rays are marched through the depth (the second)
    gpu::BindTextures(gpu::Stage::Fragment, program::kSamplerOcclusionDepth, {occlusion + 1, 1});
    BindRoofMap(program::kSamplerRoofMap);
    // The water receives the objects' shadows and the clouds'.
    const bool receives = !f.shadow.lights.empty();
    const ShadowParams& shadow = ShadowUniforms(f, receives, receives);
    TerrainWaterVsParams vs{};
    CommonVs(vs, f, batch, receives);
    if (g_waves.on) {
        vs.waves = 1;
        vs.waveTime = g_waves.time;
        std::memcpy(vs.waveDirection, g_waves.direction, sizeof(vs.waveDirection));
        std::memcpy(vs.waveMaterials, g_waves.materials, sizeof(vs.waveMaterials));
        std::memcpy(vs.waveCrests, g_waves.crests, sizeof(vs.waveCrests));
    }
    TerrainWaterFsParams fs{};
    const WaterReflections sky = BindWaterReflections(vs.matrix, program::kSamplerSkyImage, true);
    CommonFs(fs, f, SurfaceUniforms(), sky);
    RippleOffsets(fs.ripples, fs.slowRipples);
    fs.view = sky.view;
    fs.sceneView = sky.sceneView;
    fs.sceneDepthOffset = sky.sceneDepthOffset;
    fs.sceneDepthScale = sky.sceneDepthScale;
    fs.sceneOn = sky.sceneOn;
    fs.sceneSize[0] = sky.sceneSize[0];
    fs.sceneSize[1] = sky.sceneSize[1];
    const SceneLightParams light = SceneLightUniforms(f);
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockTerrainWaterVsParams, vs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockTerrainWaterFsParams, fs);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowParams, shadow);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockSceneLightParams, light);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockPointLightParams, f.shadow.point.params);
    gpu::DrawIndexed(0, mesh.indexCount);
    CountDraw(sizeof(vs) + sizeof(fs) + sizeof(shadow) + sizeof(light) + sizeof(f.shadow.point.params));
}

void DrawTerrainBatch(const Frame& f, const TerrainBatch& batch) {
    const Mesh& mesh = g_zone.meshes[batch.water ? 1 : 0];
    if (mesh.indexCount == 0 || !mesh.indices) return;
    if (batch.water) DrawWaterBatch(f, batch, mesh);
    else DrawGroundBatch(f, batch, mesh);
}

gpu::PipelineDesc TerrainDepthDesc() {
    namespace program = gpu_shaders::shadow_terrain;
    gpu::PipelineDesc desc;
    desc.program = program::kIndex;
    desc.bufferCount = 2;   // the positions and the rest
    desc.buffers[0].stride = static_cast<std::uint16_t>(kStrides[kPositions]);
    desc.buffers[1].stride = static_cast<std::uint16_t>(kStrides[kRest]);
    desc.attributeCount = 2;
    desc.attributes[0] = {program::kAttrPos, kPositions, gpu::VertexFormat::Float3, 0};
    desc.attributes[1] = {program::kAttrNormal, kRest, gpu::VertexFormat::Float3, offsetof(VertexRest, normal)};
    desc.colour = gpu::Format::None;
    desc.depth = kDepthFormat;
    desc.depthCompare = gpu::Compare::LessEqual;
    desc.depthWrite = true;
    return desc;
}

// shadow_terrain.hlsl with each occluded batch's clip matrix for the light and no sun (no face left out); the ground's
// mesh once a matrix.
int DrawTerrainDepth(const Frame& f, gpu::Pipeline pipeline) {
    namespace program = gpu_shaders::shadow_terrain;
    const Mesh& mesh = g_zone.meshes[0];
    if (mesh.indexCount == 0 || !mesh.indices) return 0;
    static std::vector<int> matrices;
    matrices.clear();
    for (const TerrainBatch& batch : f.terrainBatches)
        if (batch.occluded && !batch.water && !lib::Contains(matrices, batch.matrix)) matrices.push_back(batch.matrix);
    if (matrices.empty()) return 0;
    gpu::ApplyPipeline(pipeline);
    BindMesh(mesh, 2);
    gpu::PushUniforms(gpu::Stage::Fragment, program::kBlockShadowTerrainFsParams, ShadowTerrainFsParams{});
    for (int matrix : matrices) {
        ShadowTerrainVsParams vs{};
        vs.light = f.matrices[static_cast<std::size_t>(matrix)];
        gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockShadowTerrainVsParams, vs);
        gpu::DrawIndexed(0, mesh.indexCount);
    }
    return static_cast<int>(matrices.size());
}

GroundOcclusionCount GroundOcclusionCounts() {
    int sectors = 0;
    for (const Slot& slot : g_zone.slots) sectors += slot.occlusion.empty() ? 0 : 1;
    return {sectors, static_cast<int>(g_occlusionMade)};
}

void TerrainPieces(const Frame& f, const TerrainBatch& batch, std::vector<TerrainPiece>* out) {
    const int layer = batch.water ? 1 : 0;
    const auto first = static_cast<std::size_t>(batch.firstSector);
    for (int page = 0; page < g_zone.pages; ++page)
        for (std::size_t i = first; i < first + static_cast<std::size_t>(batch.sectorCount); ++i) {
            const TerrainDrawn& d = f.terrainSectors[i];
            const Slot& slot = g_zone.slots[static_cast<std::size_t>(d.slot)];
            if (!slot.sector) continue;
            for (int q = 0; q < kQuarters; ++q) {
                const int run = page * kQuarters + q;
                if ((d.quarters >> q & 1) && slot.layers[layer].runs[static_cast<std::size_t>(run)].count != 0)
                    out->push_back(TerrainPiece{d.slot, layer, run});
            }
        }
}

static const SlotLayer& PieceLayer(const TerrainPiece& piece) {
    return g_zone.slots[static_cast<std::size_t>(piece.slot)].layers[piece.layer];
}

std::span<const TerrainGroup> TerrainPieceGroups(const TerrainPiece& piece) {
    const SlotLayer& l = PieceLayer(piece);
    const Run& run = l.runs[static_cast<std::size_t>(piece.run)];
    return std::span<const TerrainGroup>(l.groups.data() + run.firstGroup, run.groupCount);
}

const TerrainGroup& TerrainPieceWhole(const TerrainPiece& piece) {
    return PieceLayer(piece).runs[static_cast<std::size_t>(piece.run)].whole;
}

void TerrainGroupPositions(const TerrainPiece& piece, const TerrainGroup& group, std::uint32_t step,
                           std::vector<Vector3>* out) {
    out->clear();
    const SectorData* sector = g_zone.slots[static_cast<std::size_t>(piece.slot)].sector;
    if (!sector) return;
    const SlotLayer& l = PieceLayer(piece);
    for (std::uint32_t i = 0; i < group.count; i += step) out->push_back(sector->vertices[l.records[group.first + i]]);
}

void TerrainFrameDone() {
    std::uint32_t slots = 0;
    for (const Slot& slot : g_zone.slots) slots += slot.sector ? 1 : 0;
    g_stats.terrainReuse[FrameStats::Reuse::New] += g_built;
    g_stats.terrainReuse[FrameStats::Reuse::Moved] += g_movedSlots;
    g_stats.terrainReuse[FrameStats::Reuse::Same] += slots > g_built + g_movedSlots ? slots - g_built - g_movedSlots : 0;
    g_built = g_movedSlots = 0;
}

void TerrainLevels(TerrainLevelMode mode) {
    if (mode != g_levelMode)
        ei::Log("render: terrain level map %s",
                mode == TerrainLevelMode::Off ? "off" : mode == TerrainLevelMode::Levels ? "on" : "on, through point mips");
    g_levelMode = mode;
}

void SetTerrainCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: the zone's terrain %s", on ? "kept on the GPU" : "built and uploaded every pass");
    g_cacheOn = on;
}

// --- The ground's casters into the shadow map ------------------------------------------------------

void ShadowTerrainSector(const SectorData* sector) {
    if (!sector) return;
    const int k = SlotIndex(sector->sx, sector->sy);
    if (k < 0 || g_zone.slots[static_cast<std::size_t>(k)].held != sector) return;
    const Slot& slot = g_zone.slots[static_cast<std::size_t>(k)];
    std::uint32_t& word = g_casting[static_cast<std::size_t>(k) / 32];
    const std::uint32_t bit = 1u << (k % 32);
    if (!slot.casts || (word & bit) != 0) return;
    Timer timer(FrameStats::Time::ShadowMs);
    if (!ShadowCasterReaches(slot.sphere)) return;
    word |= bit;
    ++g_casters;
}

std::size_t TerrainCasterCount() { return g_casters; }

int DrawTerrainCasters(gpu::Pipeline pipeline, const Matrix4& light) {
    namespace program = gpu_shaders::shadow_terrain;
    if (g_casters == 0 || !g_zone.casterIndices) return 0;
    gpu::ApplyPipeline(pipeline);
    const gpu::BufferBinding vertices{g_zone.casterVertices, 0};
    gpu::BindVertexBuffers(0, {&vertices, 1});
    gpu::BindIndexBuffer(gpu::BufferBinding{g_zone.casterIndices, 0});
    ShadowTerrainVsParams vs{};
    vs.light = light;
    vs.cast = 1;
    for (std::size_t w = 0; w < g_casting.size() && w < kMaskedSectors / 32; ++w) vs.sectors[w / 4][w % 4] = g_casting[w];
    gpu::PushUniforms(gpu::Stage::Vertex, program::kBlockShadowTerrainVsParams, vs);
    gpu::DrawIndexed(0, g_zone.casterIndexCount);
    return 1;
}

void TerrainCastersDone() {
    std::fill(g_casting.begin(), g_casting.end(), 0u);
    g_casters = 0;
}

} // namespace render
