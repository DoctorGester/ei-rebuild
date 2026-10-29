// Databases read at startup from database.res (prints.db, acks.db) and the world's water level changes.
#include "acks_db.h"
#include "model.h"
#include "chunk_archive.h"
#include "terrain.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <vector>

// The rest of the archive is in chunk_archive.cpp.

namespace {

// Fields 1-3 of chunk `id`, four bytes each, if the chunk is present.
void TripleChunk(ChunkArchive* ar, int id, void* first, void* second, void* third) {
    if (!ar->EnterChunk(id)) return;
    ar->Field(1, first, 4);
    ar->Field(2, second, 4);
    ar->Field(3, third, 4);
    ar->LeaveChunk();
}
void TripleChunk(ChunkArchive* ar, int id, std::uint32_t* values) { TripleChunk(ar, id, &values[0], &values[1], &values[2]); }
void TripleChunk(ChunkArchive* ar, int id, PrintLook* look) { TripleChunk(ar, id, &look->alpha, &look->life, &look->lifetime); }

} // namespace

void ChunkArchive::SerializePrints(int id, std::vector<PrintRecord>* records) {
    RecordChunks(this, id, records, &PrintRecord::Serialize);
}
void ChunkArchive::SerializePrints2(int id, std::vector<PrintRecord2>* records) {
    RecordChunks(this, id, records, &PrintRecord2::Serialize);
}
void ChunkArchive::SerializeAcks(int id, std::vector<acks::UnitAcks>* records) {
    RecordChunks(this, id, records, &acks::UnitAcks::Serialize);
}
void ChunkArchive::SerializeAcks2(int id, std::vector<acks::ModelAcks>* records) {
    RecordChunks(this, id, records, &acks::ModelAcks::Serialize);
}
void ChunkArchive::SerializeAcks3(int id, std::vector<acks::ExtraAcks>* records) {
    RecordChunks(this, id, records, &acks::ExtraAcks::Serialize);
}

// ---- prints.db records ----------------------------------------------------------------------------

PrintRecord::PrintRecord() : dryLook{}, wetLook{} {}
PrintRecord::PrintRecord(const PrintRecord& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {}
PrintRecord2::PrintRecord2() : dryLook{}, wetLook{}, unreadChunk{} {}
PrintRecord2::PrintRecord2(const PrintRecord2& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {
    std::memcpy(unreadChunk, from.unreadChunk, sizeof(unreadChunk));
}

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
}

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
    TripleChunk(ar, 10, unreadChunk);
}

void WaterTweens::SetTarget(int key, float target, int steps) {
    if (steps < 1) steps = 1;
    for (TweenEntry& e : entries) {
        if (e.key != key) continue;
        e.step = (target - e.value) / steps;
        e.steps = steps;
        return;
    }
    // A new entry, from 0.
    entries.push_back(TweenEntry{key, steps, 0.0f, target / steps});
}

bool WaterTweens::Tick() {
    bool moving = false;
    for (auto e = entries.begin(); e != entries.end();) {
        EI_NAN_AUDIT(e->value);
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (e->value == 0.0f) {
            // Finished and back at zero: removed.
            e = entries.erase(e);
            continue;
        }
        ++e;
    }
    return moving;
}

void WaterTweens::Save(ByteStream* stream) const {
    stream->WriteValue(static_cast<int>(entries.size()));
    for (const TweenEntry& e : entries) stream->WriteValue(e);
}

void WaterTweens::ReadEntries(ByteStream* stream) {
    int count = 0;
    stream->ReadValue(count);
    entries.resize(count);
    for (TweenEntry& e : entries) stream->ReadValue(e);
}

void WaterTweens::Load(ByteStream* stream, terrain::Terrain* terrain) {
    ReadEntries(stream);
    for (const TweenEntry& e : entries) terrain->SetWaterLevel(e.key, e.value);
}

void WaterTweens::LoadEntries(ByteStream* stream) {
    // The count is read into an uninitialised local; the game's streams always write it.
    ReadEntries(stream);
}
