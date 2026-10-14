// Checks for world_object_sets.cpp: ours only (the functions allocate, free and release objects).
// The sort leaves the set strictly increasing; a radix pass leaves the target ordered by its byte;
// SetSize keeps the elements it should; the comparison splits the sets consistently and leaves the
// sent set equal to the current one.
#include "verify.h"

#include "../../src/lib/containers.h"

#include <cstdint>
#include <cstring>

using ObjectSet = lib::AlignedArray<std::uint32_t>;

void __fastcall ObjectSetSetSize(ObjectSet* self, void*, int n, int growBy);
void __fastcall ObjectSetResize(ObjectSet* self, void*, int n, int growBy);
void __fastcall ObjectSetRadixPass(int shift, ObjectSet* src, ObjectSet* dst);
void __fastcall ObjectSetSortUnique(ObjectSet* self);
void __fastcall DiffObjectSets(ObjectSet* sent, ObjectSet* current, ObjectSet* added, ObjectSet* gone, ObjectSet* same);

namespace {
void* s_unused;

bool StrictlyIncreasing(const ObjectSet& a) {
    for (int i = 1; i < a.size; ++i)
        if (!(a.data[i - 1] < a.data[i])) return false;
    return true;
}

void __fastcall SetSizeVerify(ObjectSet* self, void*, int n, int growBy) {
    std::uint32_t before[32];
    int kept = self->size < n ? self->size : n;
    if (kept > 32) kept = 32;
    if (kept > 0) std::memcpy(before, self->data, static_cast<std::size_t>(kept) * 4);
    ObjectSetSetSize(self, nullptr, n, growBy);
    bool ok = self->size == n && self->capacity >= n && (n != 0 || !self->data) &&
              (kept <= 0 || std::memcmp(before, self->data, static_cast<std::size_t>(kept) * 4) == 0);
    ei::verify::Check("ObjectSet::SetSize", true, ok, "");
}
void __fastcall ResizeVerify(ObjectSet* self, void*, int n, int growBy) {
    ObjectSetResize(self, nullptr, n, growBy);
    ei::verify::Check("ObjectSet::Resize", true, self->size == n && self->capacity >= n && self->data != nullptr, "");
}
void __fastcall RadixPassVerify(int shift, ObjectSet* src, ObjectSet* dst) {
    ObjectSetRadixPass(shift, src, dst);
    bool ok = true;
    for (int i = 1; i < src->size; ++i)
        ok = ok && ((dst->data[i - 1] >> shift) & 0xFF) <= ((dst->data[i] >> shift) & 0xFF);
    ei::verify::Check("ObjectSet radix pass", true, ok, "");
}
void __fastcall SortUniqueVerify(ObjectSet* self) {
    int before = self->size;
    ObjectSetSortUnique(self);
    ei::verify::Check("ObjectSet::SortUnique", true, StrictlyIncreasing(*self) && self->size <= before, "");
}
void __fastcall DiffVerify(ObjectSet* sent, ObjectSet* current, ObjectSet* added, ObjectSet* gone, ObjectSet* same) {
    int sentBefore = sent->size;
    DiffObjectSets(sent, current, added, gone, same);
    bool ok = StrictlyIncreasing(*current) && sent->size == current->size &&
              (current->size == 0 || std::memcmp(sent->data, current->data, static_cast<std::size_t>(current->size) * 4) == 0) &&
              added->size + same->size == current->size && gone->size + same->size == sentBefore;
    ei::verify::Check("DiffObjectSets", true, ok, "");
}
} // namespace

EI_VERIFY(0x0047B770, SetSizeVerify, 0, s_unused);
EI_VERIFY(0x0047B880, ResizeVerify, 0, s_unused);
EI_VERIFY(0x0047B120, RadixPassVerify, 0, s_unused);
EI_VERIFY(0x0047ADF0, SortUniqueVerify, 0, s_unused);
EI_VERIFY(0x0047B1F0, DiffVerify, 0, s_unused);
