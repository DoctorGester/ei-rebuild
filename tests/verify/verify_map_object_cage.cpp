// Checks for the cage interpolation (map_object_cage.cpp). Both write only the result point and
// advance the stream: ours, the point and the stream saved and restored, the original; compared.
#include "verify.h"

#include "../../src/game/map_object_cage.h"

#include <cstring>

namespace {
template <typename Ours, typename Original>
void SameResult(const char* name, CageInterpolator* self, const float** stream, Ours ours, Original original) {
    float before[3];
    std::memcpy(before, &self->point, sizeof(before));
    const float* start = *stream;
    ours();
    float after[3];
    std::memcpy(after, &self->point, sizeof(after));
    const float* end = *stream;
    std::memcpy(&self->point, before, sizeof(before));
    *stream = start;
    original();
    bool ok = std::memcmp(after, &self->point, sizeof(after)) == 0 && end == *stream;
    ei::verify::Check(name, true, ok, "%g %g %g vs %g %g %g", self->point.x, self->point.y, self->point.z, after[0],
                      after[1], after[2]);
}
} // namespace

// 0050B8C0: SUB ESP,0x3C; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_cube)(CageInterpolator*, void*, const float**);
static void __fastcall CubeVerify(CageInterpolator* self, void*, const float** stream) {
    SameResult("CageInterpolator::InterpolateCube", self, stream, [&] { self->InterpolateCube(stream); },
               [&] { s_cube(self, nullptr, stream); });
}
EI_VERIFY(0x0050B8C0, CubeVerify, 5, s_cube);

// 0050BC00: SUB ESP,0x30; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_prism)(CageInterpolator*, void*, const float**);
static void __fastcall PrismVerify(CageInterpolator* self, void*, const float** stream) {
    SameResult("CageInterpolator::InterpolatePrism", self, stream, [&] { self->InterpolatePrism(stream); },
               [&] { s_prism(self, nullptr, stream); });
}
EI_VERIFY(0x0050BC00, PrismVerify, 5, s_prism);
