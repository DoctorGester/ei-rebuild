// The STLport hashtable instantiations whose values are game objects (game_hashes.h).
#include "game_hashes.h"

#include "ei/hook.h"

namespace gamehash {

PartyInPlace* PartyTable::At(const std::uint32_t* key) {
    PartyInPlace local;
    local.Get()->Construct();
    PartyTable::Value pair;
    pair.key = *key;
    pair.mapped.Get()->CopyConstruct(local.Get());
    PartyInPlace* result = &FindOrInsert(pair).mapped;
    pair.mapped.Get()->DestructBody();
    local.Get()->DestructBody();
    return result;
}

RefPtr* ShapeTable::At(const ShapeCacheHandle* handle) {
    RefPtr none{nullptr};
    alignas(ShapePair) unsigned char storage[sizeof(ShapePair)];
    ShapePair* pair = reinterpret_cast<ShapePair*>(storage);
    ConstructShapePair(pair, nullptr, handle, &none);
    RefPtr* result = &FindOrInsert(*pair).value;
    DestroyShapePair(pair);
    return result;
}

FiguredGroupsTable::Value* __fastcall ConstructGroupPair(FiguredGroupsTable::Value* self, void*, const std::uint32_t* key,
                                                         const FiguredTable* table) {
    self->key = *key;
    self->mapped.CopyConstruct(*table);
    return self;
}

} // namespace gamehash

EI_REWRITTEN(0x005BCCB0, gamehash::InitShapeHandle);
EI_REWRITTEN(0x005BE090, gamehash::ShapeTable::At);
EI_REWRITTEN(0x005BE320, gamehash::ShapeAbi::Find);
EI_REWRITTEN(0x005BE730, gamehash::ShapeAbi::FindOrInsert);
EI_REWRITTEN(0x004A91B0, gamehash::PartyTable::At);
EI_REWRITTEN(0x004ABA70, gamehash::PartyAbi::DeleteNode);
EI_REWRITTEN(0x004AD050, gamehash::PartyAbi::Clear);
