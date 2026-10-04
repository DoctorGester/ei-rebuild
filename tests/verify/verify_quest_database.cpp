// Checks for the quest database load (quest_database.cpp).
#include "verify.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

bool __fastcall QuestDatabaseLoad(char* db, void*, const void* image);

namespace {

template <typename T>
T At(const char* base, std::size_t offset) {
    T v;
    std::memcpy(&v, base + offset, sizeof(T));
    return v;
}

// The database object (0x40 bytes): constructor 006AE0F0, destructor 006ADF40.
__declspec(naked) void* __fastcall DatabaseConstruct(void*) {
    __asm mov eax, 0x006AE0F0
    __asm jmp eax
}
__declspec(naked) void __fastcall DatabaseDestroy(void*) {
    __asm mov eax, 0x006ADF40
    __asm jmp eax
}

std::string Str(const char* stl) {   // STLport string: start, finish
    const char* start = At<const char*>(stl, 0);
    const char* finish = At<const char*>(stl, 4);
    return start ? std::string(start, finish) : std::string();
}

void Dword(std::string& out, const char* p, std::size_t offset) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X ", At<std::uint32_t>(p, offset));
    out += buf;
}

// A record's contents as text: quests (0x38) and briefings (0x6C).
std::string Quest(const char* q) {
    std::string s = "Q[" + Str(q) + "] ";
    for (std::size_t o : {0x0Cu, 0x10u, 0x14u, 0x18u, 0x1Cu}) Dword(s, q, o);
    for (const char* p = At<const char*>(q, 0x20); p && p != At<const char*>(q, 0x24); p += 4) Dword(s, p, 0);
    return s + "[" + Str(q + 0x2C) + "]";
}
std::string Briefing(const char* b) {
    std::string s = "B[" + Str(b) + "] ";
    for (std::size_t o : {0x0Cu, 0x10u, 0x68u}) Dword(s, b, o);
    for (int i = 0; i < 6; ++i) {
        const char* list = b + 0x14 + i * 12;
        s += "{";
        for (const char* p = At<const char*>(list, 0); p && p != At<const char*>(list, 4); p += 4) {
            s += At<const char*>(p, 0);
            s += ";";
        }
        s += "}";
    }
    return s + "[" + Str(b + 0x5C) + "]";
}

struct Contents {
    std::vector<std::string> quests;
    std::vector<std::string> briefings;
};

Contents Read(const char* db, int questsFrom, int briefingsFrom) {
    Contents c;
    const char* qb = At<const char*>(db, 0);
    int qn = static_cast<int>((At<const char*>(db, 4) - qb) / 0x38);
    for (int i = questsFrom; i < qn; ++i) c.quests.push_back(Quest(qb + i * 0x38));
    const char* bb = At<const char*>(db, 0x20);
    int bn = static_cast<int>((At<const char*>(db, 0x24) - bb) / 0x6C);
    for (int i = briefingsFrom; i < bn; ++i) c.briefings.push_back(Briefing(bb + i * 0x6C));
    return c;
}

int Count(const char* db, std::size_t vector, int size) {
    return static_cast<int>((At<const char*>(db, vector + 4) - At<const char*>(db, vector)) / size);
}

std::set<std::string> Names(const char* db, std::size_t vector, int size) {
    std::set<std::string> names;
    for (int i = 0; i < Count(db, vector, size); ++i) names.insert(Str(At<const char*>(db, vector) + i * size));
    return names;
}

} // namespace

// 006AE8E0: PUSH -1; PUSH 00735A89 (7 bytes). The original loads the quest into a fresh database
// (constructed and destroyed here); ours into the real one. The records ours added must be the
// ones the original added whose names the real database did not have before.
static bool(__fastcall* s_load)(char*, void*, const void*);
static bool __fastcall QuestDatabaseLoadVerify(char* db, void*, const void* image) {
    alignas(8) char scratch[0x40];
    DatabaseConstruct(scratch);
    bool expectedResult = s_load(scratch, nullptr, image);
    Contents fresh = Read(scratch, 0, 0);
    DatabaseDestroy(scratch);

    std::set<std::string> questNames = Names(db, 0, 0x38);
    std::set<std::string> briefingNames = Names(db, 0x20, 0x6C);
    int quests = Count(db, 0, 0x38);
    int briefings = Count(db, 0x20, 0x6C);
    bool result = QuestDatabaseLoad(db, nullptr, image);
    Contents added = Read(db, quests, briefings);

    Contents expected;
    for (const std::string& q : fresh.quests) {
        std::size_t end = q.find(']');
        if (!questNames.count(q.substr(2, end - 2))) expected.quests.push_back(q);
    }
    for (const std::string& b : fresh.briefings) {
        std::size_t end = b.find(']');
        if (!briefingNames.count(b.substr(2, end - 2))) expected.briefings.push_back(b);
    }
    ei::verify::Check("QuestDatabaseLoad.result", expectedResult, result, "");
    ei::verify::Check("QuestDatabaseLoad.quests", true, expected.quests == added.quests, "added %d expected %d",
                      static_cast<int>(added.quests.size()), static_cast<int>(expected.quests.size()));
    ei::verify::Check("QuestDatabaseLoad.briefings", true, expected.briefings == added.briefings,
                      "added %d expected %d", static_cast<int>(added.briefings.size()),
                      static_cast<int>(expected.briefings.size()));
    return result;
}
EI_VERIFY(0x006AE8E0, QuestDatabaseLoadVerify, 7, s_load);
