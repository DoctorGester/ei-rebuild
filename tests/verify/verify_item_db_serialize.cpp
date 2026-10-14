// Checks for item_db_serialize.cpp (A-H23): the item records' Serialize. Reached at startup (every
// scenario): the item database loader (006A67A0, Rebirth-patched) reads the item tables through the
// vector serialisers, which call these for every record.
//
// As verify_spell_db_load.cpp: loading reads fields by id and leaves the archive as it was, so ours
// reads into the real record, a digest of what the serialiser reads is taken, then the original
// reads into the same record again and the digests are compared (strings by content). Writing
// appends: ours only (the current chunk is the same afterwards).
#include "verify.h"

#include "../../src/game/chunk_archive.h"
#include "../../src/game/item_db.h"

#include <cstdint>
#include <cstring>

using namespace itemdb;

namespace {

constexpr std::uint64_t kSeed = 0xCBF29CE484222325ull;

void Mix(std::uint64_t& digest, const void* bytes, std::size_t length) {
    const auto* p = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < length; ++i) digest = (digest ^ p[i]) * 0x100000001B3ull;
    digest = (digest ^ 0x100) * 0x100000001B3ull;   // separator
}
void MixText(std::uint64_t& digest, const char* text) { Mix(digest, text, text ? std::strlen(text) : 0); }

std::uint64_t HeadDigest(const ItemRecordHead* r) {
    std::uint64_t d = kSeed;
    MixText(d, r->text04);
    MixText(d, r->text08);
    MixText(d, r->materialClass);
    Mix(d, &r->type, 4);
    Mix(d, &r->field4, 12);
    Mix(d, &r->price, offsetof(ItemRecordHead, list50) - offsetof(ItemRecordHead, price));
    Mix(d, &r->classMask, 5);
    return d;
}
std::uint64_t Digest(const ItemRecordHead* r) { return HeadDigest(r); }
std::uint64_t Digest(const WeaponRecord* r) {
    std::uint64_t d = HeadDigest(r);
    Mix(d, &r->category, sizeof(WeaponRecord) - offsetof(WeaponRecord, category));
    return d;
}
std::uint64_t Digest(const ArmorRecord* r) {
    std::uint64_t d = HeadDigest(r);
    Mix(d, &r->category, offsetof(ArmorRecord, showsWounds) + 1 - offsetof(ArmorRecord, category));
    Mix(d, &r->layer, 4 + 3 + 8);
    return d;
}
std::uint64_t Digest(const QuickItemRecord* r) {
    std::uint64_t d = HeadDigest(r);
    Mix(d, &r->category, offsetof(QuickItemRecord, text7C) - offsetof(QuickItemRecord, category));
    MixText(d, r->text7C);
    Mix(d, r->attributeBonus, 3 + 8);
    return d;
}
std::uint64_t Digest(const QuestItemRecord* r) {
    std::uint64_t d = HeadDigest(r);
    Mix(d, &r->category, 4);
    d = (d ^ static_cast<std::uint64_t>(r->zonesEnd - r->zonesBegin)) * 0x100000001B3ull;
    for (const mfc::CString* z = r->zonesBegin; z != r->zonesEnd; ++z) MixText(d, *z);
    return d;
}
std::uint64_t Digest(const LootItemRecord* r) {
    std::uint64_t d = HeadDigest(r);
    Mix(d, &r->category, 12);
    return d;
}
std::uint64_t Digest(const MaterialRecordHead* r) {
    std::uint64_t d = kSeed;
    MixText(d, r->key);
    MixText(d, r->soundName);
    MixText(d, r->name);
    Mix(d, &r->number, sizeof(MaterialRecordHead) - offsetof(MaterialRecordHead, number));
    return d;
}

template <typename Record, typename Ours>
void SerializeVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Ours ours,
                     Record* self, ChunkArchive* ar) {
    ChunkArchive::Ref* before = ar->Current();
    ours(self, ar);
    if (!ar->loading_) {
        ei::verify::Check(name, true, ar->Current() == before, "save");
        return;
    }
    const std::uint64_t mine = Digest(self);
    original(self, nullptr, ar);
    const std::uint64_t theirs = Digest(self);
    ei::verify::Check(name, true, mine == theirs && ar->Current() == before, "record %p", static_cast<void*>(self));
}

#define SERIALIZE_VERIFY(Type, Call, Label)                                                                   \
    void(__fastcall* s_##Type)(Type*, void*, ChunkArchive*);                                                  \
    void __fastcall Type##Verify(Type* self, void*, ChunkArchive* ar) {                                       \
        SerializeVerify(Label, s_##Type, [](Type* r, ChunkArchive* a) { r->Call(a); }, self, ar);             \
    }

SERIALIZE_VERIFY(MaterialRecordHead, Serialize, "MaterialRecord::Serialize")
SERIALIZE_VERIFY(ItemRecordHead, SerializeHead, "ItemRecordHead::SerializeHead")
SERIALIZE_VERIFY(WeaponRecord, Serialize, "WeaponRecord::Serialize")
SERIALIZE_VERIFY(ArmorRecord, Serialize, "ArmorRecord::Serialize")
SERIALIZE_VERIFY(QuickItemRecord, Serialize, "QuickItemRecord::Serialize")
SERIALIZE_VERIFY(QuestItemRecord, Serialize, "QuestItemRecord::Serialize")
SERIALIZE_VERIFY(LootItemRecord, Serialize, "LootItemRecord::Serialize")

} // namespace

// Stolen bytes: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6); 0069F700 PUSH ESI; MOV ESI,ECX; PUSH EDI;
// MOV EDI,[ESP+0xC] (8); 0069F9C0 PUSH EBP; MOV EBP,[ESP+8]; PUSH ESI (6).
EI_VERIFY(0x0069F620, MaterialRecordHeadVerify, 6, s_MaterialRecordHead);
EI_VERIFY(0x0069F700, ItemRecordHeadVerify, 8, s_ItemRecordHead);
EI_VERIFY(0x0069F800, WeaponRecordVerify, 6, s_WeaponRecord);
EI_VERIFY(0x0069F8C0, ArmorRecordVerify, 6, s_ArmorRecord);
EI_VERIFY(0x0069F940, QuickItemRecordVerify, 6, s_QuickItemRecord);
EI_VERIFY(0x0069F9C0, QuestItemRecordVerify, 6, s_QuestItemRecord);
EI_VERIFY(0x0069FA90, LootItemRecordVerify, 6, s_LootItemRecord);
