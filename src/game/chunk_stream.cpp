// Chunked files. See chunk_stream.h.
// Every file operation asks for the file again through File(), as the game does.
#include "chunk_stream.h"

#include "asset_text.h"
#include "console.h"

#include "lib/file_io.h"
#include "mob_script_decoder.h"
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
void FileSeek(lib::File* file, std::int32_t offset, std::uint32_t from) { file->Seek(offset, from); }

// A chunk holding one value: header, then the value (counted with the header).
void WriteValueChunk(Chunk* chunk, std::uint32_t id, const void* value, std::uint32_t valueSize) {
    int size = kHeader + static_cast<int>(valueSize);
    FileWrite(chunk->File(), &id, 4);
    FileWrite(chunk->File(), &size, 4);
    if (chunk->parent) chunk->parent->Advance(size);
    chunk->consumed += size;
    FileWrite(chunk->File(), value, valueSize);
}

void ReadCounted(Chunk* chunk, void* data, std::uint32_t size) {
    FileRead(chunk->File(), data, size);
    if (chunk->parent) chunk->parent->Advance(static_cast<int>(size));
    chunk->consumed += static_cast<int>(size);
}

} // namespace

Chunk::~Chunk() = default;

void Chunk::Advance(int bytes) {
    if (parent) parent->Advance(bytes);
    consumed += bytes;
}

void Chunk::OnChunk() {}

lib::File* Chunk::File() { return parent ? parent->File() : nullptr; }

// --- Writing -----------------------------------------------------------------------------------

void Chunk::BeginWrite(std::uint32_t newId) {
    FileWrite(File(), &newId, 4);
    start = File()->GetPosition();
    FileWrite(File(), &size, 4);
    consumed = 0;
    if (parent) parent->Advance(kHeader);
    consumed += kHeader;
}

void Chunk::EndWrite() {
    std::uint32_t end = File()->GetPosition();
    FileSeek(File(), static_cast<std::int32_t>(start), 0);
    FileWrite(File(), &consumed, 4);
    FileSeek(File(), static_cast<std::int32_t>(end), 0);
}

void Chunk::WriteHeader(std::uint32_t chunkId, int payload) {
    int chunkSize = payload + kHeader;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &chunkSize, 4);
    if (parent) parent->Advance(chunkSize);
    consumed += chunkSize;
}

void Chunk::WriteFloat(std::uint32_t chunkId, std::uint32_t valueBits) { WriteValueChunk(this, chunkId, &valueBits, 4); }

void Chunk::WriteInt(std::uint32_t chunkId, int value) { WriteValueChunk(this, chunkId, &value, 4); }

void Chunk::WriteByte(std::uint32_t chunkId, std::uint8_t value) { WriteValueChunk(this, chunkId, &value, 1); }

// The three values are written one by one from the arguments.
void Chunk::WriteVector3(std::uint32_t chunkId, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    int chunkSize = 0x14;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &chunkSize, 4);
    if (parent) parent->Advance(chunkSize);
    consumed += chunkSize;
    FileWrite(File(), &x, 4);
    FileWrite(File(), &y, 4);
    FileWrite(File(), &z, 4);
}

// The four values are written as one 16-byte block.
void Chunk::WriteQuaternion(std::uint32_t chunkId, Quaternion value) {
    int chunkSize = 0x18;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &chunkSize, 4);
    if (parent) parent->Advance(chunkSize);
    consumed += chunkSize;
    FileWrite(File(), &value, 0x10);
}

void Chunk::WriteBlob(std::uint32_t chunkId, const void* data, int byteCount) {
    int total = byteCount + kHeader;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &total, 4);
    if (parent) parent->Advance(total);
    consumed += total;
    FileWrite(File(), data, static_cast<std::uint32_t>(byteCount));
}

// --- Reading -----------------------------------------------------------------------------------

bool Chunk::SkipContent() {
    if (consumed != kHeader || size == kHeader) return false;
    FileSeek(File(), size - kHeader, kSeekCurrent);
    int rest = size - kHeader;
    if (parent) parent->Advance(rest);
    consumed += rest;
    return true;
}

void Chunk::ReadHeader() {
    SkipContent();
    FileRead(File(), &id, 4);
    FileRead(File(), &size, 4);
    consumed = 0;
    if (parent) parent->Advance(kHeader);
    consumed += kHeader;
}

// The parent is not checked for null (its fields are read unconditionally).
void Chunk::ForEachChild() {
    ReadHeader();
    for (;;) {
        OnChunk();
        SkipContent();
        if (parent->size == parent->consumed) return;
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

// The three floats' bits as read.
Vector3 Chunk::ReadVector3() {
    std::uint32_t v[3];
    ReadCounted(this, &v[0], 4);
    ReadCounted(this, &v[1], 4);
    ReadCounted(this, &v[2], 4);
    return {std::bit_cast<float>(v[0]), std::bit_cast<float>(v[1]), std::bit_cast<float>(v[2])};
}

// A short read leaves the defaults (1, 0, 0, 0) in place.
Quaternion Chunk::ReadQuaternion() {
    std::uint32_t v[4] = {0x3F800000u, 0, 0, 0};
    ReadCounted(this, v, 0x10);
    return {std::bit_cast<float>(v[0]), std::bit_cast<float>(v[1]), std::bit_cast<float>(v[2]), std::bit_cast<float>(v[3])};
}

void Chunk::ReadBytes(void* data, int byteCount) {
    FileRead(File(), data, static_cast<std::uint32_t>(byteCount));
    if (parent) parent->Advance(byteCount);
    consumed += byteCount;
}

// --- Text ----------------------------------------------------------------------------------------

// The characters are read one at a time, each counted; the text ends at the first NUL (ReleaseBuffer(-1)).
std::string Chunk::ReadString() {
    std::string text;
    int length = size - kHeader;
    char* p = str::SetLength(text, length);
    std::uint8_t c;
    for (int i = length; i > 0; --i) {
        FileRead(File(), &c, 1);
        if (parent) parent->Advance(1);
        ++consumed;
        *p++ = static_cast<char>(c);
    }
    str::CutAtNull(text);
    text::FromAssetText(&text);   // .mob text (unit names, dialogue) in UTF-8
    return text;
}

// The content read into the decoder's input (resized to it, zero-filled) in one read, then decoded.
std::string Chunk::ReadEncodedString() {
    std::uint32_t length = static_cast<std::uint32_t>(size - kHeader);
    MobScriptDecoder decoder;
    decoder.input.resize(length, 0);
    FileRead(File(), decoder.input.data(), length);
    if (parent) parent->Advance(static_cast<int>(length));
    consumed += static_cast<int>(length);
    std::string text = decoder.Decode();
    text::FromAssetText(&text);   // the scripts' literals in UTF-8
    return text;
}

// A chunk holding the characters (no terminator), in the asset code page as ReadString reads them. The buffer is
// taken after the file (GetBuffer(0)).
void Chunk::WriteString(std::uint32_t chunkId, std::string text) {
    text = text::ToAssetText(text);
    int chunkSize = static_cast<int>(text.size()) + kHeader;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &chunkSize, 4);
    if (parent) parent->Advance(chunkSize);
    consumed += chunkSize;
    int length = text.size();
    lib::File* file = File();
    FileWrite(file, text.data(), static_cast<std::uint32_t>(length));
}

// `count` string chunks read through a child chunk (its constructor inlined). The count is unsigned.
std::vector<std::string> Chunk::ReadStringArray() {
    std::uint32_t count;
    ReadCounted(this, &count, 4);
    Chunk child(this);
    std::vector<std::string> array;
    for (std::uint32_t i = count; i != 0; --i) {
        child.ReadHeader();
        array.push_back(child.ReadString());
    }
    return array;
}

// The size covers the count and every element's chunk. The texts in the asset code page, as WriteString writes them.
void Chunk::WriteStringArray(std::uint32_t arrayId, const std::vector<std::string>* texts) {
    std::vector<std::string> converted;
    for (const std::string& text : *texts) converted.push_back(text::ToAssetText(text));
    const std::vector<std::string>* array = &converted;
    int count = static_cast<int>(array->size());
    int characters = 0;
    for (const std::string& text : *array) characters += static_cast<int>(text.size());
    std::uint32_t chunkId = arrayId;
    int chunkSize = characters + count * 8 + 0xC;
    FileWrite(File(), &chunkId, 4);
    FileWrite(File(), &chunkSize, 4);
    if (parent) parent->Advance(chunkSize);
    consumed += chunkSize;
    int n = count;
    FileWrite(File(), &n, 4);
    for (const std::string& text : *array) {
        int elementSize = static_cast<int>(text.size()) + kHeader;
        FileWrite(File(), &arrayId, 4);
        FileWrite(File(), &elementSize, 4);
        FileWrite(File(), text.data(), static_cast<std::uint32_t>(text.size()));
    }
}

// --- The root chunk ------------------------------------------------------------------------------

int RootChunk::Open(const std::string& path, unsigned flags) {
    return file.Open(path, flags & 0xFFFF);
}

lib::File* RootChunk::File() { return &file; }

RootChunk::~RootChunk() = default;
