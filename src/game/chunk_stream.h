#pragma once

// Chunked files (0042BEC0-0042D150, the root chunk to 0042D270): the format of the .mob zone files (read by 00481F00 when a
// zone loads) and of the map-editor object specs (0046A000-0046E000). A chunk is an id and a size
// (header included), followed by its content; chunks nest. A Chunk object tracks one chunk being
// read or written and passes every byte it consumes up to its parent. The file is reached through
// virtual slot 1: a chunk asks its parent, and the root (RootChunk) returns the MFC CFile it
// embeds. Names provisional; see docs/notes/core.md.
// Chunk and RootChunk are real virtual classes: the compiler makes their vtables (game.exe's
// 0073BE94, 0073BE9C and the .mob root's 0073FAD4 are retired). The map specs' records and child
// chunks (map_spec.h, map_spec_unit.cpp) derive from Chunk.

#include "maths.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class Chunk {
public:
    // Nothing read or written yet, under `parent`.
    explicit Chunk(Chunk* parent) : id_(0), consumed_(0), size_(0), start_(0), parent_(parent) {}
    // 0042BEF0 (and the unit record's chunk classes' 0046C7E0, 0046CBB0 and 0046D070): nothing to do.
    ~Chunk();

    // 0: 0042BEE0 (empty). Called by ForEachChild for each child header read; the readers (the
    // zone's object records, map_spec.h) read the chunk's value by its id.
    virtual void OnChunk();
    // The file (an MFC CFile): the parent's, or null without a parent.
    virtual mfc::CFileVirtuals* File();

    // Counts `bytes` as consumed here and in every parent.
    void Advance(int bytes);
    // 0042BF50 / 0042BFB0. Writing a chunk of unknown size: the header with a placeholder size,
    // and at the end the size (consumed_) patched in.
    void BeginWrite(std::uint32_t id);
    void EndWrite();
    // Writes a header for `payload` content bytes and counts all of it as consumed.
    void WriteHeader(std::uint32_t id, int payload);
    // When only the header was read, skips the content; returns whether it did (AL).
    bool SkipContent();
    // Skips the unread content of the current chunk and reads the next header.
    void ReadHeader();
    // Reads the headers of the chunks that follow until the parent is consumed, calling
    // OnChunk for each and skipping what it leaves unread.
    void ForEachChild();
    // Values of the current chunk.
    float ReadFloat();
    int ReadInt();
    std::uint8_t ReadByte();                // 0042C4C0 (AL)
    Vector3* ReadVector3(Vector3* out);     // 0042CCC0, returns out
    float* ReadQuad(float* out);            // four values stored as 1, 3, 0, 2; returns out
    float* ReadQuaternion(float* out);      // 16 bytes (default 1, 0, 0, 0); returns out
    void ReadBytes(void* data, int size);
    // Whole chunks holding one value. Floats are taken as their bits (stack arguments), so the
    // bytes written are exactly the caller's.
    void WriteFloat(std::uint32_t id, std::uint32_t valueBits);
    void WriteInt(std::uint32_t id, int value);
    void WriteByte(std::uint32_t id, std::uint8_t value);
    void WriteVector3(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t z);
    void WriteQuaternion(std::uint32_t id, Quaternion value);
    void WriteBlob(std::uint32_t id, const void* data, int size);
    // Text. A string chunk's content is its characters (no terminator); the string arrays are a
    // count followed by one string chunk per element (all with the array chunk's id). CStrings are
    // returned and passed by value as in the original (hidden return pointer, callee-destroyed
    // argument). The arrays are MFC CStringArrays (console::CStringArray; the map specs pass their
    // mfc::CStringArray in their layouts).
    mfc::CString* ReadString(mfc::CString* out);                             // 0042C580, returns out
    mfc::CString* ReadEncodedString(mfc::CString* out);                      // 0042C650, returns out
    void WriteString(std::uint32_t id, mfc::CString text);
    void ReadStringArray(mfc::CStringArray* array);
    void WriteStringArray(std::uint32_t id, mfc::CStringArray* array);

    std::uint32_t id_;       // +0x04
    std::int32_t consumed_;  // +0x08 bytes of the chunk read or written so far, header included
    std::int32_t size_;      // +0x0C size from the header, header included
    std::uint32_t start_;    // +0x10 file position of the size field while writing
    Chunk* parent_;          // +0x14
};
static_assert(offsetof(Chunk, id_) == 0x04);
static_assert(offsetof(Chunk, parent_) == 0x14);
static_assert(sizeof(Chunk) == 0x18);

// The root chunk of a file: slot 1 returns its CFile. The .mob object file's root
// (CWorldServer::LoadMapObjects, game.exe's vtable 0073FAD4 with the same slots) is one too.
class RootChunk : public Chunk {
public:
    // No parent, the file closed.
    RootChunk() : Chunk(nullptr) {}

    mfc::CFileVirtuals* File() override;   // 1: 0042D260 (&file_)

    // 0042D270 (and the .mob root's 00596130): the CFile destroyed.
    ~RootChunk();

    // Opens the file: CFile::Open through its vtable with the low 16 bits of `flags`.
    int Open(mfc::CString path, unsigned flags);

    mfc::CFile file_;        // +0x18
};
static_assert(offsetof(RootChunk, file_) == 0x18);
static_assert(sizeof(RootChunk) == 0x28);
