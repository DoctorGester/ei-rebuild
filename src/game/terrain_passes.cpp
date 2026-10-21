// The landscape's draw passes (006C6230-006C66E0; terrain.h), called by the client's draw 00569E40
// after Terrain::SetVisibleSectors: the ground pages, then (after the objects) the water pages. A
// pass pushes the landscape's matrix on the renderer's pipeline, prepares every visible sector
// (level of detail, clip test, seams, projection) and draws the sectors page by page through
// DrawSectorPage (terrain_draw.cpp). Nothing here talks to the device: the texture goes through
// the renderer's SetTexture and the vertices through DrawSectorPage.
#include "terrain.h"
#include "terrain_sector.h"
#include "camera.h"
#include "model.h"

#include "ei/hook.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstdint>

using terrain::SectorData;

namespace {

constexpr float kSectorCells = 32.0f;   // 0073CA90: a sector's width in cells
constexpr std::uint32_t kLit = 4;       // SectorAt's stage: the sector is lit (and so built)
constexpr int kProjectStack = 1;        // the pipeline stack on-screen vertices are projected with

// The squared-distance limits of the levels of detail 0 and 1 (0079BD2C / 0079BD30).
float DetailNear() { return ei::GameData<const float>(0x0079BD2C); }
float DetailFar() { return ei::GameData<const float>(0x0079BD30); }


// Calls `visit(sector)` for every visible sector (SetVisibleSectors' rows and columns) that is
// there once lit; a visit returning false ends the walk (false).
template <typename Visit>
bool ForVisibleSectors(Terrain* terrain, Visit visit) {
    for (int row = 0; row < terrain->visibleColumnFirst.size; ++row) {
        for (int column = terrain->visibleColumnFirst[row]; column <= terrain->visibleColumnLast[row]; ++column) {
            SectorData* sector = terrain->SectorAt(column, terrain->visibleRowFirst + row, kLit, 1);
            if (sector && !visit(sector)) return false;
        }
    }
    return true;
}

bool HasTilesOn(const SectorData* sector, int page) {
    for (const auto& quarter : sector->tiles)
        if (quarter[page].size != 0) return true;
    return false;
}

// The body the two passes share: the landscape's matrix (rotation, the position in the last
// column) on the stacks 2 and 1 around the pages' draw.
void DrawPass(Terrain* terrain, CSceneRenderer* renderer, int firstPage, int lastPage) {
    Matrix4& matrix = terrain->drawMatrix;
    terrain->localRotation_.ToMatrix(matrix.m);
    matrix.m[3] = terrain->localPosition_.x;
    matrix.m[7] = terrain->localPosition_.y;
    matrix.m[11] = terrain->localPosition_.z;
    matrix.m[12] = matrix.m[13] = matrix.m[14] = 0.0f;
    matrix.m[15] = 1.0f;
    CTransformPipeline* pipeline = renderer->pipeline_;
    pipeline->PushMatrixProduct(matrix, CTransformPipeline::kClipStack);
    pipeline->PushMatrixProduct(matrix, kProjectStack);
    terrain->DrawPages(renderer, firstPage, lastPage);
    pipeline = renderer->pipeline_;
    pipeline->PopMatrix(CTransformPipeline::kClipStack);
    pipeline->PopMatrix(kProjectStack);
}

} // namespace

void SectorData::SetDetail(int level) { detail = level; }

// --- The sector's part of the passes (006BFB00-006BFC7B; A-Z1) -------------------------------------

int SectorData::IsDrawn() const { return clipMode == 0 || clipMode == terrain::kCrossing ? 1 : 0; }

void SectorData::ClipTest(CSceneRenderer* renderer) {
    // The verdict as it is stored: -1 counts as outside.
    const auto verdict = [&](const terrain::SectorBox& bounds) {
        const int where = renderer->pipeline_->ClipTest(terrain::BoundsOf(bounds));
        return where == -1 ? terrain::kCulled : where;
    };
    clipMode = verdict(box);
    if (clipMode == terrain::kCulled) return;
    for (int q = 0; q < 4; ++q) quarterMode[q] = verdict(quarters[q]);
}

void SectorData::FindSeams() {
    seamAfter = 0;
    seamBefore = 0;
    // A lit neighbour drawn at the next coarser level: this sector's edge towards it is drawn with
    // the seam triangles (terrain_draw.cpp). The lookup does not load anything (create 0).
    const auto coarser = [&](int nx, int ny, int detail) {
        const SectorData* neighbour = reinterpret_cast<Terrain*>(owner)->SectorAt(nx, ny, kLit, 0);
        return neighbour && neighbour->detail == detail && neighbour->IsDrawn();
    };
    if (detail == 0) {
        if (coarser(sx, sy - 1, 1)) seamBefore = 1;
        if (coarser(sx, sy + 1, 1)) seamAfter = 1;
    }
    if (detail == 1) {
        if (coarser(sx - 1, sy, 2)) seamBefore = 1;
        if (coarser(sx + 1, sy, 2)) seamAfter = 1;
    }
}

void Terrain::ChooseDetail(SectorData* sector) {
    const Vector3 centre = {(static_cast<float>(sector->sx) + 0.5f) * kSectorCells,
                            (static_cast<float>(sector->sy) + 0.5f) * kSectorCells,
                            (sector->maxZ + sector->minZ) * 0.5f};
    // (z^2 + y^2) + x^2. The original keeps the sum unrounded; the passes run on the main thread
    // (24-bit precision), where the helper's stores change nothing.
    const float distanceSquared = DistanceSquaredZYX(centre + localPosition_, TheCamera()->localPosition_);
    int level = 2;
    EI_NAN_AUDIT(distanceSquared);
    if (distanceSquared < DetailNear() * DetailNear() && minDetail <= 0) level = 0;
    else if (distanceSquared < DetailFar() * DetailFar() && minDetail <= 1) level = 1;
    sector->SetDetail(level);
}
EI_REPLACE_METHOD(0x006C66E0, Terrain::ChooseDetail);

int Terrain::DrawPages(CSceneRenderer* renderer, int firstPage, int lastPage) {
    if (!mapLoaded) return 0;
    ForVisibleSectors(this, [&](SectorData* sector) {
        ChooseDetail(sector);
        sector->ClipTest(renderer);
        return true;
    });
    ForVisibleSectors(this, [&](SectorData* sector) {
        if (sector->IsDrawn()) {
            sector->FindSeams();
            sector->Project(renderer->pipeline_);
        }
        return true;
    });
    for (int page = firstPage; page <= lastPage; ++page) {
        bool bound = false;
        bool drawn = ForVisibleSectors(this, [&](SectorData* sector) {
            if (!sector->IsDrawn() || !HasTilesOn(sector, page)) return true;
            if (!bound) {
                renderer->SetTexture(0, pageTextures[page % (pages + 2)]);
                bound = true;
            }
            return sector->DrawPage(renderer, this, page, 0) != 0;
        });
        if (!drawn) return 0;
    }
    return 1;
}

void Terrain::DrawGround(CSceneRenderer* renderer) { DrawPass(this, renderer, 0, pages + 1); }

void Terrain::DrawWater(CSceneRenderer* renderer) { DrawPass(this, renderer, pages + 2, pages * 2 + 3); }
