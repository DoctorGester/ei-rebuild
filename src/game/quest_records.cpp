// The quest database's records in an archive: the records' Serialize (quests,
// briefings) and the two vectors'. See quest_records.h and docs/notes/items_spells.md ("Quest database").
#include "quest_records.h"

#include "chunk_archive.h"

#include "record_lifetime.h"
#include "record_vectors.h"

#include <memory>
#include <vector>

namespace {

// STLport instantiations (library.csv), through their addresses: vector<float>::_M_fill_insert, the records'
// destructors (quests, briefings) and the vectors' resize (new records default-constructed, extra ones
// destroyed).
void __fastcall QuestDestroy(QuestRecord* record) { record->Destruct(); }
void __fastcall BriefingDestroy(QuestBriefingRecord* record) { record->Destruct(); }

// The vectors' serialiser (one instantiation per record type in the game): chunk `id`
// holding one chunk 1 per record; loading destroys the old records (erase(begin, end): the tail
// copy is empty) and resizes to the number of record chunks first.
template <typename Record, typename Abi>
void SerializeRecords(ChunkArchive* ar, int id, stlp::vector<Record>* records, void(__fastcall* destroy)(Record*),
                      void (Abi::*resize)(std::size_t),
                      void (Record::*serialize)(ChunkArchive*)) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        Record* end = records->Finish();
        for (Record* p = records->Start(); p != end; ++p) destroy(p);
        records->Finish() = records->Start();
        count = ar->CountChunks(1);
        (static_cast<Abi*>(records)->*resize)(static_cast<std::size_t>(count));
    } else {
        count = static_cast<int>(records->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        Record* record = records->Start() + i;
        if (ar->EnterChunk(1)) {
            (record->*serialize)(ar);
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
        places.clear();
        count = ar->CountChunks(1);
        places.insert(places.end(), static_cast<unsigned>(count), 0.0f);
    } else {
        count = static_cast<int>(places.size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->Field(1, &places[i], 4);
    }
    ar->LeaveChunk();
}

void QuestBriefingRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &exp, 4);
    ar->Field(2, &money, 4);
    ar->SerializeStrings(3, &lists[0], ar);
    ar->StringField(4, &text);
    for (int i = 1; i < 6; ++i) InlineStrings(ar, 4 + i, &lists[i]);
    ar->Field(10, &specials, 4);
}

void QuestRecord::Destruct() {
    std::destroy_at(&text);
    std::destroy_at(&places);
    std::destroy_at(&name);
}

void QuestBriefingRecord::Destruct() {
    std::destroy_at(&text);
    for (int i = 5; i >= 0; --i) std::destroy_at(&lists[i]);
    std::destroy_at(&name);
}

// ---- The records as vector elements (record_vectors.h) -------------------------------------

namespace {
using PlaceElement = stlp::PlainElement<float>;
using NameElement = stlp::ObjectElement<std::string>;
} // namespace

void QuestRecord::CopyConstruct(const QuestRecord* source) {
    std::construct_at(&name, source->name);
    exp = source->exp;
    field2 = source->field2;
    money = source->money;
    zone = source->zone;
    order = source->order;
    std::construct_at(&places, source->places);
    std::construct_at(&text, source->text);
}

void QuestRecord::AssignQuest(const QuestRecord* source) {
    name = source->name;
    exp = source->exp;
    field2 = source->field2;
    money = source->money;
    zone = source->zone;
    order = source->order;
    places = source->places;
    text = source->text;
}

void QuestRecord::ConstructDefault() {
    std::construct_at(&name);
    std::construct_at(&places);
    std::construct_at(&text);
}

void QuestBriefingRecord::CopyConstruct(const QuestBriefingRecord* source) {
    std::construct_at(&name, source->name);
    exp = source->exp;
    money = source->money;
    for (int i = 0; i < 6; ++i) std::construct_at(&lists[i], source->lists[i]);
    std::construct_at(&text, source->text);
    specials = source->specials;
}

void QuestBriefingRecord::AssignBriefing(const QuestBriefingRecord* source) {
    name = source->name;
    exp = source->exp;
    money = source->money;
    for (int i = 0; i < 6; ++i) lists[i] = source->lists[i];
    text = source->text;
    specials = source->specials;
}

void QuestBriefingRecord::ConstructDefault() {
    std::construct_at(&name);
    for (std::vector<std::string>& list : lists) std::construct_at(&list);
    std::construct_at(&text);
}

void ChunkArchive::SerializeQuests(int id, stlp::vector<QuestRecord>* quests, void*) {
    SerializeRecords(this, id, quests, QuestDestroy, &recordvec::QuestVector::ResizeDefault, &QuestRecord::Serialize);
}

void ChunkArchive::SerializeBriefings(int id, stlp::vector<QuestBriefingRecord>* briefings, void*) {
    SerializeRecords(this, id, briefings, BriefingDestroy, &recordvec::BriefingVector::ResizeDefault,
                     &QuestBriefingRecord::Serialize);
}
