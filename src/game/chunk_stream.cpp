// Chunked files. See chunk_stream.h.
//
// Every file operation asks for the file again through slot 1, as the original does.
#include "chunk_stream.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

constexpr int kSlotOnChunk = 0;
constexpr int kSlotFile = 1;
// MFC CFile slots.
constexpr int kFileGetPosition = 3;
constexpr int kFileSeek = 10;
constexpr int kFileRead = 13;
constexpr int kFileWrite = 14;
constexpr std::uint32_t kSeekCurrent = 1;
constexpr int kHeader = 8;

std::uint32_t FileRead(void* file, void* data, std::uint32_t size) { return ei::VCall<std::uint32_t>(file, kFileRead, data, size); }
void FileWrite(void* file, const void* data, std::uint32_t size) { ei::VCall<void>(file, kFileWrite, data, size); }
std::uint32_t FilePosition(void* file) { return ei::VCall<std::uint32_t>(file, kFileGetPosition); }
void FileSeek(void* file, std::int32_t offset, std::uint32_t from) { ei::VCall<std::uint32_t>(file, kFileSeek, offset, from); }

// FLD + FSTP of a float, as the original copies some values it read (a signalling NaN comes out
// quiet).
__declspec(naked) std::uint32_t __cdecl FpuCopyBits(const void* /*value*/) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [eax]
        push eax
        fstp dword ptr [esp]
        pop eax
        ret
    }
}

} // namespace

void* Chunk::File() { return ei::VCall<void*>(this, kSlotFile); }

Chunk* Chunk::Construct(Chunk* parent) {
    consumed_ = 0;
    size_ = 0;
    id_ = 0;
    start_ = 0;
    std::uintptr_t vtable = kChunkVtable;
    std::memcpy(&vtable_, &vtable, sizeof(vtable));
    parent_ = parent;
    return this;
}
EI_REPLACE_METHOD(0x0042BEC0, Chunk::Construct);

void Chunk::Destruct() {
    std::uintptr_t vtable = kChunkVtable;
    std::memcpy(&vtable_, &vtable, sizeof(vtable));
}
EI_REPLACE_METHOD(0x0042BEF0, Chunk::Destruct);

void Chunk::Advance(int bytes) {
    if (parent_) parent_->Advance(bytes);
    consumed_ += bytes;
}
EI_REPLACE_METHOD(0x0042BF00, Chunk::Advance);

void* __fastcall ChunkFile(Chunk* self) { return self->parent_ ? self->parent_->File() : nullptr; }
EI_REPLACE(0x0042BF40, ChunkFile);

// --- Writing -----------------------------------------------------------------------------------

void Chunk::BeginWrite(std::uint32_t id) {
    FileWrite(File(), &id, 4);
    start_ = FilePosition(File());
    FileWrite(File(), &size_, 4);
    consumed_ = 0;
    if (parent_) parent_->Advance(kHeader);
    consumed_ += kHeader;
}
EI_REPLACE_METHOD(0x0042BF50, Chunk::BeginWrite);

void Chunk::EndWrite() {
    std::uint32_t end = FilePosition(File());
    FileSeek(File(), static_cast<std::int32_t>(start_), 0);
    FileWrite(File(), &consumed_, 4);
    FileSeek(File(), static_cast<std::int32_t>(end), 0);
}
EI_REPLACE_METHOD(0x0042BFB0, Chunk::EndWrite);

void Chunk::WriteHeader(std::uint32_t id, int payload) {
    int size = payload + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
}
EI_REPLACE_METHOD(0x0042C000, Chunk::WriteHeader);

namespace {
// A chunk holding one value: header, then the value (counted with the header).
void WriteValueChunk(Chunk* chunk, std::uint32_t id, const void* value, std::uint32_t valueSize) {
    int size = kHeader + static_cast<int>(valueSize);
    FileWrite(chunk->File(), &id, 4);
    FileWrite(chunk->File(), &size, 4);
    if (chunk->parent_) chunk->parent_->Advance(size);
    chunk->consumed_ += size;
    FileWrite(chunk->File(), value, valueSize);
}
} // namespace

void Chunk::WriteFloat(std::uint32_t id, std::uint32_t valueBits) { WriteValueChunk(this, id, &valueBits, 4); }
EI_REPLACE_METHOD(0x0042C2C0, Chunk::WriteFloat);

void Chunk::WriteInt(std::uint32_t id, int value) { WriteValueChunk(this, id, &value, 4); }
EI_REPLACE_METHOD(0x0042C380, Chunk::WriteInt);

void Chunk::WriteByte(std::uint32_t id, std::uint8_t value) { WriteValueChunk(this, id, &value, 1); }
EI_REPLACE_METHOD(0x0042C500, Chunk::WriteByte);

// The three values are written one by one from the arguments.
void Chunk::WriteVector3(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    int size = 0x14;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    FileWrite(File(), &x, 4);
    FileWrite(File(), &y, 4);
    FileWrite(File(), &z, 4);
}
EI_REPLACE_METHOD(0x0042CD70, Chunk::WriteVector3);

// The four values are written as one 16-byte block.
void Chunk::WriteQuaternion(std::uint32_t id, Float4 value) {
    int size = 0x18;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    FileWrite(File(), &value, 0x10);
}
EI_REPLACE_METHOD(0x0042D0E0, Chunk::WriteQuaternion);

void Chunk::WriteBlob(std::uint32_t id, const void* data, int size) {
    int total = size + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &total, 4);
    if (parent_) parent_->Advance(total);
    consumed_ += total;
    FileWrite(File(), data, static_cast<std::uint32_t>(size));
}
EI_REPLACE_METHOD(0x0042CFA0, Chunk::WriteBlob);

// --- Reading -----------------------------------------------------------------------------------

bool Chunk::SkipContent() {
    if (consumed_ != kHeader || size_ == kHeader) return false;
    FileSeek(File(), size_ - kHeader, kSeekCurrent);
    int rest = size_ - kHeader;
    if (parent_) parent_->Advance(rest);
    consumed_ += rest;
    return true;
}
EI_REPLACE_METHOD(0x0042C060, Chunk::SkipContent);

void Chunk::ReadHeader() {
    SkipContent();
    FileRead(File(), &id_, 4);
    FileRead(File(), &size_, 4);
    consumed_ = 0;
    if (parent_) parent_->Advance(kHeader);
    consumed_ += kHeader;
}
EI_REPLACE_METHOD(0x0042C0B0, Chunk::ReadHeader);

// The parent is not checked for null (the original reads its fields unconditionally).
void Chunk::ForEachChild() {
    ReadHeader();
    for (;;) {
        ei::VCall<void>(this, kSlotOnChunk);
        SkipContent();
        if (parent_->size_ == parent_->consumed_) return;
        ReadHeader();
    }
}
EI_REPLACE_METHOD(0x0042C140, Chunk::ForEachChild);

namespace {
// Reads `size` bytes and counts them.
void ReadCounted(Chunk* chunk, void* data, std::uint32_t size) {
    FileRead(chunk->File(), data, size);
    if (chunk->parent_) chunk->parent_->Advance(static_cast<int>(size));
    chunk->consumed_ += static_cast<int>(size);
}
} // namespace

float Chunk::ReadFloat() {
    float value;
    ReadCounted(this, &value, 4);
    return value;
}
EI_REPLACE_METHOD(0x0042C280, Chunk::ReadFloat);

int Chunk::ReadInt() {
    int value;
    ReadCounted(this, &value, 4);
    return value;
}
EI_REPLACE_METHOD(0x0042C340, Chunk::ReadInt);

std::uint8_t Chunk::ReadByte() {
    std::uint8_t value;
    ReadCounted(this, &value, 1);
    return value;
}
EI_REPLACE_METHOD(0x0042C4C0, Chunk::ReadByte);

// x and z are copied through the FPU, y as an integer (as the original does).
Vector3* Chunk::ReadVector3(Vector3* out) {
    std::uint32_t v[3];
    ReadCounted(this, &v[0], 4);
    ReadCounted(this, &v[1], 4);
    ReadCounted(this, &v[2], 4);
    std::uint32_t x = FpuCopyBits(&v[0]);
    std::memcpy(&out->x, &x, 4);
    std::uint32_t z = FpuCopyBits(&v[2]);
    std::memcpy(&out->z, &z, 4);
    std::memcpy(&out->y, &v[1], 4);
    return out;
}
EI_REPLACE_METHOD(0x0042CCC0, Chunk::ReadVector3);

float* Chunk::ReadQuad(float* out) {
    std::uint32_t v[4];
    for (auto& value : v) ReadCounted(this, &value, 4);
    std::memcpy(&out[0], &v[2], 4);
    std::memcpy(&out[1], &v[0], 4);
    std::memcpy(&out[2], &v[3], 4);
    std::memcpy(&out[3], &v[1], 4);
    return out;
}
EI_REPLACE_METHOD(0x0042CEC0, Chunk::ReadQuad);

// A short read leaves the defaults (1, 0, 0, 0) in place.
float* Chunk::ReadQuaternion(float* out) {
    std::uint32_t v[4] = {0x3F800000u, 0, 0, 0};
    ReadCounted(this, v, 0x10);
    std::memcpy(out, v, sizeof(v));
    return out;
}
EI_REPLACE_METHOD(0x0042D060, Chunk::ReadQuaternion);

void Chunk::ReadBytes(void* data, int size) {
    FileRead(File(), data, static_cast<std::uint32_t>(size));
    if (parent_) parent_->Advance(size);
    consumed_ += size;
}
EI_REPLACE_METHOD(0x0042D020, Chunk::ReadBytes);
