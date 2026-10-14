// Databases read at startup from database.res (prints.db, acks.db; 006D0000-006E0000) and the
// world's water level changes (006D4400).
#include "model.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>

// ---- ChunkArchive -------------------------------------------------------------------------------
// The rest of the archive is in chunk_archive.cpp.

namespace {

// vector<T> as three pointers, for record types only original code constructs.
using RawVector = stlp::vector<char>;

using RecordFn = void(__fastcall*)(void* record, void*);                       // destructor body
using ResizeFn = void(__fastcall*)(void* vector, void*, unsigned count);        // vector::resize
using SerializeFn = void (*)(void* record, ChunkArchive* ar);

// Element destructors (release the records' strings) and vector::resize of each record type.
__declspec(naked) void __fastcall DestroyPrint(void*, void*) EI_ORIGINAL(0x006D29A0)
__declspec(naked) void __fastcall DestroyPrint2(void*, void*) EI_ORIGINAL(0x006D29E0)
__declspec(naked) void __fastcall ResizePrints(void*, void*, unsigned) EI_ORIGINAL(0x006D26E0)
__declspec(naked) void __fastcall ResizePrints2(void*, void*, unsigned) EI_ORIGINAL(0x006D2830)
__declspec(naked) void __fastcall DestroyAck(void*, void*) EI_ORIGINAL(0x006DC390)
__declspec(naked) void __fastcall DestroyAck2(void*, void*) EI_ORIGINAL(0x006DC920)
__declspec(naked) void __fastcall DestroyAck3(void*, void*) EI_ORIGINAL(0x006DCAF0)
__declspec(naked) void __fastcall ResizeAcks(void*, void*, unsigned) EI_ORIGINAL(0x006DB9C0)
__declspec(naked) void __fastcall ResizeAcks2(void*, void*, unsigned) EI_ORIGINAL(0x006DBBD0)
__declspec(naked) void __fastcall ResizeAcks3(void*, void*, unsigned) EI_ORIGINAL(0x006DBE20)
// acks.db record Serialize functions (acks_db.cpp), called through their addresses so that their
// verify wrappers run.
__declspec(naked) void __fastcall SerializeAck(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3780)
__declspec(naked) void __fastcall SerializeAck2(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3FD0)
__declspec(naked) void __fastcall SerializeAck3(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3DC0)

// How each instantiation serialises a record: prints.db records through their vtable, acks.db
// records by a direct call.
void SerializePrint(void* record, ChunkArchive* ar) { static_cast<PrintRecord*>(record)->Serialize(ar); }
void SerializePrint2(void* record, ChunkArchive* ar) { static_cast<PrintRecord2*>(record)->Serialize(ar); }
void SerializeAckRecord(void* record, ChunkArchive* ar) { SerializeAck(record, nullptr, ar); }
void SerializeAck2Record(void* record, ChunkArchive* ar) { SerializeAck2(record, nullptr, ar); }
void SerializeAck3Record(void* record, ChunkArchive* ar) { SerializeAck3(record, nullptr, ar); }

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
        for (char* p = v->begin(); p != end; p += size) destroy(p, nullptr);
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
void __fastcall PrintSerializeHook(PrintRecord* self, void*, ChunkArchive* ar) { self->PrintRecord::Serialize(ar); }
void __fastcall Print2SerializeHook(PrintRecord2* self, void*, ChunkArchive* ar) { self->PrintRecord2::Serialize(ar); }

// 006CC7F0: the terrain's water level `index` is set to `level` (thiscall on the world).
__declspec(naked) void __fastcall ApplyWaterLevel(void* /*world*/, void*, int /*index*/, float /*level*/) EI_ORIGINAL(0x006CC7F0)

} // namespace

void ChunkArchive::SerializePrints(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord), DestroyPrint, ResizePrints,
                   SerializePrint);
}
void ChunkArchive::SerializePrints2(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord2), DestroyPrint2, ResizePrints2,
                   SerializePrint2);
}
void ChunkArchive::SerializeAcks(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x1C4, DestroyAck, ResizeAcks, SerializeAckRecord);
}
void ChunkArchive::SerializeAcks2(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x5C, DestroyAck2, ResizeAcks2, SerializeAck2Record);
}
void ChunkArchive::SerializeAcks3(int id, void* records, void*) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x38, DestroyAck3, ResizeAcks3, SerializeAck3Record);
}
EI_REPLACE_METHOD(0x006D23A0, ChunkArchive::SerializePrints);
EI_REPLACE_METHOD(0x006D25F0, ChunkArchive::SerializePrints2);
EI_REPLACE_METHOD(0x006DB430, ChunkArchive::SerializeAcks);
EI_REPLACE_METHOD(0x006DB680, ChunkArchive::SerializeAcks2);
EI_REPLACE_METHOD(0x006DB8D0, ChunkArchive::SerializeAcks3);

// ---- prints.db records ----------------------------------------------------------------------------

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
}

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
    TripleChunk(ar, 10, values10);
}

EI_REPLACE(0x006D41D0, PrintSerializeHook);
EI_REPLACE(0x006D4270, Print2SerializeHook);

// ---- TweenArray ---------------------------------------------------------------------------------

// 006D43E0 / 006D4840: RemoveAll / SetSize (RET 8).
EI_REPLACE_METHOD(0x006D43E0, TweenArray::RemoveAll);
EI_REPLACE_METHOD(0x006D4840, TweenArray::SetSize);

void __fastcall TweenSetTarget(TweenArray* tweens, void*, int key, float target, int steps) {
    TweenArray& a = *tweens;
    if (steps < 1) steps = 1;
    int count = a.size;
    for (int i = 0; i < count; ++i) {
        if (a.data[i].key != key) continue;
        a.data[i].step = (target - a.data[i].value) / steps;
        a.data[i].steps = steps;
        return;
    }
    // A new entry. The original first copies an uninitialised local into it; every field is
    // overwritten below.
    a.SetSize(count + 1, -1);
    TweenEntry& e = a.data[a.size - 1];
    e.key = key;
    e.value = 0.0f;
    e.steps = steps;
    e.step = (target - a.data[count].value) / steps;
}
EI_REPLACE(0x006D4410, TweenSetTarget);

bool __fastcall TweenTick(TweenArray* tweens) {
    TweenArray& a = *tweens;
    bool moving = false;
    for (int i = 0; i < a.size; ++i) {
        TweenEntry* e = &a.data[i];
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (!(e->value < 0.0f) && !(e->value > 0.0f)) {
            // Finished and back at zero (or NaN): remove it.
            int after = a.size - i - 1;
            if (after != 0) std::memmove(e, e + 1, static_cast<unsigned>(after) << 4);
            --a.size;
            --i;
        }
    }
    return moving;
}
EI_REPLACE(0x006D44E0, TweenTick);

void __fastcall TweenSave(const TweenArray* tweens, void*, ScriptStream* stream) {
    int count = tweens->size;
    stream->Write(&count, 4);
    for (int i = 0; i < tweens->size; ++i) stream->Write(&tweens->data[i], 16);
}
EI_REPLACE(0x006D4570, TweenSave);
EI_REPLACE(0x006D47E0, TweenSave);

void TweenReadEntries(TweenArray& a, ScriptStream* stream, int unread) {
    int count = unread;
    stream->Read(&count, 4);
    a.SetSize(count, -1);
    for (int i = 0; i < a.size; ++i) stream->Read(&a.data[i], 16);
}

void __fastcall TweenLoad(TweenArray* tweens, void*, ScriptStream* stream, void* world) {
    // The original reads the count into its `stream` argument slot.
    TweenReadEntries(*tweens, stream, reinterpret_cast<int>(stream));
    for (int i = 0; i < tweens->size; ++i) ApplyWaterLevel(world, nullptr, tweens->data[i].key, tweens->data[i].value);
}
EI_REPLACE(0x006D45D0, TweenLoad);

void __fastcall TweenLoadEntries(TweenArray* tweens, void*, ScriptStream* stream) {
    // The original reads the count into an uninitialised local; the game's streams always write it.
    TweenReadEntries(*tweens, stream, 0);
}
EI_REPLACE(0x006D4660, TweenLoadEntries);
