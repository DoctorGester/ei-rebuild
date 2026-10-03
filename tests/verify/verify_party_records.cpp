// Differential checks for the party records (src/game/party_records.cpp). Find runs both versions;
// the others create or release records and run ours only, followed by a check of the vector.
#include "verify.h"

#include "../../src/game/party_records.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

const char* NameOf(const PartyRecord* r) { return static_cast<const char*>(r->name); }
int Count(const PartyRecordList* l) { return static_cast<int>(l->finish - l->start); }

// 004A5E10: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,ECX (6 bytes).
void*(__fastcall* s_find)(const PartyRecordList*, void*, const char*);
void* __fastcall FindVerify(const PartyRecordList* self, void*, const char* name) {
    void* expected = s_find(self, nullptr, name);
    void* actual = PartyRecordsFind(self, nullptr, name);
    ei::verify::Check("PartyRecordsFind", expected, actual, "\"%s\" in %d records", name, Count(self));
    return expected;
}

// 004A5E90: ours only; one more record, named `name`, referenced once.
void __fastcall AddVerify(PartyRecordList* self, void*, const char* name) {
    std::vector<char> copy(name, name + std::strlen(name) + 1);
    int before = Count(self);
    PartyRecordsAdd(self, nullptr, name);
    const PartyRecord* last = self->finish[-1];
    bool ok = Count(self) == before + 1 && last && last->refCount == 1 && std::strcmp(NameOf(last), copy.data()) == 0;
    ei::verify::Check("PartyRecordsAdd", true, ok, "%d -> %d records", before, Count(self));
}

// 004A5FA0: ours only; the remaining pointers are those the original's walk leaves (a matching
// record is erased and the one moved into its slot is skipped).
void __fastcall RemoveVerify(PartyRecordList* self, void*, const char* name) {
    std::vector<PartyRecord*> expected(self->start, self->finish);
    std::vector<bool> matches;
    for (PartyRecord* r : expected) matches.push_back(std::strcmp(NameOf(r), name) == 0);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (matches[i]) {
            expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(i));
            matches.erase(matches.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    PartyRecordsRemove(self, nullptr, name);
    bool ok = static_cast<std::size_t>(Count(self)) == expected.size() &&
              (expected.empty() || std::memcmp(self->start, expected.data(), expected.size() * sizeof(void*)) == 0);
    ei::verify::Check("PartyRecordsRemove", true, ok, "%d records left, %u expected", Count(self),
                      static_cast<unsigned>(expected.size()));
}

// 004A5BD0: ours only; one unnamed record referenced once, both strings empty.
void __fastcall ResetVerify(PartyRecordList* self) {
    PartyRecordsReset(self);
    bool ok = Count(self) == 1 && self->start[0]->refCount == 1 && NameOf(self->start[0])[0] == 0 &&
              static_cast<const char*>(self->text0C)[0] == 0 && static_cast<const char*>(self->text10)[0] == 0;
    ei::verify::Check("PartyRecordsReset", true, ok, "%d records", Count(self));
}

// ---- The party manager: ours only ---------------------------------------------------------------

int Records(void* manager) { return Count(reinterpret_cast<PartyRecordList*>(static_cast<char*>(manager) + 0x24)); }
int Templates(void* manager) {
    auto* v = reinterpret_cast<void**>(static_cast<char*>(manager) + 0x38);
    return static_cast<int>(static_cast<void**>(v[1]) - static_cast<void**>(v[0]));
}

void __fastcall NewGameVerify(void* manager) {
    PartyManagerNewGame(manager);
    ei::verify::Check("PartyManagerNewGame", true, Records(manager) == 1 && Templates(manager) == 5, "%d records, %d templates",
                      Records(manager), Templates(manager));
}
void __fastcall NewGameFourVerify(void* manager) {
    PartyManagerNewGameFour(manager);
    ei::verify::Check("PartyManagerNewGameFour", true, Records(manager) == 1 && Templates(manager) == 4,
                      "%d records, %d templates", Records(manager), Templates(manager));
}
void __fastcall BeginRedeployVerify(void* manager, void*, void* players) {
    int before = Records(manager);
    PartyManagerBeginRedeploy(manager, nullptr, players);
    ei::verify::Check("PartyManagerBeginRedeploy", before, Records(manager), "records");
}
void __fastcall EndRedeployVerify(void* manager, void*, void* players) {
    int before = Records(manager);
    PartyManagerEndRedeploy(manager, nullptr, players);
    ei::verify::Check("PartyManagerEndRedeploy", before, Records(manager), "records");
}

int __stdcall NextIdVerify(int* out) {
    int before = *reinterpret_cast<int*>(0x0078B0E8);
    int r = PartyNextId(out);
    bool ok = *out == before && r == before + 1 && *reinterpret_cast<int*>(0x0078B0E8) == r;
    ei::verify::Check("PartyNextId", true, ok, "%d -> %d", before, r);
    return r;
}

// Whether the map stores `key` (the bucket chain walked independently of the rewrite).
bool Stored(const void* map, int key) {
    auto* buckets = *reinterpret_cast<int* const* const*>(static_cast<const char*>(map) + 4);
    auto* end = *reinterpret_cast<int* const* const*>(static_cast<const char*>(map) + 8);
    unsigned count = static_cast<unsigned>(end - buckets);
    if (count == 0) return false;
    for (const int* node = buckets[static_cast<unsigned>(key) % count]; node; node = reinterpret_cast<const int*>(node[0]))
        if (node[1] == key) return true;
    return false;
}

bool __fastcall MapLoadVerify(const void* map, void*, int a, int b, const int* key, void* target) {
    bool expected = *key == 0 || Stored(map, *key);
    bool r = PartyMapLoad(map, nullptr, a, b, key, target);
    ei::verify::Check("PartyMapLoad", expected, r, "key %d", *key);
    return r;
}
bool __fastcall MapStoreVerify(void* map, void*, const int* key, const void* source) {
    bool r = PartyMapStore(map, nullptr, key, source);
    ei::verify::Check("PartyMapStore", true, r && Stored(map, *key), "key %d", *key);
    return r;
}

} // namespace

EI_VERIFY(0x004A88A0, NewGameVerify, 0, s_unused);
EI_VERIFY(0x004A88C0, NewGameFourVerify, 0, s_unused);
EI_VERIFY(0x004A8890, BeginRedeployVerify, 0, s_unused);
EI_VERIFY(0x004A8880, EndRedeployVerify, 0, s_unused);
EI_VERIFY(0x004A8910, NextIdVerify, 0, s_unused);
EI_VERIFY(0x004A8790, MapLoadVerify, 0, s_unused);
EI_VERIFY(0x004A8800, MapStoreVerify, 0, s_unused);
EI_VERIFY(0x004A5E10, FindVerify, 6, s_find);
EI_VERIFY(0x004A5E90, AddVerify, 0, s_unused);
EI_VERIFY(0x004A5FA0, RemoveVerify, 0, s_unused);
EI_VERIFY(0x004A5BD0, ResetVerify, 0, s_unused);
