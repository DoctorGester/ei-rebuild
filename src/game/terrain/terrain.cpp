// The landscape (terrain.h): height queries, edge fade, ground decal triangles, draw passes and the per-frame update.
#include "engine/scene/light.h"
#include "game/terrain/terrain.h"

#include "engine/scene/camera.h"
#include "engine/models/figure_mesh.h"   // VertexPacket
#include "game/terrain/ground_decal.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/data/options.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain_sector.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
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
    float d = -Dot(Vector3{nx, ny, det}, Vector3{v0.x, v0.y, v0.z});
    return -((d + Dot2D(nx, ny, x, y)) / det);
}

// The height at (x, y) of the surface through the vertices `at(ix, iy)` gives (std::nullopt: none there): the cell's
// triangle holding the point, stepping to a neighbouring cell while the point lies outside (the vertices may be
// displaced sideways); 0 without the cell's diagonal.
template <typename At>
float HeightOver(float x, float y, At at) {
    int ix0 = lib::RoundEven(x), iy0 = lib::RoundEven(y);
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    for (int iteration = 1;; ++iteration) {
        const std::optional<Vector3> v00 = at(ix0, iy0);
        const std::optional<Vector3> v10 = at(ix1, iy0);
        const std::optional<Vector3> v01 = at(ix0, iy1);
        const std::optional<Vector3> v11 = at(ix1, iy1);
        if (!v00 || !v11) return 0.0f;
        // Only the diagonal's ends (the original retried the same cell forever): its midpoint.
        if (!v10 && !v01) return (v00->z + v11->z) * 0.5f;
        if (!v10) return PlaneHeight(*v00, *v11, x, y, *v01);
        if (!v01) return PlaneHeight(*v00, *v10, x, y, *v11);

        // Both triangles of the cell must be counter-clockwise, else take the diagonal's midpoint.
        const Vector2 from10to00 = XY(*v00) - XY(*v10);
        const Vector2 from10to11 = XY(*v11) - XY(*v10);
        const float turnS = Cross2D(from10to11, from10to00);
        if (turnS <= 0.0f) return (v11->z + v01->z) * 0.5f;
        const Vector2 from01to00 = XY(*v00) - XY(*v01);
        const Vector2 from01to11 = XY(*v11) - XY(*v01);
        const float turnT = Cross2D(from01to00, from01to11);
        if (turnT <= 0.0f) return (v11->z + v01->z) * 0.5f;

        const Vector2 point{x, y};
        const Vector2 to10 = XY(*v10) - point;
        const Vector2 to01 = XY(*v01) - point;
        float side = Cross2D(point - XY(*v00), XY(*v11) - XY(*v00));
        if (side > 0.0f) {
            // Lower-right triangle (v00, v10, v11); step to a neighbour if the point is outside it.
            if (iteration >= 10) return PlaneHeight(*v00, *v10, x, y, *v11);
            if (Cross2D(to10, from10to00) > 0.0f) { --iy0; --iy1; continue; }
            if (Cross2D(to10, from10to11) >= 0.0f) return PlaneHeight(*v00, *v10, x, y, *v11);
            ++ix0; ++ix1;
        } else {
            // Upper-left triangle (v00, v11, v01).
            if (iteration >= 10) return PlaneHeight(*v00, *v11, x, y, *v01);
            float across = Cross2D(to01, from01to00);
            if (across < 0.0f) { --ix0; --ix1; continue; }
            const float beyond = Cross2D(to01, from01to11);
            if (beyond <= 0.0f) return PlaneHeight(*v00, *v11, x, y, *v01);
            ++iy0; ++iy1;
        }
    }
}

std::optional<Vector3> Copy(const Vector3* v) { return v ? std::optional<Vector3>(*v) : std::nullopt; }

// The ground height raised to the water's where the water is above it.
float AboveGround(float water, float ground) {
    const float depth = water > ground ? water - ground : 0.0f;
    return ground + depth;
}

// The waves (Terrain::WaterVertex; terrain_water.hlsl's `waved` mirrors these).
constexpr float kFullTurn = kPi + kPi;
constexpr float kSineIndex = 256.0f / 3.14159265f;   // sine table entries per radian
constexpr float kQuarterTurn = 0.785398126f;         // between the circling's columns (and rows)
constexpr float kTimeScale = 0.05f;                  // the circling's speed per world tick
constexpr float kGlowScale = 50.0f;
constexpr float kRiseScale = 0.25f;
constexpr float kCircleScale = 3.0f;

// The sine table entry (Terrain::sine) at `phase` radians: phase * 256 / pi rounded half away from zero, mod 512.
float Sine(const float (&table)[512], float phase) { return table[lib::RoundAway(phase * kSineIndex) & 0x1FF]; }

// The texture class outside the map.
constexpr int kOutsideClass = 8;

// A vertex column (or row) of the map as a sector's and its vertex in that sector. Vertex 32 of a sector is
// vertex 0 of the next; such a shared vertex is the earlier sector's (the first sector keeps its vertex 0).
struct SplitVertex {
    int sector = 0, vertex = 0;
};
SplitVertex SplitOf(int v) {
    const int sector = v / kSectorCells, vertex = v - sector * kSectorCells;
    if (vertex == 0 && sector != 0) return {sector - 1, kLastVertex};
    return {sector, vertex};
}

// The ground decal's quad is padded by one cell.
constexpr float kMargin = 1.0f;

// A cell bound: the coordinate rounded to nearest.
int CellBound(float v) { return std::lround(v); }

Vector3 Corner(const VertexPacket& quad, int corner) { return {quad.x()[corner], quad.y()[corner], quad.z()[corner]}; }

constexpr int kProjectStack = 1;        // the pipeline stack on-screen vertices are projected with

// Calls `visit(sector)` for every visible sector (ShowAllSectors' rows and columns) that is there once built.
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

// --- Class name and id ------------------------------------------------------------------------------

const char* Terrain::ClassName() { return "CTerrain"; }
ClassId Terrain::GetClassId() { return ClassId::Terrain; }

// --- Height queries ----------------------------------------------------------------------------------

SectorData* Terrain::VertexSector(int ix, int iy, int* index) {
    if (ix < 0 || iy < 0) return nullptr;
    if (width < static_cast<float>(ix) || height < static_cast<float>(iy)) return nullptr;

    const SplitVertex x = SplitOf(ix), y = SplitOf(iy);
    *index = x.vertex + y.vertex * kSectorRow;
    return SectorAt(x.sector, y.sector, SectorStage::Loaded, true);
}

const Vector3* Terrain::VertexAt(int ix, int iy, Layer layer) {
    int index = 0;
    SectorData* sector = VertexSector(ix, iy, &index);
    if (!sector) return nullptr;
    if (layer == Layer::Ground) return &sector->vertices[index];
    if (!sector->hasWater) return nullptr;
    return &sector->vertices[index + kSectorVertices];
}

Terrain::MaterialWaves Terrain::WavesOf(int material) const {
    const TerrainMaterial& m = materials[material];
    const float glow = waveGlow * m.priority * kGlowScale;
    return {std::fmod(wavePhase * m.priority, kFullTurn), waveHeight * m.priority,
            {glow * m.waveColour.x, glow * m.waveColour.y, glow * m.waveColour.z},
            m.kind == TerrainMaterial::kKeepsGroundXY};
}

Vector3 Terrain::WaterVertex(const SectorData& sector, int index) const {
    const Vector3& rest = sector.vertices[kSectorVertices + index];
    if (!lib::Has(landscapeFlags, LandscapeFlag::Waves) || std::ssize(sector.records) < 2 * kSectorVertices) return rest;
    const int material = sector.records[kSectorVertices + index].waterMaterial;
    if (material >= std::min(lib::Count(materials), kWaveMaterials)) return rest;
    const MaterialWaves waves = WavesOf(material);
    if (waves.keepsGroundXY) return rest;
    const int row = index / kSectorRow, column = index % kSectorRow;
    const Vector2 direction = XY(waveDirection);
    const float start = std::fmod(Dot2D(direction, XY(sector.vertices[kSectorVertices])) + kFullTurn, kFullTurn);
    const float along = start + waves.turn + row * direction.y + column * direction.x;
    const float lift = Sine(sine, along + wavePhases[index]) * waves.lift;
    return {Sine(sine, column * kQuarterTurn + waveTime) * waves.lift * kCircleScale + rest.x,
            Sine(sine, row * kQuarterTurn + waveTime) * waves.lift * kCircleScale + rest.y, lift * kRiseScale + rest.z};
}

float Terrain::HeightAt(float x, float y, Layer layer) {
    if (layer == Layer::Ground) return HeightOver(x, y, [&](int ix, int iy) { return Copy(VertexAt(ix, iy, layer)); });
    return HeightOver(x, y, [&](int ix, int iy) -> std::optional<Vector3> {
        int index = 0;
        const SectorData* sector = VertexSector(ix, iy, &index);
        if (!sector || !sector->hasWater) return std::nullopt;
        return WaterVertex(*sector, index);
    });
}

float Terrain::SurfaceHeight(float x, float y) {
    return AboveGround(HeightAt(x, y, Layer::Water), HeightAt(x, y, Layer::Ground));
}

float Terrain::CalmSurfaceHeight(float x, float y) {
    const float water = HeightOver(x, y, [&](int ix, int iy) { return Copy(VertexAt(ix, iy, Layer::Water)); });
    return AboveGround(water, HeightAt(x, y, Layer::Ground));
}

int Terrain::SurfaceClass(float x, float y) {
    int ix = lib::RoundEven(x);
    int iy = lib::RoundEven(y);
    if (ix < 0 || iy < 0 || ix >= sectorsX * kSectorCells || iy >= sectorsY * kSectorCells) return kOutsideClass;
    SectorData* sector = SectorAt(ix / kSectorCells, iy / kSectorCells, SectorStage::Built, true);
    int tile = (iy % kSectorCells) / 2 * kSectorTiles + (ix % kSectorCells) / 2;
    if (!sector) return kOutsideClass;
    int texture = sector->tileTextures[tile];
    if (sector->hasWater && sector->waterTiles[tile] != kNoMaterial) {
        float water = HeightAt(x, y, Layer::Water);
        const float ground = HeightAt(x, y, Layer::Ground);
        if (ground < water) texture = sector->tileTextures[tile + kLayerTiles];
    }
    return texture < 0 ? kOutsideClass : textureClass[texture];   // a tile without a texture: as outside
}

// --- Edge fade ---------------------------------------------------------------------------------------

// The fade of the light at the map's edges (terrain.h): from the sector build (the fog factor of the vertices'
// specular words), the figures' draws and the ground prints.

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

// --- Ground decal ------------------------------------------------------------------------------------

// The terrain part of a ground decal (the shadow projector). The decal quad is slid along the builder's slope
// until its lowest point matches the lowest ground vertex under its bounding cells; the ground cells of that
// rectangle then go to the builder as two triangles each.
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
        x0 = std::min(x0, bound);
        bound = CellBound(MinKeepA(MinKeepA(MinKeepA(fourthY, u.y), v.y), origin.y) - kMargin);
        y0 = std::min(y0, bound);
        bound = CellBound(MaxKeepB(origin.x, MaxKeepB(v.x, MaxKeepB(u.x, fourthX))) + kMargin);
        x1 = std::max(x1, bound);
        bound = CellBound(MaxKeepB(origin.y, MaxKeepB(v.y, MaxKeepB(u.y, fourthY))) + kMargin);
        y1 = std::max(y1, bound);
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, static_cast<int>(width - 1.0f));
        y1 = std::min(y1, static_cast<int>(height - 1.0f));
        previous = lowest;
        if (y0 > y1) break;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) lowest = MinKeepA(VertexAt(x, y, Layer::Ground)->z, lowest);
        }
        if (!(lowest < previous)) break;   // it no longer drops a NaN ends it too
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

// --- Draw passes -------------------------------------------------------------------------------------

// The landscape's draw passes (terrain.h), called by the client's draw after Terrain::ShowAllSectors: the ground
// pages, then (after the objects) the water pages. A pass pushes the landscape's matrix on the renderer's
// pipeline, prepares every visible sector (the clip test of the sector and its quarters; the water levels of a drawn
// sector with tiles in the pass) and hands the pass to the renderer (render::TerrainPass, which draws the zone's
// mesh of the layer whole, built from the sectors' data; the GPU clips what lies outside the view). Nothing here
// talks to the device: the last page's texture is left bound through the renderer's SetTexture, as page by page
// drawing left it.

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
    pipeline->PopMatrix(TransformPipeline::kClipStack);
    pipeline->PopMatrix(kProjectStack);
}

void Terrain::DrawPages(SceneRenderer* renderer, int firstPage, int lastPage) {
    if (!mapLoaded) return;
    // An extra page whose texture file is missing has no texture and is not drawn.
    lastPage = std::min(lastPage, firstPage + static_cast<int>(pageTextures.size()) - 1);
    // The sectors in view, clip tested and loaded; a drawn one with tiles on a page of the pass takes its pending
    // water levels.
    static std::vector<const SectorData*> drawn;
    drawn.clear();
    int shown = -1;   // the last page with tiles in view
    ForVisibleSectors(this, [&](SectorData* sector) {
        sector->ClipTest(renderer);
        if (!sector->IsDrawn()) return;
        drawn.push_back(sector);
        for (int page = lastPage; page >= firstPage; --page) {
            if (!HasTilesOn(sector, page)) continue;
            shown = std::max(shown, page);
            if (sector->hasWater) sector->ApplyWaterLevels();
            break;
        }
    });
    if (shown >= 0) renderer->SetTexture(0, pageTextures[static_cast<std::size_t>(shown % LayerPages())]);
    render::TerrainPass(this, renderer, firstPage, lastPage, drawn);
}

void Terrain::DrawGround(SceneRenderer* renderer) { DrawPass(renderer, 0, LayerPages() - 1); }

void Terrain::DrawWater(SceneRenderer* renderer) { DrawPass(renderer, LayerPages(), 2 * LayerPages() - 1); }

// --- Update ------------------------------------------------------------------------------------------

// The landscape's per-frame work: the update (RunTick) and the sun (SetSun). The animated tiles' steps are counted here
// and the GPU works every tile's frame out from them (terrain_stages.hlsli's tile_uv). The waves move nothing here:
// WaterVertex and the GPU work them out from the phase and the time.

void Terrain::SetSun(Light* light, const Vector4& colour) {
    ambient = colour;
    sun = light;
}

bool Terrain::RunTick() {
    if (lib::Has(landscapeFlags, LandscapeFlag::Ambience)) PlayAmbience();
    wavePhase += waveSpeed;
    waveTime = (g_clientWorld.tick + g_clientWorld.tickFraction) * kTimeScale;
    ++frame;
    if (frame % kTileFrameEvery == 0 && lib::Has(landscapeFlags, LandscapeFlag::AnimatedTiles)) ++tileSteps;
    return true;
}

} // namespace terrain
