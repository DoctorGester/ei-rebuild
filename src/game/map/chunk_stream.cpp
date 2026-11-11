// Chunked files. See chunk_stream.h.
// Every file operation asks for the file again through File(), as the game does.
#include "game/map/chunk_stream.h"

#include "engine/core/asset_text.h"
#include "game/console/console.h"

#include "platform/platform.h"
#include "game/map/mob_script_decoder.h"
#include "lib/str.h"

#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int kHeader = 8;

std::uint32_t FileRead(platform::File* file, void* data, std::uint32_t size) { return file->Read(data, size); }
void FileSeek(platform::File* file, std::int32_t offset, platform::SeekFrom from) { file->Seek(offset, from); }

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

platform::File* Chunk::File() { return parent ? parent->File() : nullptr; }

// --- Reading -----------------------------------------------------------------------------------

bool Chunk::SkipContent() {
    if (consumed != kHeader || size == kHeader) return false;
    FileSeek(File(), size - kHeader, platform::SeekFrom::Current);
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

// --- The root chunk ------------------------------------------------------------------------------

int RootChunk::Open(const std::string& path, platform::FileMode mode) {
    return file.Open(path, mode);
}

platform::File* RootChunk::File() { return &file; }

RootChunk::~RootChunk() = default;
