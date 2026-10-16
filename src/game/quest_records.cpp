// The quest database's records in an archive: the records' Serialize (006AD7D0 quests, 006AD900
// briefings) and the two vectors' (006B1F20, 006B2010). See quest_records.h and
// docs/notes/items_spells.md ("Quest database").
#include "quest_records.h"

#include "chunk_archive.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "record_lifetime.h"

namespace {

// STLport instantiations (library.csv), through their addresses: vector<float>::_M_fill_insert
// (006ADC10, RET 0xC), the records' destructors (006AF4D0 quests, 006AF5A0 briefings, plain RET)
// and the vectors' resize (006B2100 / 006B2240, RET 4: new records default-constructed, extra
// ones destroyed).
__declspec(naked) void __fastcall FloatsFillInsert(stlp::vector<float>*, void*, float* /*position*/, unsigned /*count*/,
                                                   const float* /*value*/) EI_ORIGINAL(0x006ADC10)
void __fastcall QuestDestroy(QuestRecord* record) { static_cast<QuestRecord*>(record)->Destruct(); }
void __fastcall BriefingDestroy(QuestBriefingRecord* record) { static_cast<QuestBriefingRecord*>(record)->Destruct(); }
__declspec(naked) void __fastcall QuestsResize(stlp::vector<QuestRecord>*, void*, unsigned /*count*/) EI_ORIGINAL(0x006B2100)
__declspec(naked) void __fastcall BriefingsResize(stlp::vector<QuestBriefingRecord>*, void*, unsigned /*count*/)
    EI_ORIGINAL(0x006B2240)
// The records' Serialize (rewritten below), called through their addresses so that their verify
// wrappers, which compare with the originals, run for every record.
__declspec(naked) void __fastcall QuestSerializeAt(QuestRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x006AD7D0)
__declspec(naked) void __fastcall BriefingSerializeAt(QuestBriefingRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x006AD900)

// The vectors' serialiser (one instantiation per record type in the original): chunk `id`
// holding one chunk 1 per record; loading destroys the old records (erase(begin, end): the tail
// copy is empty) and resizes to the number of record chunks first.
template <typename Record>
void SerializeRecords(ChunkArchive* ar, int id, stlp::vector<Record>* records, void(__fastcall* destroy)(Record*),
                      void(__fastcall* resize)(stlp::vector<Record>*, void*, unsigned),
                      void(__fastcall* serialize)(Record*, void*, ChunkArchive*)) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        Record* end = records->Finish();
        for (Record* p = records->Start(); p != end; ++p) destroy(p);
        records->Finish() = records->Start();
        count = ar->CountChunks(1);
        resize(records, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(records->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        Record* record = records->Start() + i;
        if (ar->EnterChunk(1)) {
            serialize(record, nullptr, ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

void QuestRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &exp, 4);
    ar->Field(2, &field2, 4);
    ar->Field(3, &zone, 4);
    ar->StringField(4, &text);
    ar->Field(5, &money, 4);
    ar->Field(6, &order, 4);
    if (!ar->EnterChunk(7)) return;
    int count;
    if (ar->loading_) {
        // erase(begin, end), then resize(count, 0.0f): the vector is empty, so a fill insert.
        places.Finish() = places.Start();
        count = ar->CountChunks(1);
        float zero = 0.0f;
        FloatsFillInsert(&places, nullptr, places.Finish(), static_cast<unsigned>(count), &zero);
    } else {
        count = static_cast<int>(places.size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->Field(1, &places.Start()[i], 4);
    }
    ar->LeaveChunk();
}
EI_REPLACE_METHOD(0x006AD7D0, QuestRecord::Serialize);

void QuestBriefingRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &exp, 4);
    ar->Field(2, &money, 4);
    ar->SerializeCStrings(3, &lists[0], ar);
    ar->StringField(4, &text);
    for (int i = 1; i < 6; ++i) InlineStrings(ar, 4 + i, &lists[i]);
    ar->Field(10, &field10, 4);
}
EI_REPLACE_METHOD(0x006AD900, QuestBriefingRecord::Serialize);

void QuestRecord::Destruct() {
    text.~string();
    places.FreeStorage();
    name.~string();
}
EI_REPLACE_METHOD(0x006AF4D0, QuestRecord::Destruct);

void QuestBriefingRecord::Destruct() {
    text.~string();
    for (int i = 5; i >= 0; --i) records::DestroyVector(lists[i]);
    name.~string();
}
EI_REPLACE_METHOD(0x006AF5A0, QuestBriefingRecord::Destruct);

// ---- S10a: the records as vector elements (record_vectors.h) ----------------------------------

namespace {
using PlaceElement = stlp::PlainElement<float>;
using NameElement = stlp::ObjectElement<mfc::CString>;
} // namespace

void QuestRecord::CopyConstruct(const QuestRecord* source) {
    new (&name) stlp::string(source->name);
    exp = source->exp;
    field2 = source->field2;
    money = source->money;
    zone = source->zone;
    order = source->order;
    places.CopyConstruct<PlaceElement>(source->places);
    new (&text) stlp::string(source->text);
}

void QuestRecord::AssignQuest(const QuestRecord* source) {
    name.AssignFrom(source->name);
    exp = source->exp;
    field2 = source->field2;
    money = source->money;
    zone = source->zone;
    order = source->order;
    places.AssignVector<PlaceElement>(source->places);
    text.AssignFrom(source->text);
}

void QuestRecord::ConstructDefault() {
    stlp::StringElement::Default(&name);
    places.ConstructEmpty();
    stlp::StringElement::Default(&text);
}

void QuestBriefingRecord::CopyConstruct(const QuestBriefingRecord* source) {
    new (&name) stlp::string(source->name);
    exp = source->exp;
    money = source->money;
    for (int i = 0; i < 6; ++i) lists[i].CopyConstruct<NameElement>(source->lists[i]);
    new (&text) stlp::string(source->text);
    field10 = source->field10;
}

void QuestBriefingRecord::AssignBriefing(const QuestBriefingRecord* source) {
    name.AssignFrom(source->name);
    exp = source->exp;
    money = source->money;
    for (int i = 0; i < 6; ++i) lists[i].AssignVector<NameElement>(source->lists[i]);
    text.AssignFrom(source->text);
    field10 = source->field10;
}

void QuestBriefingRecord::ConstructDefault() {
    stlp::StringElement::Default(&name);
    for (CStringVector& list : lists) list.ConstructEmpty();
    stlp::StringElement::Default(&text);
}

void ChunkArchive::SerializeQuests(int id, stlp::vector<QuestRecord>* quests, void*) {
    SerializeRecords(this, id, quests, QuestDestroy, QuestsResize, QuestSerializeAt);
}
EI_REPLACE_METHOD(0x006B1F20, ChunkArchive::SerializeQuests);

void ChunkArchive::SerializeBriefings(int id, stlp::vector<QuestBriefingRecord>* briefings, void*) {
    SerializeRecords(this, id, briefings, BriefingDestroy, BriefingsResize, BriefingSerializeAt);
}
EI_REPLACE_METHOD(0x006B2010, ChunkArchive::SerializeBriefings);
