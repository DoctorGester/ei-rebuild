// Databases read at startup from database.res (prints.db, acks.db) and the world's water level changes.
#include "acks_db.h"
#include "model.h"
#include "chunk_archive.h"
#include "terrain.h"

#include "lib/crt.h"
#include "lib/stlp.h"
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

void ChunkArchive::SerializePrints(int id, std::vector<PrintRecord>* records, void*) {
    RecordChunks(this, id, records, &PrintRecord::Serialize);
}
void ChunkArchive::SerializePrints2(int id, std::vector<PrintRecord2>* records, void*) {
    RecordChunks(this, id, records, &PrintRecord2::Serialize);
}
void ChunkArchive::SerializeAcks(int id, std::vector<acks::UnitAcks>* records, void*) {
    RecordChunks(this, id, records, &acks::UnitAcks::Serialize);
}
void ChunkArchive::SerializeAcks2(int id, std::vector<acks::ModelAcks>* records, void*) {
    RecordChunks(this, id, records, &acks::ModelAcks::Serialize);
}
void ChunkArchive::SerializeAcks3(int id, std::vector<acks::ExtraAcks>* records, void*) {
    RecordChunks(this, id, records, &acks::ExtraAcks::Serialize);
}

// ---- prints.db records ----------------------------------------------------------------------------

PrintRecord::PrintRecord() : look2{}, look3{} {}
PrintRecord::PrintRecord(const PrintRecord& from) : name(from.name), look2(from.look2), look3(from.look3) {}
PrintRecord2::PrintRecord2() : look2{}, look3{}, values10{} {}
PrintRecord2::PrintRecord2(const PrintRecord2& from) : name(from.name), look2(from.look2), look3(from.look3) {
    std::memcpy(values10, from.values10, sizeof(values10));
}

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &look2);
    TripleChunk(ar, 3, &look3);
}

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &look2);
    TripleChunk(ar, 3, &look3);
    TripleChunk(ar, 10, values10);
}

void WaterTweens::SetTarget(int key, float target, int steps) {
    if (steps < 1) steps = 1;
    int count = std::ssize(*this);
    for (int i = 0; i < count; ++i) {
        if ((*this)[i].key != key) continue;
        (*this)[i].step = (target - (*this)[i].value) / steps;
        (*this)[i].steps = steps;
        return;
    }
    // A new entry; every field is overwritten below.
    resize(count + 1);
    TweenEntry& e = (*this)[std::ssize(*this) - 1];
    e.key = key;
    e.value = 0.0f;
    e.steps = steps;
    e.step = (target - (*this)[count].value) / steps;
}

bool WaterTweens::Tick() {
    bool moving = false;
    for (int i = 0; i < std::ssize(*this); ++i) {
        TweenEntry* e = &(*this)[i];
        EI_NAN_AUDIT(e->value);
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (e->value == 0.0f) {
            // Finished and back at zero: remove it.
            int after = std::ssize(*this) - i - 1;
            if (after != 0) std::memmove(e, e + 1, static_cast<unsigned>(after) << 4);
            pop_back();
            --i;
        }
    }
    return moving;
}

void WaterTweens::Save(CStream* stream) const {
    int count = std::ssize(*this);
    stream->Write(&count, 4);
    for (int i = 0; i < std::ssize(*this); ++i) stream->Write(&(*this)[i], 16);
}

void WaterTweens::ReadEntries(CStream* stream, int unread) {
    int count = unread;
    stream->Read(&count, 4);
    resize(count);
    for (int i = 0; i < std::ssize(*this); ++i) stream->Read(&(*this)[i], 16);
}

void WaterTweens::Load(CStream* stream, Terrain* terrain) {
    ReadEntries(stream, 0);
    for (int i = 0; i < std::ssize(*this); ++i) terrain->SetWaterLevel((*this)[i].key, (*this)[i].value);
}

void WaterTweens::LoadEntries(CStream* stream) {
    // The count is read into an uninitialised local; the game's streams always write it.
    ReadEntries(stream, 0);
}
