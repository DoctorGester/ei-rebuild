// Checks for the renderer's sprite draw (renderer_sprites.cpp).
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>
#include <vector>

struct SpriteSortRecord {
    std::int32_t key;
    SpriteSortRecord* next;
    TLVertex* corners;
    std::uint32_t outcodes;
};

void __fastcall SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending);
void __fastcall SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count);

namespace {
struct AlignedArray {
    char* data;
    int size;
    int capacity;
    int growBy;
};
} // namespace

// 00503710: PUSH EBP; MOV EBP,ESP; SUB ESP,408h (9 bytes). The original writes into a scratch list
// first; both rebuild the records' links the same way. The two lists are compared.
static void(__fastcall* s_radix)(int, SpriteSortRecord* const*, SpriteSortRecord**, int, int);
static void __fastcall RadixPassVerify(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                       int ascending) {
    std::vector<SpriteSortRecord*> expected(count > 0 ? count : 1);
    s_radix(shift, src, expected.data(), count, ascending);
    SpriteRadixPass(shift, src, dst, count, ascending);
    bool same = count <= 0 || std::memcmp(expected.data(), dst, count * sizeof(SpriteSortRecord*)) == 0;
    ei::verify::Check("SpriteRadixPass", true, same, "shift %d count %d ascending %d", shift, count, ascending);
}
EI_VERIFY(0x00503710, RadixPassVerify, 9, s_radix);

// 005037D0: SUB ESP,28h; PUSH EBX; MOV EAX,[ESP+30h] (8 bytes). Pure: the original into a scratch
// list, ours into the real one; every slot is written.
static void(__fastcall* s_merge)(int, SpriteSortRecord* const*, SpriteSortRecord**, int);
static void __fastcall MergePassVerify(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count) {
    std::vector<SpriteSortRecord*> expected(count > 0 ? count : 1);
    s_merge(width, src, expected.data(), count);
    SpriteMergePass(width, src, dst, count);
    bool same = count <= 0 || std::memcmp(expected.data(), dst, count * sizeof(SpriteSortRecord*)) == 0;
    ei::verify::Check("SpriteMergePass", true, same, "width %d count %d", width, count);
}
EI_VERIFY(0x005037D0, MergePassVerify, 8, s_merge);

// 00503680 draws: ours only; the draw call counter goes up by one.
static void* s_drawIndexed;
static int __fastcall DrawIndexedVerify(CTextureDevice* device, void*, int primitive, int fvf, void* vertices,
                                        int vertexCount, std::uint16_t* indices, int indexCount, int flags) {
    int calls = device->drawCount_;
    int result = device->DrawIndexedDirect(primitive, fvf, vertices, vertexCount, indices, indexCount, flags);
    ei::verify::Check("DrawIndexed", calls + 1, device->drawCount_, "vertices %d indices %d", vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x00503680, DrawIndexedVerify, 0, s_drawIndexed);

// 00502CA0 draws: ours only. The result is 0 or 1, and the last batch's corners (007AEF90) are back
// to front: whole sprites whose first corners' z never grows and is not negative.
static void* s_sprites;
static int __fastcall DrawSpritesVerify(CSceneRenderer* renderer, void*, char additive, const void* pointArray,
                                        const void* spriteArray) {
    int result = renderer->DrawSpritesImpl(additive, pointArray, spriteArray);
    int points = static_cast<const int*>(pointArray)[1];
    bool ok = result == 0 || result == 1;
    if (points > 0) {
        const auto& corners = *reinterpret_cast<const AlignedArray*>(0x007AEF90);
        const auto* v = reinterpret_cast<const TLVertex*>(corners.data);
        ok = ok && corners.size % 4 == 0;
        std::int32_t last = 0x7FFFFFFF;
        for (int i = 0; ok && i < corners.size; i += 4) {
            std::int32_t key;
            std::memcpy(&key, &v[i].z, 4);
            ok = key >= 0 && key <= last;
            last = key;
        }
    }
    ei::verify::Check("DrawSprites", true, ok, "points %d additive %d result %d", points, additive != 0, result);
    return result;
}
EI_VERIFY(0x00502CA0, DrawSpritesVerify, 0, s_sprites);
