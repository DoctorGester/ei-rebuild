// Checks for the controller's save-game lists (combat_save.cpp, A-U7): the step lists 005582A0 /
// 00558420 and the armour spell list 005585F0 / 00545430.
// - Saves: the original and ours write into capture streams; bytes and write sizes compared, the
//   original's writes go on to the real stream.
// - Loads: ours only, reading through a recorder. Step lists: round trip, the original save of the
//   loaded list must equal the bytes read. Armour spells: 4 + 8 bytes per entry of the whole list
//   read, each entry's countdown and (when resolved) item id as read.
// Reached by: saving and loading a game with units (world_load, ui_saveload; CUnitServer's
// SaveServer / LoadServer call them).
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/combat.h"

#include <cstring>

namespace {
using unitverify::CaptureStream;
using unitverify::ReadRecorder;
void* s_unused;

int ReadInt(const std::vector<unsigned char>& bytes, std::size_t at) {
    int value = 0;
    if (at + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 4);
    return value;
}
} // namespace

// 00558420: PUSH ECX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5 bytes).
static void(__fastcall* s_saveSteps)(const AnimationSteps*, UnitStream*);
static void __fastcall SaveStepListVerify(const AnimationSteps* steps, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_saveSteps(steps, &expected);
    SaveStepList(steps, &actual);
    ei::verify::Check("SaveStepList", true, expected.Same(actual), "%d steps: first difference at %d", steps->count,
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x00558420, SaveStepListVerify, 5, s_saveSteps);

static void __fastcall LoadStepListVerify(AnimationSteps* steps, UnitStream* stream) {
    ReadRecorder record(stream);
    LoadStepList(steps, &record);
    bool ok = true;
    if (s_saveSteps) {
        CaptureStream saved(stream);
        s_saveSteps(steps, &saved);
        ok = saved.bytes == record.bytes;
    }
    ei::verify::Check("LoadStepList", true, ok, "%d steps, %u bytes read", steps->count,
                      static_cast<unsigned>(record.bytes.size()));
}
EI_VERIFY(0x005582A0, LoadStepListVerify, 0, s_unused);

// 00545430: PUSH ECX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5 bytes).
static void(__fastcall* s_saveSpell)(const ArmourSpell*, void*, UnitStream*);
static void __fastcall SaveArmourSpellVerify(const ArmourSpell* entry, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_saveSpell(entry, nullptr, &expected);
    entry->Save(&actual);
    ei::verify::Check("SaveArmourSpell", true, expected.Same(actual), "countdown %d: first difference at %d",
                      entry->countdown, expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x00545430, SaveArmourSpellVerify, 5, s_saveSpell);

static void __fastcall LoadArmourSpellsVerify(ArmourSpellList* list, UnitStream* stream) {
    ReadRecorder record(stream);
    LoadArmourSpells(list, &record);
    bool ok = record.bytes.size() == 4 + 8 * static_cast<std::size_t>(list->count);
    std::size_t at = 4;
    int nodes = 0;
    for (auto* node = list->head; node && ok; node = node->next, at += 8, ++nodes) {
        int id = ReadInt(record.bytes, at);
        ok = node->data.countdown == ReadInt(record.bytes, at + 4) && (!node->data.item || node->data.item->id_ == id) &&
             (id != -1 || !node->data.item);
    }
    ok = ok && nodes == list->count;
    ei::verify::Check("LoadArmourSpells", true, ok, "%d entries, %u bytes read", list->count,
                      static_cast<unsigned>(record.bytes.size()));
}
EI_VERIFY(0x005585F0, LoadArmourSpellsVerify, 0, s_unused);
