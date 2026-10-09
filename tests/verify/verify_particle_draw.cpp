// Differential checks for the particle draw pass and its draw-order sort (particle_draw.cpp).
//
// The sort pieces run the original on a copy of the range and ours on the range, and compare every
// entry (the original's recursive call lands on this wrapper again, so each level is checked on its
// own). DrawAll calls the sort and the source draw through their addresses, so those wrappers see
// its calls; DrawAll itself (it draws) runs ours only and checks the order it built.
#include "verify.h"

#include "../../src/game/effects.h"

#include <cstring>
#include <vector>

namespace {

int FirstDifference(const ParticleDepth* a, const ParticleDepth* b, int n) {
    for (int i = 0; i < n; ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(ParticleDepth)) != 0) return i;
    return -1;
}

void CheckRange(const char* name, const std::vector<ParticleDepth>& expected, const ParticleDepth* actual) {
    int n = static_cast<int>(expected.size());
    int at = n ? FirstDifference(expected.data(), actual, n) : -1;
    ei::verify::Check(name, true, at < 0, "%d entries, first difference at %d", n, at);
}

} // namespace

// 00499380: SUB ESP,8; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes).
using IntroFn = void(__fastcall*)(ParticleDepth*, ParticleDepth*, void*, int, int);
static IntroFn s_introSort;
static void __fastcall IntroSortVerify(ParticleDepth* first, ParticleDepth* last, void* tag, int depth, int compare) {
    std::vector<ParticleDepth> copy(first, last);
    if (!copy.empty()) s_introSort(copy.data(), copy.data() + copy.size(), tag, depth, compare);
    IntroSortByDepth(first, last, tag, depth, compare);
    CheckRange("IntroSortByDepth", copy, first);
}
EI_VERIFY(0x00499380, IntroSortVerify, 6, s_introSort);

// 004994E0: SUB ESP,0x14; MOV EAX,EDX; PUSH EBX (6 bytes).
using FinalFn = void(__fastcall*)(ParticleDepth*, ParticleDepth*, int);
static FinalFn s_finalSort;
static void __fastcall FinalSortVerify(ParticleDepth* first, ParticleDepth* last, int compare) {
    std::vector<ParticleDepth> copy(first, last);
    if (!copy.empty()) s_finalSort(copy.data(), copy.data() + copy.size(), compare);
    FinalSortByDepth(first, last, compare);
    CheckRange("FinalSortByDepth", copy, first);
}
EI_VERIFY(0x004994E0, FinalSortVerify, 6, s_finalSort);

// 00499CD0: MOV EAX,EDX; SUB ESP,0xC (5 bytes).
using MakeHeapFn = void(__fastcall*)(ParticleDepth*, ParticleDepth*, void*, int, void*);
static MakeHeapFn s_makeHeap;
static void __fastcall MakeHeapVerify(ParticleDepth* first, ParticleDepth* last, void* tag, int compare, void* distance) {
    std::vector<ParticleDepth> copy(first, last);
    if (!copy.empty()) s_makeHeap(copy.data(), copy.data() + copy.size(), tag, compare, distance);
    MakeHeapByDepth(first, last, tag, compare, distance);
    CheckRange("MakeHeapByDepth", copy, first);
}
EI_VERIFY(0x00499CD0, MakeHeapVerify, 5, s_makeHeap);

// 00499C20: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). `result` is the slot just past [first, last).
using PopHeapFn = void(__fastcall*)(ParticleDepth*, ParticleDepth*, ParticleDepth*, ParticleDepth, int, void*);
static PopHeapFn s_popHeap;
static void __fastcall PopHeapVerify(ParticleDepth* first, ParticleDepth* last, ParticleDepth* result,
                                     ParticleDepth value, int compare, void* distance) {
    if (result != last) {
        // Not the shape the sort uses: leave it to ours alone.
        PopHeapByDepth(first, last, result, value, compare, distance);
        return;
    }
    std::vector<ParticleDepth> copy(first, last + 1);
    ParticleDepth* c = copy.data();
    s_popHeap(c, c + (last - first), c + (last - first), value, compare, distance);
    PopHeapByDepth(first, last, result, value, compare, distance);
    CheckRange("PopHeapByDepth", copy, first);
}
EI_VERIFY(0x00499C20, PopHeapVerify, 5, s_popHeap);

// 00499200: ours only (allocates): the entry is stored and the array holds it.
using SetAtGrowFn = void(__fastcall*)(ParticleDepthArray*, void*, int, const ParticleDepth*);
static SetAtGrowFn s_unusedSetAtGrow;
static void __fastcall SetAtGrowVerify(ParticleDepthArray* self, void*, int index, const ParticleDepth* entry) {
    ParticleDepth value = *entry;
    int sizeBefore = self->size;
    self->ParticleDepthArray::SetAtGrow(index, value);
    int expectedSize = index >= sizeBefore ? index + 1 : sizeBefore;
    ei::verify::Check("ParticleDepthArray::SetAtGrow", true,
                      self->size == expectedSize && self->maxSize >= self->size &&
                          !std::memcmp(&self->data[index], &value, sizeof(value)),
                      "index %d size %d -> %d, max %d", index, sizeBefore, self->size, self->maxSize);
}
EI_VERIFY(0x00499200, SetAtGrowVerify, 0, s_unusedSetAtGrow);

// 00484AE0: SUB ESP,0xC; PUSH ESI; MOV ESI,ECX (6 bytes). Output: the original into a scratch
// vector, ours into the caller's.
using WorldPositionFn = Vector3*(__fastcall*)(CParticleSource*, void*, Vector3*);
static WorldPositionFn s_worldPosition;
static Vector3* __fastcall WorldPositionVerify(CParticleSource* self, void*, Vector3* out) {
    Vector3 expected;
    s_worldPosition(self, nullptr, &expected);
    Vector3* r = self->CParticleSource::WorldPosition(out);
    ei::verify::Check("CParticleSource::WorldPosition", true, r == out && !std::memcmp(&expected, out, sizeof(Vector3)),
                      "type %X attached %d", self->type_, self->attachedTo != nullptr);
    return r;
}
EI_VERIFY(0x00484AE0, WorldPositionVerify, 6, s_worldPosition);

// 00498940: ours only (it draws): the manager is released again, the order holds the sources in
// the manager's order before the sort, and no entry is nearer than the one after it.
using DrawAllFn = int(__fastcall*)(CParticleManager*, void*, void*);
static DrawAllFn s_unusedDrawAll;
static int __fastcall DrawAllVerify(CParticleManager* self, void*, void* renderer) {
    int refs = self->refCount_;
    int sources = 0;
    for (auto* node = self->objectList_.head; node && node->data; node = node->next) ++sources;
    int result = self->CParticleManager::DrawAll(renderer);
    const auto& order = *reinterpret_cast<const ParticleDepthArray*>(0x007A9C50);
    int unsorted = -1;
    for (int i = 0; i + 1 < order.size; ++i)
        if (order.data[i + 1].depth > order.data[i].depth) {
            unsorted = i;
            break;
        }
    ei::verify::Check("CParticleManager::DrawAll", true,
                      self->refCount_ == refs && order.size == sources && unsorted < 0,
                      "%d sources, %d ordered, out of order at %d, refs %d -> %d", sources, order.size, unsorted, refs,
                      self->refCount_);
    return result;
}
EI_VERIFY(0x00498940, DrawAllVerify, 0, s_unusedDrawAll);
