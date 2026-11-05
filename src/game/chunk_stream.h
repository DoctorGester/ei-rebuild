#pragma once

// Chunked files: the .mob zone files and the map-editor object specs, which the game only reads. A chunk is an id
// and a size (header included), followed by its content; chunks nest. A Chunk passes every byte it consumes up to its
// parent and reaches the file through File(): a chunk asks its parent, the root returns the file it embeds. See
// docs/notes/core.md.

#include "lib/vecmath.h"
#include "platform/platform.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

class Chunk {
public:
    explicit Chunk(Chunk* newParent) : parent(newParent) {}
    ~Chunk();

    // Called by ForEachChild for each child header read; the readers read the chunk's value by its id.
    virtual void OnChunk();
    virtual platform::File* File();

    void Advance(int bytes);
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
    // A string chunk's content is its characters (no terminator); the string arrays are a count followed by one
    // string chunk per element (all with the array chunk's id).
    std::string ReadString();
    std::string ReadEncodedString();
    std::vector<std::string> ReadStringArray();

    std::uint32_t id = 0;
    std::int32_t consumed = 0;  // bytes of the chunk read so far, header included
    std::int32_t size = 0;   // size from the header, header included
    Chunk* parent = nullptr;
};

// The root chunk of a file: File() returns its file.
class RootChunk : public Chunk {
public:
    RootChunk() : Chunk(nullptr) {}

    platform::File* File() override;

    ~RootChunk();

    // Opens the file (platform::File::Open).
    int Open(const std::string& path, platform::FileMode mode);

    platform::File file;
};
