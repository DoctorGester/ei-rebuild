#pragma once

// The engine's byte streams (CStream, CMemoryStream), the constructors and destructors of the
// reference-counted base classes, and two small allocation helpers (00410000-00430000,
// 00470000-00480000). See docs/notes/core.md. Names are provisional.
//
// Class map (vtables):
//   0073BB68  CRefObject root: slot 0 deleting destructor, slot 1 empty (RET 4). Its destructor
//             body only restores this vtable (two identical copies, 00412720 and 00413430).
//   0073BB58  abstract reference-counted value (adds pure slots 2 and 3, see core_refcount.h);
//             constructor body 00419720, deleting destructor 00412700.
//   0073BDB0  CStream (below): abstract byte stream (slots 1-6 pure).
//   0073BD90  CMemoryStream (below). 0073C960 is a second vtable with the same slots 1-7 and
//             another copy of the deleting destructor (004790D0); the network queue's streams use it.
//
// Streams are built and torn down by the game's constructor and destructor bodies (or the
// rewrites of them below, called explicitly where the original inlines them), which store the
// game's vtables. The classes are __declspec(novtable) and their C++ destructors are empty, so C++
// never stores a vtable of its own: a CMemoryStream local or member is plain storage until one of
// those bodies runs, as in the original.

#include "engine_object.h"
#include "net_coder.h"

#include "lib/containers.h"

#include <cstddef>
#include <cstdint>

namespace mfc {
class CString;
}

// The engine's abstract byte stream (vtable 0073BDB0). The save game, the network messages and the
// chunk archives are memory streams behind it; the message recordings are file streams
// (net_link.h).
class __declspec(novtable) CStream {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BDB0;

    virtual ~CStream() {}                                               // 0: 00417400 (deleting destructor)
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) = 0;   // 1: returns the bytes read
    virtual void Write(const void* buffer, std::uint32_t size) = 0;     // 2
    virtual void Seek(std::uint32_t position) = 0;                      // 3
    virtual std::uint32_t Size() = 0;                                   // 4
    virtual std::uint32_t Position() = 0;                               // 5
    virtual void Clear() = 0;                                           // 6: memory streams empty, file streams rewind
    virtual void CopyFrom(CStream* source);                             // 7: 004A4D40 (all of `source`, 1 KB at a time)
    void CopyFromImpl(CStream* source);   // body of the slot above (registered by member pointer)

    // 0047A200. Constructor body: the vtable, version_ = 2000000000, ok_ = 1. Returns this.
    CStream* ConstructBase();
    // 004174E0. Destructor body: restores the vtable.
    void DestructBase();
    // 00417400 (slot 0). DestructBase, then operator delete when bit 0 of `flags` is set.
    CStream* DeletingDestruct(unsigned flags);

    // 00461190. Reads bytes up to a zero or a failed read into a CString constructed at `out`;
    // returns `out`.
    mfc::CString* ReadString(void* out);
    // 004E5840 (RET 4; exit_area.cpp). ReadString into a temporary, assigned to `out`.
    void ReadInto(mfc::CString* out);

    // 004A51C0 (RET 4). Reads a block WriteSized wrote (a two-byte size word, two more bytes when
    // its bit 0 is set; the size is the word / 2) into `to`: `to` cleared (slot 6), sought to 0,
    // then given the block (slot 2).
    void ReadSized(CStream* to);
    // 004A5250 (RET 4). Writes all of `from` with its size in front: size * 2 in two bytes, or
    // size * 2 + 1 in four bytes above 30000 (a signed compare). `from` is sought to 0 and read.
    void WriteSized(CStream* from);
    // core_stream_diff.cpp (A-S4): the save game's difference coding. A difference is a run
    // header byte (the run length, 0..255) followed by alternating runs: equal bytes (only the
    // length) and changed bytes (the length and the new bytes), starting with an equal run; a
    // full run (255) is followed by an empty run of the other kind; a zero byte ends it.
    // 004A4DA0 (RET 8). Writes the difference from `base` to `current` (same size, else nothing);
    // both are read from their start.
    void WriteDiff(CStream* base, CStream* current);
    // 004A50D0 (RET 8). Rebuilds into this stream (from its start) `base` with the difference read
    // from `diff` (at its position), until base's size is reached or `diff` runs out.
    void ApplyDiff(CStream* base, CStream* diff);
    // core_stream_chunks.cpp (A-S4): tagged blocks (a four-byte tag, the size including these
    // eight bytes, then the content).
    // 004A4940 (RET 8, AL). Reads the next block's tag into *tag and its content into `out`
    // (emptied, reading_ cleared, written, sought to 0). False when the tag, the size or the
    // content cannot be read (`out` is then already emptied once the size was read).
    bool ReadTagged(std::uint32_t* tag, CStream* out);
    // 004A4A90 (RET 8, AL). From the start (reading_ set), reads blocks until one has tag `id`;
    // when none has, `out` is emptied and the result is false.
    bool FindTagged(std::uint32_t id, CStream* out);
    // 004A4AF0 (RET 0xC; A-S9; called only by Rebirth's EI_Plugin.asi). Copies this stream's tagged
    // blocks, from its position, to `out` (reading_ cleared), the block with tag `id` replaced by all
    // of `replacement` (sought to 0, read); without one, `replacement` is appended under `id`.
    void ReplaceTagged(std::uint32_t id, CStream* replacement, CStream* out);
    // 004A48F0 (RET 8). Reads bytes into `buffer` until a zero or `capacity` bytes;
    // buffer[capacity - 1] is always set to zero.
    void ReadText(char* buffer, int capacity);
    // 004A52D0 (RET 0xC, AL). Reads the magic number and the version (four bytes each, into one
    // variable); true and version_ set when the magic matches and oldest <= version <= newest
    // (signed).
    bool ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion);
    // 004A5340 (RET 8). Writes the magic number and the version; version_ = version.
    void WriteHeader(std::uint32_t magic, std::uint32_t version);

    // Back to the start for reading (inlined by the chunk archive): ok_ set, Seek(0), reading_ set.
    void Rewind() {
        ok_ = 1;
        Seek(0);
        reading_ = 1;
    }

    std::uint8_t reading_;          // +0x04 1 reading, 0 writing (CopyFrom clears it)
    std::uint8_t ok_;               // +0x05 cleared when a read runs past the end
    std::uint8_t unused_6[2];
    std::uint32_t version_;         // +0x08 format version (the save game's); 2000000000 when constructed
};
static_assert(offsetof(CStream, reading_) == 0x04);
static_assert(offsetof(CStream, ok_) == 0x05);
static_assert(offsetof(CStream, version_) == 0x08);
static_assert(sizeof(CStream) == 0x0C);

// A growable memory stream that can also pack bits (vtable 0073BD90). The bit writer (net_delta.h)
// assembles bits in bits_ and stores the byte at data_[bitsIndex_] when a new one starts; Seek,
// Resize and SetSize flush and reset that state.
class __declspec(novtable) CMemoryStream : public CStream {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD90;

    ~CMemoryStream() override {}                                        // 0: 004173B0 (deleting destructor)
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // 1: 004A5380 (a short read zero-fills, clears ok_)
    std::uint32_t ReadImpl(void* buffer, std::uint32_t size);   // body of the slot above (registered by member pointer)
    void Write(const void* buffer, std::uint32_t size) override;       // 2: 004A53F0 (grows to (capacity + size) * 2)
    void Seek(std::uint32_t position) override;                        // 3: 004A5490 (then EndBits)
    void SeekImpl(std::uint32_t position);   // body of the slot above (registered by member pointer)
    std::uint32_t Size() override;                                     // 4: 00417390 (size_)
    std::uint32_t Position() override;                                 // 5: 004173A0 (position_)
    void Clear() override;                                             // 6: 00417350
    void CopyFrom(CStream* source) override;                           // 7: 004A55D0 (one Read into a resized buffer)
    void CopyFromImpl(CStream* source);   // body of the slot above (registered by member pointer)

    // 0041DD70. Constructor body: CStream's fields, the vtable, an empty stream (Reset's fields).
    // Fields +4, +6, +7 and +0xC are left as they are. Returns this.
    CMemoryStream* Construct();
    // 0066C4F0 (world_effect_lifetime.cpp). Construct with the second vtable (0073C960, the
    // network queue's streams). Returns this.
    CMemoryStream* ConstructQueueStream();
    // 004173D0, 004790A0. Destructor body: frees the buffer (the pointer is left dangling), then
    // CStream's vtable.
    void Destruct();
    // 004173B0, 004790D0 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    CMemoryStream* DeletingDestruct(unsigned flags);
    // 00468C20. Empties the stream: ok_ = 1, no buffer, no pending bits. Does not free the old
    // buffer (Clear, slot 6, is the same with the free).
    void Reset();
    // 004A54D0. Size `size`, position 0; a larger buffer (1.5 x size + 0x10) keeps the contents
    // (the old size's bytes) and gets the pending bits byte (EndBits). Within the capacity the bits
    // state is left alone.
    void Resize(std::uint32_t size);
    // 004A5570. Size `size`, position 0; a larger buffer (exactly `size`) does not keep them and
    // only bitMask_ is reset (bitsIndex_ stays).
    void SetSize(std::uint32_t size);

    // The bit writer / reader of the delta code (net_delta.cpp).
    // 0047DFC0. Appends one bit (a new byte is started after eight).
    // Several writers have an inline copy of it (same effect).
    void PutBit(bool bit);
    // The readers' inline bit read: true when the next bit is set (reads a byte after eight).
    bool GetBit();
    // 004A5610: stores bits_, appends a zero byte for the next bits (slot 2), bitMask_ = 1.
    void StartByte();
    // 004A5650: stores bits_, no pending byte, bitMask_ = 0x80.
    void EndBits();
    // 0047DA80 (RET 4). Writes the memory stream `from` into this stream (its slot 2): `from`'s
    // pending bits stored first (EndBits), then its size times two, in one byte when that is below
    // 0x100 (a signed compare), otherwise in four bytes with bit 0 set; then its bytes.
    void WriteStream(CMemoryStream* from);
    // 0047DAE0 (RET 4). Reads what WriteStream wrote (this stream's slot 1) into the memory stream
    // `to`: resized to the length (SetSize), filled, back at position 0, ok set. 0 for a length
    // over 100000 (nothing more read) or a short read, else 1.
    int ReadStream(CMemoryStream* to);
    // 0047D7F0 / 0047D970 (RET 8). The whole of this memory stream (a fixed view over its bytes)
    // compressed / decompressed by `coder` (00430450 / 00430960) into a growing buffer, which then
    // replaces `out`'s contents; `out` is left at position 0. The coder keeps its window from call
    // to call (the client's and the server's message streams).
    void CompressTo(CoderState* coder, CMemoryStream* out);
    void DecompressTo(CoderState* coder, CMemoryStream* out);

    std::uint8_t bits_;             // +0x0C byte being assembled by the bit writer (+0x0D..0x0F unused)
    std::int32_t bitMask_;          // +0x10 bit of the last bit written (0x80: start a new byte)
    std::int32_t bitsIndex_;        // +0x14 where bits_ goes in the data (-1: nowhere yet)
    std::uint8_t* data_;            // +0x18
    std::uint32_t capacity_;        // +0x1C bytes allocated
    std::uint32_t position_;        // +0x20 read / write position
    std::uint32_t size_;            // +0x24 end of the data
};
static_assert(offsetof(CMemoryStream, bits_) == 0x0C);
static_assert(offsetof(CMemoryStream, bitMask_) == 0x10);
static_assert(offsetof(CMemoryStream, data_) == 0x18);
static_assert(offsetof(CMemoryStream, size_) == 0x24);
static_assert(sizeof(CMemoryStream) == 0x28);

// A memory stream on the stack with the network queue's vtable, as the original builds and
// destroys it inline.
struct QueueStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    QueueStream() { Get()->ConstructQueueStream(); }
    ~QueueStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};
static_assert(sizeof(QueueStream) == 0x28 && offsetof(QueueStream, bytes) == 0);

// The game's save stream: the global CMemoryStream at 007AFDF8 (defined in world_game.cpp).
CMemoryStream* GameSaveStream();

constexpr std::uintptr_t kRefObjectRootVtable = 0x0073BB68;
constexpr std::uintptr_t kRefValueVtable = 0x0073BB58;
// The stream vtables under the names the world_* and net_* code uses.
constexpr std::uintptr_t kStreamVtable = CStream::kVtableAddress;
constexpr std::uintptr_t kMemoryStreamVtable = CMemoryStream::kVtableAddress;

// CRefObject root (vtable 0073BB68).
void __fastcall RefObjectDestructBase(void* self);                                 // 00412720, 00413430
void* __fastcall RefObjectDeletingDestructor(void* self, void* edx, unsigned flags); // 00412730 (slot 0)
// Abstract reference-counted value (vtable 0073BB58).
void* __fastcall RefValueConstructBase(void* self);                                // 00419720, returns this
void* __fastcall RefValueDeletingDestructor(void* self, void* edx, unsigned flags);  // 00412700 (slot 0)

// 0047B940 (and its copy 004D9ED0). Allocates room for `count` pointers on a 32-byte boundary; the block returned by
// operator new is stored just before the aligned pointer (freed through it by 0047B620).
void** __fastcall AlignedPointerArrayAlloc(int count);

// 0047B620 (thiscall). Releases every element of an aligned array of objects (slot 4; the size is
// read again after each), frees the block and empties the array (the growth step stays).
void __fastcall AlignedObjectsClear(lib::AlignedArray<CEngineObject*>* objects);

// The 0x2828-byte coder state (CoderState) is declared in net_coder.h; its constructor 0042FCA0
// is in core_streams.cpp.
