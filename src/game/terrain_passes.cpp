// The landscape's draw passes (terrain.h), called by the client's draw after Terrain::SetVisibleSectors:
// the ground pages, then (after the objects) the water pages. A pass pushes the landscape's matrix on the
// renderer's pipeline, prepares every visible sector (level of detail, clip test of the sector and its quarters,
// seams) and hands the sectors page by page to the renderer (render::TerrainPage, which builds the page's tiles
// from the sector's data); the GPU clips what crosses the view's edges. Nothing here talks to the device: the texture goes through the renderer's SetTexture.
#include "camera.h"
#include "game_singletons.h"
#include "model.h"
#include "options.h"
#include "render/render.h"
#include "terrain.h"
#include "terrain_sector.h"

#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <cstdint>

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

// The sector's level of detail from the squared distance of its centre (mid height) to the camera: 0 within
// the options' lod1, 1 within lod2, else 2.
void ChooseDetail(const Terrain* terrain, SectorData* sector) {
    const Vector3 centre = {(static_cast<float>(sector->sx) + 0.5f) * kSectorCells,
                            (static_cast<float>(sector->sy) + 0.5f) * kSectorCells,
                            (sector->maxZ + sector->minZ) * 0.5f};
    // (z^2 + y^2) + x^2. The sum is kept unrounded; the passes run on the main thread (24-bit precision),
    // where the helper's stores change nothing.
    const float distanceSquared = DistanceSquaredZYX(centre + terrain->localPosition, g_camera.localPosition);
    int level = 2;
    EI_NAN_AUDIT(distanceSquared);
    if (distanceSquared < options::g_options.lod1 * options::g_options.lod1) level = 0;
    else if (distanceSquared < options::g_options.lod2 * options::g_options.lod2) level = 1;
    sector->detail = level;
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

void SectorData::FindSeams() {
    seamAfter = false;
    seamBefore = false;
    // A built neighbour drawn at the next coarser level: this sector's edge towards it is drawn with
    // the seam triangles (render_terrain.cpp). The lookup does not load anything.
    const auto coarser = [&](int nx, int ny, int level) {
        const SectorData* neighbour = owner->SectorAt(nx, ny, SectorStage::Built, false);
        return neighbour && neighbour->detail == level && neighbour->IsDrawn();
    };
    if (detail == 0) {
        if (coarser(sx, sy - 1, 1)) seamBefore = true;
        if (coarser(sx, sy + 1, 1)) seamAfter = true;
    }
    if (detail == 1) {
        if (coarser(sx - 1, sy, 2)) seamBefore = true;
        if (coarser(sx + 1, sy, 2)) seamAfter = true;
    }
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
    ForVisibleSectors(this, [&](SectorData* sector) {
        ChooseDetail(this, sector);
        sector->ClipTest(renderer);
    });
    ForVisibleSectors(this, [&](SectorData* sector) {
        if (sector->IsDrawn()) sector->FindSeams();
    });
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
