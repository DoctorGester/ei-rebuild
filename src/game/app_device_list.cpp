// The lever table's records (LeverRecord, lever.h: Levers.ldb; Rebirth patches the start-up code
// that loads them from a chunk archive): the record's fields and the list's archiving. See docs/notes/app.md.
#include "chunk_archive.h"
#include "lever.h"

#include <vector>

// The record's fields 0 .. 7 (strings 0, 5, 6, 7).
void LeverRecord::Archive(ChunkArchive* archive) {
    archive->StringField(0, &name);
    archive->Field(1, spots, 0x10);
    archive->Field(2, &spotCount, 4);
    archive->Field(3, &farScale, 4);
    archive->Field(4, &duration, 4);
    archive->StringField(5, &unreadTextFirst);
    archive->StringField(6, &sound);
    archive->StringField(7, &unreadTextLast);
}

void ChunkArchive::SerializeLevers(int id, std::vector<LeverRecord>* list) {
    RecordChunks(this, id, list, &LeverRecord::Archive);
}
