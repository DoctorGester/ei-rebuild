// Checks for line_batch.cpp (A-E1). Debug drawing: no scenario reaches either (the figure parts'
// bounds need the show-bounds flag, the world client's line lists are empty).
//  - LineBatch::AddPoint: side by side on a copy of the list (its packets copied into a block of
//    the same capacity); the lists' fields and the packets in use are compared.
//  - LineBatch::Draw: ours only (it draws); the result and the work arrays' sizes.
#include "verify.h"

#include "../../src/game/renderer_lines.h"

#include <cstring>

namespace {
void* s_unused;

// 0049B140: SUB ESP,8 (3), PUSH EBX (1), MOV EBX,ECX (2).
int(__fastcall* s_addPoint)(LineBatch*, void*, const float*);
int __fastcall AddPointVerify(LineBatch* self, void*, const float* point) {
    LineBatch copy = *self;
    if (self->packets.data) {
        copy.packets.data = lib::AlignedArray<LinePacket>::Allocate(self->packets.capacity);
        std::memcpy(copy.packets.data, self->packets.data, static_cast<std::size_t>(self->packets.size) * sizeof(LinePacket));
    }
    int expected = s_addPoint(&copy, nullptr, point);
    int actual = self->AddPoint(point);
    bool same = expected == actual && copy.unknown_00 == self->unknown_00 && copy.packets.size == self->packets.size &&
                copy.packets.capacity == self->packets.capacity && copy.packets.growBy == self->packets.growBy &&
                copy.count == self->count && copy.colour == self->colour && copy.unknown_1C == self->unknown_1C &&
                (copy.packets.data != nullptr) == (self->packets.data != nullptr);
    if (same && self->packets.data)
        same = std::memcmp(copy.packets.data, self->packets.data,
                           static_cast<std::size_t>(self->packets.size) * sizeof(LinePacket)) == 0;
    ei::verify::Check("LineBatch::AddPoint", true, same, "%d points (original %d)", actual, expected);
    if (copy.packets.data) lib::AlignedArray<LinePacket>::Free(copy.packets.data);
    return actual;
}

int __fastcall DrawVerify(LineBatch* self, void*, CTransformPipeline* pipeline, CTextureDevice* device) {
    int result = self->Draw(pipeline, device);
    ei::verify::Check("LineBatch::Draw", true,
                      result == 1 && LineProjected().size == self->packets.size * 4 &&
                          LineOutcodes().size == self->packets.size * 4,
                      "%d packets", self->packets.size);
    return result;
}
} // namespace

EI_VERIFY(0x0049B140, AddPointVerify, 6, s_addPoint);
EI_VERIFY(0x0049B780, DrawVerify, 0, s_unused);
