// The global table at 0079C1A0 that levers look up (0052CA40 by the lever's +0xA4; loaded by the
// Rebirth-patched 00463390 from WinMain): an STLport vector of 0x2C-byte records (CStrings at +0,
// +0x20, +0x24, +0x28) and a hash map whose 0x14-byte nodes hold a vector at +4. Its constructor
// (004632A0) runs from a static initializer, before the hooks are installed; the destructor
// (00463120, registered with atexit through the thunk 00463110) is rewritten here. The STL members
// it inlines are library code. See docs/notes/app.md.
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"

#include <cstdint>

namespace levertable {

namespace {

struct HashNode {
    HashNode* next;
    // +4: the value (a vector), destroyed by 00464DF0
};

struct Table {
    std::uint8_t* recordsBegin;   // +0x00 vector<record 0x2C>
    std::uint8_t* recordsEnd;     // +0x04
    std::uint8_t* recordsEos;     // +0x08
    std::uint32_t hashFunctors;   // +0x0C
    HashNode** buckets;           // +0x10 vector<node*>
    HashNode** bucketsEnd;        // +0x14
    HashNode** bucketsEos;        // +0x18
    std::uint32_t count;          // +0x1C (with the node allocator)
};
static_assert(sizeof(Table) == 0x20);

constexpr std::size_t kRecordSize = 0x2C;

// The node value's deleting destructor (00464DF0, called with 0); the node allocator's deallocate
// (004648D0, thiscall on the count's allocator proxy); the bucket storage's deallocate (00406590).
__declspec(naked) void* __fastcall NodeValueDelete(void*, void*, unsigned) EI_ORIGINAL(0x00464DF0)
void BucketsDeallocate(HashNode** buckets, std::size_t n) { stlp::deallocate(buckets, n * sizeof(HashNode*)); }

void DestroyString(std::uint8_t* p) { reinterpret_cast<mfc::CString*>(p)->~CString(); }

} // namespace

// 00463120: the hash map (nodes, then the bucket storage), then the records and their storage
// (operator delete above 128 bytes, else back to the node pool's free list at 0079B224).
void __fastcall LeverTableDestruct(Table* self) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(self->bucketsEnd - self->buckets); ++i) {
        for (HashNode* node = self->buckets[i]; node;) {
            HashNode* next = node->next;
            NodeValueDelete(reinterpret_cast<std::uint8_t*>(node) + 4, nullptr, 0);
            stlp::deallocate(node, 20);   // 004648D0
            node = next;
        }
        self->buckets[i] = nullptr;
    }
    self->count = 0;
    BucketsDeallocate(self->buckets, static_cast<std::size_t>(self->bucketsEos - self->buckets));
    for (std::uint8_t* p = self->recordsBegin; p != self->recordsEnd; p += kRecordSize) {
        DestroyString(p + 0x28);
        DestroyString(p + 0x24);
        DestroyString(p + 0x20);
        DestroyString(p);
    }
    std::uint8_t* storage = self->recordsBegin;
    if (!storage) return;
    std::size_t bytes = static_cast<std::size_t>((self->recordsEos - storage) / static_cast<int>(kRecordSize)) * kRecordSize;
    if (bytes > 0x80) {
        gamecrt::operator_delete(storage);
        return;
    }
    void** freeList = reinterpret_cast<void**>(0x0079B224) + ((bytes - 1) >> 3);
    *reinterpret_cast<void**>(storage) = *freeList;
    *freeList = storage;
}
EI_REPLACE(0x00463120, LeverTableDestruct);

} // namespace levertable
