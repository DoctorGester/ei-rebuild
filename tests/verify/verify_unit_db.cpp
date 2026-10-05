// Checks for unit_db.cpp, the units database records' serialisers: prototypes, models and NPCs
// (startup: units.udb is loaded by 0053A200 in every scenario).
//
// Loading reads fields by id and leaves the archive as it was, so reading twice gives the same
// record: ours reads into the real record, its fields are taken (the bytes with the CString and
// vector slots left out, and a digest of every string's text), then the original reads into the
// same record again and the two are compared. The record keeps the original's reading. Writing
// appends to the archive: ours only.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/unit_db.h"

#include <cstdint>
#include <cstring>

namespace {

struct Layout {
    int size;
    const int* strings;      // offsets of the CStrings
    int stringCount;
    const int* vectors;      // offsets of the vector<CString>s
    int vectorCount;
};

struct Snapshot {
    unsigned char bytes[0x168];
    std::uint64_t digest;
};

const char* TextAt(const unsigned char* base, int offset) { return *reinterpret_cast<const char* const*>(base + offset); }

void Mix(std::uint64_t& digest, const char* text) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) digest = (digest ^ *p) * 0x100000001B3ull;
    digest = (digest ^ 0x100) * 0x100000001B3ull;   // separator
}

Snapshot Take(const void* record, const Layout& layout) {
    Snapshot s{};
    const auto* base = static_cast<const unsigned char*>(record);
    std::memcpy(s.bytes, base, static_cast<std::size_t>(layout.size));
    s.digest = 0xCBF29CE484222325ull;
    for (int i = 0; i < layout.stringCount; ++i) {
        Mix(s.digest, TextAt(base, layout.strings[i]));
        std::memset(s.bytes + layout.strings[i], 0, 4);
    }
    for (int i = 0; i < layout.vectorCount; ++i) {
        const auto* v = reinterpret_cast<const CStringVector*>(base + layout.vectors[i]);
        s.digest = (s.digest ^ static_cast<std::uint64_t>(v->end - v->begin)) * 0x100000001B3ull;
        for (const mfc::CString* text = v->begin; text != v->end; ++text) Mix(s.digest, *text);
        std::memset(s.bytes + layout.vectors[i], 0, sizeof(CStringVector));
    }
    return s;
}

template <typename Record>
void SerializeVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Record* self,
                     ChunkArchive* ar, const Layout& layout) {
    self->Serialize(ar);
    if (!ar->loading_) return;
    Snapshot ours = Take(self, layout);
    original(self, nullptr, ar);
    Snapshot theirs = Take(self, layout);
    int diff = -1;
    for (int i = 0; i < layout.size && diff < 0; ++i)
        if (ours.bytes[i] != theirs.bytes[i]) diff = i;
    bool same = diff < 0 && ours.digest == theirs.digest;
    ei::verify::Check(name, true, same, "\"%s\": %s +0x%X", static_cast<const char*>(self->name),
                      diff < 0 ? "strings differ" : "first difference at", diff < 0 ? 0 : diff);
}

constexpr int kPrototypeStrings[] = {0x00, 0x04, 0x34, 0x118, 0x11C};
constexpr int kPrototypeVectors[] = {0xE8, 0x100, 0x10C};
constexpr Layout kPrototypeLayout{sizeof(UnitPrototypeRecord), kPrototypeStrings, 5, kPrototypeVectors, 3};

constexpr int kModelStrings[] = {0x00, 0x88, 0x98, 0xA8, 0xB8, 0xC8, 0xD8, 0xE8, 0x118, 0x124};
constexpr int kModelVectors[] = {0xEC, 0xF8, 0x138};
constexpr Layout kModelLayout{sizeof(UnitModelRecord), kModelStrings, 10, kModelVectors, 3};

} // namespace

// 0052D740: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC]; PUSH EDI (7 bytes).
static void(__fastcall* s_prototypeSerialize)(UnitPrototypeRecord*, void*, ChunkArchive*);
static void __fastcall PrototypeSerializeVerify(UnitPrototypeRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitPrototypeRecord::Serialize", s_prototypeSerialize, self, ar, kPrototypeLayout);
}
EI_VERIFY(0x0052D740, PrototypeSerializeVerify, 7, s_prototypeSerialize);

// 0052DC70: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x14] (8 bytes).
static void(__fastcall* s_modelSerialize)(UnitModelRecord*, void*, ChunkArchive*);
static void __fastcall ModelSerializeVerify(UnitModelRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitModelRecord::Serialize", s_modelSerialize, self, ar, kModelLayout);
}
EI_VERIFY(0x0052DC70, ModelSerializeVerify, 8, s_modelSerialize);

namespace {
constexpr int kNpcStrings[] = {0x00};
constexpr int kNpcVectors[] = {0x24, 0x30, 0x3C, 0x48};
constexpr Layout kNpcLayout{sizeof(UnitNpcRecord), kNpcStrings, 1, kNpcVectors, 4};
} // namespace

// 0052DA60: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x10] (7 bytes).
static void(__fastcall* s_npcSerialize)(UnitNpcRecord*, void*, ChunkArchive*);
static void __fastcall NpcSerializeVerify(UnitNpcRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitNpcRecord::Serialize", s_npcSerialize, self, ar, kNpcLayout);
}
EI_VERIFY(0x0052DA60, NpcSerializeVerify, 7, s_npcSerialize);
