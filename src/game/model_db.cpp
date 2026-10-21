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

// ---- ChunkArchive -------------------------------------------------------------------------------
// The rest of the archive is in chunk_archive.cpp.

namespace {

// vector<T> as three pointers, for record types only original code constructs.
using RawVector = stlp::vector<char>;

using RecordFn = void (*)(void* record);                                       // destructor body
using ResizeFn = void(__fastcall*)(void* vector, void*, unsigned count);        // vector::resize
using SerializeFn = void (*)(void* record, ChunkArchive* ar);

// Element destructors (release the records' strings); vector::resize of each record type is
// recordvec::ResizeUntyped (006D26E0, 006D2830, 006DB9C0, 006DBBD0, 006DBE20).
void DestroyPrint(void* record) { static_cast<PrintRecord*>(record)->name.~string(); }      // 006D29A0
void DestroyPrint2(void* record) { static_cast<PrintRecord2*>(record)->name.~string(); }    // 006D29E0
void DestroyAck(void* record) { static_cast<acks::UnitAcks*>(record)->Destruct(); }         // 006DC390
void DestroyAck2(void* record) { static_cast<acks::ModelAcks*>(record)->Destruct(); }       // 006DC920
void DestroyAck3(void* record) { static_cast<acks::ExtraAcks*>(record)->Destruct(); }       // 006DCAF0

// How each instantiation serialises a record: prints.db records through their vtable, acks.db
// records by a direct call.
void SerializePrint(void* record, ChunkArchive* ar) { static_cast<PrintRecord*>(record)->Serialize(ar); }
void SerializePrint2(void* record, ChunkArchive* ar) { static_cast<PrintRecord2*>(record)->Serialize(ar); }
void SerializeAckRecord(void* record, ChunkArchive* ar) { static_cast<acks::UnitAcks*>(record)->Serialize(ar); }
void SerializeAck2Record(void* record, ChunkArchive* ar) { static_cast<acks::ModelAcks*>(record)->Serialize(ar); }
void SerializeAck3Record(void* record, ChunkArchive* ar) { static_cast<acks::ExtraAcks*>(record)->Serialize(ar); }

// The body shared by the vector serialisers (one template instantiation per record type in the
// original). Loading destroys the old records, resizes to the number of record chunks and reads
// each; saving writes the records in order.
void SerializeArray(ChunkArchive* ar, int id, RawVector* v, int size, RecordFn destroy, ResizeFn resize,
                    SerializeFn serialize) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        // erase(begin(), end()): the tail copy is empty; the old records are destroyed in order.
        char* end = v->end();
        for (char* p = v->begin(); p != end; p += size) destroy(p);
        v->Finish() = v->Start();
        count = ar->CountChunks(1);
        resize(v, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(v->size()) / size;
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        char* record = v->begin() + i * size;
        if (ar->EnterChunk(1)) {
            serialize(record, ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

// Fields 1-3 of chunk `id`, four bytes each, if the chunk is present.
void TripleChunk(ChunkArchive* ar, int id, std::uint32_t* values) {
    if (!ar->EnterChunk(id)) return;
    ar->Field(1, &values[0], 4);
    ar->Field(2, &values[1], 4);
    ar->Field(3, &values[2], 4);
    ar->LeaveChunk();
}

// The hooks of the records' slot 0 (a virtual: registered through these shims).

} // namespace

void ChunkArchive::SerializePrints(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord), DestroyPrint, recordvec::ResizeUntyped<recordvec::PrintVector>,
                   SerializePrint);
}
void ChunkArchive::SerializePrints2(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord2), DestroyPrint2, recordvec::ResizeUntyped<recordvec::Print2Vector>,
                   SerializePrint2);
}
void ChunkArchive::SerializeAcks(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x1C4, DestroyAck, recordvec::ResizeUntyped<recordvec::UnitAcksVector>, SerializeAckRecord);
}
void ChunkArchive::SerializeAcks2(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x5C, DestroyAck2, recordvec::ResizeUntyped<recordvec::ModelAcksVector>, SerializeAck2Record);
}
void ChunkArchive::SerializeAcks3(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x38, DestroyAck3, recordvec::ResizeUntyped<recordvec::ExtraAcksVector>, SerializeAck3Record);
}
EI_REWRITTEN(0x006D23A0, ChunkArchive::SerializePrints);
EI_REWRITTEN(0x006D25F0, ChunkArchive::SerializePrints2);
EI_REWRITTEN(0x006DB430, ChunkArchive::SerializeAcks);
EI_REWRITTEN(0x006DB680, ChunkArchive::SerializeAcks2);
EI_REWRITTEN(0x006DB8D0, ChunkArchive::SerializeAcks3);

// ---- prints.db records ----------------------------------------------------------------------------

void PrintRecord::SerializeImpl(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
}

void PrintRecord2::SerializeImpl(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
    TripleChunk(ar, 10, values10);
}

EI_REPLACE_METHOD(0x006D41D0, PrintRecord::SerializeImpl);
EI_REPLACE_METHOD(0x006D4270, PrintRecord2::SerializeImpl);

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

void WaterTweens::Load(CStream* stream, void* terrain) {
    // The original reads the count into its `stream` argument slot.
    ReadEntries(stream, reinterpret_cast<int>(stream));
    for (int i = 0; i < size; ++i) static_cast<Terrain*>(terrain)->SetWaterLevel(data[i].key, data[i].value);
}
EI_REWRITTEN(0x006D45D0, WaterTweens::Load);

void WaterTweens::LoadEntries(CStream* stream) {
    // The original reads the count into an uninitialised local; the game's streams always write it.
    ReadEntries(stream, 0);
}
EI_REWRITTEN(0x006D4660, WaterTweens::LoadEntries);
