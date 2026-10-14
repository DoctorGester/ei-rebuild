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
    const int before = self->detail;
    s_setDetail(self, nullptr, level);
    const int expected = self->detail;
    self->detail = before;
    self->SetDetail(level);
    ei::verify::Check("SectorData::SetDetail", expected, self->detail, "level %d", level);
}
EI_VERIFY(0x006C1390, SetDetailVerify, 10, s_setDetail);

// 006C66E0: SUB ESP,0x18; MOV EDX,ECX (5 bytes). It only writes the sector's level of detail: the
// original, the level put back, ours; compared. A sector exactly at a limit distance could differ
// by a rounding (the original compares the unrounded sum; equal at the main thread's precision).
void(__fastcall* s_chooseDetail)(Terrain*, void*, SectorData*);

void __fastcall ChooseDetailVerify(Terrain* self, void*, SectorData* sector) {
    const int before = sector->detail;
    s_chooseDetail(self, nullptr, sector);
    const int expected = sector->detail;
    sector->detail = before;
    self->ChooseDetail(sector);
    ei::verify::Check("Terrain::ChooseDetail", expected, sector->detail, "sector %d,%d min %d", sector->sx,
                      sector->sy, self->minDetail);
}
EI_VERIFY(0x006C66E0, ChooseDetailVerify, 5, s_chooseDetail);

// The sector's part of the passes (A-Z1), reached from DrawPages (which calls the three through
// their addresses) each frame for every visible sector.

// 006BFB00: MOV EAX,[ECX+0x60]; TEST EAX,EAX (5 bytes). Pure.
int(__fastcall* s_isDrawn)(const SectorData*);

int __fastcall IsDrawnVerify(const SectorData* self) {
    const int expected = s_isDrawn(self);
    ei::verify::Check("SectorData::IsDrawn", expected, self->IsDrawn(), "+0x60 %u", self->clipMode);
    return expected;
}
EI_VERIFY(0x006BFB00, IsDrawnVerify, 5, s_isDrawn);

// 006BFB20: PUSH EBP; MOV EBP,[ESP+8] (5 bytes). It writes the sector's verdict and, unless that is
// "culled", its quarters' (the pipeline's test is pure): the original, the five values put back,
// ours; all five compared (a culled sector's quarters are the ones both runs started from).
struct ClipVerdicts {
    int sector;
    int quarters[4];
};

ClipVerdicts ReadVerdicts(const SectorData* sector) {
    return {sector->clipMode,
            {sector->quarterMode[0], sector->quarterMode[1], sector->quarterMode[2], sector->quarterMode[3]}};
}

void WriteVerdicts(SectorData* sector, const ClipVerdicts& verdicts) {
    sector->clipMode = verdicts.sector;
    for (int q = 0; q < 4; ++q) sector->quarterMode[q] = verdicts.quarters[q];
}

void(__fastcall* s_clipTest)(SectorData*, void*, CSceneRenderer*);

void __fastcall ClipTestVerify(SectorData* self, void*, CSceneRenderer* renderer) {
    const ClipVerdicts before = ReadVerdicts(self);
    s_clipTest(self, nullptr, renderer);
    const ClipVerdicts expected = ReadVerdicts(self);
    WriteVerdicts(self, before);
    self->ClipTest(renderer);
    const ClipVerdicts actual = ReadVerdicts(self);
    bool same = expected.sector == actual.sector;
    for (int q = 0; q < 4; ++q) same = same && expected.quarters[q] == actual.quarters[q];
    ei::verify::Check("SectorData::ClipTest", true, same, "sector %d,%d: %u [%d %d %d %d] / ours %u [%d %d %d %d]", self->sx,
                      self->sy, expected.sector, expected.quarters[0], expected.quarters[1], expected.quarters[2],
                      expected.quarters[3], actual.sector, actual.quarters[0], actual.quarters[1], actual.quarters[2],
                      actual.quarters[3]);
}
EI_VERIFY(0x006BFB20, ClipTestVerify, 5, s_clipTest);

// 006BFB80: PUSH EBP; PUSH ESI; MOV ESI,ECX; MOV EBP,1 (9 bytes). It writes the two seam flags:
// the original, the flags put back, ours; compared. Its neighbour lookups (SectorAt without
// create) only move a sector to the cache's front once per frame, which the second run finds done.
void(__fastcall* s_findSeams)(SectorData*);

void __fastcall FindSeamsVerify(SectorData* self) {
    const int before174 = self->seamBefore, before178 = self->seamAfter;
    s_findSeams(self);
    const int expected174 = self->seamBefore, expected178 = self->seamAfter;
    self->seamBefore = before174;
    self->seamAfter = before178;
    self->FindSeams();
    ei::verify::Check("SectorData::FindSeams", true, expected174 == self->seamBefore && expected178 == self->seamAfter,
                      "sector %d,%d detail %d: %d %d / ours %d %d", self->sx, self->sy, self->detail, expected174,
                      expected178, self->seamBefore, self->seamAfter);
}
EI_VERIFY(0x006BFB80, FindSeamsVerify, 9, s_findSeams);

// 006C6460: ours only (it draws; its callees SectorAt, ChooseDetail, Project and DrawSectorPage run
// through their own wrappers). Checked: 0 without a map, else 1 (a failed draw would show here).
int(__fastcall* s_drawPages)(Terrain*, void*, CSceneRenderer*, int, int);

int __fastcall DrawPagesVerify(Terrain* self, void*, CSceneRenderer* renderer, int firstPage, int lastPage) {
    const int result = self->DrawPages(renderer, firstPage, lastPage);
    ei::verify::Check("Terrain::DrawPages", self->mapLoaded ? 1 : 0, result, "pages %d..%d of %d", firstPage,
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
