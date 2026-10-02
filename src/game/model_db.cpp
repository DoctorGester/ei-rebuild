// Databases read at startup from database.res (prints.db, acks.db; 006D0000-006E0000) and the
// world's water level changes (006D4400).
#include "model.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

// ---- ChunkArchive -------------------------------------------------------------------------------

__declspec(naked) bool ChunkArchive::EnterChunk(int) EI_ORIGINAL(0x004098D0)
__declspec(naked) void ChunkArchive::LeaveChunk() EI_ORIGINAL(0x00409980)
__declspec(naked) void ChunkArchive::Field(int, void*, unsigned) EI_ORIGINAL(0x004092B0)
__declspec(naked) void ChunkArchive::StringField(int, stlp::string*) EI_ORIGINAL(0x00409470)
__declspec(naked) int ChunkArchive::CountChunks(int) EI_ORIGINAL(0x00409A60)

namespace {

// vector<T> as three pointers, for record types only original code constructs.
struct RawVector {
    char* start;
    char* finish;
    char* eos;
};

using RecordFn = void(__fastcall*)(void* record, void*);                       // destructor body
using ResizeFn = void(__fastcall*)(void* vector, void*, unsigned count);        // vector::resize
using SerializeFn = void(__fastcall*)(void* record, void*, ChunkArchive* ar);

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
// acks.db record Serialize functions (outside this range, not rewritten), called directly.
__declspec(naked) void __fastcall SerializeAck(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3780)
__declspec(naked) void __fastcall SerializeAck2(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3FD0)
__declspec(naked) void __fastcall SerializeAck3(void*, void*, ChunkArchive*) EI_ORIGINAL(0x006E3DC0)

// prints.db records are serialised through their vtable (slot 0).
void __fastcall SerializeVirtual(void* record, void*, ChunkArchive* ar) {
    auto fn = *static_cast<SerializeFn*>(*static_cast<void**>(record));
    fn(record, nullptr, ar);
}

// The body shared by the vector serialisers (one template instantiation per record type in the
// original). Loading destroys the old records, resizes to the number of record chunks and reads
// each; saving writes the records in order.
void SerializeArray(ChunkArchive* ar, int id, RawVector* v, int size, RecordFn destroy, ResizeFn resize,
                    SerializeFn serialize) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        // erase(begin(), end()): the tail copy is empty; the old records are destroyed in order.
        char* end = v->finish;
        for (char* p = v->start; p != end; p += size) destroy(p, nullptr);
        v->finish = v->start;
        count = ar->CountChunks(1);
        resize(v, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(v->finish - v->start) / size;
    }
    for (int i = 0; i < count; ++i) {
        ar->stack_->prev->occurrence = i + 1;
        char* record = v->start + i * size;
        if (ar->EnterChunk(1)) {
            serialize(record, nullptr, ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

void ChunkArchive::SerializePrints(int id, void* records) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord), DestroyPrint, ResizePrints,
                   SerializeVirtual);
}
void ChunkArchive::SerializePrints2(int id, void* records) {
    SerializeArray(this, id, static_cast<RawVector*>(records), sizeof(PrintRecord2), DestroyPrint2, ResizePrints2,
                   SerializeVirtual);
}
void ChunkArchive::SerializeAcks(int id, void* records) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x1C4, DestroyAck, ResizeAcks, SerializeAck);
}
void ChunkArchive::SerializeAcks2(int id, void* records) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x5C, DestroyAck2, ResizeAcks2, SerializeAck2);
}
void ChunkArchive::SerializeAcks3(int id, void* records) {
    SerializeArray(this, id, static_cast<RawVector*>(records), 0x38, DestroyAck3, ResizeAcks3, SerializeAck3);
}
EI_REPLACE_METHOD(0x006D23A0, ChunkArchive::SerializePrints);
EI_REPLACE_METHOD(0x006D25F0, ChunkArchive::SerializePrints2);
EI_REPLACE_METHOD(0x006DB430, ChunkArchive::SerializeAcks);
EI_REPLACE_METHOD(0x006DB680, ChunkArchive::SerializeAcks2);
EI_REPLACE_METHOD(0x006DB8D0, ChunkArchive::SerializeAcks3);

// ---- prints.db records ----------------------------------------------------------------------------

namespace {
// Fields 1-3 of chunk `id`, four bytes each, if the chunk is present.
void TripleChunk(ChunkArchive* ar, int id, std::uint32_t* values) {
    if (!ar->EnterChunk(id)) return;
    ar->Field(1, &values[0], 4);
    ar->Field(2, &values[1], 4);
    ar->Field(3, &values[2], 4);
    ar->LeaveChunk();
}
} // namespace

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
}
EI_REPLACE_METHOD(0x006D41D0, PrintRecord::Serialize);

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, values2);
    TripleChunk(ar, 3, values3);
    TripleChunk(ar, 10, values10);
}
EI_REPLACE_METHOD(0x006D4270, PrintRecord2::Serialize);

// ---- TweenArray ---------------------------------------------------------------------------------

namespace {
// 006CC7F0: the terrain's water level `index` is set to `level` (thiscall on the world).
__declspec(naked) void __fastcall ApplyWaterLevel(void* /*world*/, void*, int /*index*/, float /*level*/) EI_ORIGINAL(0x006CC7F0)
} // namespace

void TweenArray::RemoveAll() {
    if (data) {
        gamecrt::operator_delete(data);
        data = nullptr;
    }
    maxSize = 0;
    size = 0;
}
EI_REPLACE_METHOD(0x006D43E0, TweenArray::RemoveAll);

void TweenArray::SetSize(int count, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (count == 0) {
        if (data) {
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        maxSize = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = static_cast<TweenEntry*>(gamecrt::operator_new(static_cast<unsigned>(count) << 4));
        std::memset(data, 0, static_cast<unsigned>(count) << 4);
        maxSize = count;
        size = count;
        return;
    }
    if (count <= maxSize) {
        if (count > size) std::memset(data + size, 0, static_cast<unsigned>(count - size) << 4);
        size = count;
        return;
    }
    int step = growBy;
    if (step == 0) {
        step = size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int newMax = step + maxSize;
    if (count >= newMax) newMax = count;
    auto* fresh = static_cast<TweenEntry*>(gamecrt::operator_new(static_cast<unsigned>(newMax) << 4));
    std::memcpy(fresh, data, static_cast<unsigned>(size) << 4);
    std::memset(fresh + size, 0, static_cast<unsigned>(count - size) << 4);
    gamecrt::operator_delete(data);
    size = count;
    data = fresh;
    maxSize = newMax;
}
EI_REPLACE_METHOD(0x006D4840, TweenArray::SetSize);

void TweenArray::SetTarget(int key, float target, int steps) {
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
EI_REPLACE_METHOD(0x006D4410, TweenArray::SetTarget);

bool TweenArray::Tick() {
    bool moving = false;
    for (int i = 0; i < size; ++i) {
        TweenEntry* e = &data[i];
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (!(e->value < 0.0f) && !(e->value > 0.0f)) {
            // Finished and back at zero (or NaN): remove it.
            int after = size - i - 1;
            if (after != 0) std::memmove(e, e + 1, static_cast<unsigned>(after) << 4);
            --size;
            --i;
        }
    }
    return moving;
}
EI_REPLACE_METHOD(0x006D44E0, TweenArray::Tick);

void TweenArray::Save(ScriptStream* stream) const {
    int count = size;
    stream->Write(&count, 4);
    for (int i = 0; i < size; ++i) stream->Write(&data[i], 16);
}
EI_REPLACE_METHOD(0x006D4570, TweenArray::Save);
EI_REPLACE_METHOD(0x006D47E0, TweenArray::Save);

void TweenArray::ReadEntries(ScriptStream* stream, int unread) {
    int count = unread;
    stream->Read(&count, 4);
    SetSize(count, -1);
    for (int i = 0; i < size; ++i) stream->Read(&data[i], 16);
}

void TweenArray::Load(ScriptStream* stream, void* world) {
    // The original reads the count into its `stream` argument slot.
    ReadEntries(stream, reinterpret_cast<int>(stream));
    for (int i = 0; i < size; ++i) ApplyWaterLevel(world, nullptr, data[i].key, data[i].value);
}
EI_REPLACE_METHOD(0x006D45D0, TweenArray::Load);

void TweenArray::LoadEntries(ScriptStream* stream) {
    // The original reads the count into an uninitialised local; the game's streams always write it.
    ReadEntries(stream, 0);
}
EI_REPLACE_METHOD(0x006D4660, TweenArray::LoadEntries);
