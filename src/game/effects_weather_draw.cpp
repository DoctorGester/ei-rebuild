// The visibility fog's draw (its cells as sprites through the scene renderer) and the rain / snow layer's work
// arrays. See docs/notes/renderer.md (section 12).
#include "effects.h"
#include "at_exit_functions.h"
#include "figure_mesh.h"   // VertexPacket

#include "clip.h"         // TLVertex
#include "core_maths.h"
#include "model.h"        // CSceneRenderer, CTransformPipeline

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

// ---- The visibility fog ---------------------------------------------------------------------------------

namespace {

// A cell's sprite: its colour and its tile of the fog texture.
struct FogSprite {
    std::uint32_t colour;
    const FogTile* tile;
};

// The draw's work arrays (function-local statics): the sprites' centres and sizes and their records (a CArray of
// SpriteArrayClass), bit 0 / bit 1 of the guard set once the records / the points are constructed. Exit handlers
// free the records and the points.
constinit lib::AlignedArray<ParticleVertex> g_points = /*007ABA30*/ {};
lib::AlignedArray<ParticleVertex>& Points() { return g_points; }
void __cdecl FreePoints() { g_points.FreeData(); }
constinit lib::StaticObject<mfc::CArray<FogSprite>> g_sprites = /*007ABA40*/ {};
mfc::CArray<FogSprite>& Sprites() { return *g_sprites; }
void __cdecl DestroySprites() { mfc::impl::DestructArrayOf<mfc::impl::SpriteArrayClass>(*g_sprites); }
std::uint8_t g_workArrayGuards = /*007ABA54*/ {};
constexpr std::uint8_t kSpritesConstructed = 1;
constexpr std::uint8_t kPointsConstructed = 2;

constexpr int kWorkRecords = 0x1000;      // the arrays' first size
constexpr unsigned kBoundsCells = 12000;  // only a fog of more cells gets its bounds computed here
constexpr float kFogSpriteSize = 0.4f;
constexpr float kColourScale = 255.0f;
constexpr int kOutOfView = 2;

} // namespace

int CVisibilityFog::Draw(CSceneRenderer* renderer) {
    if (cells.Size() == 0) return 1;
    if (cells.Size() > kBoundsCells && !boundsValid) ComputeBounds();
    if (renderer->pipeline_->ClipTest(&bounds) == kOutOfView) return 1;

    if (!(g_workArrayGuards & kSpritesConstructed)) {
        g_workArrayGuards |= kSpritesConstructed;
        Sprites().Construct(mfc::impl::SpriteArrayClass::Vtable());
        libcrt::AtExit(DestroySprites);
    }
    if (!(g_workArrayGuards & kPointsConstructed)) {
        g_workArrayGuards |= kPointsConstructed;
        Points().Construct();
        libcrt::AtExit(FreePoints);
    }
    Sprites().SetSize(kWorkRecords);
    Points().SetSize(kWorkRecords);
    Sprites().SetSize(static_cast<int>(cells.Size()));
    Points().SetSize(static_cast<int>(cells.Size()));

    int remaining = static_cast<int>(cells.Size());
    ParticleVertex* point = Points().data;
    FogSprite* sprite = Sprites().data;
    for (CellMap::Node* node = cells.First(); node; node = cells.After(node)) {
        if (--remaining < 0) break;
        const FogCell& cell = node->value;
        std::memcpy(point, &cell.pos, sizeof(cell.pos));
        point->size = kFogSpriteSize;
        const int alpha = x87::fistp(static_cast<float>(cell.alpha * kColourScale));
        sprite->colour = (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        sprite->tile = g_fogTiles + cell.frame;
        ++point;
        ++sprite;
    }
    return renderer->DrawSprites(0, &Points(), &Sprites());
}
EI_REWRITTEN(0x004E1EB0, CVisibilityFog::Draw);

// ---- Rain and snow ----------------------------------------------------------------------------------------

namespace {

constexpr int kDrops = 0x8000;            // drops (flakes) at most; each is a triangle
constexpr int kDropVertices = kDrops * 3;
constexpr int kRings = 40;                // the offsets below: 40, -40, 39, -39, ..., 1, -1, 0

// A drop's triangle on the 4 x 4 "RainSnow" texture's first tile: the three corners inside the
// tile (u from the left, v from the top of the tile, which is the texture's bottom row upwards).
struct Corner {
    float u, v;
};
constexpr Corner kCorners[3] = {{0.02f, 0.02f}, {0.98f, 0.02f}, {0.5f, 0.98f}};
constexpr float kTile = 0.25f;

} // namespace

void CRainSnow::PrepareBuffers() {
    positions.SetSize(kDrops);
    vertices.SetSize(kDropVertices);
    frames.SetSize(kDrops, -1);

    offsets.SetSize(2 * kRings + 1);
    for (int i = 0; i < kRings; ++i) {
        offsets[2 * i] = kRings - i;
        offsets[2 * i + 1] = -offsets[2 * i];
    }
    offsets[offsets.size - 1] = 0;

    // Every vertex's fixed part: white, no specular, its corner's texture coordinates.
    for (int i = 0; i < vertices.size; ++i) {
        TLVertex& vertex = vertices[i];
        const Corner& corner = kCorners[i % 3];
        vertex.color = 0xFFFFFFFFu;
        vertex.specular = 0xFF000000u;
        vertex.u = corner.u * kTile;
        vertex.v = 1.0f - (1.0f - corner.v) * kTile;
    }

    indices.SetSize(kDropVertices, -1);
    for (int i = 0; i < indices.size; ++i) indices.data[i] = static_cast<std::uint16_t>(i);
}
EI_REWRITTEN(0x004E3C20, CRainSnow::PrepareBuffers);
EI_REPLACE(0x004E2260, FreePoints);

EI_REPLACE(0x004E2280, DestroySprites);
