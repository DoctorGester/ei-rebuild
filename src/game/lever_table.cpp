// The global lever table that levers look up (loaded from WinMain by the Rebirth-patched loader): an STLport
// vector of 0x2C-byte records (four CStrings) and a hash map whose 0x14-byte nodes hold a vector. Its destructor
// is defined here. See docs/notes/app.md.
#include "lever.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstdint>

namespace levertable {

namespace {

struct HashNode {
    HashNode* next;
    stlp::vector<char> value;   // its storage freed on destruction
};

struct Table {
    std::uint8_t* recordsBegin;   // vector<record 0x2C>
    std::uint8_t* recordsEnd;
    std::uint8_t* recordsEos;
    std::uint32_t hashFunctors;
    HashNode** buckets;           // vector<node*>
    HashNode** bucketsEnd;
    HashNode** bucketsEos;
    std::uint32_t count;          // with the node allocator
};
static_assert(sizeof(Table) == 0x20);

constexpr std::size_t kRecordSize = 0x2C;

// The node allocator's deallocate (thiscall on the count's allocator proxy); the bucket storage's deallocate.
void BucketsDeallocate(HashNode** buckets, std::size_t n) { stlp::deallocate(buckets, n * sizeof(HashNode*)); }

void DestroyString(std::uint8_t* p) { reinterpret_cast<mfc::CString*>(p)->~CString(); }

} // namespace

// The hash map (nodes, then the bucket storage), then the records and their storage
// (operator delete above 128 bytes, else back to the node pool's free list).
void __fastcall LeverTableDestruct(Table* self) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(self->bucketsEnd - self->buckets); ++i) {
        for (HashNode* node = self->buckets[i]; node;) {
            HashNode* next = node->next;
            node->value.FreeStorage();
            stlp::deallocate(node, 20);
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
    libcrt::NodeDeallocate(storage, bytes);
}
EI_REWRITTEN(0x00463120, LeverTableDestruct);

} // namespace levertable

// The table, empty at the start: built by its static initialiser and destroyed by the exit function it registers.
constinit lib::StaticObject<LeverRecordTable> g_leverTable = /*0079C1A0*/ {};

namespace {
void __cdecl ConstructLeverTable() {
    g_leverTable->Construct();
    libcrt::AtExit(ei::Code<void(__cdecl*)()>(0x00463110));
}
void __cdecl DestroyLeverTable() {
    levertable::LeverTableDestruct(reinterpret_cast<levertable::Table*>(&g_leverTable.Get()));
}
} // namespace

EI_REPLACE(0x004630F0, ConstructLeverTable);
EI_REPLACE(0x00463110, DestroyLeverTable);
