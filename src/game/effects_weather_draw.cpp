// The visibility fog's draw (its cells as sprites through the scene renderer) and the rain / snow layer's work
// arrays. See docs/notes/renderer.md (section 12).
#include "clip.h"   // TLVertex
#include "core_maths.h"
#include "effects.h"
#include "figure_mesh.h"   // VertexPacket
#include "model.h"         // CSceneRenderer, CTransformPipeline

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/static_object.h"

#include "ei/x87.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// ---- The visibility fog ---------------------------------------------------------------------------------

namespace {

// A cell's sprite: its colour and its tile of the fog texture.
struct FogSprite {
    std::uint32_t colour;
    const FogTile* tile;
};

// The draw's work arrays: the sprites' centres and sizes and their records.
std::vector<ParticleVertex> g_points;
std::vector<FogSprite> g_sprites;

constexpr int kWorkRecords = 0x1000;      // the arrays' first size
constexpr unsigned kBoundsCells = 12000;  // only a fog of more cells gets its bounds computed here
constexpr float kFogSpriteSize = 0.4f;
constexpr float kColourScale = 255.0f;
constexpr int kOutOfView = 2;

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

int CVisibilityFog::Draw(CSceneRenderer* renderer) {
    if (cells.size() == 0) return 1;
    if (cells.size() > kBoundsCells && !boundsValid) ComputeBounds();
    if (renderer->pipeline_->ClipTest(&bounds) == kOutOfView) return 1;

    g_sprites.resize(kWorkRecords);
    g_points.resize(kWorkRecords);
    g_sprites.resize(static_cast<int>(cells.size()));
    g_points.resize(static_cast<int>(cells.size()));

    int remaining = static_cast<int>(cells.size());
    ParticleVertex* point = g_points.data();
    FogSprite* sprite = g_sprites.data();
    for (auto& node : cells) {
        if (--remaining < 0) break;
        const FogCell& cell = node.second;
        lib::StoreBytes(point, cell.pos);
        point->size = kFogSpriteSize;
        const int alpha = x87::fistp(static_cast<float>(cell.alpha * kColourScale));
        sprite->colour = (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        sprite->tile = g_fogTiles + cell.frame;
        ++point;
        ++sprite;
    }
    return renderer->DrawSprites(0, &g_points, &g_sprites);
}

// ---- Rain and snow ----------------------------------------------------------------------------------------

void CRainSnow::PrepareBuffers() {
    positions.resize(kDrops);
    vertices.resize(kDropVertices);
    frames.resize(kDrops);

    offsets.resize(2 * kRings + 1);
    for (int i = 0; i < kRings; ++i) {
        offsets[2 * i] = kRings - i;
        offsets[2 * i + 1] = -offsets[2 * i];
    }
    offsets[std::ssize(offsets) - 1] = 0;

    // Every vertex's fixed part: white, no specular, its corner's texture coordinates.
    for (int i = 0; i < std::ssize(vertices); ++i) {
        TLVertex& vertex = vertices[i];
        const Corner& corner = kCorners[i % 3];
        vertex.color = 0xFFFFFFFFu;
        vertex.specular = 0xFF000000u;
        vertex.u = corner.u * kTile;
        vertex.v = 1.0f - (1.0f - corner.v) * kTile;
    }

    indices.resize(kDropVertices);
    for (int i = 0; i < std::ssize(indices); ++i) indices[i] = static_cast<std::uint16_t>(i);
}
