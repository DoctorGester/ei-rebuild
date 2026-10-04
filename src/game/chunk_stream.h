#pragma once

// Chunked files (0042BEC0-0042D150): the format of the .mob zone files (read by 00481F00 when a
// zone loads) and of the map-editor object specs (0046A000-0046E000). A chunk is an id and a size
// (header included), followed by its content; chunks nest. A Chunk object tracks one chunk being
// read or written and passes every byte it consumes up to its parent. The file is reached through
// virtual slot 1: a chunk asks its parent, the root (vtable 0073BE9C, which embeds the MFC CFile at
// +0x18, constructor 0042D1C0) returns its file. Names provisional; see
// docs/notes/unmapped_410_470.md.

#include "maths.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// Four floats passed by value (16 bytes on the stack), kept as bits.
struct Float4 {
    std::uint32_t bits[4];
};

struct Chunk {
    void* vtable_;           // 0073BE94: slot 0 OnChunk (empty here, 0042BEE0), slot 1 File (0042BF40)
    std::uint32_t id_;       // +0x04
    std::int32_t consumed_;  // +0x08 bytes of the chunk read or written so far, header included
    std::int32_t size_;      // +0x0C size from the header, header included
    std::uint32_t start_;    // +0x10 file position of the size field while writing
    Chunk* parent_;          // +0x14

    // 0042BEC0. Returns this.
    Chunk* Construct(Chunk* parent);
    // 0042BEF0. Destructor body: restores the vtable.
    void Destruct();
    // 0042BF00. Counts `bytes` as consumed here and in every parent.
    void Advance(int bytes);
    // Slot 1 through the vtable: the file (an MFC CFile).
    void* File();
    // 0042BF50 / 0042BFB0. Writing a chunk of unknown size: the header with a placeholder size,
    // and at the end the size (consumed_) patched in.
    void BeginWrite(std::uint32_t id);
    void EndWrite();
    // 0042C000. Writes a header for `payload` content bytes and counts all of it as consumed.
    void WriteHeader(std::uint32_t id, int payload);
    // 0042C060. When only the header was read, skips the content; returns whether it did (AL).
    bool SkipContent();
    // 0042C0B0. Skips the unread content of the current chunk and reads the next header.
    void ReadHeader();
    // 0042C140. Reads the headers of the chunks that follow until the parent is consumed, calling
    // slot 0 (OnChunk) for each and skipping what it leaves unread.
    void ForEachChild();
    // Values of the current chunk.
    float ReadFloat();                      // 0042C280
    int ReadInt();                          // 0042C340
    std::uint8_t ReadByte();                // 0042C4C0 (AL)
    Vector3* ReadVector3(Vector3* out);     // 0042CCC0, returns out
    float* ReadQuad(float* out);            // 0042CEC0: four values stored as 1, 3, 0, 2; returns out
    float* ReadQuaternion(float* out);      // 0042D060: 16 bytes (default 1, 0, 0, 0); returns out
    void ReadBytes(void* data, int size);   // 0042D020
    // Whole chunks holding one value. Floats are taken as their bits (stack arguments), so the
    // bytes written are exactly the caller's.
    void WriteFloat(std::uint32_t id, std::uint32_t valueBits);   // 0042C2C0
    void WriteInt(std::uint32_t id, int value);             // 0042C380
    void WriteByte(std::uint32_t id, std::uint8_t value);   // 0042C500
    void WriteVector3(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t z); // 0042CD70
    void WriteQuaternion(std::uint32_t id, Float4 value);                    // 0042D0E0
    void WriteBlob(std::uint32_t id, const void* data, int size);            // 0042CFA0
    // Text. A string chunk's content is its characters (no terminator); the string arrays are a
    // count followed by one string chunk per element (all with the array chunk's id). CStrings are
    // returned and passed by value as in the original (hidden return pointer, callee-destroyed
    // argument).
    mfc::CString* ReadString(mfc::CString* out);                             // 0042C580, returns out
    mfc::CString* ReadEncodedString(mfc::CString* out);                      // 0042C650, returns out
    void WriteString(std::uint32_t id, mfc::CString text);                   // 0042C820
    void ReadStringArray(void* array);                                       // 0042CA20 (CStringArray)
    void WriteStringArray(std::uint32_t id, void* array);                    // 0042CB80 (CStringArray)
};
static_assert(offsetof(Chunk, parent_) == 0x14);
static_assert(sizeof(Chunk) == 0x18);

constexpr std::uintptr_t kChunkVtable = 0x0073BE94;

// The root chunk of a file (vtable 0073BE9C: slot 1 returns the CFile embedded at +0x18): 0042D160
// opens the file (CFile::Open through its vtable with the low 16 bits of `flags`), 0042D270 is the
// destructor body.
int __fastcall ChunkRootOpen(Chunk* self, void*, mfc::CString path, unsigned flags);
void __fastcall ChunkRootDestruct(Chunk* self);

// 0042BF40: slot 1 of the base chunk (__fastcall shim of a virtual): the parent's file, or null.
void* __fastcall ChunkFile(Chunk* self);
