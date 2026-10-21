#pragma once

// The STLport hashtable instantiations whose values are game objects (ticket S11a): lib/stlp_hash.h's
// hashtable over traits of the game's ported types, registered in game_hashes.cpp at the
// instantiations' addresses; checked in game by verify_stlp_hash.cpp until S21d.

#include "ai_shape_cache.h"
#include "party.h"
#include "party_manager.h"
#include "record_vectors.h"
#include "unit_looks.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/stlp_hash.h"

#include <cstdint>

namespace gamehash {

// hash_map<int, CParty> (the party manager's parties, PartyMap): 0x78-byte nodes. A node's party
// is copied by CParty's copy constructor (006613C0; it reads its source non-const) and destroyed
// by its destructor body (006611D0).
struct PartyElement {
    static void Construct(PartyInPlace* to, const PartyInPlace& from) {
        to->Get()->CopyConstruct(const_cast<PartyInPlace&>(from).Get());
    }
    static void Destroy(PartyInPlace* p) { p->Get()->DestructBody(); }
};
using PartyTraits = stlp::DwordObjectTraits<PartyInPlace, PartyElement>;
// 004A91B0 (RET 4). hash_map<int, CParty>::operator[]: a party constructed on the stack, a pair of
// the key and a copy of it, find_or_insert of the pair (a new node copies it again), then the pair's
// party and the stack party destroyed. The node's party.
struct PartyTable : stlp::hashtable<PartyTraits> {
    PartyInPlace* At(const std::uint32_t* key);
};
using PartyAbi = stlp::HashtableAbi<PartyTraits>;

// A zone's unit looks (UnitSet, world.h): the figured records by key in a map per prototype (0x44-byte
// nodes; ~hashtable 0055AD10), those maps by prototype (0x1C-byte nodes), the dressed records by
// prototype (0x2C-byte nodes). The records' policies are record_vectors.h's.
using FiguredTable = stlp::hashtable<stlp::DwordObjectTraits<FiguredUnitRecord, recordvec::FiguredUnitElement>>;
struct FiguredTableElement {
    static void Construct(FiguredTable* to, const FiguredTable& from) { to->CopyConstruct(from); }
    static void Destroy(FiguredTable* p) { p->Destroy(); }
};
using FiguredGroupsTraits = stlp::DwordObjectTraits<FiguredTable, FiguredTableElement>;
using DressedTraits = stlp::DwordObjectTraits<DressedUnitRecord, recordvec::DressedUnitElement>;
using FiguredAbi = stlp::HashtableAbi<stlp::DwordObjectTraits<FiguredUnitRecord, recordvec::FiguredUnitElement>>;
using FiguredGroupsTable = stlp::hashtable<FiguredGroupsTraits>;
using FiguredGroupsAbi = stlp::HashtableAbi<FiguredGroupsTraits>;
using DressedTable = stlp::hashtable<DressedTraits>;
using DressedAbi = stlp::HashtableAbi<DressedTraits>;

// 0055ABB0 (RET 8). pair<const unsigned, FiguredTable>(key, table): the key, then a copy of the
// table (its functor bytes, then _M_copy_from). Returns this.
FiguredGroupsTable::Value* __fastcall ConstructGroupPair(FiguredGroupsTable::Value* self, void*, const std::uint32_t* key,
                                                         const FiguredTable* table);

// The unit shapes' cache (UnitShapeCache::Map, ai_shape_cache.h): pair<const handle, RefPtr>,
// 0x18-byte nodes. A key lands in bucket handle.key % count; two handles are equal when their
// heights compare equal or unordered (FCOMP, then TEST AH, 0x40). The pair is copied and destroyed
// by its compiler-written members (005BE370 / 005BE280, ai_movement.cpp).
using ShapePair = UnitShapeCache::Map::Pair;
struct ShapeTraits {
    using Value = ShapePair;
    using Key = ShapeCacheHandle;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k.key; }
    static bool Equal(const Key& a, const Key& b) {
        EI_NAN_AUDIT(a.height, b.height);
        return a.height == b.height;
    }
    static void Construct(Value* to, const Value& from) { ConstructShapePair(to, nullptr, &from.key, &from.value); }
    static void Destroy(Value* p) { DestroyShapePair(p); }
};
// 005BE090 (RET 4). The shape cache's operator[]: a pair of a copy of the handle and a null
// RefPtr, find_or_insert of it, the pair destroyed. The node's RefPtr.
struct ShapeTable : stlp::hashtable<ShapeTraits> {
    RefPtr* At(const ShapeCacheHandle* handle);
};
using ShapeAbi = stlp::HashtableAbi<ShapeTraits>;

// 005BCCB0 (returns this): a handle that has resolved nothing: no reference, no cache, key -1 (the
// height is left as it is).
inline ShapeCacheHandle* __fastcall InitShapeHandle(ShapeCacheHandle* self) {
    self->ref = nullptr;
    self->cache = nullptr;
    self->key = 0xFFFFFFFFu;
    return self;
}

// The cache's map as the table the instantiations above run on (the same 0x14 bytes).
inline ShapeTable* ShapesOf(UnitShapeCache* cache) { return reinterpret_cast<ShapeTable*>(&cache->shapes); }

} // namespace gamehash
