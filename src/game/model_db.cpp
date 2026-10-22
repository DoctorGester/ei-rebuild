// Databases read at startup from database.res (prints.db, acks.db; 006D0000-006E0000) and the
// world's water level changes (006D4400).
#include "acks_db.h"
#include "model.h"
#include "record_vectors.h"
#include "terrain.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <type_traits>

// ---- ChunkArchive -------------------------------------------------------------------------------
// The rest of the archive is in chunk_archive.cpp.

namespace {

// The body shared by the vector serialisers (one instantiation per record type in the original,
// `Abi` the vector's instantiation: its element policy destroys a record, ResizeDefault is its
// resize(n): 006D26E0, 006D2830, 006DB9C0, 006DBBD0, 006DBE20). Loading destroys the old records
// (006D29A0, 006D29E0, 006DC390, 006DC920, 006DCAF0), resizes to the number of record chunks and
// reads each; saving writes the records in order. prints.db records serialise through their
// vtable, acks.db records by a direct call.
template <typename Abi>
void SerializeArray(ChunkArchive* ar, int id, typename Abi::Vector* v) {
    using Record = std::remove_pointer_t<decltype(v->begin())>;
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        // erase(begin(), end()): the tail copy is empty; the old records are destroyed in order.
        Record* end = v->end();
        for (Record* p = v->begin(); p != end; ++p) Abi::Element::Destroy(p);
        v->Finish() = v->Start();
        count = ar->CountChunks(1);
        v->template ResizeDefault<typename Abi::Element>(static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(v->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        Record* record = v->begin() + i;
        if (ar->EnterChunk(1)) {
            record->Serialize(ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

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

void ChunkArchive::SerializePrints(int id, stlp::vector<PrintRecord>* records, void*) {
    SerializeArray<recordvec::PrintVector>(this, id, records);
}
void ChunkArchive::SerializePrints2(int id, stlp::vector<PrintRecord2>* records, void*) {
    SerializeArray<recordvec::Print2Vector>(this, id, records);
}
void ChunkArchive::SerializeAcks(int id, stlp::vector<acks::UnitAcks>* records, void*) {
    SerializeArray<recordvec::UnitAcksVector>(this, id, records);
}
void ChunkArchive::SerializeAcks2(int id, stlp::vector<acks::ModelAcks>* records, void*) {
    SerializeArray<recordvec::ModelAcksVector>(this, id, records);
}
void ChunkArchive::SerializeAcks3(int id, stlp::vector<acks::ExtraAcks>* records, void*) {
    SerializeArray<recordvec::ExtraAcksVector>(this, id, records);
}
EI_REWRITTEN(0x006D23A0, ChunkArchive::SerializePrints);
EI_REWRITTEN(0x006D25F0, ChunkArchive::SerializePrints2);
EI_REWRITTEN(0x006DB430, ChunkArchive::SerializeAcks);
EI_REWRITTEN(0x006DB680, ChunkArchive::SerializeAcks2);
EI_REWRITTEN(0x006DB8D0, ChunkArchive::SerializeAcks3);

// ---- prints.db records ----------------------------------------------------------------------------

PrintRecord::PrintRecord() : look2{}, look3{} { stlp::string::Reserve(name, 8); }
PrintRecord::PrintRecord(const PrintRecord& from) : name(from.name), look2(from.look2), look3(from.look3) {}
PrintRecord2::PrintRecord2() : look2{}, look3{}, values10{} { stlp::string::Reserve(name, 8); }
PrintRecord2::PrintRecord2(const PrintRecord2& from) : name(from.name), look2(from.look2), look3(from.look3) {
    std::memcpy(values10, from.values10, sizeof(values10));
}

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &look2);
    TripleChunk(ar, 3, &look3);
}
EI_REWRITTEN(0x006D41D0, PrintRecord::Serialize);

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &look2);
    TripleChunk(ar, 3, &look3);
    TripleChunk(ar, 10, values10);
}
EI_REWRITTEN(0x006D4270, PrintRecord2::Serialize);

// ---- WaterTweens --------------------------------------------------------------------------------

EI_REWRITTEN(0x006D43E0, WaterTweens::RemoveAll);
EI_REWRITTEN(0x006D4840, WaterTweens::SetSize);
void WaterTweens::SetTarget(int key, float target, int steps) {
    if (steps < 1) steps = 1;
    int count = size;
    for (int i = 0; i < count; ++i) {
        if (data[i].key != key) continue;
        data[i].step = (target - data[i].value) / steps;
        data[i].steps = steps;
        return;
    }
    // A new entry. The original first copies an uninitialised local into it; every field is
    // overwritten below.
    SetSize(count + 1, -1);
    TweenEntry& e = data[size - 1];
    e.key = key;
    e.value = 0.0f;
    e.steps = steps;
    e.step = (target - data[count].value) / steps;
}
EI_REWRITTEN(0x006D4410, WaterTweens::SetTarget);

bool WaterTweens::Tick() {
    bool moving = false;
    for (int i = 0; i < size; ++i) {
        TweenEntry* e = &data[i];
        EI_NAN_AUDIT(e->value);
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (e->value == 0.0f) {
            // Finished and back at zero: remove it.
            int after = size - i - 1;
            if (after != 0) std::memmove(e, e + 1, static_cast<unsigned>(after) << 4);
            --size;
            --i;
        }
    }
    return moving;
}
EI_REWRITTEN(0x006D44E0, WaterTweens::Tick);

void WaterTweens::Save(CStream* stream) const {
    int count = size;
    stream->Write(&count, 4);
    for (int i = 0; i < size; ++i) stream->Write(&data[i], 16);
}
EI_REWRITTEN(0x006D4570, WaterTweens::Save);
EI_REWRITTEN(0x006D47E0, WaterTweens::Save);

void WaterTweens::ReadEntries(CStream* stream, int unread) {
    int count = unread;
    stream->Read(&count, 4);
    SetSize(count, -1);
    for (int i = 0; i < size; ++i) stream->Read(&data[i], 16);
}

void WaterTweens::Load(CStream* stream, Terrain* terrain) {
    // The original reads the count into its `stream` argument slot.
    ReadEntries(stream, reinterpret_cast<int>(stream));
    for (int i = 0; i < size; ++i) terrain->SetWaterLevel(data[i].key, data[i].value);
}
EI_REWRITTEN(0x006D45D0, WaterTweens::Load);

void WaterTweens::LoadEntries(CStream* stream) {
    // The original reads the count into an uninitialised local; the game's streams always write it.
    ReadEntries(stream, 0);
}
EI_REWRITTEN(0x006D4660, WaterTweens::LoadEntries);
