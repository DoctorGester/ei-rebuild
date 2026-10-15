// Checks for the connection's owning lists and the clients' find (src/game/library_net_lists.cpp,
// S12e).
//
// Ours only (they allocate, copy counted entries or release them): the copy constructors against
// their source (every element as its policy copies it, each entry's count up by its copies), the
// default inserts by the node's links, the default element and the blocks the pool hands out, the
// erase / clear / ~list by the links and the blocks on top of the free lists. Against the original
// (pure): the find by peer address and the counted pointer's init. A start-up check drives all 14
// through their game addresses over scratch lists of fake counted entries (whose counts never reach
// zero); in the suite the connection's members run in every game with a session (Connection::Reset,
// the history, the freed ids), the copy constructors only through Connection::Clone.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/library_containers.h"
#include "../../src/game/library_net_lists.h"
#include "../../src/game/library_trees.h"
#include "../../src/lib/stlp_hash.h"
#include "../../src/lib/stlp_list.h"
#include "../../src/lib/stlp_pair.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::LinkedBetween;
using stlp_verify::Name;
using namespace netlists;

// The block on top of the free list for `bytes` and the one after it (null when the list is short
// or the size comes from operator new).
void* FreeTop(std::size_t bytes, int depth) {
    void** list = FreeList(bytes);
    void* p = list ? *list : nullptr;
    for (; p && depth > 0; --depth) p = *static_cast<void**>(p);
    return p;
}

// Every node of a list linked both ways, ending at the sentinel; the number of elements, or -1.
template <typename List>
int Count(const List& list) {
    int n = 0;
    auto* prev = list.head;
    for (auto* node = list.head->next;; prev = node, node = node->next, ++n) {
        if (node->prev != prev || n > 1000000) return -1;
        if (node == list.head) return n;
    }
}

// Each entry's count before a copy, and how many items of the source hold it.
struct EntryCounts {
    std::unordered_map<net::ReplicaEntry*, std::pair<std::int16_t, int>> counts;
    void Add(const SentItems& items) {
        for (auto* n = items.head->next; n != items.head; n = n->next)
            if (n->data.entry) {
                auto it = counts.emplace(n->data.entry, std::make_pair(n->data.entry->refs_, 0)).first;
                ++it->second.second;
            }
    }
    // Every entry counted once more for each copy.
    bool Raised() const {
        for (const auto& [entry, c] : counts)
            if (entry->refs_ != static_cast<std::int16_t>(c.first + c.second)) return false;
        return true;
    }
};

bool SameItems(const SentItems& copy, const SentItems& from) {
    if (copy.head == from.head || Count(copy) != Count(from)) return false;
    for (auto *a = copy.head->next, *b = from.head->next; b != from.head; a = a->next, b = b->next)
        if (a->data.action != b->data.action || a->data.version != b->data.version || a->data.entry != b->data.entry)
            return false;
    return true;
}

bool SameElement(const PacketRecord& copy, const PacketRecord& from) {
    return std::memcmp(&copy.time, &from.time, sizeof(copy.time)) == 0 && copy.sequence == from.sequence &&
           std::memcmp(&copy.header, &from.header, sizeof(copy.header)) == 0 && copy.rolledBack == from.rolledBack &&
           SameItems(copy.items, from.items);
}
bool SameElement(const FreedIds& copy, const FreedIds& from) {
    std::size_t n = from.size();
    if (copy.size() != n || copy.Capacity() != n) return false;
    if (n == 0) return copy.begin() == nullptr;
    return copy.begin() != from.begin() && std::memcmp(copy.begin(), from.begin(), n * sizeof(net::FreedId)) == 0;
}
bool SameElement(const net::ReliableChunk& copy, const net::ReliableChunk& from) {
    return std::memcmp(&copy, &from, sizeof(copy)) == 0;
}

void AddCounts(EntryCounts&, const FreedIds&) {}
void AddCounts(EntryCounts&, const net::ReliableChunk&) {}
void AddCounts(EntryCounts& counts, const PacketRecord& p) { counts.Add(p.items); }

// list(const list&): a list of its own with the source's elements in order.
template <std::uintptr_t A, typename T, typename E>
struct CopyCheck {
    using List = stlp::list<T>;

    static List* __fastcall Verify(List* self, void*, const List* from) {
        EntryCounts counts;
        for (auto* n = from->head->next; n != from->head; n = n->next) AddCounts(counts, n->data);
        List* result = CopyConstruct<T, E>(self, nullptr, from);
        bool ok = result == self && self->head != from->head && Count(*self) == Count(*from) && counts.Raised();
        for (auto *a = self->head->next, *b = from->head->next; ok && b != from->head; a = a->next, b = b->next)
            ok = SameElement(a->data, b->data);
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "%d elements (source %d), sentinel %p", Count(*self), Count(*from),
                          self->head);
        ei::verify::Report(Name<A>("stlp::list::list(const list&)"), ok, detail);
        return result;
    }
};

// insert(pos): a node linked before pos holding the default element. The pool hands out the block
// on top of its free list (a packet: the default's item sentinel takes it and goes back; the copy's
// sentinel is the next one).
template <std::uintptr_t A, typename Abi>
struct InsertDefaultCheck {
    using List = typename Abi::List;
    using Node = typename Abi::Node;

    static bool Default(const SentRecord& r, const void*, const void*) { return r.entry == nullptr; }
    static bool Default(const FreedIds& v, const void*, const void*) { return v.begin() == nullptr && v.Capacity() == 0; }
    static bool Default(const net::ReliableChunk&, const void*, const void*) { return true; }
    static bool Default(const PacketRecord& p, const void* top, const void* second) {
        bool items = Count(p.items) == 0 && p.rolledBack == 0;
        bool blocks = !top || !second || (p.items.head == second && FreeTop(sizeof(SentItems::Node), 0) == top);
        return items && blocks;
    }

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos) {
        Node* prev = pos->prev;
        void* top = FreeTop(sizeof(Node) <= 128 ? sizeof(Node) : sizeof(SentItems::Node), 0);
        void* second = FreeTop(sizeof(SentItems::Node), 1);
        Node** result = Abi::InsertDefault(self, nullptr, out, pos);
        Node* node = *out;
        bool ok = result == out && LinkedBetween(node, prev, pos) && (sizeof(Node) > 128 || !top || node == top) &&
                  Default(node->data, top, second);
        char detail[96] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "node %p (pool top %p), pos %p", node, top, pos);
        ei::verify::Report(Name<A>("stlp::list::insert(pos)"), ok, detail);
        return result;
    }
};

// The block an element gives back last when destroyed (on top of its free list afterwards), or null.
const void* LastFreed(const PacketRecord& p) { return p.items.head; }
const void* LastFreed(const FreedIds&) { return nullptr; }

template <std::uintptr_t A, typename Abi>
struct EraseCheck {
    using List = typename Abi::List;
    using Node = typename Abi::Node;
    static constexpr std::size_t kNode = sizeof(Node);
    static constexpr std::size_t kItemNode = sizeof(SentItems::Node);

    // The free list a destroyed node leaves on top: the node itself from the pool, else the block
    // its element freed last.
    static bool OnTop(const Node* node, const void* elementBlock) {
        if constexpr (kNode <= 128) return FreeTop(kNode, 0) == node;
        else return !elementBlock || FreeTop(kItemNode, 0) == elementBlock;
    }

    static Node** __fastcall Verify(List* self, void*, Node** out, Node* pos) {
        Node* prev = pos->prev;
        Node* next = pos->next;
        const void* block = LastFreed(pos->data);
        Node** result = Abi::Erase(self, nullptr, out, pos);
        bool ok = result == out && *out == next && prev->next == next && next->prev == prev && OnTop(pos, block);
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "pos %p, returned %p, next %p", pos, *out, next);
        ei::verify::Report(Name<A>("stlp::list::erase"), ok, detail);
        return result;
    }
    static void __fastcall Clear(List* self) {
        Node* last = self->head->prev;
        const void* block = last != self->head ? LastFreed(last->data) : nullptr;
        Abi::Clear(self);
        bool ok = self->head->next == self->head && self->head->prev == self->head &&
                  (last == self->head || OnTop(last, block));
        ei::verify::Report(Name<A>("stlp::list::clear"), ok, ok ? "" : "the sentinel is not linked to itself or the last node is not on top");
    }
    static void __fastcall Destruct(List* self) {
        Node* last = self->head->prev;
        const void* block = last != self->head ? LastFreed(last->data) : nullptr;
        bool linked = Count(*self) >= 0;
        Abi::Destruct(self);
        bool ok = linked && (last == self->head || OnTop(last, block));
        ei::verify::Report(Name<A>("stlp::list::~list"), ok, ok ? "" : "the list was not linked or the last items' sentinel is not on top");
    }
};

// find_if by peer address: pure, the original first.
void* s_findOriginal;
ClientsNode** __fastcall FindVerify(ClientsNode** out, void*, ClientsNode* first, ClientsNode* last,
                                    const net::NetAddress* address) {
    ClientsNode* theirs = nullptr;
    ClientsNode** theirResult = reinterpret_cast<decltype(&FindClientByAddress)>(s_findOriginal)(&theirs, nullptr, first, last, address);
    ClientsNode** result = FindClientByAddress(out, nullptr, first, last, address);
    ei::verify::Check("stlp::find_if(clients, address) 0043D780", theirs, *out, "first %p, last %p", first, last);
    ei::verify::Report("stlp::find_if(clients, address) result 0043D780", result == out && theirResult == &theirs, "");
    return result;
}

void* s_initOriginal;
net::Connection** __fastcall InitVerify(net::Connection** ref, net::Connection* connection) {
    net::Connection* theirs = reinterpret_cast<net::Connection*>(std::uintptr_t{1});
    net::Connection** theirResult = reinterpret_cast<decltype(&InitClientRef)>(s_initOriginal)(&theirs, connection);
    net::Connection** result = InitClientRef(ref, connection);
    ei::verify::Check("clients' counted pointer init 0043D7D0", theirs, *ref, "");
    ei::verify::Report("clients' counted pointer init result 0043D7D0", result == ref && theirResult == &theirs, "");
    return result;
}

// ---- Start-up check -------------------------------------------------------------------------------

template <typename Fn>
Fn At(std::uintptr_t address) {
    return reinterpret_cast<Fn>(address);
}

// A fake counted object (CRefObject's header) that never reaches zero.
struct FakeCounted {
    void* vtable = nullptr;
    int refs = 50;
};

void NetListsStartupCheck() {
    FakeCounted fakes[3];
    auto entry = [&](int i) { return reinterpret_cast<net::ReplicaEntry*>(&fakes[i]); };

    // The history: packets with items, copied, erased, cleared and destroyed.
    {
        SentPackets history;
        history.Construct();
        for (int k = 0; k < 3; ++k) {
            SentPackets::Node* packet = nullptr;
            At<decltype(&SentPacketAbi::InsertDefault)>(0x004397E0)(&history, nullptr, &packet, history.head);
            packet->data.time = 0.25 * k;
            packet->data.sequence = static_cast<std::uint16_t>(100 + k);
            std::memset(&packet->data.header, 0x30 + k, sizeof(packet->data.header));
            packet->data.rolledBack = static_cast<std::uint8_t>(k & 1);
            for (int i = 0; i < k + 1; ++i) {
                SentItems::Node* item = nullptr;
                At<decltype(&SentItemAbi::InsertDefault)>(0x00438F60)(&packet->data.items, nullptr, &item,
                                                                      packet->data.items.head);
                item->data.action = static_cast<std::uint32_t>(i & 3);
                item->data.version = static_cast<std::uint32_t>(k * 10 + i);
                item->data.entry = i == 1 ? nullptr : entry((k + i) % 3);
                if (item->data.entry) ++item->data.entry->refs_;
            }
        }
        SentPackets copy;
        At<decltype(&CopyConstruct<PacketRecord, PacketRecordElement>)>(0x00437F60)(&copy, nullptr, &history);
        SentPackets::Node* after = nullptr;
        At<decltype(&SentPacketAbi::Erase)>(0x00437F30)(&copy, nullptr, &after, copy.head->next->next);
        At<decltype(&SentPacketAbi::Clear)>(0x0043CA40)(&copy);
        At<decltype(&SentPacketAbi::Destruct)>(0x004399C0)(&copy);
        At<decltype(&SentPacketAbi::Destruct)>(0x004399C0)(&history);
    }
    bool balanced = fakes[0].refs == 50 && fakes[1].refs == 50 && fakes[2].refs == 50;
    ei::verify::Report("connection lists start-up: entry counts back", balanced, balanced ? "" : "an entry's count changed");

    // The freed-id batches.
    {
        FreedBatches batches;
        batches.Construct();
        for (int k = 0; k < 3; ++k) {
            FreedBatches::Node* batch = nullptr;
            At<decltype(&FreedBatchAbi::InsertDefault)>(0x00439000)(&batches, nullptr, &batch, batches.head);
            for (int i = 0; i < k * 3; ++i)
                batch->data.push_back(net::FreedId{static_cast<std::uint16_t>(k * 100 + i), static_cast<std::uint8_t>(i), 0});
        }
        FreedBatches copy;
        At<decltype(&CopyConstruct<FreedIds, FreedIdsElement>)>(0x00437930)(&copy, nullptr, &batches);
        FreedBatches::Node* after = nullptr;
        At<decltype(&FreedBatchAbi::Erase)>(0x004390C0)(&copy, nullptr, &after, copy.head->next);
        At<decltype(&FreedBatchAbi::Clear)>(0x0043BA20)(&copy);
        At<decltype(&FreedBatchAbi::Clear)>(0x0043BA20)(&batches);
        stlp::deallocate(copy.head, sizeof(FreedBatches::Node));
        stlp::deallocate(batches.head, sizeof(FreedBatches::Node));
    }

    // The chunk lists.
    {
        Chunks chunks;
        chunks.Construct();
        for (int k = 0; k < 2; ++k) {
            Chunks::Node* chunk = nullptr;
            At<decltype(&ChunkAbi::InsertDefault)>(0x00439C80)(&chunks, nullptr, &chunk, chunks.head);
            std::memset(&chunk->data, 0x41 + k, sizeof(chunk->data));
        }
        Chunks copy;
        At<decltype(&CopyConstruct<net::ReliableChunk, ChunkElement>)>(0x00438690)(&copy, nullptr, &chunks);
        chunks.ClearWith([](net::ReliableChunk*) {});
        copy.ClearWith([](net::ReliableChunk*) {});
        stlp::deallocate(chunks.head, sizeof(Chunks::Node));
        stlp::deallocate(copy.head, sizeof(Chunks::Node));
    }

    // The clients' find by address and the counted pointer's init, over fake connections.
    {
        alignas(net::Connection) static unsigned char storage[3][sizeof(net::Connection)];
        net::Connection* connections[3];
        ClientsNode nodes[4];
        for (int i = 0; i < 3; ++i) {
            connections[i] = reinterpret_cast<net::Connection*>(storage[i]);
            net::NetAddress& a = connections[i]->address_;
            std::memset(&a, 0, sizeof(a));
            a.family = 2;
            a.port = static_cast<std::uint16_t>(7000 + (i & 1));
            a.ip = 0x0100007Fu;
        }
        ClientsNode* head = &nodes[3];
        head->next = head->prev = head;
        for (int i = 0; i < 3; ++i) {
            nodes[i].connection = connections[i];
            nodes[i].next = head;
            nodes[i].prev = head->prev;
            head->prev->next = &nodes[i];
            head->prev = &nodes[i];
        }
        net::NetAddress probes[3] = {connections[1]->address_, connections[0]->address_, connections[0]->address_};
        probes[2].zero[7] = 1;
        for (const auto& probe : probes) {
            ClientsNode* found = nullptr;
            At<decltype(&FindClientByAddress)>(0x0043D780)(&found, nullptr, head->next, head, &probe);
            At<decltype(&FindClientByAddress)>(0x0043D780)(&found, nullptr, head->next->next, head->prev, &probe);
        }
        net::Connection* ref = nullptr;
        At<decltype(&InitClientRef)>(0x0043D7D0)(&ref, connections[2]);
        At<decltype(&InitClientRef)>(0x0043D7D0)(&ref, nullptr);
    }
}

// S12e's other rows, which the suite rarely reaches: the dword hashtables' erase by key, the
// hero screen's caches and their node allocation, the link loader's archive erase, the game
// record's and the print records' strings.
void OtherRowsStartupCheck() {
    using Map28 = stlp::HashtableAbi<stlp::DwordKeyTraits<28>>;
    using Map8 = stlp::HashtableAbi<stlp::DwordKeyTraits<8>>;
    {
        Map28::Table table;
        Map28::ConstructDefault(&table);
        for (std::uint32_t k = 0; k < 40; ++k) {
            stlp::DwordKeyed<28> v{};
            v.key = k * 7 % 23;
            v.rest[0] = static_cast<unsigned char>(k);
            Map28::FindOrInsert(&table, nullptr, &v);
        }
        for (std::uint32_t key : {0u, 5u, 22u, 99u, 5u})
            At<decltype(&Map28::EraseKey)>(0x004E2F00)(&table, nullptr, &key);
        Map28::Destroy(&table);
    }
    {
        Map8::Table table;
        Map8::ConstructDefault(&table);
        for (std::uint32_t k = 0; k < 30; ++k) {
            stlp::DwordKeyed<8> v{};
            v.key = k * 101;
            Map8::FindOrInsert(&table, nullptr, &v);
        }
        for (std::uint32_t key : {0u, 101u, 2929u, 7u})
            At<decltype(&Map8::EraseKey)>(0x006BD060)(&table, nullptr, &key);
        Map8::Destroy(&table);
    }
    for (std::uintptr_t ctor : {0x00633F50u, 0x00633FF0u}) {
        lib_trees::HeroItemCache cache;
        At<decltype(&lib_trees::ConstructHeroItemCache)>(ctor)(&cache);
        lib_trees::ItemCache::Destroy(&cache.map);
    }
    for (std::size_t n : {0u, 1u, 2u, 5u}) {
        auto* nodes = At<decltype(&lib_trees::ItemCache::AllocateNodes)>(0x00619770)(nullptr, nullptr, n, nullptr);
        stlp::deallocate(nodes, n * sizeof(lib_trees::ItemCache::Node));
    }
    {
        using Archives = stlp::ListAbi<stlp::StringDwordPair, stlp::StringDwordElement>;
        Archives::List list;
        list.Construct();
        const char* names[] = {"a.res", "", "a name longer than twenty-four bytes.res"};
        for (std::uint32_t i = 0; i < 3; ++i) {
            stlp::StringDwordPair value{stlp::string(names[i]), i};
            Archives::Node* out = nullptr;
            Archives::Insert(&list, nullptr, &out, list.head, &value);
        }
        while (list.head->next != list.head) {
            Archives::Node* out = nullptr;
            At<decltype(&Archives::Erase)>(0x004FAE00)(&list, nullptr, &out, list.head->next);
        }
        stlp::deallocate(list.head, sizeof(Archives::Node));
    }
    for (std::size_t bytes : {8u, 16u, 24u, 200u}) {
        net::GameInfoRecord record{};
        record.name.start = static_cast<char*>(stlp::allocate(bytes));
        record.name.finish = record.name.start;
        record.name.eos = record.name.start + bytes;
        record.description.start = static_cast<char*>(stlp::allocate(16));
        record.description.finish = record.description.start;
        record.description.eos = record.description.start + 16;
        At<decltype(&libcontainers::DestroyGameInfoRecord)>(0x0041AC30)(&record);
    }
    for (std::uintptr_t destroy : {0x006D29A0u, 0x006D29E0u})
        for (const char* text : {"print", "a print name of more than one hundred and twenty-eight bytes, so that its block comes "
                                          "from operator new and not from the node pool's lists"}) {
            alignas(PrintRecord) unsigned char storage[sizeof(PrintRecord)] = {};
            auto* record = reinterpret_cast<PrintRecord*>(storage);
            new (&record->name) stlp::string(text);
            At<decltype(&libcontainers::DestroyPrintName)>(destroy)(record);
        }
}

void S12eStartupCheck() {
    NetListsStartupCheck();
    OtherRowsStartupCheck();
}

} // namespace

EI_VERIFY(0x00437930, (CopyCheck<0x00437930, FreedIds, FreedIdsElement>::Verify), 0, s_unused);
EI_VERIFY(0x00437F60, (CopyCheck<0x00437F60, PacketRecord, PacketRecordElement>::Verify), 0, s_unused);
EI_VERIFY(0x00438690, (CopyCheck<0x00438690, net::ReliableChunk, ChunkElement>::Verify), 0, s_unused);
EI_VERIFY(0x00438F60, (InsertDefaultCheck<0x00438F60, SentItemAbi>::Verify), 0, s_unused);
EI_VERIFY(0x00439000, (InsertDefaultCheck<0x00439000, FreedBatchAbi>::Verify), 0, s_unused);
EI_VERIFY(0x004397E0, (InsertDefaultCheck<0x004397E0, SentPacketAbi>::Verify), 0, s_unused);
EI_VERIFY(0x00439C80, (InsertDefaultCheck<0x00439C80, ChunkAbi>::Verify), 0, s_unused);
EI_VERIFY(0x00437F30, (EraseCheck<0x00437F30, SentPacketAbi>::Verify), 0, s_unused);
EI_VERIFY(0x004390C0, (EraseCheck<0x004390C0, FreedBatchAbi>::Verify), 0, s_unused);
EI_VERIFY(0x0043CA40, (EraseCheck<0x0043CA40, SentPacketAbi>::Clear), 0, s_unused);
EI_VERIFY(0x0043BA20, (EraseCheck<0x0043BA20, FreedBatchAbi>::Clear), 0, s_unused);
EI_VERIFY(0x004399C0, (EraseCheck<0x004399C0, SentPacketAbi>::Destruct), 0, s_unused);
// find_if: MOV EAX,[ESP+0xC]; PUSH ESI (5 bytes). The init: PUSH ECX; MOV EAX,ECX; MOV [ESP+0],0 (11).
EI_VERIFY(0x0043D780, FindVerify, 5, s_findOriginal);
EI_VERIFY(0x0043D7D0, InitVerify, 11, s_initOriginal);

EI_STARTUP_CHECK(S12eStartupCheck);
