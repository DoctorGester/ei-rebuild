// Verify wrappers for the unit acknowledgements (acks_db.cpp). Reached when units are selected or
// ordered through the battle screen (006366F0: ui_battle_commands and the other clickunit
// scenarios).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/acks_db.h"

using combatverify::RandomState;

namespace {

// ---- AckTable::Find: the original, then ours on the same table; the records compared. A missing
// name is added (with a null record) by the first run; the second then finds that entry, which
// gives the same null. 006D9A30 / 006D9CE0 / 006D9F80: PUSH -1; PUSH imm32 (7 bytes).
template <typename Record>
using FindFn = Record*(__fastcall*)(acks::AckTable<Record>*, void*, const char*);

template <typename Record>
Record* CompareFind(const char* label, FindFn<Record> original, acks::AckTable<Record>* self, const char* name) {
    Record* expected = original(self, nullptr, name);
    Record* actual = self->Find(name);
    ei::verify::Check(label, expected, actual, "\"%s\"", name);
    return expected;
}

FindFn<acks::UnitAcks> s_findUnits;
acks::UnitAcks* __fastcall FindUnitsVerify(acks::AckTable<acks::UnitAcks>* self, void*, const char* name) {
    return CompareFind("AckTable<UnitAcks>::Find", s_findUnits, self, name);
}
EI_VERIFY(0x006D9A30, FindUnitsVerify, 7, s_findUnits);

FindFn<acks::ModelAcks> s_findModels;
acks::ModelAcks* __fastcall FindModelsVerify(acks::AckTable<acks::ModelAcks>* self, void*, const char* name) {
    return CompareFind("AckTable<ModelAcks>::Find", s_findModels, self, name);
}
EI_VERIFY(0x006D9CE0, FindModelsVerify, 7, s_findModels);

FindFn<acks::ExtraAcks> s_findExtras;
acks::ExtraAcks* __fastcall FindExtrasVerify(acks::AckTable<acks::ExtraAcks>* self, void*, const char* name) {
    return CompareFind("AckTable<ExtraAcks>::Find", s_findExtras, self, name);
}
EI_VERIFY(0x006D9F80, FindExtrasVerify, 7, s_findExtras);

// ---- PickSpeech / PickEnvironmentSpeech: the random generators saved, the original run, its result
// and the generators' state after it kept; the generators restored, ours run; the speech and the
// state after compared. 006D8780 / 006D88E0: MOV EAX,FS:[0] (6 bytes).
using PickFn = const SpeechRecord*(__fastcall*)(const acks::SpeechList*, void*, int);
PickFn s_pick;
const SpeechRecord* __fastcall PickSpeechVerify(const acks::SpeechList* list, void*, int roll) {
    RandomState before, afterOriginal, afterOurs;
    before.Save();
    const SpeechRecord* expected = s_pick(list, nullptr, roll);
    afterOriginal.Save();
    before.Restore();
    const SpeechRecord* actual = acks::PickSpeech(list, nullptr, roll);
    afterOurs.Save();
    ei::verify::Check("acks::PickSpeech", expected, actual, "roll %d", roll);
    ei::verify::Check("acks::PickSpeech random state", true, afterOriginal.SameAs(afterOurs), "roll %d", roll);
    return actual;
}
EI_VERIFY(0x006D8780, PickSpeechVerify, 6, s_pick);

using PickEnvironmentFn = const SpeechRecord*(__fastcall*)(const stlp::vector<acks::EnvironmentSpeech>*, void*,
                                                           std::uint32_t, int);
PickEnvironmentFn s_pickEnvironment;
const SpeechRecord* __fastcall PickEnvironmentSpeechVerify(const stlp::vector<acks::EnvironmentSpeech>* list, void*,
                                                           std::uint32_t environment, int roll) {
    RandomState before, afterOriginal, afterOurs;
    before.Save();
    const SpeechRecord* expected = s_pickEnvironment(list, nullptr, environment, roll);
    afterOriginal.Save();
    before.Restore();
    const SpeechRecord* actual = acks::PickEnvironmentSpeech(list, nullptr, environment, roll);
    afterOurs.Save();
    ei::verify::Check("acks::PickEnvironmentSpeech", expected, actual, "env %08X roll %d", environment, roll);
    ei::verify::Check("acks::PickEnvironmentSpeech random state", true, afterOriginal.SameAs(afterOurs), "roll %d",
                      roll);
    return actual;
}
EI_VERIFY(0x006D88E0, PickEnvironmentSpeechVerify, 6, s_pickEnvironment);

// ---- SayAck / SayNamedAck: ours only (they play sounds and queue the speech on the controller);
// their table lookups and picks run through the wrappers above.
const SpeechRecord* (__fastcall* s_sayAck)(CUnit*, int);
const SpeechRecord* __fastcall SayAckVerify(CUnit* unit, int kind) { return acks::SayAck(unit, kind); }
EI_VERIFY(0x006D8430, SayAckVerify, 0, s_sayAck);

const SpeechRecord* (__fastcall* s_sayNamedAck)(CUnit*, const char*, bool);
const SpeechRecord* __fastcall SayNamedAckVerify(CUnit* unit, const char* name, bool alternate) {
    return acks::SayNamedAck(unit, name, alternate);
}
EI_VERIFY(0x006D8A40, SayNamedAckVerify, 0, s_sayNamedAck);

} // namespace
