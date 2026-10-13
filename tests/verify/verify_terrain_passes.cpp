// Verify wrappers for the landscape's draw passes (terrain_passes.cpp). Every scenario that shows
// the world reaches them each frame: the client's draw 00569E40 calls 006C6230 (ground) and
// 006C6330 (water), both through 006C6460, which calls 006C66E0 per visible sector.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstdint>

using terrain::SectorData;

namespace {

// 006C1390: MOV EAX,[ESP+4]; MOV [ECX+0x170],EAX (10 bytes). Reached from the original 006C66E0
// run by the wrapper below. The original, the level put back, ours; the level compared.
void(__fastcall* s_setDetail)(SectorData*, void*, int);

void __fastcall SetDetailVerify(SectorData* self, void*, int level) {
    const int before = self->unknown_170;
    s_setDetail(self, nullptr, level);
    const int expected = self->unknown_170;
    self->unknown_170 = before;
    self->SetDetail(level);
    ei::verify::Check("SectorData::SetDetail", expected, self->unknown_170, "level %d", level);
}
EI_VERIFY(0x006C1390, SetDetailVerify, 10, s_setDetail);

// 006C66E0: SUB ESP,0x18; MOV EDX,ECX (5 bytes). It only writes the sector's level of detail: the
// original, the level put back, ours; compared. A sector exactly at a limit distance could differ
// by a rounding (the original compares the unrounded sum; equal at the main thread's precision).
void(__fastcall* s_chooseDetail)(Terrain*, void*, SectorData*);

void __fastcall ChooseDetailVerify(Terrain* self, void*, SectorData* sector) {
    const int before = sector->unknown_170;
    s_chooseDetail(self, nullptr, sector);
    const int expected = sector->unknown_170;
    sector->unknown_170 = before;
    self->ChooseDetail(sector);
    ei::verify::Check("Terrain::ChooseDetail", expected, sector->unknown_170, "sector %d,%d min %d", sector->sx,
                      sector->sy, self->minDetail);
}
EI_VERIFY(0x006C66E0, ChooseDetailVerify, 5, s_chooseDetail);

// 006C6460: ours only (it draws; its callees SectorAt, ChooseDetail, Project and DrawSectorPage run
// through their own wrappers). Checked: 0 without a map, else 1 (a failed draw would show here).
int(__fastcall* s_drawPages)(Terrain*, void*, CSceneRenderer*, int, int);

int __fastcall DrawPagesVerify(Terrain* self, void*, CSceneRenderer* renderer, int firstPage, int lastPage) {
    const int result = self->DrawPages(renderer, firstPage, lastPage);
    ei::verify::Check("Terrain::DrawPages", self->unknown_100 ? 1 : 0, result, "pages %d..%d of %d", firstPage,
                      lastPage, self->pages);
    return result;
}
EI_VERIFY(0x006C6460, DrawPagesVerify, 0, s_drawPages);

// 006C6230 / 006C6330: ours only. Checked: the pipeline's stacks 1 and 2 are as deep as before, and
// the landscape's matrix carries its position in the last column under a (0, 0, 0, 1) row.
void CheckPass(const char* name, Terrain* self, CSceneRenderer* renderer, int depth1, int depth2) {
    const CTransformPipeline* pipeline = renderer->pipeline_;
    const float* m = self->drawMatrix.m;
    const bool matrix = m[3] == self->localPosition_.x && m[7] == self->localPosition_.y &&
                        m[11] == self->localPosition_.z && m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f &&
                        m[15] == 1.0f;
    ei::verify::Check(name, true, matrix && pipeline->stacks_[1].count == depth1 && pipeline->stacks_[2].count == depth2,
                      "matrix %d, stacks %d/%d and %d/%d", matrix, pipeline->stacks_[1].count, depth1,
                      pipeline->stacks_[2].count, depth2);
}

void(__fastcall* s_drawGround)(Terrain*, void*, CSceneRenderer*);

void __fastcall DrawGroundVerify(Terrain* self, void*, CSceneRenderer* renderer) {
    const int depth1 = renderer->pipeline_->stacks_[1].count;
    const int depth2 = renderer->pipeline_->stacks_[2].count;
    self->DrawGround(renderer);
    CheckPass("Terrain::DrawGround", self, renderer, depth1, depth2);
}
EI_VERIFY(0x006C6230, DrawGroundVerify, 0, s_drawGround);

void(__fastcall* s_drawWater)(Terrain*, void*, CSceneRenderer*);

void __fastcall DrawWaterVerify(Terrain* self, void*, CSceneRenderer* renderer) {
    const int depth1 = renderer->pipeline_->stacks_[1].count;
    const int depth2 = renderer->pipeline_->stacks_[2].count;
    self->DrawWater(renderer);
    CheckPass("Terrain::DrawWater", self, renderer, depth1, depth2);
}
EI_VERIFY(0x006C6330, DrawWaterVerify, 0, s_drawWater);

} // namespace
