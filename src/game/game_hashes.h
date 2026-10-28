#pragma once

// The STLport hashtable instantiations whose values are game objects: lib/stlp_hash.h's hashtable over traits of
// the game's types, registered in game_hashes.cpp.

#include "ai_shape_cache.h"
#include "party.h"
#include "party_manager.h"
#include "record_vectors.h"
#include "unit_looks.h"
#include "world.h"

#include "lib/stlp_hash.h"

#include <cstdint>

namespace gamehash {

// hash_map<int, CParty> (the party manager's parties): PartyTable, party_manager.h.
using PartyAbi = stlp::HashtableAbi<PartyTraits>;

// A zone's unit looks (UnitSet, world.h): the figured records by key in a map per prototype (0x44-byte
// nodes), those maps by prototype (0x1C-byte nodes), the dressed records by prototype (0x2C-byte nodes).
// The records' policies are record_vectors.h's.
using FiguredTable = UnitSet::FiguredTable;
struct FiguredTableElement {
    static void Construct(FiguredTable* to, const FiguredTable& from) { to->CopyConstruct(from); }
    static void Destroy(FiguredTable* p) { p->Destroy(); }
};
using FiguredGroupsTraits = stlp::DwordObjectTraits<FiguredTable, FiguredTableElement>;
using DressedTraits = stlp::DwordObjectTraits<DressedUnitRecord, recordvec::DressedUnitElement>;
using FiguredAbi = stlp::HashtableAbi<stlp::DwordObjectTraits<FiguredUnitRecord, recordvec::FiguredUnitElement>>;
using FiguredGroupsTable = UnitSet::FiguredGroups;
using FiguredGroupsAbi = stlp::HashtableAbi<FiguredGroupsTraits>;
using DressedTable = UnitSet::DressedTable;
using DressedAbi = stlp::HashtableAbi<DressedTraits>;

struct FiguredGroupPair : FiguredGroupsTable::Value {
    // Pair<const unsigned, FiguredTable>(key, table): the key, then a copy of the
    // table (its functor bytes, then _M_copy_from). Returns this.
    FiguredGroupPair* Construct(const std::uint32_t* from, const FiguredTable* table);
};

// The unit shapes' cache's table (ShapeTable, ai_shape_cache.h).
using ShapeAbi = stlp::HashtableAbi<ShapeTraits>;

// A handle that has resolved nothing: no reference, no cache, key -1 (the height is left as it is).
inline ShapeCacheHandle* __fastcall InitShapeHandle(ShapeCacheHandle* self) {
    self->ref = nullptr;
    self->cache = nullptr;
    self->key = 0xFFFFFFFFu;
    return self;
}

} // namespace gamehash
