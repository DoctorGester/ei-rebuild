// Chunked files. See chunk_stream.h.
//
// Every file operation asks for the file again through slot 1 (File), as the original does.
#include "chunk_stream.h"

#include "console.h"   // CStringArray

#include "ei/hook.h"
#include "lib/stlp.h"
#include "mob_script_decoder.h"
#include "originals_engine.h"
#include "ei/x87.h"

#include <cstring>
#include <new>

namespace {

constexpr std::uint32_t kSeekCurrent = 1;
constexpr int kHeader = 8;

// The file's MFC CFile slots, through its vtable (mfc::CFile itself declares no virtuals).
mfc::CFileVirtuals* AsFile(void* file) { return static_cast<mfc::CFileVirtuals*>(file); }
std::uint32_t FileRead(void* file, void* data, std::uint32_t size) { return AsFile(file)->Read(data, size); }
void FileWrite(void* file, const void* data, std::uint32_t size) { AsFile(file)->Write(data, size); }
std::uint32_t FilePosition(void* file) { return AsFile(file)->GetPosition(); }
void FileSeek(void* file, std::int32_t offset, std::uint32_t from) { AsFile(file)->Seek(offset, from); }

// The hook of slot 1 (a virtual: registered through this shim).
void* __fastcall ChunkFileHook(Chunk* self) { return self->Chunk::File(); }

// A chunk holding one value: header, then the value (counted with the header).
void WriteValueChunk(Chunk* chunk, std::uint32_t id, const void* value, std::uint32_t valueSize) {
    int size = kHeader + static_cast<int>(valueSize);
    FileWrite(chunk->File(), &id, 4);
    FileWrite(chunk->File(), &size, 4);
    if (chunk->parent_) chunk->parent_->Advance(size);
    chunk->consumed_ += size;
    FileWrite(chunk->File(), value, valueSize);
}

// Reads `size` bytes and counts them.
void ReadCounted(Chunk* chunk, void* data, std::uint32_t size) {
    FileRead(chunk->File(), data, size);
    if (chunk->parent_) chunk->parent_->Advance(static_cast<int>(size));
    chunk->consumed_ += static_cast<int>(size);
}

// The decoder of 004D8E10 (outside this range): two byte vectors, the decoded text and the coded
// input; vector<uint8_t> members (library): constructor, erase, fill insert.
using ByteVector = stlp::vector<std::uint8_t>;
struct TextDecoder {
    ByteVector text;    // +0x00
    ByteVector input;   // +0x0C
};

} // namespace

Chunk* Chunk::Construct(Chunk* parent) { return new (this) Chunk(parent); }
EI_REPLACE_METHOD(0x0042BEC0, Chunk::Construct);

void Chunk::Destruct() {}
EI_REPLACE_METHOD(0x0042BEF0, Chunk::Destruct);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x0046C7E0, Chunk::Destruct);
EI_REPLACE_METHOD(0x0046CBB0, Chunk::Destruct);
EI_REPLACE_METHOD(0x0046D070, Chunk::Destruct);

void Chunk::Advance(int bytes) {
    if (parent_) parent_->Advance(bytes);
    consumed_ += bytes;
}
EI_REPLACE_METHOD(0x0042BF00, Chunk::Advance);

void Chunk::OnChunk() {}   // 0042BEE0 (short_functions.cpp)

void* Chunk::File() { return parent_ ? parent_->File() : nullptr; }
EI_REPLACE(0x0042BF40, ChunkFileHook);

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
void Chunk::WriteQuaternion(std::uint32_t id, Quaternion value) {
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
        OnChunk();
        SkipContent();
        if (parent_->size_ == parent_->consumed_) return;
        ReadHeader();
    }
}
EI_REPLACE_METHOD(0x0042C140, Chunk::ForEachChild);

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
    std::uint32_t x;
    std::memcpy(&x, &v[0], 4);
    std::memcpy(&out->x, &x, 4);
    std::uint32_t z;
    std::memcpy(&z, &v[2], 4);
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

// --- Text ----------------------------------------------------------------------------------------

// The characters are read one at a time, each counted; the text ends at the first NUL
// (ReleaseBuffer(-1)).
mfc::CString* Chunk::ReadString(mfc::CString* out) {
    mfc::CString text;
    int length = size_ - kHeader;
    char* p = text.GetBufferSetLength(length);
    std::uint8_t c;
    for (int i = length; i > 0; --i) {
        FileRead(File(), &c, 1);
        if (parent_) parent_->Advance(1);
        ++consumed_;
        *p++ = static_cast<char>(c);
    }
    text.ReleaseBuffer(-1);
    new (out) mfc::CString(text);
    return out;
}
EI_REPLACE_METHOD(0x0042C580, Chunk::ReadString);

// The content read into the decoder's input (resized to it, zero-filled) in one read, then decoded.
mfc::CString* Chunk::ReadEncodedString(mfc::CString* out) {
    std::uint32_t length = static_cast<std::uint32_t>(size_ - kHeader);
    TextDecoder decoder;
    decoder.text.ConstructEmpty();   // 0042D310
    decoder.input.ConstructEmpty();
    std::uint8_t zero = 0;
    std::uint32_t had = static_cast<std::uint32_t>(decoder.input.size());
    if (length < had)
        decoder.input.EraseRange<stlp::PlainElement<unsigned char>>(decoder.input.begin() + length, decoder.input.end());
    else
        decoder.input.FillInsert<stlp::PlainElement<unsigned char>>(decoder.input.end(), length - had, zero);
    FileRead(File(), decoder.input.begin(), length);
    if (parent_) parent_->Advance(static_cast<int>(length));
    consumed_ += static_cast<int>(length);
    {
        mfc::CString text;
        reinterpret_cast<MobScriptDecoder*>(&decoder)->Decode(&text);
        new (out) mfc::CString(text);
    }
    stlp::deallocate(decoder.input.Start(), static_cast<std::size_t>(decoder.input.EndOfStorage() - decoder.input.Start()));
    stlp::deallocate(decoder.text.Start(), static_cast<std::size_t>(decoder.text.EndOfStorage() - decoder.text.Start()));
    return out;
}
EI_REPLACE_METHOD(0x0042C650, Chunk::ReadEncodedString);

// A chunk holding the characters (no terminator). The buffer is taken after the file (GetBuffer(0)).
void Chunk::WriteString(std::uint32_t id, mfc::CString text) {
    int size = text.GetLength() + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    int length = text.GetLength();
    void* file = File();
    FileWrite(file, text.GetBuffer(0), static_cast<std::uint32_t>(length));
}
EI_REPLACE_METHOD(0x0042C820, Chunk::WriteString);

// The array is emptied (SetSize(0, -1)), then `count` string chunks are read through a child chunk
// (its constructor inlined) and appended. The count is unsigned.
void Chunk::ReadStringArray(void* strings) {
    auto* array = static_cast<console::CStringArray*>(strings);
    std::uint32_t count;
    ReadCounted(this, &count, 4);
    Chunk child(this);
    array->SetSize(0, -1);
    for (std::uint32_t i = count; i != 0; --i) {
        child.ReadHeader();
        alignas(mfc::CString) std::uint8_t slot[sizeof(mfc::CString)];   // the original reuses a stack slot
        mfc::CString* text = child.ReadString(reinterpret_cast<mfc::CString*>(slot));
        array->SetAtGrow(array->GetSize(), *text);
        text->~CString();
    }
}
EI_REPLACE_METHOD(0x0042CA20, Chunk::ReadStringArray);

// The size covers the count and every element's chunk. Each element's buffer is taken with
// GetBuffer(0) after the file is asked for (it may make a shared string unique).
void Chunk::WriteStringArray(std::uint32_t id, void* strings) {
    auto* array = static_cast<console::CStringArray*>(strings);
    int count = array->GetSize();
    int characters = 0;
    for (int i = 0; i < count; ++i) characters += (*array)[i].GetLength();
    std::uint32_t chunkId = id;
    int size = characters + count * 8 + 0xC;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    int n = array->GetSize();
    FileWrite(File(), &n, 4);
    for (int i = 0; i < array->GetSize(); ++i) {
        int elementSize = (*array)[i].GetLength() + kHeader;
        FileWrite(File(), &id, 4);
        FileWrite(File(), &elementSize, 4);
        int length = (*array)[i].GetLength();
        void* file = File();
        FileWrite(file, (*array)[i].GetBuffer(0), static_cast<std::uint32_t>(length));
    }
}
EI_REPLACE_METHOD(0x0042CB80, Chunk::WriteStringArray);

// --- The root chunk ------------------------------------------------------------------------------

int RootChunk::Open(mfc::CString path, unsigned flags) {
    return AsFile(&file_)->Open(static_cast<const char*>(path), flags & 0xFFFF, nullptr);
}
EI_REPLACE_METHOD(0x0042D160, RootChunk::Open);

void* RootChunk::File() { return &file_; }   // 0042D260 (short_functions.cpp)

void RootChunk::Destruct() { file_.~CFile(); }
EI_REPLACE_METHOD(0x0042D270, RootChunk::Destruct);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x00596130, RootChunk::Destruct);
