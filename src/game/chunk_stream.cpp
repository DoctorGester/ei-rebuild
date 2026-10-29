// Chunked files. See chunk_stream.h.
// Every file operation asks for the file again through File(), as the game does.
#include "chunk_stream.h"

#include "asset_text.h"
#include "console.h"

#include "lib/file_io.h"
#include "mob_script_decoder.h"
#include "ei/x87.h"
#include "lib/str.h"

#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kSeekCurrent = 1;
constexpr int kHeader = 8;

std::uint32_t FileRead(lib::File* file, void* data, std::uint32_t size) { return file->Read(data, size); }
void FileWrite(lib::File* file, const void* data, std::uint32_t size) { file->Write(data, size); }
std::uint32_t FilePosition(lib::File* file) { return file->GetPosition(); }
void FileSeek(lib::File* file, std::int32_t offset, std::uint32_t from) { file->Seek(offset, from); }

// A chunk holding one value: header, then the value (counted with the header).
void WriteValueChunk(Chunk* chunk, std::uint32_t id, const void* value, std::uint32_t valueSize) {
    int size = kHeader + static_cast<int>(valueSize);
    FileWrite(chunk->File(), &id, 4);
    FileWrite(chunk->File(), &size, 4);
    if (chunk->parent_) chunk->parent_->Advance(size);
    chunk->consumed_ += size;
    FileWrite(chunk->File(), value, valueSize);
}

void ReadCounted(Chunk* chunk, void* data, std::uint32_t size) {
    FileRead(chunk->File(), data, size);
    if (chunk->parent_) chunk->parent_->Advance(static_cast<int>(size));
    chunk->consumed_ += static_cast<int>(size);
}

} // namespace

Chunk::~Chunk() = default;

void Chunk::Advance(int bytes) {
    if (parent_) parent_->Advance(bytes);
    consumed_ += bytes;
}

void Chunk::OnChunk() {}

lib::File* Chunk::File() { return parent_ ? parent_->File() : nullptr; }

// --- Writing -----------------------------------------------------------------------------------

void Chunk::BeginWrite(std::uint32_t id) {
    FileWrite(File(), &id, 4);
    start_ = FilePosition(File());
    FileWrite(File(), &size_, 4);
    consumed_ = 0;
    if (parent_) parent_->Advance(kHeader);
    consumed_ += kHeader;
}

void Chunk::EndWrite() {
    std::uint32_t end = FilePosition(File());
    FileSeek(File(), static_cast<std::int32_t>(start_), 0);
    FileWrite(File(), &consumed_, 4);
    FileSeek(File(), static_cast<std::int32_t>(end), 0);
}

void Chunk::WriteHeader(std::uint32_t id, int payload) {
    int size = payload + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
}

void Chunk::WriteFloat(std::uint32_t id, std::uint32_t valueBits) { WriteValueChunk(this, id, &valueBits, 4); }

void Chunk::WriteInt(std::uint32_t id, int value) { WriteValueChunk(this, id, &value, 4); }

void Chunk::WriteByte(std::uint32_t id, std::uint8_t value) { WriteValueChunk(this, id, &value, 1); }

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

// The four values are written as one 16-byte block.
void Chunk::WriteQuaternion(std::uint32_t id, Quaternion value) {
    int size = 0x18;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    FileWrite(File(), &value, 0x10);
}

void Chunk::WriteBlob(std::uint32_t id, const void* data, int size) {
    int total = size + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &total, 4);
    if (parent_) parent_->Advance(total);
    consumed_ += total;
    FileWrite(File(), data, static_cast<std::uint32_t>(size));
}

// --- Reading -----------------------------------------------------------------------------------

bool Chunk::SkipContent() {
    if (consumed_ != kHeader || size_ == kHeader) return false;
    FileSeek(File(), size_ - kHeader, kSeekCurrent);
    int rest = size_ - kHeader;
    if (parent_) parent_->Advance(rest);
    consumed_ += rest;
    return true;
}

void Chunk::ReadHeader() {
    SkipContent();
    FileRead(File(), &id_, 4);
    FileRead(File(), &size_, 4);
    consumed_ = 0;
    if (parent_) parent_->Advance(kHeader);
    consumed_ += kHeader;
}

// The parent is not checked for null (its fields are read unconditionally).
void Chunk::ForEachChild() {
    ReadHeader();
    for (;;) {
        OnChunk();
        SkipContent();
        if (parent_->size_ == parent_->consumed_) return;
        ReadHeader();
    }
}

float Chunk::ReadFloat() {
    float value;
    ReadCounted(this, &value, 4);
    return value;
}

int Chunk::ReadInt() {
    int value;
    ReadCounted(this, &value, 4);
    return value;
}

std::uint8_t Chunk::ReadByte() {
    std::uint8_t value;
    ReadCounted(this, &value, 1);
    return value;
}

// x and z are copied through the FPU, y as an integer.
Vector3* Chunk::ReadVector3(Vector3* out) {
    std::uint32_t v[3];
    ReadCounted(this, &v[0], 4);
    ReadCounted(this, &v[1], 4);
    ReadCounted(this, &v[2], 4);
    std::uint32_t x;
    x = v[0];
    out->x = std::bit_cast<float>(x);
    std::uint32_t z;
    z = v[2];
    out->z = std::bit_cast<float>(z);
    out->y = std::bit_cast<float>(v[1]);
    return out;
}

// A short read leaves the defaults (1, 0, 0, 0) in place.
float* Chunk::ReadQuaternion(float* out) {
    std::uint32_t v[4] = {0x3F800000u, 0, 0, 0};
    ReadCounted(this, v, 0x10);
    std::memcpy(out, v, sizeof(v));
    return out;
}

void Chunk::ReadBytes(void* data, int size) {
    FileRead(File(), data, static_cast<std::uint32_t>(size));
    if (parent_) parent_->Advance(size);
    consumed_ += size;
}

// --- Text ----------------------------------------------------------------------------------------

// The characters are read one at a time, each counted; the text ends at the first NUL (ReleaseBuffer(-1)).
std::string Chunk::ReadString() {
    std::string text;
    int length = size_ - kHeader;
    char* p = str::SetLength(text, length);
    std::uint8_t c;
    for (int i = length; i > 0; --i) {
        FileRead(File(), &c, 1);
        if (parent_) parent_->Advance(1);
        ++consumed_;
        *p++ = static_cast<char>(c);
    }
    str::CutAtNull(text);
    text::FromAssetText(&text);   // .mob text (unit names, dialogue) in UTF-8
    return text;
}

// The content read into the decoder's input (resized to it, zero-filled) in one read, then decoded.
std::string Chunk::ReadEncodedString() {
    std::uint32_t length = static_cast<std::uint32_t>(size_ - kHeader);
    MobScriptDecoder decoder;
    decoder.input.resize(length, 0);
    FileRead(File(), decoder.input.data(), length);
    if (parent_) parent_->Advance(static_cast<int>(length));
    consumed_ += static_cast<int>(length);
    std::string text = decoder.Decode();
    text::FromAssetText(&text);   // the scripts' literals in UTF-8
    return text;
}

// A chunk holding the characters (no terminator), in the asset code page as ReadString reads them. The buffer is
// taken after the file (GetBuffer(0)).
void Chunk::WriteString(std::uint32_t id, std::string text) {
    text = text::ToAssetText(text);
    int size = static_cast<int>(text.size()) + kHeader;
    FileWrite(File(), &id, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    int length = text.size();
    lib::File* file = File();
    FileWrite(file, str::Buffer(text, 0), static_cast<std::uint32_t>(length));
}

// The array is emptied, then `count` string chunks are read through a child chunk (its constructor
// inlined) and appended. The count is unsigned.
void Chunk::ReadStringArray(std::vector<std::string>* array) {
    std::uint32_t count;
    ReadCounted(this, &count, 4);
    Chunk child(this);
    array->clear();
    for (std::uint32_t i = count; i != 0; --i) {
        child.ReadHeader();
        array->push_back(child.ReadString());
    }
}

// The size covers the count and every element's chunk. The texts in the asset code page, as WriteString writes them.
void Chunk::WriteStringArray(std::uint32_t id, const std::vector<std::string>* texts) {
    std::vector<std::string> converted;
    for (const std::string& text : *texts) converted.push_back(text::ToAssetText(text));
    const std::vector<std::string>* array = &converted;
    int count = static_cast<int>(array->size());
    int characters = 0;
    for (const std::string& text : *array) characters += static_cast<int>(text.size());
    std::uint32_t chunkId = id;
    int size = characters + count * 8 + 0xC;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &size, 4);
    if (parent_) parent_->Advance(size);
    consumed_ += size;
    int n = count;
    FileWrite(File(), &n, 4);
    for (const std::string& text : *array) {
        int elementSize = static_cast<int>(text.size()) + kHeader;
        FileWrite(File(), &id, 4);
        FileWrite(File(), &elementSize, 4);
        FileWrite(File(), text.data(), static_cast<std::uint32_t>(text.size()));
    }
}

// --- The root chunk ------------------------------------------------------------------------------

int RootChunk::Open(const std::string& path, unsigned flags) {
    return file_.Open(path, flags & 0xFFFF);
}

lib::File* RootChunk::File() { return &file_; }

RootChunk::~RootChunk() = default;
