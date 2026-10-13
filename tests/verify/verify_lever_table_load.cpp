// Check for lever_table_load.cpp (A-E1). Main thread; vanilla launch only (Rebirth patches 00463390).
// Side by side on the lever table itself: the original loads it, its records are copied by content,
// then ours loads it again (the records are emptied and read anew, the index entries of the same
// names are overwritten, so the table ends as after one load) and the two contents are compared;
// every record must also be found under its name. Reached once at every start of the game (the
// application's start-up 00456440), by reading. Not run.
#include "verify.h"

#include "../../src/game/lever.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
struct RecordCopy {
    std::string name;
    float spots[2][2];
    std::uint32_t spotCount;
    float farScale;
    std::uint32_t duration;
    std::string text[3];
};

std::string TextOf(const void* text) { return static_cast<const char*>(text); }

std::vector<RecordCopy> CopyRecords(const LeverRecordTable* table) {
    std::vector<RecordCopy> copies;
    for (const LeverRecord* record = table->begin; record != table->end; ++record) {
        RecordCopy copy;
        copy.name = TextOf(record->name);
        std::memcpy(copy.spots, record->spots, sizeof copy.spots);
        copy.spotCount = record->spotCount;
        copy.farScale = record->farScale;
        copy.duration = record->duration;
        copy.text[0] = TextOf(record->text20);
        copy.text[1] = TextOf(record->sound);
        copy.text[2] = TextOf(record->text28);
        copies.push_back(copy);
    }
    return copies;
}

bool SameRecord(const RecordCopy& a, const RecordCopy& b) {
    return a.name == b.name && std::memcmp(a.spots, b.spots, sizeof a.spots) == 0 && a.spotCount == b.spotCount &&
           std::memcmp(&a.farScale, &b.farScale, sizeof a.farScale) == 0 && a.duration == b.duration &&
           a.text[0] == b.text[0] && a.text[1] == b.text[1] && a.text[2] == b.text[2];
}

// How many records the index does not lead to a record of the same name for.
int Unindexed(const LeverRecordTable* table) {
    int missing = 0;
    for (const LeverRecord* record = table->begin; record != table->end; ++record) {
        const char* name = static_cast<const char*>(record->name);
        auto* node = table->byName.Find(stlp::string(name));
        if (!node || !node->value || std::strcmp(static_cast<const char*>(node->value->name), name) != 0) ++missing;
    }
    return missing;
}

// 00463390: PUSH -1 (2), PUSH imm32 (5).
int(__fastcall* s_load)(LeverRecordTable*, void*, const char*);
int __fastcall LeverTableLoadVerify(LeverRecordTable* self, void*, const char* folder) {
    int expected = s_load(self, nullptr, folder);
    if (expected != 1) return expected;   // the fatal error path does not come back in practice
    std::vector<RecordCopy> expectedRecords = CopyRecords(self);
    std::size_t expectedIndexed = self->byName.Size();

    int actual = self->Load(folder);
    std::vector<RecordCopy> actualRecords = CopyRecords(self);
    ei::verify::Check("LeverRecordTable::Load", expected, actual, "");
    ei::verify::Check("LeverRecordTable::Load", static_cast<int>(expectedRecords.size()),
                      static_cast<int>(actualRecords.size()), "records");
    int different = 0;
    for (std::size_t i = 0; i < expectedRecords.size() && i < actualRecords.size(); ++i)
        if (!SameRecord(expectedRecords[i], actualRecords[i])) ++different;
    ei::verify::Check("LeverRecordTable::Load", 0, different, "records that differ");
    ei::verify::Check("LeverRecordTable::Load", static_cast<int>(expectedIndexed), static_cast<int>(self->byName.Size()),
                      "index size");
    ei::verify::Check("LeverRecordTable::Load", 0, Unindexed(self), "names not indexed");
    return actual;
}
} // namespace

EI_VERIFY(0x00463390, LeverTableLoadVerify, 7, s_load);
