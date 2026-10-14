// Checks for the spell database's records and vectors in an archive (item_db_load.cpp, A-H21:
// 00683B80, 00683AF0, 00683D40, 00683EB0 and the vectors 00684EF0 / 00684FE0 / 00684C50). Reached at
// startup: the spell database loader (00684550, Rebirth-patched) reads spells.sdb through them.
//
// As verify_quest_records.cpp: a record's loading reads fields by id and leaves the archive as it
// was, so ours reads into the real record, a digest of it is taken, then the original reads into
// the same record again and the digests are compared (the record keeps the original's reading).
// Writing appends: ours only (the current chunk is the same afterwards). The vectors allocate
// records: ours only (the current chunk is the same afterwards; each record went through its
// wrapper).
#include "verify.h"

#include "../../src/game/chunk_archive.h"
#include "../../src/game/item_db.h"

#include <cstdint>
#include <cstring>

using namespace itemdb;

namespace {
void* s_unused;

constexpr std::uint64_t kSeed = 0xCBF29CE484222325ull;

void Mix(std::uint64_t& digest, const void* bytes, std::size_t length) {
    const auto* p = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < length; ++i) digest = (digest ^ p[i]) * 0x100000001B3ull;
    digest = (digest ^ 0x100) * 0x100000001B3ull;   // separator
}
void Mix(std::uint64_t& digest, const mfc::CString& text) {
    Mix(digest, static_cast<const char*>(text), static_cast<std::size_t>(text.GetLength()));
}
void Mix(std::uint64_t& digest, const stlp::string& text) { Mix(digest, text.data(), text.size()); }
void Mix(std::uint64_t& digest, const stlp::vector<int>& values) {
    Mix(digest, values.begin(), values.size() * sizeof(int));
}
void Mix(std::uint64_t& digest, const stlp::vector<stlp::string>& list) {
    digest = (digest ^ list.size()) * 0x100000001B3ull;
    for (const stlp::string& text : list) Mix(digest, text);
}

std::uint64_t Digest(const SpellRecord* r) {
    std::uint64_t d = kSeed;
    Mix(d, r->name);
    Mix(d, r->code);
    Mix(d, r->text08);
    Mix(d, &r->price, offsetof(SpellRecord, list48) - offsetof(SpellRecord, price));
    Mix(d, r->list48);
    Mix(d, r->list54);
    Mix(d, &r->icon, offsetof(SpellRecord, unused_7D) - offsetof(SpellRecord, icon));
    Mix(d, &r->classMask, 4);
    return d;
}
std::uint64_t Digest(const SpellModifierRecord* r) {
    std::uint64_t d = kSeed;
    Mix(d, r->name);
    Mix(d, r->code, offsetof(SpellModifierRecord, text1C) - offsetof(SpellModifierRecord, code));
    Mix(d, r->text1C);
    Mix(d, &r->classMask, 4);
    return d;
}
std::uint64_t Digest(const SpellListRecord* r) {
    std::uint64_t d = kSeed;
    Mix(d, r->name);
    Mix(d, r->list0C);
    Mix(d, r->list18);
    Mix(d, r->text24);
    Mix(d, &r->value30, 4);
    return d;
}
const char* NameOf(const SpellRecord* r) { return r->name; }
const char* NameOf(const SpellModifierRecord* r) { return r->name; }
const char* NameOf(const SpellListRecord* r) { return r->name.data(); }
int NameLength(const SpellRecord* r) { return r->name.GetLength(); }
int NameLength(const SpellModifierRecord* r) { return r->name.GetLength(); }
int NameLength(const SpellListRecord* r) { return static_cast<int>(r->name.size()); }

template <typename Record>
void SerializeVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Record* self,
                     ChunkArchive* ar) {
    ChunkArchive::Ref* before = ar->Current();
    self->Serialize(ar);
    if (!ar->loading_) {
        ei::verify::Check(name, true, ar->Current() == before, "save");
        return;
    }
    std::uint64_t ours = Digest(self);
    original(self, nullptr, ar);
    std::uint64_t theirs = Digest(self);
    ei::verify::Check(name, true, ours == theirs && ar->Current() == before, "%.*s", NameLength(self), NameOf(self));
}

} // namespace

// 00683B80 / 00683AF0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_spellSerialize)(SpellRecord*, void*, ChunkArchive*);
static void __fastcall SpellSerializeVerify(SpellRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("SpellRecord::Serialize", s_spellSerialize, self, ar);
}
EI_VERIFY(0x00683B80, SpellSerializeVerify, 6, s_spellSerialize);

static void(__fastcall* s_modifierSerialize)(SpellModifierRecord*, void*, ChunkArchive*);
static void __fastcall ModifierSerializeVerify(SpellModifierRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("SpellModifierRecord::Serialize", s_modifierSerialize, self, ar);
}
EI_VERIFY(0x00683AF0, ModifierSerializeVerify, 6, s_modifierSerialize);

// 00683D40: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes).
static void(__fastcall* s_listSerialize)(SpellListRecord*, void*, ChunkArchive*);
static void __fastcall ListSerializeVerify(SpellListRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("SpellListRecord::Serialize", s_listSerialize, self, ar);
}
EI_VERIFY(0x00683D40, ListSerializeVerify, 8, s_listSerialize);

// 00683EB0: MOV EAX,[ESP+4]; PUSH EBP; MOV EBP,ECX (7 bytes). Loading: ours, then the original into
// the same vector; the values compared.
static void(__fastcall* s_intVectorField)(ChunkArchive*, void*, int, stlp::vector<int>*, void*);
static void __fastcall IntVectorFieldVerify(ChunkArchive* self, void*, int id, stlp::vector<int>* values, void* unused) {
    ChunkArchive::Ref* before = self->Current();
    self->IntVectorField(id, values, unused);
    if (!self->loading_) {
        ei::verify::Check("ChunkArchive::IntVectorField", true, self->Current() == before, "save");
        return;
    }
    std::uint64_t ours = kSeed;
    Mix(ours, *values);
    s_intVectorField(self, nullptr, id, values, unused);
    std::uint64_t theirs = kSeed;
    Mix(theirs, *values);
    ei::verify::Check("ChunkArchive::IntVectorField", true, ours == theirs && self->Current() == before, "chunk %d: %u values",
                      id, static_cast<unsigned>(values->size()));
}
EI_VERIFY(0x00683EB0, IntVectorFieldVerify, 7, s_intVectorField);

// 00684EF0 / 00684FE0 / 00684C50 (stolen 0): the current chunk is the same afterwards.
template <void (ChunkArchive::*Serialize)(int, void*, void*)>
static void __fastcall VectorVerify(ChunkArchive* self, void*, int id, void* records, void* unused) {
    ChunkArchive::Ref* before = self->Current();
    (self->*Serialize)(id, records, unused);
    ei::verify::Check("ChunkArchive::SerializeSpell*", true, self->Current() == before, "chunk %d", id);
}
EI_VERIFY(0x00684EF0, VectorVerify<&ChunkArchive::SerializeSpells>, 0, s_unused);
EI_VERIFY(0x00684FE0, VectorVerify<&ChunkArchive::SerializeSpellModifiers>, 0, s_unused);
EI_VERIFY(0x00684C50, VectorVerify<&ChunkArchive::SerializeSpellLists>, 0, s_unused);
