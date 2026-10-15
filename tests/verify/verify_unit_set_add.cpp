// Checks for the unit set's builder and its record helpers (src/game/world_unit_set_add.cpp, S16a).
//
// - UnitSet::Add (005593D0): ours runs first on the real set (SV2f: ours allocates first). The set
//   was kept before in our memory (every group, record, id and name, bucket by bucket); after ours
//   a twin set is built in the game's heap from that copy, the original adds the same unit to the
//   twin, both sets are described again and compared (bucket counts, chains, every record's fields,
//   ids and names), then the twin is destroyed. Reached by every run: the main menu world's
//   CWorldServer::SendZone adds each unit (more on zone changes).
// - The record helpers: the head's constructor against the original on a copy; the copies, the
//   head's destructor, equal() of id ranges, _Construct of a CString and the CString's deleting
//   destructor ours only.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/game_hashes.h"
#include "../../src/game/record_vectors.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"
#include "../../src/lib/stlp_hash.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void* s_unused;

using gamehash::DressedTable;
using gamehash::FiguredGroupsTable;
using gamehash::FiguredTable;
using recordvec::DressedUnitElement;
using recordvec::FiguredUnitElement;
using IdElement = stlp::PlainElement<std::uint32_t>;
using NameElement = stlp::ObjectElement<mfc::CString>;

// ---- A set kept in our memory ------------------------------------------------------------------

struct KeptRecord {
    std::uint32_t key = 0;
    std::uint32_t prototype = 0;
    std::vector<std::uint32_t> armour;
    std::vector<std::uint32_t> weapons;
    std::uint32_t extraWeapon = 0;
    std::uint32_t modelNumber = 0;
    std::uint32_t morph[3] = {};
    std::vector<std::string> names;
    bool operator==(const KeptRecord&) const = default;
};

std::vector<std::uint32_t> KeepIds(const UnitIdList& ids) { return std::vector<std::uint32_t>(ids.begin(), ids.end()); }

KeptRecord Keep(std::uint32_t key, const DressedUnitRecord& r) {
    KeptRecord k;
    k.key = key;
    k.prototype = r.prototype;
    k.armour = KeepIds(r.items);
    k.weapons = KeepIds(r.extraWeapons);
    k.extraWeapon = static_cast<std::uint8_t>(r.extraWeapon);
    k.modelNumber = static_cast<std::uint32_t>(r.modelNumber);
    return k;
}

KeptRecord Keep(std::uint32_t key, const FiguredUnitRecord& r) {
    KeptRecord k;
    k.key = key;
    k.prototype = r.prototype;
    k.armour = KeepIds(r.armourIds);
    k.weapons = KeepIds(r.weaponIds);
    k.extraWeapon = r.extraWeapon;
    k.modelNumber = r.modelNumber;
    std::memcpy(&k.morph[0], &r.morphS, 4);
    std::memcpy(&k.morph[1], &r.morphT, 4);
    std::memcpy(&k.morph[2], &r.morphU, 4);
    const CStringVector& names = FiguredUnitElement::NamesOf(r);
    for (const mfc::CString* name = names.begin(); name != names.end(); ++name) k.names.emplace_back(static_cast<const char*>(*name));
    return k;
}

// A table bucket by bucket, each chain from its head.
template <typename Value>
struct KeptTable {
    std::size_t count = 0;
    std::vector<std::vector<Value>> buckets;
    bool operator==(const KeptTable&) const = default;
};

template <typename Table, typename Fn>
auto KeepTable(Table& t, Fn keep) {
    KeptTable<decltype(keep(t.BucketVector()[0]->value))> kept;
    kept.count = t.Count();
    kept.buckets.resize(t.BucketCount());
    for (std::size_t b = 0; b < t.BucketCount(); ++b)
        for (auto* n = t.BucketVector()[b]; n; n = n->next) kept.buckets[b].push_back(keep(n->value));
    return kept;
}

struct KeptGroup {
    std::uint32_t key = 0;
    KeptTable<KeptRecord> records;
    bool operator==(const KeptGroup&) const = default;
};

struct KeptSet {
    KeptTable<KeptGroup> groups;
    KeptTable<KeptRecord> dressed;
    bool operator==(const KeptSet&) const = default;
};

FiguredGroupsTable& GroupsOf(UnitSet* set) { return reinterpret_cast<FiguredGroupsTable&>(set->figured); }
DressedTable& DressedOf(UnitSet* set) { return reinterpret_cast<DressedTable&>(set->dressed); }

KeptTable<KeptRecord> KeepFigured(FiguredTable& t) {
    return KeepTable(t, [](const FiguredTable::Value& v) { return Keep(v.key, v.mapped); });
}

KeptSet KeepSet(UnitSet* set) {
    KeptSet kept;
    kept.groups = KeepTable(GroupsOf(set), [](FiguredGroupsTable::Value& v) {
        KeptGroup g;
        g.key = v.key;
        g.records = KeepFigured(v.mapped);
        return g;
    });
    kept.dressed = KeepTable(DressedOf(set), [](const DressedTable::Value& v) { return Keep(v.key, v.mapped); });
    return kept;
}

// ---- The twin, built in the game's heap from a kept set ------------------------------------------

void MakeIds(UnitIdList& ids, const std::vector<std::uint32_t>& from) {
    ids.ConstructEmpty();
    for (std::uint32_t id : from) ids.PushBack<IdElement>(id);
}

void Make(DressedUnitRecord* r, const KeptRecord& k) {
    std::memset(r, 0, sizeof(*r));
    r->prototype = static_cast<std::uint16_t>(k.prototype);
    MakeIds(r->items, k.armour);
    MakeIds(r->extraWeapons, k.weapons);
    r->extraWeapon = static_cast<std::int8_t>(k.extraWeapon);
    r->modelNumber = static_cast<int>(k.modelNumber);
}

void Make(FiguredUnitRecord* r, const KeptRecord& k) {
    std::memset(r, 0, sizeof(*r));
    r->prototype = static_cast<std::uint16_t>(k.prototype);
    MakeIds(r->armourIds, k.armour);
    MakeIds(r->weaponIds, k.weapons);
    r->extraWeapon = static_cast<std::uint8_t>(k.extraWeapon);
    r->modelNumber = k.modelNumber;
    std::memcpy(&r->morphS, &k.morph[0], 4);
    std::memcpy(&r->morphT, &k.morph[1], 4);
    std::memcpy(&r->morphU, &k.morph[2], 4);
    CStringVector& names = FiguredUnitElement::NamesOf(*r);
    names.ConstructEmpty();
    for (const std::string& text : k.names) {
        mfc::CString name(text.c_str());
        names.PushBack<NameElement>(name);
    }
}

// A table of the kept bucket count holding copies of the kept values in the kept chains.
template <typename Table, typename Kept, typename MakeValue, typename DestroyValue>
void MakeTable(Table& t, const KeptTable<Kept>& kept, MakeValue make, DestroyValue destroy) {
    t.Construct(kept.buckets.size(), 0, 0, 0);
    for (std::size_t b = 0; b < kept.buckets.size(); ++b) {
        typename Table::Node** link = &t.BucketVector()[b];
        for (const Kept& k : kept.buckets[b]) {
            typename Table::Value value;
            make(&value, k);
            typename Table::Node* node = Table::NewNode(value);
            destroy(&value);
            *link = node;
            link = &node->next;
        }
    }
    t.CountRef() = kept.count;
}

void MakeFigured(FiguredTable& t, const KeptTable<KeptRecord>& kept) {
    MakeTable(
        t, kept,
        [](FiguredTable::Value* v, const KeptRecord& k) {
            v->key = k.key;
            Make(&v->mapped, k);
        },
        [](FiguredTable::Value* v) { FiguredUnitElement::Destroy(&v->mapped); });
}

void MakeSet(UnitSet* set, const KeptSet& kept) {
    MakeTable(
        GroupsOf(set), kept.groups,
        [](FiguredGroupsTable::Value* v, const KeptGroup& g) {
            v->key = g.key;
            MakeFigured(v->mapped, g.records);
        },
        [](FiguredGroupsTable::Value* v) { v->mapped.Destroy(); });
    MakeTable(
        DressedOf(set), kept.dressed,
        [](DressedTable::Value* v, const KeptRecord& k) {
            v->key = k.key;
            Make(&v->mapped, k);
        },
        [](DressedTable::Value* v) { DressedUnitElement::Destroy(&v->mapped); });
}

std::size_t Records(const KeptSet& set) {
    std::size_t n = 0;
    for (const auto& bucket : set.groups.buckets)
        for (const KeptGroup& g : bucket) n += g.records.count;
    return n;
}

} // namespace

// 005593D0: PUSH -1; PUSH 0x721232 (7 bytes).
using AddFn = void(__fastcall*)(UnitSet*, void*, CUnit*);
static AddFn s_add;
static void __fastcall AddVerify(UnitSet* set, void*, CUnit* unit) {
    KeptSet before = KeepSet(set);
    set->Add(unit);
    KeptSet ours = KeepSet(set);

    alignas(UnitSet) unsigned char storage[sizeof(UnitSet)];
    UnitSet* twin = reinterpret_cast<UnitSet*>(storage);
    MakeSet(twin, before);
    s_add(twin, nullptr, unit);
    KeptSet theirs = KeepSet(twin);
    gamehash::FiguredGroupsAbi::Destroy(&GroupsOf(twin));
    gamehash::DressedAbi::Destroy(&DressedOf(twin));

    char detail[128] = "";
    bool ok = ours == theirs;
    if (!ok)
        std::snprintf(detail, sizeof(detail), "unit %d: ours %u groups %u figured %u dressed, original's %u / %u / %u", unit->id_,
                      static_cast<unsigned>(ours.groups.count), static_cast<unsigned>(Records(ours)),
                      static_cast<unsigned>(ours.dressed.count), static_cast<unsigned>(theirs.groups.count),
                      static_cast<unsigned>(Records(theirs)), static_cast<unsigned>(theirs.dressed.count));
    ei::verify::Report("UnitSet::Add", ok, detail);
}
EI_VERIFY(0x005593D0, AddVerify, 7, s_add);

// 00559DB0: PUSH ECX; MOV DL,[ESP+3] (5 bytes). The original on a copy of the head, ours on it.
using HeadFn = DressedUnitRecord*(__fastcall*)(DressedUnitRecord*);
static HeadFn s_constructHead;
static DressedUnitRecord* __fastcall ConstructHeadVerify(DressedUnitRecord* self) {
    alignas(DressedUnitRecord) unsigned char copy[sizeof(DressedUnitRecord)];
    std::memcpy(copy, self, sizeof(copy));
    DressedUnitRecord* theirs = reinterpret_cast<DressedUnitRecord*>(copy);
    bool returned = s_constructHead(theirs) == theirs;
    DressedUnitRecord* result = unitset::ConstructHead(self);
    bool ok = returned && result == self && std::memcmp(copy, self, sizeof(copy)) == 0;
    ei::verify::Report("DressedUnitRecord::DressedUnitRecord", ok, ok ? "" : "other bytes");
    return result;
}
EI_VERIFY(0x00559DB0, ConstructHeadVerify, 5, s_constructHead);

// 00559DE0 (ours only): the items' storage, the last freed, is on top of its free list.
static void __fastcall DestroyHeadVerify(DressedUnitRecord* self) {
    std::uint32_t* items = self->items.begin();
    std::size_t bytes = self->items.Capacity() * sizeof(std::uint32_t);
    unitset::DestroyHead(self);
    bool ok = !items || bytes > 128 || *stlp_verify::FreeList(bytes) == items;
    ei::verify::Report("DressedUnitRecord::~DressedUnitRecord", ok, ok ? "" : "the items' storage is not on top of its free list");
}
EI_VERIFY(0x00559DE0, DestroyHeadVerify, 0, s_unused);

// 0055A860 / 0055A9F0 (ours only): the copy holds the same fields, ids and names in storage of its own.
static FiguredUnitRecord* __fastcall CopyFiguredVerify(FiguredUnitRecord* self, void*, const FiguredUnitRecord* from) {
    FiguredUnitRecord* result = unitset::CopyFigured(self, nullptr, from);
    bool ok = result == self && Keep(0, *self) == Keep(0, *from) &&
              (from->armourIds.size() == 0 || self->armourIds.begin() != from->armourIds.begin());
    ei::verify::Report("FiguredUnitRecord::FiguredUnitRecord(const&)", ok, ok ? "" : "not a copy");
    return result;
}
EI_VERIFY(0x0055A860, CopyFiguredVerify, 0, s_unused);

static DressedUnitRecord* __fastcall CopyDressedVerify(DressedUnitRecord* self, void*, const DressedUnitRecord* from) {
    DressedUnitRecord* result = unitset::CopyDressed(self, nullptr, from);
    bool ok = result == self && Keep(0, *self) == Keep(0, *from) &&
              (from->items.size() == 0 || self->items.begin() != from->items.begin());
    ei::verify::Report("DressedUnitRecord::DressedUnitRecord(const&)", ok, ok ? "" : "not a copy");
    return result;
}
EI_VERIFY(0x0055A9F0, CopyDressedVerify, 0, s_unused);

// 0055D020 (ours only: its first five bytes end in a JZ): against an element loop of its own.
static bool __fastcall EqualIdsVerify(const std::uint32_t* first, const std::uint32_t* last, const std::uint32_t* other) {
    bool expected = std::memcmp(first, other, static_cast<std::size_t>(last - first) * sizeof(std::uint32_t)) == 0;
    bool result = unitset::EqualIds(first, last, other);
    ei::verify::Check("stlp::equal<unsigned*>", expected, result, "%d ids", static_cast<int>(last - first));
    return result;
}
EI_VERIFY(0x0055D020, EqualIdsVerify, 0, s_unused);

// 0055C270 (ours only): a null place is skipped; else the CString there holds the same text.
static void __fastcall ConstructNameVerify(mfc::CString* p, const mfc::CString* x) {
    stlp::ConstructAt<mfc::CString>(p, x);
    bool ok = !p || std::strcmp(static_cast<const char*>(*p), static_cast<const char*>(*x)) == 0;
    ei::verify::Report("stlp::_Construct<CString>", ok, ok ? "" : "another text");
}
EI_VERIFY(0x0055C270, ConstructNameVerify, 0, s_unused);

// 0051FAD0 (ours only): returns this (the CString's release is MFC's, checked there).
static mfc::CString* __fastcall DeleteNameVerify(mfc::CString* self, void*, unsigned flags) {
    mfc::CString* result = unitset::DeleteName(self, nullptr, flags);
    ei::verify::Report("CString scalar deleting destructor", result == self, "not this");
    return result;
}
EI_VERIFY(0x0051FAD0, DeleteNameVerify, 0, s_unused);
