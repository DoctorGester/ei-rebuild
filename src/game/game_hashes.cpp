// The STLport hashtable instantiations whose values are game objects (game_hashes.h).
#include "lib/static_object.h"
#include "game_hashes.h"

namespace gamehash {

void PartyElement::Construct(PartyInPlace* to, const PartyInPlace& from) {
    new (to->Get()) CParty(*const_cast<PartyInPlace&>(from).Get());
}
void PartyElement::Destroy(PartyInPlace* p) { p->Get()->~CParty(); }

PartyInPlace* PartyTable::At(const int* key) {
    CParty local;
    PartyTable::Value pair;
    pair.key = static_cast<std::uint32_t>(*key);
    new (pair.mapped.Get()) CParty(local);
    PartyInPlace* result = &FindOrInsert(pair).mapped;
    pair.mapped.Get()->~CParty();
    return result;
}

} // namespace gamehash

RefPtr* ShapeTable::At(const ShapeCacheHandle* handle) {
    RefPtr none{nullptr};
    lib::InPlace<ShapePair> storage;
    ShapePair* pair = storage.Get();
    pair->Construct(handle, &none);
    RefPtr* result = &FindOrInsert(*pair).value;
    DestroyShapePair(pair);
    return result;
}
