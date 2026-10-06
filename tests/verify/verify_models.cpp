// Differential checks for model and resource loading (src/game/model.cpp, model_db.cpp).
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <new>
#include <vector>

namespace {
void* s_unused;

// ---- Streams (save/load) ------------------------------------------------------------------------

// Captures what a save function writes, then forwards it in the same pieces.
class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(ScriptStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    std::vector<unsigned char> bytes;
    std::vector<unsigned> chunks;
};

// Reads from the real stream and records the bytes.
class RecordStream : public FakeStream {
public:
    explicit RecordStream(ScriptStream* real) : real_(real) {}
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        std::uint32_t result = real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    void Write(const void*, std::uint32_t) override {}
    std::vector<unsigned char> bytes;

private:
    ScriptStream* real_;
};

// Plays recorded bytes back.
class ReplayStream : public FakeStream {
public:
    explicit ReplayStream(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        for (unsigned i = 0; i < size; ++i) static_cast<unsigned char*>(buffer)[i] = pos_ < bytes_.size() ? bytes_[pos_++] : 0;
        return size;
    }
    void Write(const void*, std::uint32_t) override {}
    bool Consumed() const { return pos_ == bytes_.size(); }

private:
    const std::vector<unsigned char>& bytes_;
    std::size_t pos_ = 0;
};

// ---- TweenArray helpers -------------------------------------------------------------------------

// A copy with its own buffer (same size as the real one), so original code can run on it.
TweenArray Clone(const TweenArray& a) {
    TweenArray c = a;
    if (a.data) {
        unsigned bytes = static_cast<unsigned>(a.maxSize) << 4;
        c.data = static_cast<TweenEntry*>(gamecrt::operator_new(bytes));
        std::memcpy(c.data, a.data, bytes);
    }
    return c;
}

void Free(TweenArray& a) {
    if (a.data) gamecrt::operator_delete(a.data);
    a.data = nullptr;
}

bool Same(const TweenArray& a, const TweenArray& b) {
    if (a.size != b.size || a.maxSize != b.maxSize || a.growBy != b.growBy || !a.data != !b.data) return false;
    return !a.data || std::memcmp(a.data, b.data, static_cast<unsigned>(a.size) << 4) == 0;
}

// ---- TriangleIndexArray helpers ---------------------------------------------------------------

IndexTriangle* AllocTriangles(int count) {
    char* block = static_cast<char*>(gamecrt::operator_new(static_cast<unsigned>(count * 3 + 0x12) * 2));
    auto* data = reinterpret_cast<IndexTriangle*>(reinterpret_cast<std::uintptr_t>(block + 0x23) & ~std::uintptr_t(0x1F));
    reinterpret_cast<char**>(data)[-1] = block;
    return data;
}
void FreeTriangles(IndexTriangle* data) {
    if (data) gamecrt::operator_delete(reinterpret_cast<char**>(data)[-1]);
}

} // namespace

// ---- 3D library ---------------------------------------------------------------------------------

// 004F16A0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x24 (6 bytes). Pure.
static int(__fastcall* s_clipTest)(const CTransformPipeline*, void*, const ClipBounds*);
static int __fastcall ClipTestVerify(const CTransformPipeline* self, void*, const ClipBounds* b) {
    int expected = s_clipTest(self, nullptr, b);
    int actual = self->ClipTest(b);
    ei::verify::Check("CTransformPipeline::ClipTest", expected, actual, "flags %u -> %d vs %d", b->flags, expected, actual);
    return expected;
}
EI_VERIFY(0x004F16A0, ClipTestVerify, 6, s_clipTest);

// 004F93A0: MOV EDX,[ESP+4]; PUSH ESI (5 bytes). Original on a scratch copy, ours on the real one.
static std::uint32_t(__fastcall* s_setBox)(ClipBounds*, void*, const float*, const float*);
static std::uint32_t __fastcall SetBoxVerify(ClipBounds* self, void*, const float* min, const float* max) {
    ClipBounds scratch = *self;
    std::uint32_t expected = s_setBox(&scratch, nullptr, min, max);
    std::uint32_t actual = self->SetBox(min, max);
    bool same = expected == actual && std::memcmp(&scratch, self, sizeof(ClipBounds)) == 0;
    ei::verify::Check("ClipBounds::SetBox", true, same, "%08X vs %08X", expected, actual);
    return expected;
}
EI_VERIFY(0x004F93A0, SetBoxVerify, 5, s_setBox);

// Matrix builders: ours into a scratch matrix first (an input may alias the output), then the
// original into the real one; the 64 bytes and the returned pointer are compared.
// 004F1370: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static Matrix4*(__stdcall* s_viewport)(Matrix4*, const int*);
static Matrix4* __stdcall ViewportVerify(Matrix4* out, const int* rect) {
    Matrix4 scratch;
    Matrix4* ours = ViewportMatrix(&scratch, rect);
    Matrix4* result = s_viewport(out, rect);
    bool same = ours == &scratch && result == out && std::memcmp(&scratch, out, sizeof(Matrix4)) == 0;
    ei::verify::Check("ViewportMatrix", true, same, "rect %d %d %d %d", rect[0], rect[1], rect[2], rect[3]);
    return result;
}
EI_VERIFY(0x004F1370, ViewportVerify, 6, s_viewport);

// 004F13F0: PUSH EBP; MOV EBP,ESP; PUSH ECX; XOR EDX,EDX (6 bytes).
static Matrix4*(__stdcall* s_shear)(Matrix4*, const Vector3*);
static Matrix4* __stdcall ShearVerify(Matrix4* out, const Vector3* dir) {
    Matrix4 scratch;
    Matrix4* ours = ShearMatrix(&scratch, dir);
    Matrix4* result = s_shear(out, dir);
    bool same = ours == &scratch && result == out && std::memcmp(&scratch, out, sizeof(Matrix4)) == 0;
    ei::verify::Check("ShearMatrix", true, same, "dir %g %g %g", dir->x, dir->y, dir->z);
    return result;
}
EI_VERIFY(0x004F13F0, ShearVerify, 6, s_shear);

// 004F0E80: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static Matrix4*(__stdcall* s_projection)(Matrix4*, float, float, float, float);
static Matrix4* __stdcall ProjectionVerify(Matrix4* out, float fov, float aspect, float zNear, float zFar) {
    Matrix4 scratch;
    Matrix4* ours = ProjectionMatrix(&scratch, fov, aspect, zNear, zFar);
    Matrix4* result = s_projection(out, fov, aspect, zNear, zFar);
    bool same = ours == &scratch && result == out && std::memcmp(&scratch, out, sizeof(Matrix4)) == 0;
    ei::verify::Check("ProjectionMatrix", true, same, "fov %g aspect %g near %g far %g", fov, aspect, zNear, zFar);
    return result;
}
EI_VERIFY(0x004F0E80, ProjectionVerify, 6, s_projection);

// 004F57B0: MOV EAX,[ESP+8]; PUSH EBX (5 bytes). The original on a copy with its own buffer, ours
// on the real array; sizes and the kept triangles are compared.
static void(__fastcall* s_resize)(TriangleIndexArray*, void*, int, int);
static void __fastcall ResizeVerify(TriangleIndexArray* self, void*, int count, int growBy) {
    TriangleIndexArray scratch = *self;
    if (self->data) {
        scratch.data = AllocTriangles(self->capacity);
        std::memcpy(scratch.data, self->data, static_cast<unsigned>(self->size) * 6);
    }
    int oldSize = self->size;
    s_resize(&scratch, nullptr, count, growBy);
    self->SetSize(count, growBy);
    int kept = oldSize < self->size ? oldSize : self->size;
    bool same = scratch.size == self->size && scratch.capacity == self->capacity && scratch.growBy == self->growBy &&
                !scratch.data == !self->data &&
                (!self->data || std::memcmp(scratch.data, self->data, static_cast<unsigned>(kept) * 6) == 0);
    ei::verify::Check("TriangleIndexArray::Resize", true, same, "%d -> %d: size %d/%d cap %d/%d", oldSize, count,
                      scratch.size, self->size, scratch.capacity, self->capacity);
    FreeTriangles(scratch.data);
}
EI_VERIFY(0x004F57B0, ResizeVerify, 5, s_resize);

// 004F6D30 opens a file and allocates: ours only. On success the list must hold an archive of that
// name with a resource; on failure none.
static int __fastcall AddArchiveVerify(CLinkFigureLoader* self, void*, const stlp::string* path) {
    int result = self->AddArchive(path);
    LinkArchiveNode* found = nullptr;
    for (LinkArchiveNode* n = self->archives_->next; n != self->archives_; n = n->next)
        if (n->name == *path) found = n;
    bool ok = result ? (found && found->resource) : !found;
    ei::verify::Check("CLinkFigureLoader::AddArchive", true, ok, "\"%.*s\" result %d", static_cast<int>(path->size()),
                      path->data(), result);
    return result;
}
EI_VERIFY(0x004F6D30, AddArchiveVerify, 0, s_unused);

// ---- Databases ----------------------------------------------------------------------------------

// The vector serialisers allocate and destroy records: ours only. When loading, the number of
// records afterwards must equal the number of record chunks, counted beforehand with the original
// archive calls (EnterChunk / CountChunks / LeaveChunk leave the archive as it was).
namespace {
struct VectorView {
    char* start;
    char* finish;
};
template <void (ChunkArchive::*Fn)(int, void*, void*)>
void SerializeArrayVerify(const char* name, ChunkArchive* ar, int id, void* records, void* unused, int size) {
    int expected = -1;
    if (ar->loading_ && ar->EnterChunk(id)) {
        expected = ar->CountChunks(1);
        ar->LeaveChunk();
    }
    (ar->*Fn)(id, records, unused);
    if (expected >= 0) {
        auto* v = static_cast<VectorView*>(records);
        int actual = static_cast<int>(v->finish - v->start) / size;
        ei::verify::Check(name, expected, actual, "chunk %d: %d records vs %d", id, expected, actual);
    }
}
} // namespace

static void __fastcall SerializePrintsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeArrayVerify<&ChunkArchive::SerializePrints>("ChunkArchive::SerializePrints", ar, id, records, unused, 0x28);
}
EI_VERIFY(0x006D23A0, SerializePrintsVerify, 0, s_unused);
static void __fastcall SerializePrints2Verify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeArrayVerify<&ChunkArchive::SerializePrints2>("ChunkArchive::SerializePrints2", ar, id, records, unused, 0x34);
}
EI_VERIFY(0x006D25F0, SerializePrints2Verify, 0, s_unused);
static void __fastcall SerializeAcksVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeArrayVerify<&ChunkArchive::SerializeAcks>("ChunkArchive::SerializeAcks", ar, id, records, unused, 0x1C4);
}
EI_VERIFY(0x006DB430, SerializeAcksVerify, 0, s_unused);
static void __fastcall SerializeAcks2Verify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeArrayVerify<&ChunkArchive::SerializeAcks2>("ChunkArchive::SerializeAcks2", ar, id, records, unused, 0x5C);
}
EI_VERIFY(0x006DB680, SerializeAcks2Verify, 0, s_unused);
static void __fastcall SerializeAcks3Verify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeArrayVerify<&ChunkArchive::SerializeAcks3>("ChunkArchive::SerializeAcks3", ar, id, records, unused, 0x38);
}
EI_VERIFY(0x006DB8D0, SerializeAcks3Verify, 0, s_unused);

// Record Serialize (slot 0) when loading: reading leaves the archive as it was, so the original
// reads into a scratch copy and ours into the real record; the fields and the name are compared.
// Saving writes, so it runs ours only. Ours is called qualified (a virtual call would come back
// here).
// 006D41D0 / 006D4270: PUSH EBX; MOV EBX,ECX; PUSH ESI; MOV ESI,[ESP+0xC] (8 bytes).
template <typename Record>
static void SerializeRecordVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Record* self,
                                  ChunkArchive* ar) {
    if (!ar->loading_) {
        self->Record::Serialize(ar);
        return;
    }
    // The scratch name must be a real STLport string: a default-constructed stlp::string has no
    // buffer (null pointers), which STLport code never expects (it always keeps a terminator), so
    // the original's assign crashed on it. The copy constructor allocates like STLport's.
    Record scratch;
    std::memcpy(static_cast<void*>(&scratch), self, sizeof(void*));   // the game's vtable
    std::memcpy(reinterpret_cast<char*>(&scratch) + 0x10, reinterpret_cast<char*>(self) + 0x10, sizeof(Record) - 0x10);
    scratch.name.~string();
    new (&scratch.name) stlp::string(self->name);
    original(&scratch, nullptr, ar);
    self->Record::Serialize(ar);
    bool same = scratch.name == self->name &&
                std::memcmp(reinterpret_cast<char*>(&scratch) + 0x10, reinterpret_cast<char*>(self) + 0x10, sizeof(Record) - 0x10) == 0;
    ei::verify::Check(name, true, same, "\"%.*s\" vs \"%.*s\"", static_cast<int>(scratch.name.size()), scratch.name.data(),
                      static_cast<int>(self->name.size()), self->name.data());
}

static void(__fastcall* s_printSerialize)(PrintRecord*, void*, ChunkArchive*);
static void __fastcall PrintSerializeVerify(PrintRecord* self, void*, ChunkArchive* ar) {
    SerializeRecordVerify("PrintRecord::Serialize", s_printSerialize, self, ar);
}
EI_VERIFY(0x006D41D0, PrintSerializeVerify, 8, s_printSerialize);

static void(__fastcall* s_print2Serialize)(PrintRecord2*, void*, ChunkArchive*);
static void __fastcall Print2SerializeVerify(PrintRecord2* self, void*, ChunkArchive* ar) {
    SerializeRecordVerify("PrintRecord2::Serialize", s_print2Serialize, self, ar);
}
EI_VERIFY(0x006D4270, Print2SerializeVerify, 8, s_print2Serialize);

// ---- TweenArray ---------------------------------------------------------------------------------

// 006D43E0: PUSH ESI; MOV ESI,ECX; PUSH EDI; XOR EDI,EDI (6 bytes).
static void(__fastcall* s_removeAll)(TweenArray*, void*);
static void __fastcall RemoveAllVerify(TweenArray* self, void*) {
    TweenArray scratch = Clone(*self);
    s_removeAll(&scratch, nullptr);
    self->RemoveAll();
    ei::verify::Check("TweenArray::RemoveAll", true, Same(scratch, *self), "size %d", self->size);
    Free(scratch);
}
EI_VERIFY(0x006D43E0, RemoveAllVerify, 6, s_removeAll);

// 006D4840: MOV EAX,[ESP+8]; PUSH EBX (5 bytes).
static void(__fastcall* s_setSize)(TweenArray*, void*, int, int);
static void __fastcall SetSizeVerify(TweenArray* self, void*, int count, int growBy) {
    TweenArray scratch = Clone(*self);
    int before = self->size;
    s_setSize(&scratch, nullptr, count, growBy);
    self->SetSize(count, growBy);
    ei::verify::Check("TweenArray::SetSize", true, Same(scratch, *self), "%d -> %d", before, count);
    Free(scratch);
}
EI_VERIFY(0x006D4840, SetSizeVerify, 5, s_setSize);

// 006D4410: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_setTarget)(TweenArray*, void*, int, float, int);
static void __fastcall SetTargetVerify(TweenArray* self, void*, int key, float target, int steps) {
    TweenArray scratch = Clone(*self);
    s_setTarget(&scratch, nullptr, key, target, steps);
    self->SetTarget(key, target, steps);
    ei::verify::Check("TweenArray::SetTarget", true, Same(scratch, *self), "key %d target %g steps %d", key, target, steps);
    Free(scratch);
}
EI_VERIFY(0x006D4410, SetTargetVerify, 5, s_setTarget);

// 006D44E0: PUSH ECX; PUSH EBX; PUSH ESI; MOV ESI,ECX (5 bytes).
static bool(__fastcall* s_tick)(TweenArray*, void*);
static bool __fastcall TickVerify(TweenArray* self, void*) {
    TweenArray scratch = Clone(*self);
    bool expected = s_tick(&scratch, nullptr);
    bool actual = self->Tick();
    ei::verify::Check("TweenArray::Tick", true, expected == actual && Same(scratch, *self), "size %d", self->size);
    Free(scratch);
    return expected;
}
EI_VERIFY(0x006D44E0, TickVerify, 5, s_tick);

// Save: both into capture streams; the original's bytes then go to the real stream.
// 006D4570 / 006D47E0: PUSH ECX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes).
static void SaveVerify(void(__fastcall* original)(const TweenArray*, void*, ScriptStream*), const TweenArray* self,
                       ScriptStream* stream) {
    CaptureStream expected, actual;
    original(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("TweenArray::Save", true, expected.bytes == actual.bytes && expected.chunks == actual.chunks, "size %d",
                      self->size);
    expected.Forward(stream);
}
static void(__fastcall* s_save)(const TweenArray*, void*, ScriptStream*);
static void __fastcall Save1Verify(const TweenArray* self, void*, ScriptStream* stream) { SaveVerify(s_save, self, stream); }
EI_VERIFY(0x006D4570, Save1Verify, 5, s_save);
static void(__fastcall* s_save2)(const TweenArray*, void*, ScriptStream*);
static void __fastcall Save2Verify(const TweenArray* self, void*, ScriptStream* stream) { SaveVerify(s_save2, self, stream); }
EI_VERIFY(0x006D47E0, Save2Verify, 5, s_save2);

// Load with water levels: the original runs for real (its water level calls cannot be repeated)
// while recording the stream; ours reads the recorded bytes into a copy of the old array.
// 006D45D0: PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x10] (7 bytes).
static void(__fastcall* s_load)(TweenArray*, void*, ScriptStream*, void*);
static void __fastcall LoadVerify(TweenArray* self, void*, ScriptStream* stream, void* world) {
    TweenArray scratch = Clone(*self);
    RecordStream record(stream);
    s_load(self, nullptr, &record, world);
    ReplayStream replay(record.bytes);
    scratch.ReadEntries(&replay, reinterpret_cast<int>(&record));
    ei::verify::Check("TweenArray::Load", true, Same(scratch, *self) && replay.Consumed(), "size %d", self->size);
    Free(scratch);
}
EI_VERIFY(0x006D45D0, LoadVerify, 7, s_load);

// 006D4660: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes). Original on a copy from the real stream,
// ours on the real array from the recorded bytes.
static void(__fastcall* s_loadEntries)(TweenArray*, void*, ScriptStream*);
static void __fastcall LoadEntriesVerify(TweenArray* self, void*, ScriptStream* stream) {
    TweenArray scratch = Clone(*self);
    RecordStream record(stream);
    s_loadEntries(&scratch, nullptr, &record);
    ReplayStream replay(record.bytes);
    self->LoadEntries(&replay);
    ei::verify::Check("TweenArray::LoadEntries", true, Same(scratch, *self) && replay.Consumed(), "size %d", self->size);
    Free(scratch);
}
EI_VERIFY(0x006D4660, LoadEntriesVerify, 5, s_loadEntries);
