// The quest database's records in an archive: the records' Serialize (quests,
// briefings) and the two vectors'. See quest_records.h and docs/notes/items_spells.md ("Quest database").
#include "quest_records.h"

#include "chunk_archive.h"

#include <vector>

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

void ChunkArchive::SerializeQuests(int id, std::vector<QuestRecord>* quests, void*) {
    RecordChunks(this, id, quests, &QuestRecord::Serialize);
}

void ChunkArchive::SerializeBriefings(int id, std::vector<QuestBriefingRecord>* briefings, void*) {
    RecordChunks(this, id, briefings, &QuestBriefingRecord::Serialize);
}
