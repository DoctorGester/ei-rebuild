#pragma once

// Chunked files: the .mob zone files and the map-editor object specs. A chunk is an id and a size (header
// included), followed by its content; chunks nest. A Chunk passes every byte it consumes up to its parent and
// reaches the file through File(): a chunk asks its parent, the root returns the file it embeds. See
// docs/notes/core.md.

#include "maths.h"
#include "platform/platform.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

class Chunk {
public:
    explicit Chunk(Chunk* newParent) : id(0), consumed(0), size(0), start(0), parent(newParent) {}
    ~Chunk();

    // Called by ForEachChild for each child header read; the readers read the chunk's value by its id.
    virtual void OnChunk();
    virtual platform::File* File();

    void Advance(int bytes);
    // Writing a chunk of unknown size: a placeholder size, patched in at the end.
    void BeginWrite(std::uint32_t id);
    void EndWrite();
    // Writes a header for `payload` content bytes and counts all of it as consumed.
    void WriteHeader(std::uint32_t id, int payload);
    // When only the header was read, skips the content; returns whether it did.
    bool SkipContent();
    // Skips the unread content of the current chunk and reads the next header.
    void ReadHeader();
    // Reads the headers of the chunks that follow until the parent is consumed, calling
    // OnChunk for each and skipping what it leaves unread.
    void ForEachChild();
    float ReadFloat();
    int ReadInt();
    std::uint8_t ReadByte();
    Vector3 ReadVector3();
    Quaternion ReadQuaternion();            // 16 bytes (default 1, 0, 0, 0)
    void ReadBytes(void* data, int size);
    // Whole chunks holding one value. Floats are taken as their bits (stack arguments), so the
    // bytes written are exactly the caller's.
    void WriteFloat(std::uint32_t id, std::uint32_t valueBits);
    void WriteInt(std::uint32_t id, int value);
    void WriteByte(std::uint32_t id, std::uint8_t value);
    void WriteVector3(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t z);
    void WriteQuaternion(std::uint32_t id, Quaternion value);
    void WriteBlob(std::uint32_t id, const void* data, int size);
    // A string chunk's content is its characters (no terminator); the string arrays are a count followed by one
    // string chunk per element (all with the array chunk's id).
    std::string ReadString();
    std::string ReadEncodedString();
    void WriteString(std::uint32_t id, std::string text);
    std::vector<std::string> ReadStringArray();
    void WriteStringArray(std::uint32_t id, const std::vector<std::string>* array);

    std::uint32_t id;
    std::int32_t consumed;   // bytes of the chunk read or written so far, header included
    std::int32_t size;       // size from the header, header included
    std::uint32_t start;     // file position of the size field while writing
    Chunk* parent;
};

// The root chunk of a file: File() returns its file.
class RootChunk : public Chunk {
public:
    RootChunk() : Chunk(nullptr) {}

    platform::File* File() override;

    ~RootChunk();

    // Opens the file: platform::File::Open with the low 16 bits of `flags`.
    int Open(const std::string& path, unsigned flags);

    platform::File file;
};
