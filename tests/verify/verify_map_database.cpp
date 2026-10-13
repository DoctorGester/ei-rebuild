// Differential check for the map database loader (map_database.cpp, 0045EA20).
//
// The loader appends to the database and continues the last map of an earlier file, so its result
// depends on everything loaded before. The original runs on a shadow database (made with the
// original constructor, 0045E820, the first time the real one is seen empty) and ours on the real
// one, for every call; reading the text twice has no other effect (the text manager's cursor is
// reset by Open and Close). After each call the two databases are compared in depth: every allod
// and map, their CStrings by text, their arrays (the allods' map lists, the maps' exits) element by
// element. A database first seen non-empty is checked by ours alone (array sizes only).
#include "verify.h"

#include "../../src/game/map_database.h"
#include "../../src/game/object_tables.h"

#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

using namespace mapdb;

__declspec(naked) void __fastcall ConstructDatabase(CMapDatabase*) EI_ORIGINAL(0x0045E820)

struct Diff {
    char where[200] = "";
    bool failed = false;
    void Fail(const char* format, ...) {
        if (failed) return;
        failed = true;
        va_list ap;
        va_start(ap, format);
        std::vsnprintf(where, sizeof(where), format, ap);
        va_end(ap);
    }
};

const char* At(const void* p, std::size_t offset) { return static_cast<const char*>(p) + offset; }
int IntAt(const void* p, std::size_t offset) {
    int v;
    std::memcpy(&v, At(p, offset), 4);
    return v;
}

void Strings(Diff& d, const char* what, int index, const void* a, const void* b, std::size_t offset, int count) {
    for (int i = 0; i < count && !d.failed; ++i) {
        const char* x = *reinterpret_cast<const char* const*>(At(a, offset + i * 4));
        const char* y = *reinterpret_cast<const char* const*>(At(b, offset + i * 4));
        if (std::strcmp(x, y) != 0) d.Fail("%s %d +0x%X: \"%.40s\" vs \"%.40s\"", what, index, unsigned(offset + i * 4), x, y);
    }
}

void Bytes(Diff& d, const char* what, int index, const void* a, const void* b, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to && !d.failed; ++i)
        if (At(a, i)[0] != At(b, i)[0])
            d.Fail("%s %d +0x%X: %02X vs %02X", what, index, unsigned(i), At(a, i)[0] & 0xFF, At(b, i)[0] & 0xFF);
}

// An MFC CArray at `offset` (vtable, data, size, max, grow): all but the data pointer.
bool SameHeader(Diff& d, const char* what, int index, const void* a, const void* b, std::size_t offset) {
    Bytes(d, what, index, a, b, offset, offset + 4);
    Bytes(d, what, index, a, b, offset + 8, offset + 0x14);
    return !d.failed;
}
const char* DataOf(const void* p, std::size_t offset) { return *reinterpret_cast<const char* const*>(At(p, offset + 4)); }

void CompareAllod(Diff& d, int i, const char* a, const char* b) {
    Strings(d, "allod", i, a, b, allod::kNames, 6);
    if (!SameHeader(d, "allod maps", i, a, b, allod::kMaps)) return;
    int n = IntAt(a, allod::kMaps + 8);
    if (n > 0 && std::memcmp(DataOf(a, allod::kMaps), DataOf(b, allod::kMaps), n * 4) != 0) d.Fail("allod %d map list", i);
}

void CompareMap(Diff& d, int i, const char* a, const char* b) {
    Strings(d, "map", i, a, b, map::kName, 8);
    Bytes(d, "map", i, a, b, map::kSizeX, map::kExits);
    if (!SameHeader(d, "map exits", i, a, b, map::kExits)) return;
    int n = IntAt(a, map::kExits + 8);
    for (int e = 0; e < n && !d.failed; ++e) {
        const char* x = DataOf(a, map::kExits) + e * exit::kSize;
        const char* y = DataOf(b, map::kExits) + e * exit::kSize;
        Bytes(d, "exit", e, x, y, 0, exit::kTarget);
        Strings(d, "exit", e, x, y, exit::kTarget, 1);
        Bytes(d, "exit", e, x, y, exit::kTargetIndex, exit::kSize);
    }
}

void Compare(Diff& d, const CMapDatabase& a, const CMapDatabase& b) {
    if (a.allods.size != b.allods.size || a.allods.maxSize != b.allods.maxSize || a.maps.size != b.maps.size ||
        a.maps.maxSize != b.maps.maxSize) {
        d.Fail("allods %d/%d (max %d/%d), maps %d/%d (max %d/%d)", a.allods.size, b.allods.size, a.allods.maxSize,
               b.allods.maxSize, a.maps.size, b.maps.size, a.maps.maxSize, b.maps.maxSize);
        return;
    }
    for (int i = 0; i < a.allods.size && !d.failed; ++i)
        CompareAllod(d, i, reinterpret_cast<const char*>(a.allods.data) + i * allod::kSize,
                     reinterpret_cast<const char*>(b.allods.data) + i * allod::kSize);
    for (int i = 0; i < a.maps.size && !d.failed; ++i)
        CompareMap(d, i, reinterpret_cast<const char*>(a.maps.data) + i * map::kSize,
                   reinterpret_cast<const char*>(b.maps.data) + i * map::kSize);
}

CMapDatabase* s_real;     // the database the shadow follows
CMapDatabase* s_shadow;

// 0045EA20: PUSH -1 (2), PUSH imm32 (5). Only AL is the result.
int(__fastcall* s_load)(CMapDatabase*, void*, const char*);
void ShadowIfEmpty(CMapDatabase* self) {
    if (!s_shadow && self->allods.size == 0 && self->maps.size == 0 && !self->allods.data && !self->maps.data) {
        s_real = self;
        s_shadow = static_cast<CMapDatabase*>(gamecrt::operator_new(sizeof(CMapDatabase)));
        ConstructDatabase(s_shadow);
    }
}
bool __fastcall LoadVerify(CMapDatabase* self, void*, const char* fileName) {
    ShadowIfEmpty(self);
    bool shadowed = self == s_real;
    bool expected = shadowed ? (s_load(s_shadow, nullptr, fileName) & 0xFF) != 0 : false;
    bool actual = self->Load(fileName);
    if (!shadowed) {
        ei::verify::Check("CMapDatabase::Load (ours only)", true, self->allods.size >= 0 && self->maps.size >= 0, "%s",
                          fileName ? fileName : "(null)");
        return actual;
    }
    Diff d;
    if (expected != actual) d.Fail("result %d vs %d", expected, actual);
    Compare(d, *s_shadow, *self);
    ei::verify::Check("CMapDatabase::Load", true, !d.failed, "%s: %s", fileName ? fileName : "(null)", d.where);
    return actual;
}

// 0045EA00 (WinMain's load of both files; A-E1). Ours calls Load directly, so the loader's check
// above is not reached from it: the original's two loads run on the shadow here (the same file
// names, written out again), ours on the real database, and the two are compared in depth.
void* s_unused;
bool __fastcall LoadAllVerify(CMapDatabase* self) {
    ShadowIfEmpty(self);
    bool shadowed = self == s_real;
    bool expected = false;
    if (shadowed) {
        s_load(s_shadow, nullptr, "map.txt");
        expected = (s_load(s_shadow, nullptr, "map-LMP.txt") & 0xFF) != 0;
    }
    bool actual = self->LoadAll();
    Diff d;
    if (shadowed) {
        if (expected != actual) d.Fail("result %d vs %d", expected, actual);
        Compare(d, *s_shadow, *self);
    }
    ei::verify::Check(shadowed ? "CMapDatabase::LoadAll" : "CMapDatabase::LoadAll (ours only)", true, !d.failed, "%s", d.where);
    return actual;
}

} // namespace

EI_VERIFY(0x0045EA20, LoadVerify, 7, s_load);
EI_VERIFY(0x0045EA00, LoadAllVerify, 0, s_unused);
