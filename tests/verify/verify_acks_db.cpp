// Verify wrappers for the unit acknowledgements (acks_db.cpp). Reached when units are selected or
// ordered through the battle screen (006366F0: ui_battle_commands and the other clickunit
// scenarios).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/chunk_archive.h"

#include <new>

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

// ---- Serialising the records (acks_db.cpp). Loaded once at startup (006D7620 through the vector
// serialisers of model_db.cpp; in the vanilla run). Loading: the original reads into a scratch
// element made by the original instantiation (vector::resize to 1), the chunks a failed search left
// pushed are popped, then ours reads into the real one; the fields compared, the scratch destroyed
// by the original element destructor. Saving (not done by the game): ours only.

int Depth(ChunkArchive* ar) {
    int n = 0;
    for (ChunkArchive::StackNode* p = ar->stack_->next; p != ar->stack_; p = p->next) ++n;
    return n;
}
void PopTo(ChunkArchive* ar, int depth) {
    for (int n = Depth(ar); n > depth; --n) ar->PopChunk();
}

bool SameSpeech(const SpeechRecord& a, const SpeechRecord& b) {
    return a.chance == b.chance && a.sound == b.sound && a.text == b.text && a.name == b.name;
}
bool SameList(const acks::SpeechList& a, const acks::SpeechList& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!SameSpeech(a[i], b[i])) return false;
    return true;
}
bool SameEnvironmentList(const stlp::vector<acks::EnvironmentSpeech>& a, const stlp::vector<acks::EnvironmentSpeech>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!SameSpeech(a[i].speech, b[i].speech) || a[i].environment != b[i].environment) return false;
    return true;
}
template <std::size_t N>
int FirstDifferentList(const acks::SpeechList (&a)[N], const acks::SpeechList (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i)
        if (!SameList(a[i], b[i])) return static_cast<int>(i);
    return -1;
}

// The records' vector::resize and element destructors (the original instantiations).
__declspec(naked) void __fastcall ResizeUnitAcks(stlp::vector<acks::UnitAcks>*, void*, unsigned) EI_ORIGINAL(0x006DB9C0)
__declspec(naked) void __fastcall ResizeModelAcks(stlp::vector<acks::ModelAcks>*, void*, unsigned) EI_ORIGINAL(0x006DBBD0)
__declspec(naked) void __fastcall ResizeExtraAcks(stlp::vector<acks::ExtraAcks>*, void*, unsigned) EI_ORIGINAL(0x006DBE20)
__declspec(naked) void __fastcall DestroyUnitAcks(acks::UnitAcks*, void*) EI_ORIGINAL(0x006DC390)
__declspec(naked) void __fastcall DestroyModelAcks(acks::ModelAcks*, void*) EI_ORIGINAL(0x006DC920)
__declspec(naked) void __fastcall DestroyExtraAcks(acks::ExtraAcks*, void*) EI_ORIGINAL(0x006DCAF0)

template <typename Record, typename Describe>
void CompareRecord(const char* label, void(__fastcall* original)(Record*, void*, ChunkArchive*),
                   void(__fastcall* resize)(stlp::vector<Record>*, void*, unsigned),
                   void(__fastcall* destroy)(Record*, void*), Record* self, ChunkArchive* ar, Describe differs) {
    if (!ar->loading_) {
        self->Serialize(ar);
        return;
    }
    stlp::vector<Record> scratch;
    resize(&scratch, nullptr, 1);
    int depth = Depth(ar);
    original(&scratch[0], nullptr, ar);
    PopTo(ar, depth);
    self->Serialize(ar);
    int where = differs(scratch[0], *self);
    ei::verify::Check(label, -1, where, "'%.*s': first different part", static_cast<int>(self->head.name.size()),
                      self->head.name.data());
    destroy(&scratch[0], nullptr);
    scratch.FreeStorage();
}

// 006E3780 / 006E3FD0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes).
// 006E3DC0: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes). The part reported: 0 the name, 1.. the lists
// (list + 1), 100 / 101 the environment / named lists.
void(__fastcall* s_unitSerialize)(acks::UnitAcks*, void*, ChunkArchive*);
void __fastcall UnitAcksSerializeVerify(acks::UnitAcks* self, void*, ChunkArchive* ar) {
    CompareRecord("UnitAcks::Serialize", s_unitSerialize, ResizeUnitAcks, DestroyUnitAcks, self, ar,
                  [](const acks::UnitAcks& a, const acks::UnitAcks& b) {
                      if (!(a.head.name == b.head.name)) return 0;
                      int list = FirstDifferentList(a.lists, b.lists);
                      if (list >= 0) return list + 1;
                      if (!SameEnvironmentList(a.environmentSpeeches, b.environmentSpeeches)) return 100;
                      if (!SameList(a.namedSpeeches, b.namedSpeeches)) return 101;
                      return -1;
                  });
}
EI_VERIFY(0x006E3780, UnitAcksSerializeVerify, 8, s_unitSerialize);

template <typename Record>
int DifferentListsRecord(const Record& a, const Record& b) {
    if (!(a.head.name == b.head.name)) return 0;
    int list = FirstDifferentList(a.lists, b.lists);
    return list >= 0 ? list + 1 : -1;
}

void(__fastcall* s_modelSerialize)(acks::ModelAcks*, void*, ChunkArchive*);
void __fastcall ModelAcksSerializeVerify(acks::ModelAcks* self, void*, ChunkArchive* ar) {
    CompareRecord("ModelAcks::Serialize", s_modelSerialize, ResizeModelAcks, DestroyModelAcks, self, ar,
                  DifferentListsRecord<acks::ModelAcks>);
}
EI_VERIFY(0x006E3FD0, ModelAcksSerializeVerify, 8, s_modelSerialize);

void(__fastcall* s_extraSerialize)(acks::ExtraAcks*, void*, ChunkArchive*);
void __fastcall ExtraAcksSerializeVerify(acks::ExtraAcks* self, void*, ChunkArchive* ar) {
    CompareRecord("ExtraAcks::Serialize", s_extraSerialize, ResizeExtraAcks, DestroyExtraAcks, self, ar,
                  DifferentListsRecord<acks::ExtraAcks>);
}
EI_VERIFY(0x006E3DC0, ExtraAcksSerializeVerify, 5, s_extraSerialize);

// The archive helpers, reached from the original records' side runs above. Loading: the original
// into an empty scratch list (its C++ destructors free it as the original's do), ours into the real
// one, compared. 006E42F0 / 006E4410 / 006E4F40: MOV EAX,[ESP+4]; PUSH EDI or ESI (5 bytes).
template <typename List, typename Ours, typename Same>
void CompareListField(const char* label, void(__fastcall* original)(ChunkArchive*, void*, int, List*, ChunkArchive*),
                      Ours ours, ChunkArchive* ar, int id, List* list, Same same) {
    if (!ar->loading_) {
        ours(ar, nullptr, id, list, ar);
        return;
    }
    List scratch;
    int depth = Depth(ar);
    original(ar, nullptr, id, &scratch, ar);
    PopTo(ar, depth);
    ours(ar, nullptr, id, list, ar);
    ei::verify::Check(label, true, same(scratch, *list), "chunk %d: %u / %u elements", id,
                      static_cast<unsigned>(scratch.size()), static_cast<unsigned>(list->size()));
    scratch.clear();
    scratch.FreeStorage();
}

void(__fastcall* s_listField)(ChunkArchive*, void*, int, acks::SpeechList*, ChunkArchive*);
void __fastcall SpeechListFieldVerify(ChunkArchive* ar, void*, int id, acks::SpeechList* list, ChunkArchive*) {
    CompareListField("SpeechListField", s_listField, acks::SpeechListField, ar, id, list, SameList);
}
EI_VERIFY(0x006E42F0, SpeechListFieldVerify, 5, s_listField);

void(__fastcall* s_environmentField)(ChunkArchive*, void*, int, stlp::vector<acks::EnvironmentSpeech>*, ChunkArchive*);
void __fastcall EnvironmentListFieldVerify(ChunkArchive* ar, void*, int id, stlp::vector<acks::EnvironmentSpeech>* list,
                                           ChunkArchive*) {
    CompareListField("EnvironmentSpeechListField", s_environmentField, acks::EnvironmentSpeechListField, ar, id, list,
                     SameEnvironmentList);
}
EI_VERIFY(0x006E4410, EnvironmentListFieldVerify, 5, s_environmentField);

// 006E4F40: the original into a copy of the record (a missing chunk leaves it as it was).
void(__fastcall* s_speechField)(ChunkArchive*, void*, int, SpeechRecord*);
void __fastcall SpeechFieldVerify(ChunkArchive* ar, void*, int id, SpeechRecord* speech) {
    if (!ar->loading_) {
        acks::SpeechField(ar, nullptr, id, speech);
        return;
    }
    SpeechRecord scratch;
    scratch.chance = speech->chance;
    new (&scratch.sound) stlp::string(speech->sound);   // replaces the empty (null) defaults
    new (&scratch.text) stlp::string(speech->text);
    new (&scratch.name) stlp::string(speech->name);
    int depth = Depth(ar);
    s_speechField(ar, nullptr, id, &scratch);
    PopTo(ar, depth);
    acks::SpeechField(ar, nullptr, id, speech);
    ei::verify::Check("SpeechField", true, SameSpeech(scratch, *speech), "chunk %d", id);
}
EI_VERIFY(0x006E4F40, SpeechFieldVerify, 5, s_speechField);

} // namespace
