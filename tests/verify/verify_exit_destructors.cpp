// Checks for the destructors only exit functions call (S3c): the databases' (database_destruct.cpp),
// two global lists' (static_lists.cpp) and two global arrays' (game_arrays.cpp). All ours only: a
// destructor frees what the original would free a second time. Each checks the object's shape
// before and what the destructor leaves after. They run at a normal exit, after harness_report.txt
// is written: read their "verify total" lines in ei_hook.log. The two vector destructors (0056E800,
// 0069BE20) are checked in verify_stlp_vector.cpp.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/ai_map.h"
#include "../../src/game/app_helpers.h"
#include "../../src/game/game_arrays.h"
#include "../../src/game/item_db.h"
#include "../../src/game/model.h"
#include "../../src/game/object_tables.h"
#include "../../src/game/quest_records.h"
#include "../../src/game/sector_map.h"
#include "../../src/game/texture.h"

#include <cstdint>

namespace {
void* s_unused;

// A table's three record pointers (begin, end, end of storage) well formed.
bool OrderedTable(const void* table) {
    auto* p = static_cast<char* const*>(table);
    return p[0] <= p[1] && p[1] <= p[2] && (p[0] || !p[2]);
}

// An MFC array's header well formed.
template <typename T>
bool ArrayHeaderOk(const mfc::CArray<T>& array) {
    return array.size >= 0 && array.size <= array.maxSize && (array.size == 0 || array.data);
}

// ---- The databases: each table well formed before; after, every index emptied (count 0) ----------

void __fastcall MapDatabaseDestructVerify(CMapDatabase* self) {
    bool ok = ArrayHeaderOk(self->allods) && ArrayHeaderOk(self->maps);
    self->Destruct();
    ok = ok && reinterpret_cast<std::uintptr_t>(self->allods.vtable) == CMapDatabase::kAllodArrayVtable &&
         reinterpret_cast<std::uintptr_t>(self->maps.vtable) == CMapDatabase::kMapArrayVtable;
    ei::verify::Check("CMapDatabase::Destruct", true, ok, "database %p", static_cast<void*>(self));
}

void __fastcall SpellDatabaseDestructVerify(itemdb::SpellDatabase* self) {
    bool ok = OrderedTable(&self->spells) && OrderedTable(&self->modifiers);
    for (const auto& list : self->lists) ok = ok && OrderedTable(&list);
    self->Destruct();
    ok = ok && self->spells.byCode.Size() == 0 && self->modifiers.byCode.Size() == 0;
    ei::verify::Check("SpellDatabase::Destruct", true, ok, "database %p", static_cast<void*>(self));
}

void __fastcall QuestDatabaseDestructVerify(QuestDatabase* self) {
    bool ok = OrderedTable(&self->quests) && OrderedTable(&self->briefings.records);
    self->Destruct();
    ok = ok && self->questsByName.Size() == 0 && self->briefings.index.Size() == 0;
    ei::verify::Check("QuestDatabase::Destruct", true, ok, "database %p", static_cast<void*>(self));
}

void __fastcall PrintsDatabaseDestructVerify(PrintsDatabase* self) {
    bool ok = OrderedTable(&self->blood) && OrderedTable(&self->footprints) && OrderedTable(&self->scorch);
    self->Destruct();
    ok = ok && self->bloodByName.Size() == 0 && self->footprintsByName.Size() == 0 && self->scorchByName.Size() == 0;
    ei::verify::Check("PrintsDatabase::Destruct", true, ok, "database %p", static_cast<void*>(self));
}

void __fastcall AckDatabaseDestructVerify(acks::AckDatabase* self) {
    bool ok = OrderedTable(&self->units.records) && OrderedTable(&self->models.records) &&
              OrderedTable(&self->extras.records);
    self->Destruct();
    ok = ok && self->units.byName.Size() == 0 && self->models.byName.Size() == 0 && self->extras.byName.Size() == 0;
    ei::verify::Check("AckDatabase::Destruct", true, ok, "database %p", static_cast<void*>(self));
}

// ---- The lists: linked both ways before; after, the sentinel (freed last) on top of its free list -

template <std::uintptr_t A, typename T>
struct ListDestructCheck {
    using Abi = stlp::ListAbi<T>;
    using List = typename Abi::List;
    using Node = typename List::Node;

    static void __fastcall Verify(List* self) {
        Node* head = self->head;
        bool ok = head != nullptr;
        unsigned nodes = 0;
        for (Node* n = head; ok && (n != head || nodes == 0); n = n->next, ++nodes)
            ok = n->next && n->next->prev == n && nodes < 100000;
        Abi::Destruct(self);
        void** list = stlp_verify::FreeList(sizeof(Node));
        ok = ok && (!list || *list == head);
        ei::verify::Check(stlp_verify::Name<A>("stlp::list::~list"), true, ok, "%u nodes, sentinel %p", nodes,
                          static_cast<void*>(head));
    }
};
using ProtectionListCheck = ListDestructCheck<0x004584E0, std::uint8_t>;
using DriverListCheck = ListDestructCheck<0x004F0520, DirectDrawDriver>;

// ---- The arrays: the header well formed before; after, the instantiation's vtable back ----------

template <std::uintptr_t A, typename Class>
struct ArrayDestructCheck {
    using Array = mfc::CArray<mfc::impl::Bytes<1>>;
    static void __fastcall Verify(Array* self) {
        bool ok = self->size >= 0 && self->size <= self->maxSize && (self->size == 0 || self->data);
        int size = self->size;
        mfc::impl::ArrayHeader<Class>::Destruct(self);
        ok = ok && self->vtable == Class::Vtable();
        ei::verify::Check(stlp_verify::Name<A>("CArray destructor"), true, ok, "%d elements", size);
    }
};
using RadiusKeysCheck = ArrayDestructCheck<0x005BF870, mfc::impl::FloatArrayClass>;
using SmoothNodesCheck = ArrayDestructCheck<0x005C4490, gamearrays::SmoothNodeArrayClass>;

} // namespace

EI_VERIFY(0x0045E6F0, MapDatabaseDestructVerify, 0, s_unused);
EI_VERIFY(0x00684390, SpellDatabaseDestructVerify, 0, s_unused);
EI_VERIFY(0x006ADF40, QuestDatabaseDestructVerify, 0, s_unused);
EI_VERIFY(0x006D00E0, PrintsDatabaseDestructVerify, 0, s_unused);
EI_VERIFY(0x006D70B0, AckDatabaseDestructVerify, 0, s_unused);
EI_VERIFY(0x004584E0, ProtectionListCheck::Verify, 0, s_unused);
EI_VERIFY(0x004F0520, DriverListCheck::Verify, 0, s_unused);
EI_VERIFY(0x005BF870, RadiusKeysCheck::Verify, 0, s_unused);
EI_VERIFY(0x005C4490, SmoothNodesCheck::Verify, 0, s_unused);
