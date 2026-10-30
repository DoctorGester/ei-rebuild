// The landscape's height queries (terrain.h): vertices, heights, the surface and its texture class.
#include "terrain.h"

#include "camera.h"
#include "figure_mesh.h"   // VertexPacket
#include "game_singletons.h"
#include "ground_decal.h"
#include "model.h"
#include "options.h"
#include "random.h"
#include "spell.h"
#include "terrain_sector.h"
#include "world.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain {
namespace {

// The height at (x, y) of the plane through the three vertices; the average of their heights when the
// triangle is degenerate.
float PlaneHeight(const Vector3& v0, const Vector3& v1, float x, float y, const Vector3& v2) {
    float ax = v1.x - v0.x, ay = v1.y - v0.y;
    float bx = v2.x - v0.x, by = v2.y - v0.y;
    float det = by * ax - bx * ay;
    if (std::fabs(det) < 0.01f) return (v0.z + v1.z + v2.z) * 0.33333334f;

    // Plane normal (nx, ny, det) through v0; solve for z at (x, y).
    float dz1 = v1.z - v0.z, dz2 = v2.z - v0.z;
    float nx = dz2 * ay - dz1 * by;
    float ny = dz1 * bx - dz2 * ax;
    float d = -DotUnrounded<Sum3::XYZ>(nx, ny, det, v0.x, v0.y, v0.z);
    return -(OffsetDot2DUnrounded(d, nx, ny, x, y) / det);
}

// The texture class outside the map.
constexpr int kOutsideClass = 8;

// A vertex column (or row) of the map as a sector's and its vertex in that sector. Vertex 32 of a sector is
// vertex 0 of the next; such a shared vertex is the earlier sector's (the first sector keeps its vertex 0).
struct SplitVertex {
    int sector, vertex;
};
SplitVertex SplitOf(int v) {
    const int sector = v / kSectorCells, vertex = v - sector * kSectorCells;
    if (vertex == 0 && sector != 0) return {sector - 1, kLastVertex};
    return {sector, vertex};
}

} // namespace

const Vector3* Terrain::VertexAt(int ix, int iy, Layer layer) {
    if (ix < 0 || iy < 0) return nullptr;
    EI_NAN_AUDIT(width, height);
    if (width < static_cast<float>(ix) || height < static_cast<float>(iy)) return nullptr;

    const SplitVertex x = SplitOf(ix), y = SplitOf(iy);
    SectorData* sector = SectorAt(x.sector, y.sector, SectorStage::Loaded, true);
    if (!sector) return nullptr;
    int layerOffset = 0;
    if (layer == Layer::Water) {
        if (!sector->hasWater) return nullptr;
        layerOffset = kSectorVertices;
    }
    return &sector->vertices[x.vertex + y.vertex * kSectorRow + layerOffset];
}

float Terrain::HeightAt(float x, float y, Layer layer) {
    int ix0 = lib::RoundEven(x), iy0 = lib::RoundEven(y);
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    for (int iteration = 1;; ++iteration) {
        const Vector3* v00 = VertexAt(ix0, iy0, layer);
        const Vector3* v10 = VertexAt(ix1, iy0, layer);
        const Vector3* v01 = VertexAt(ix0, iy1, layer);
        const Vector3* v11 = VertexAt(ix1, iy1, layer);
        if (!v00 || !v11) return 0.0f;
        if (!v10) {
            if (v01) return PlaneHeight(*v00, *v11, x, y, *v01);
            continue;   // the same cell again: the iteration limit below is not reached, so this never ends
        }
        if (!v01) return PlaneHeight(*v00, *v10, x, y, *v11);

        // Both triangles of the cell must be counter-clockwise, else take the diagonal's midpoint.
        const Vector2 from10to00 = {v00->x - v10->x, v00->y - v10->y};
        const Vector2 from10to11 = {v11->x - v10->x, v11->y - v10->y};
        const float turnS = from10to11.x * from10to00.y - from10to00.x * from10to11.y;
        EI_NAN_AUDIT(turnS);
        if (turnS <= 0.0f) return (v11->z + v01->z) * 0.5f;
        const Vector2 from01to00 = {v00->x - v01->x, v00->y - v01->y};
        const Vector2 from01to11 = {v11->x - v01->x, v11->y - v01->y};
        const float turnT = from01to00.x * from01to11.y - from01to11.x * from01to00.y;
        EI_NAN_AUDIT(turnT);
        if (turnT <= 0.0f) return (v11->z + v01->z) * 0.5f;

        const Vector2 to10 = {v10->x - x, v10->y - y};
        const Vector2 to01 = {v01->x - x, v01->y - y};
        float side = (x - v00->x) * (v11->y - v00->y) - (y - v00->y) * (v11->x - v00->x);
        if (side > 0.0f) {
            // Lower-right triangle (v00, v10, v11); step to a neighbour if the point is outside it.
            if (iteration >= 10) return PlaneHeight(*v00, *v10, x, y, *v11);
            if (from10to00.y * to10.x - from10to00.x * to10.y > 0.0f) { --iy0; --iy1; continue; }
            if (from10to11.y * to10.x - from10to11.x * to10.y >= 0.0f) return PlaneHeight(*v00, *v10, x, y, *v11);
            ++ix0; ++ix1;
        } else {
            // Upper-left triangle (v00, v11, v01).
            if (iteration >= 10) return PlaneHeight(*v00, *v11, x, y, *v01);
            float across = from01to00.y * to01.x - from01to00.x * to01.y;
            EI_NAN_AUDIT(across);
            if (across < 0.0f) { --ix0; --ix1; continue; }
            const float beyond = from01to11.y * to01.x - from01to11.x * to01.y;
            EI_NAN_AUDIT(beyond);
            if (beyond <= 0.0f) return PlaneHeight(*v00, *v11, x, y, *v01);
            ++iy0; ++iy1;
        }
    }
}

float Terrain::SurfaceHeight(float x, float y) {
    const float water = HeightAt(x, y, Layer::Water);
    const float ground = HeightAt(x, y, Layer::Ground);
    const float depth = water > ground ? water - ground : 0.0f;
    return ground + depth;
}

int Terrain::SurfaceClass(float x, float y) {
    int ix = lib::RoundEven(x);
    int iy = lib::RoundEven(y);
    if (ix < 0 || iy < 0 || ix >= sectorsX * kSectorCells || iy >= sectorsY * kSectorCells) return kOutsideClass;
    SectorData* sector = SectorAt(ix / kSectorCells, iy / kSectorCells, SectorStage::Built, true);
    int tile = (iy % kSectorCells) / 2 * kSectorTiles + (ix % kSectorCells) / 2;
    if (!sector) return kOutsideClass;
    if (sector->hasWater && sector->waterTiles[tile] != -1) {
        float water = HeightAt(x, y, Layer::Water);
        const float ground = HeightAt(x, y, Layer::Ground);
        EI_NAN_AUDIT(ground, water);
        if (ground < water) return textureClass[sector->tileTextures[tile + kLayerTiles]];
    }
    return textureClass[sector->tileTextures[tile]];
}

} // namespace terrain

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kTerrainClassName = "CTerrain";
} // namespace

const char* terrain::Terrain::ClassName() { return kTerrainClassName; }
ClassId terrain::Terrain::GetClassId() { return ClassId::Terrain; }

// --- Shadow ------------------------------------------------------------------------------------------

// The fade of the light at the map's edges (terrain.h): from the sector build (the fog factor of the vertices'
// specular words), the figures' draws and the ground prints.

namespace terrain {

std::uint8_t Terrain::EdgeFade(const Vector3& point) {
    if (!lib::Has(landscapeFlags, LandscapeFlag::EdgeFade)) return 0xFF;
    const float fade = options::g_options.borderFogDistance;
    // How far inside the band's inner border the point is, per edge (negative: in the band).
    float left = point.x - fade - 1.0f;
    float bottom = point.y - fade - 1.0f;
    float right = width - fade - point.x - 1.0f;
    float top = height - fade - point.y - 1.0f;
    if (!(SignBit(left) || SignBit(bottom) || SignBit(right) || SignBit(top))) return 0xFF;
    left = MinKeepA(0.0f, left);
    bottom = MinKeepA(0.0f, bottom);
    right = MinKeepA(0.0f, right);
    top = MinKeepA(0.0f, top);
    float depth = std::sqrt(top * top + right * right + bottom * bottom + left * left);
    depth = MinKeepA(fade, depth);
    int amount = std::lround(depth / fade * 255.0f);
    return static_cast<std::uint8_t>(0xFF - static_cast<std::uint8_t>(amount));
}

} // namespace terrain

// --- Decal -------------------------------------------------------------------------------------------

// The terrain part of a ground decal (the shadow projector). The decal quad is
// slid along the builder's slope until its lowest point matches the lowest ground vertex under
// its bounding cells; the ground cells of that rectangle then go to the builder as two triangles
// each.

namespace terrain {
namespace {

constexpr float kMargin = 1.0f;   // one cell around the quad

// A cell bound: (v -/+ the margin) / cell size, stored as a float first and rounded to nearest.
int CellBound(float v) { return std::lround(v); }

Vector3 Corner(const VertexPacket& quad, int corner) { return {quad.x()[corner], quad.y()[corner], quad.z()[corner]}; }

} // namespace

void Terrain::AddDecalTriangles(DecalBuilder* builder, const VertexPacket& quad) {
    Vector3 origin = Corner(quad, 3);
    Vector3 u = Corner(quad, 2);
    Vector3 v = Corner(quad, 0);
    Vector3 slope = builder->slope;
    float lowest = origin.z;
    float previous = origin.z;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = 0, y1 = 0;
    for (;;) {
        float shift = previous - lowest;
        v = slope * shift + v;
        origin = slope * shift + origin;
        u = slope * shift + u;
        // The fourth corner's x and y (u + v - origin).
        float fourthX = u.x + v.x - origin.x;
        float fourthY = u.y + v.y - origin.y;
        int bound = CellBound(MinKeepA(MinKeepA(MinKeepA(fourthX, u.x), v.x), origin.x) - kMargin);
        if (x0 >= bound) x0 = bound;
        bound = CellBound(MinKeepA(MinKeepA(MinKeepA(fourthY, u.y), v.y), origin.y) - kMargin);
        if (y0 >= bound) y0 = bound;
        bound = CellBound(MaxKeepB(origin.x, MaxKeepB(v.x, MaxKeepB(u.x, fourthX))) + kMargin);
        if (x1 <= bound) x1 = bound;
        bound = CellBound(MaxKeepB(origin.y, MaxKeepB(v.y, MaxKeepB(u.y, fourthY))) + kMargin);
        if (y1 <= bound) y1 = bound;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        int last = static_cast<int>(width - 1.0f);
        if (last < x1) x1 = last;
        last = static_cast<int>(height - 1.0f);
        if (last < y1) y1 = last;
        previous = lowest;
        if (y0 > y1) break;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) lowest = MinKeepA(VertexAt(x, y, Layer::Ground)->z, lowest);
        }
        if (!(lowest < previous)) break;   // it no longer drops (Lower never raises it); a NaN ends it too
    }
    builder->Reserve(((x1 - x0 + 1) * (y1 - y0 + 1) + (y1 - y0 + x1 - x0 + 1) * 5) * 4);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Vector3* a = VertexAt(x, y, Layer::Ground);
            const Vector3* b = VertexAt(x, y + 1, Layer::Ground);
            const Vector3* c = VertexAt(x + 1, y, Layer::Ground);
            const Vector3* d = VertexAt(x + 1, y + 1, Layer::Ground);
            builder->AddTerrainTriangle(*a, *c, *d);
            builder->AddTerrainTriangle(*a, *d, *b);
        }
    }
}

} // namespace terrain

// --- Passes ------------------------------------------------------------------------------------------

// The landscape's draw passes (terrain.h), called by the client's draw after Terrain::SetVisibleSectors:
// the ground pages, then (after the objects) the water pages. A pass pushes the landscape's matrix on the
// renderer's pipeline, prepares every visible sector (the clip test of the sector and its quarters)
// and hands the sectors page by page to the renderer (render::TerrainPage, which builds the page's tiles
// from the sector's data); the GPU clips what crosses the view's edges. Nothing here talks to the device: the texture goes through the renderer's SetTexture.

namespace terrain {
namespace {

constexpr int kProjectStack = 1;        // the pipeline stack on-screen vertices are projected with

// Calls `visit(sector)` for every visible sector (SetVisibleSectors' rows and columns) that is there once built.
template <typename Visit>
void ForVisibleSectors(Terrain* terrain, Visit visit) {
    const Terrain::SectorRect& visible = terrain->visible;
    for (int sy = visible.firstY; sy <= visible.lastY; ++sy)
        for (int sx = visible.firstX; sx <= visible.lastX; ++sx)
            if (SectorData* sector = terrain->SectorAt(sx, sy, SectorStage::Built, true)) visit(sector);
}

bool HasTilesOn(const SectorData* sector, int page) {
    for (const auto& quarter : sector->tiles)
        if (!quarter[page].empty()) return true;
    return false;
}

} // namespace

// --- The sector's part of the passes -------------------------------------

bool SectorData::IsDrawn() const { return clipMode == 0 || clipMode == kCrossing; }

void SectorData::ClipTest(SceneRenderer* renderer) {
    // The verdict as it is stored: -1 counts as outside.
    const auto verdict = [&](const SectorBox& bounds) {
        const int where = renderer->pipeline->ClipTest(&bounds);
        return where == -1 ? kCulled : where;
    };
    clipMode = verdict(box);
    if (clipMode == kCulled) return;
    for (int q = 0; q < 4; ++q) quarterMode[q] = verdict(quarters[q]);
}

void SectorData::DrawPage(const SceneRenderer* renderer, int page) {
    if (clipMode == kCulled) return;
    if (hasWater) ApplyWaterLevels();
    render::TerrainPage(this, renderer, page);
}

// --- The passes ------------------------------------------------------------------------------------

void Terrain::DrawPass(SceneRenderer* renderer, int firstPage, int lastPage) {
    Matrix4 matrix;
    localRotation.ToMatrix(matrix.m);
    matrix.m[3] = localPosition.x;
    matrix.m[7] = localPosition.y;
    matrix.m[11] = localPosition.z;
    matrix.m[12] = matrix.m[13] = matrix.m[14] = 0.0f;
    matrix.m[15] = 1.0f;
    TransformPipeline* pipeline = renderer->pipeline;
    pipeline->PushMatrixProduct(matrix, TransformPipeline::kClipStack);
    pipeline->PushMatrixProduct(matrix, kProjectStack);
    DrawPages(renderer, firstPage, lastPage);
    pipeline = renderer->pipeline;
    pipeline->PopMatrix(TransformPipeline::kClipStack);
    pipeline->PopMatrix(kProjectStack);
}

void Terrain::DrawPages(SceneRenderer* renderer, int firstPage, int lastPage) {
    if (!mapLoaded) return;
    ForVisibleSectors(this, [&](SectorData* sector) { sector->ClipTest(renderer); });
    for (int page = firstPage; page <= lastPage; ++page) {
        bool bound = false;
        ForVisibleSectors(this, [&](SectorData* sector) {
            if (!sector->IsDrawn() || !HasTilesOn(sector, page)) return;
            if (!bound) {
                renderer->SetTexture(0, pageTextures[page % LayerPages()]);
                bound = true;
            }
            sector->DrawPage(renderer, page);
        });
    }
}

void Terrain::DrawGround(SceneRenderer* renderer) { DrawPass(renderer, 0, LayerPages() - 1); }

void Terrain::DrawWater(SceneRenderer* renderer) { DrawPass(renderer, LayerPages(), 2 * LayerPages() - 1); }

} // namespace terrain

// --- Animation ---------------------------------------------------------------------------------------

// The landscape's per-frame work: the update (Terrain::RunTick), the sun (SetSun), animated tiles
// (SectorData::AnimateTiles) and water waves (SectorData::AnimateWaves). Waves: every water vertex has a phase offset (owner wavePhases); the phase along
// the wave direction runs from the sector's first water vertex (mod 2 pi) and steps by the direction per
// vertex. The 512-entry sine table at that phase lifts the vertex (by waveHeight times the material's
// priority) and gives the crest its specular colour; materials of kind kKeepsGroundXY move the vertex sideways
// (only where its depth is negative), the others up and in a circle driven by the world time (its sines per
// column and row once per call: CircleSines).

namespace terrain {
namespace {

constexpr float kSineIndex = 256.0f / 3.14159265f;        // table entries per radian
constexpr float kQuarterTurn = 0.785398126f;
constexpr float kTimeScale = 0.05f;                        // the circling waves' speed per world tick
constexpr float kGlowScale = 50.0f;
constexpr float kRiseScale = 0.25f;
constexpr float kCircleScale = 3.0f;
constexpr int kTileFrameEvery = 3;                         // animated tiles step every third frame
constexpr int kAlwaysSteps = 1000;                         // a tile speed that steps every time

// The sine table entry at `phase` radians: phase * 256 / pi rounded to nearest, mod 512.
inline float Sine(const Terrain* owner, float phase) { return owner->sine[std::lround(phase * kSineIndex) & 0x1FF]; }

inline std::uint32_t ColourByte(float v) { return static_cast<std::uint32_t>(std::lround(v)) & 0xFF; }

// The circling waves' sine per column (and per row: the same values) at the world time, once per call.
void CircleSines(const Terrain* owner, float* sines) {
    float time = (g_clientWorld.tick + g_clientWorld.tickFraction) * kTimeScale;
    for (int i = 0; i < kSectorRow; ++i) sines[i] = Sine(owner, i * kQuarterTurn + time);
}

} // namespace

void SectorData::Clear() {
    records.clear();
    for (int quarter = 0; quarter < 4; ++quarter) {
        for (std::vector<TileVertex>& list : tiles[quarter]) list.clear();
    }
    sx = sy = -1;
    hasWater = false;
    hasWaterTiles = false;
    data = nullptr;
    owner = nullptr;
    built = SectorStage::None;
    lastUsed = 0;
    clipMode = kCulled;
    render::TerrainChanged(this, render::TerrainChange::Rebuilt);
}

void SectorData::AnimateTiles(int frame) {
    if (frame % kTileFrameEvery != 0 || !lib::Has(owner->landscapeFlags, LandscapeFlag::AnimatedTiles)) return;
    bool stepped = false;
    for (AnimatedTile& tile : animated) {
        if (tile.frame == 0 && tile.speed < kAlwaysSteps &&
            g_clientRandom.Next() % kAlwaysSteps + 1 > static_cast<std::uint32_t>(tile.speed)) {
            continue;
        }
        tile.frame = (tile.frame + 1) % tile.frames;
        stepped = true;
        const TileUv& uv = owner->TileUvAt(tile.rotation, tile.frameBase + tile.frame, 0);
        for (int k = 0; k < TileUv::kVertices; ++k) {
            tile.vertices[k].u = uv.u[k];
            tile.vertices[k].v = 1.0f - uv.v[k];
        }
    }
    if (stepped) render::TerrainChanged(this, render::TerrainChange::Moved);   // the pages kept on the GPU
}

void SectorData::AnimateWaves() {
    if (!hasWater || !lib::Has(owner->landscapeFlags, LandscapeFlag::Waves)) return;

    SectorPosition* positions = &vertices[kSectorVertices];
    SectorVertex* water = &records[kSectorVertices];
    const float* phaseOffset = owner->wavePhases.data();
    Vector2 direction = {owner->waveDirection.x, owner->waveDirection.y};
    float height = owner->waveHeight;
    float phase = owner->wavePhase;
    float fullTurn = kPi + kPi;
    Vector2 first = {positions[0].x, positions[0].y};
    float rowStart = std::fmod(Dot2D(direction, first) + fullTurn, fullTurn);

    int lastMaterial = -1;
    float lastTurn = 0.0f;
    float amplitude = 0.0f;
    int kind = 0;
    Vector3 crest = {0.0f, 0.0f, 0.0f};
    float circle[kSectorRow];
    bool circling = false;
    for (int row = 0; row < kSectorRow; ++row) {
        float along = rowStart;
        for (int column = 0; column < kSectorRow; ++column, ++phaseOffset) {
            SectorVertex& record = water[row * kSectorRow + column];
            SectorPosition& position = positions[row * kSectorRow + column];
            int material = record.waterMaterial;
            if (material != SectorVertex::kNoWaterMaterial) {
                if (material != lastMaterial) {
                    const TerrainMaterial& m = owner->materials[material];
                    amplitude = height * m.priority;
                    kind = m.kind;
                    float glow = owner->waveGlow * m.priority * kGlowScale;
                    crest = {glow * m.waveColour.x, glow * m.waveColour.y, glow * m.waveColour.z};
                    float turn = std::fmod(phase * m.priority, fullTurn);
                    float delta = turn - lastTurn;
                    lastTurn = turn;
                    rowStart += delta;
                    along += delta;
                    lastMaterial = material;
                }
                float wave = Sine(owner, along + *phaseOffset);
                if (!SignBit(wave)) {
                    record.specular = PackArgb(record.specular >> 24, ColourByte(wave * crest.x), ColourByte(wave * crest.y),
                                               ColourByte(wave * crest.z));
                } else {
                    record.specular &= 0xFF000000u;
                }
                float lift = wave * amplitude;
                if (kind == TerrainMaterial::kKeepsGroundXY) {
                    if (SignBit(record.depth)) {
                        position.x = lift + record.position.x;
                        position.y = lift + record.position.y;
                    }
                } else {
                    position.z = lift * kRiseScale + record.position.z;
                    if (!circling) {
                        CircleSines(owner, circle);
                        circling = true;
                    }
                    position.x = circle[column] * amplitude * kCircleScale + record.position.x;
                    position.y = circle[row] * amplitude * kCircleScale + record.position.y;
                }
            }
            along += direction.x;
        }
        rowStart += direction.y;
    }
    render::TerrainChanged(this, render::TerrainChange::WaterMoved);
}

void Terrain::SetSun(Light* light, const Vector4* colour) {
    ambient = *colour;
    sun = light;
}

bool Terrain::RunTick() {
    if (lib::Has(landscapeFlags, LandscapeFlag::Ambience)) PlayAmbience();
    wavePhase += waveSpeed;
    ++frame;
    for (int sy = visible.firstY; sy <= visible.lastY; ++sy) {
        for (int sx = visible.firstX; sx <= visible.lastX; ++sx) {
            SectorData* sector = SectorAt(sx, sy, SectorStage::Built, false);
            if (sector && sector->IsDrawn()) {
                sector->AnimateTiles(frame);
                sector->AnimateWaves();
            }
        }
    }
    return true;
}

} // namespace terrain
