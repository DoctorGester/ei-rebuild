// The visibility fog's draw (004E1EB0: its cells as sprites through the scene renderer) and the
// rain / snow layer's work arrays (004E3C20). A-S12; see docs/notes/renderer.md (section 12).
#include "effects.h"
#include "figure_mesh.h"   // VertexPacket

#include "clip.h"         // TLVertex
#include "core_maths.h"
#include "model.h"        // CSceneRenderer, CTransformPipeline

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "originals_library.h"
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

// The draw's work arrays, statics of the original function: the sprites' centres and sizes
// (007ABA30) and their records (007ABA40, a CArray with the vtable 0073C584), bit 0 / bit 1 of
// 007ABA54 set once the records / the points are constructed. The exit handlers that free them
// stay in the exe (004E2280, 004E2260).
using FogPointArray = lib::AlignedArray<ParticleVertex>;
using FogSpriteArray = mfc::CArray<FogSprite>;
FogPointArray& Points() { return ei::GameData<FogPointArray>(0x007ABA30); }
FogSpriteArray& Sprites() { return ei::GameData<FogSpriteArray>(0x007ABA40); }
std::uint8_t& WorkArrayGuards() { return ei::GameData<std::uint8_t>(0x007ABA54); }
constexpr std::uint8_t kSpritesConstructed = 1;
constexpr std::uint8_t kPointsConstructed = 2;

constexpr int kWorkRecords = 0x1000;      // the arrays' first size
constexpr unsigned kBoundsCells = 12000;  // only a fog of more cells gets its bounds computed here
constexpr float kFogSpriteSize = 0.4f;    // 0073D6B0
constexpr float kColourScale = 255.0f;    // 0073C370
constexpr int kOutOfView = 2;

} // namespace

int CVisibilityFog::Draw(CSceneRenderer* renderer) {
    if (cells.Size() == 0) return 1;
    if (cells.Size() > kBoundsCells && !boundsValid) ComputeBounds();
    if (renderer->pipeline_->ClipTest(reinterpret_cast<const ClipBounds*>(&boundsKind)) == kOutOfView) return 1;

    if (!(WorkArrayGuards() & kSpritesConstructed)) {
        WorkArrayGuards() |= kSpritesConstructed;
        Sprites().Construct(0x0073C584);
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x004E2280));
    }
    if (!(WorkArrayGuards() & kPointsConstructed)) {
        WorkArrayGuards() |= kPointsConstructed;
        Points().Construct();
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x004E2260));
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
        sprite->tile = FogTiles() + cell.frame;
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
constexpr float kTile = 0.25f;   // 0073C3B0

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
