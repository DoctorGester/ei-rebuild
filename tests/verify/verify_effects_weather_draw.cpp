// Checks for effects_weather_draw.cpp (A-S12). Main thread.
//  - CVisibilityFog::Draw (004E1EB0; a scene with a visibility fog, the "show visibility" effect):
//    side by side. Each run gets a stand-in renderer with the real pipeline: ours' passes the
//    sprites on to the real renderer, the original's only notes the call and returns ours' result.
//    Compared: the result, whether the sprites were drawn and with what arguments, the work arrays'
//    sizes and the points and sprite records written, and the fog object afterwards. spells_hero
//    (vision fog cast on a wolf) reaches it with cells.
//  - CRainSnow::PrepareBuffers (004E3C20; the client world's setup, every game start): side by
//    side, the original on a copy of the layer with empty arrays (freed afterwards). Compared: the
//    arrays' sizes, the offsets, the indices and the vertices' fixed fields (their positions are
//    not written).
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/effects.h"
#include "../../src/game/model.h"
#include "../../src/lib/containers.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// ---- The fog's draw ----

struct FogSprite {
    std::uint32_t colour;
    const FogTile* tile;
};
lib::AlignedArray<ParticleVertex>& FogPoints() { return *reinterpret_cast<lib::AlignedArray<ParticleVertex>*>(0x007ABA30); }
mfc::CArray<FogSprite>& FogSprites() { return *reinterpret_cast<mfc::CArray<FogSprite>*>(0x007ABA40); }

// A renderer as far as the draw reads it: the vtable (slot 17) and the pipeline.
struct StandInRenderer {
    void** vtable;
    std::uint32_t flags;
    CTransformPipeline* pipeline;
    CSceneRenderer* real;   // null: only note the call
    int result;             // what the noted call returns
    int calls;
    int first;
    void* points;
    void* sprites;
};
int __fastcall StandInDrawSprites(StandInRenderer* self, void*, int first, void* points, void* sprites) {
    ++self->calls;
    self->first = first;
    self->points = points;
    self->sprites = sprites;
    if (self->real) self->result = self->real->DrawSprites(first, points, sprites);
    return self->result;
}
void** StandInVtable() {
    static void* slots[26];
    slots[17] = reinterpret_cast<void*>(&StandInDrawSprites);
    return slots;
}

// 004E1EB0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x1C (3).
int(__fastcall* s_fogDraw)(CVisibilityFog*, void*, CSceneRenderer*);
int __fastcall FogDrawVerify(CVisibilityFog* self, void*, CSceneRenderer* renderer) {
    std::uint8_t before[sizeof(CVisibilityFog)], mine[sizeof(CVisibilityFog)];
    std::memcpy(before, self, sizeof(before));

    StandInRenderer ours{StandInVtable(), renderer->flags_, renderer->pipeline_, renderer, 0, 0, 0, nullptr, nullptr};
    int result = self->CVisibilityFog::Draw(reinterpret_cast<CSceneRenderer*>(&ours));
    std::memcpy(mine, self, sizeof(mine));
    const int pointCount = ours.calls ? FogPoints().size : 0;
    const int spriteCount = ours.calls ? FogSprites().size : 0;
    const int written = static_cast<int>(self->cells.Size()) < pointCount ? static_cast<int>(self->cells.Size()) : pointCount;
    std::vector<ParticleVertex> points(FogPoints().data, FogPoints().data + (ours.calls ? written : 0));
    std::vector<FogSprite> sprites(FogSprites().data, FogSprites().data + (ours.calls ? written : 0));

    std::memcpy(self, before, sizeof(before));
    StandInRenderer theirs{StandInVtable(), renderer->flags_, renderer->pipeline_, nullptr, ours.result, 0, 0, nullptr, nullptr};
    int expected = s_fogDraw(self, nullptr, reinterpret_cast<CSceneRenderer*>(&theirs));

    bool same = expected == result && theirs.calls == ours.calls && std::memcmp(mine, self, sizeof(mine)) == 0;
    if (same && ours.calls) {
        same = theirs.first == ours.first && theirs.points == ours.points && theirs.sprites == ours.sprites &&
               FogPoints().size == pointCount && FogSprites().size == spriteCount &&
               (written == 0 ||
                (std::memcmp(points.data(), FogPoints().data, written * sizeof(ParticleVertex)) == 0 &&
                 std::memcmp(sprites.data(), FogSprites().data, written * sizeof(FogSprite)) == 0));
    }
    // Counted apart, so the report shows whether a scenario got as far as the sprites.
    ei::verify::Check(ours.calls ? "CVisibilityFog::Draw (004E1EB0), sprites drawn" : "CVisibilityFog::Draw (004E1EB0), nothing drawn",
                      true, same, "%u cells, drawn %d, result %d vs %d", static_cast<unsigned>(self->cells.Size()),
                      ours.calls, expected, result);
    return result;
}

// ---- The rain / snow layer's buffers ----

struct RainCopy {
    alignas(4) unsigned char bytes[sizeof(CRainSnow)];
    CRainSnow* get() { return reinterpret_cast<CRainSnow*>(bytes); }
};
template <typename T> void Empty(lib::AlignedArray<T>& array) {
    array.data = nullptr;
    array.size = array.capacity = 0;
}
template <typename T> void Empty(mfc::CArray<T>& array) {
    array.data = nullptr;
    array.size = array.maxSize = 0;
}

bool SameBuffers(CRainSnow* expected, CRainSnow* actual) {
    if (expected->work038.size != actual->work038.size || expected->work070.size != actual->work070.size ||
        expected->bytes05C.size != actual->bytes05C.size || expected->words080.size != actual->words080.size ||
        expected->work094.size != actual->work094.size)
        return false;
    if (std::memcmp(expected->work094.data, actual->work094.data, static_cast<std::size_t>(actual->work094.size) * 4) != 0)
        return false;
    if (std::memcmp(expected->words080.data, actual->words080.data, static_cast<std::size_t>(actual->words080.size) * 2) != 0)
        return false;
    const auto* theirs = reinterpret_cast<const TLVertex*>(expected->work070.data);
    const auto* ours = reinterpret_cast<const TLVertex*>(actual->work070.data);
    for (int i = 0; i < actual->work070.size; ++i) {
        if (std::memcmp(&theirs[i].color, &ours[i].color, sizeof(TLVertex) - offsetof(TLVertex, color)) != 0) return false;
    }
    return true;
}

// 004E3C20: SUB ESP,0x24 (3), PUSH EBX (1), MOV EBX,ECX (2).
void(__fastcall* s_prepare)(CRainSnow*);
void __fastcall PrepareBuffersVerify(CRainSnow* self) {
    RainCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    CRainSnow* copy = expected.get();
    Empty(copy->work038);
    Empty(copy->bytes05C);
    Empty(copy->work070);
    Empty(copy->words080);
    Empty(copy->work094);
    s_prepare(copy);
    self->CRainSnow::PrepareBuffers();
    ei::verify::Check("CRainSnow::PrepareBuffers (004E3C20)", true, SameBuffers(copy, self), "%d vertices, %d offsets",
                      self->work070.size, self->work094.size);
    lib::FreeAligned32(copy->work038.data);
    lib::FreeAligned32(copy->work070.data);
    lib::FreeAligned32(copy->work094.data);
    gamecrt::operator_delete(copy->bytes05C.data);
    gamecrt::operator_delete(copy->words080.data);
}

} // namespace

EI_VERIFY(0x004E1EB0, FogDrawVerify, 6, s_fogDraw);
EI_VERIFY(0x004E3C20, PrepareBuffersVerify, 6, s_prepare);
