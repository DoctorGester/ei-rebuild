#pragma once

// The engine's byte streams (CStream, CMemoryStream), the constructors and destructors of the
// reference-counted base classes, and two small allocation helpers (00410000-00430000,
// 00470000-00480000). See docs/notes/core.md. Names are provisional.
// Class map (game.exe's vtables):
//   0073BB68  CRefObject root: slot 0 deleting destructor, slot 1 empty (RET 4). Its destructor
//             body only restores this vtable (two identical copies, 00412720 and 00413430).
//   0073BB58  abstract reference-counted value (adds pure slots 2 and 3, see core_refcount.h);
//             constructor body 00419720, deleting destructor 00412700.
//   0073BDB0  CStream (below): abstract byte stream (slots 1-6 pure).
//   0073BD90  CMemoryStream (below). 0073C960 is a second table with the same slots 1-7 and
//             another copy of the deleting destructor; the network queue's streams use
//             it. Here they are all CMemoryStream.

#include "core_refcount.h"
#include "engine_object.h"
#include "net_coder.h"

#include "lib/static_object.h"
#include "lib/containers.h"

#include <cstddef>
#include <cstdint>

namespace mfc {
class CString;
}

// The engine's abstract byte stream (vtable 0073BDB0). The save game, the network messages and the
// chunk archives are memory streams behind it; the message recordings are file streams
// (net_link.h).
class CStream {
public:
    // Version_ 2000000000, ok_ set.
    CStream();
    CStream(const CStream&) = delete;
    CStream& operator=(const CStream&) = delete;
    virtual ~CStream() {}                                               // 0: 004174E0 (deleting: 00417400)
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) = 0;   // 1: returns the bytes read
    virtual void Write(const void* buffer, std::uint32_t size) = 0;     // 2
    virtual void Seek(std::uint32_t position) = 0;                      // 3
    virtual std::uint32_t Size() = 0;                                   // 4
    virtual std::uint32_t Position() = 0;                               // 5
    virtual void Clear() = 0;                                           // 6: memory streams empty, file streams rewind
    virtual void CopyFrom(CStream* source);                             // 7: 004A4D40 (all of `source`, 1 KB at a time)

    // Streams live on the game's heap (core_streams.cpp: gamecrt::operator_new / delete).
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    // Reads bytes up to a zero or a failed read into a CString constructed at `out`;
    // returns `out`.
    mfc::CString* ReadString(void* out);
    // 004E5840 (RET 4; exit_area.cpp). ReadString into a temporary, assigned to `out`.
    void ReadInto(mfc::CString* out);

    // Reads a block WriteSized wrote (a two-byte size word, two more bytes when
    // its bit 0 is set; the size is the word / 2) into `to`: `to` cleared (slot 6), sought to 0,
    // then given the block (slot 2).
    void ReadSized(CStream* to);
    // Writes all of `from` with its size in front: size * 2 in two bytes, or
    // size * 2 + 1 in four bytes above 30000 (a signed compare). `from` is sought to 0 and read.
    void WriteSized(CStream* from);
    // core_stream_diff.cpp (A-S4): the save game's difference coding. A difference is a run
    // header byte (the run length, 0..255) followed by alternating runs: equal bytes (only the
    // length) and changed bytes (the length and the new bytes), starting with an equal run; a
    // full run (255) is followed by an empty run of the other kind; a zero byte ends it.
    // Writes the difference from `base` to `current` (same size, else nothing);
    // both are read from their start.
    void WriteDiff(CStream* base, CStream* current);
    // Rebuilds into this stream (from its start) `base` with the difference read
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
    // Reads bytes into `buffer` until a zero or `capacity` bytes;
    // buffer[capacity - 1] is always set to zero.
    void ReadText(char* buffer, int capacity);
    // 004A52D0 (RET 0xC, AL). Reads the magic number and the version (four bytes each, into one
    // variable); true and version_ set when the magic matches and oldest <= version <= newest
    // (signed).
    bool ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion);
    // Writes the magic number and the version; version_ = version.
    void WriteHeader(std::uint32_t magic, std::uint32_t version);

    // Back to the start for reading (inlined by the chunk archive): ok_ set, Seek(0), reading_ set.
    void Rewind() {
        ok_ = 1;
        Seek(0);
        reading_ = 1;
    }

    std::uint8_t reading_ = 0;      // 1 reading, 0 writing (CopyFrom clears it; never read)
    std::uint8_t ok_;               // cleared when a read runs past the end
    std::uint32_t version_;         // format version (the save game's); 2000000000 when constructed
};

// A growable memory stream that can also pack bits (vtable 0073BD90). The bit writer (net_delta.h)
// assembles bits in bits_ and stores the byte at data_[bitsIndex_] when a new one starts; Seek,
// Resize and SetSize flush and reset that state.
class CMemoryStream : public CStream {
public:
    // 0041DD70 (and the queue streams' 0066C4F0): an empty stream (Reset's fields).
    CMemoryStream();
    // 004173D0, 004790A0 (deleting: 004173B0, 004790D0): the buffer freed.
    ~CMemoryStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // 1: 004A5380 (a short read zero-fills, clears ok_)
    // 2: 004A53F0 (grows to (capacity + size) * 2). The harness's observer (verify builds) is told
    // first.
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // 3: 004A5490 (then EndBits)
    std::uint32_t Size() override { return size_; }
    std::uint32_t Position() override { return position_; }
    void Clear() override;
    void CopyFrom(CStream* source) override;                           // 7: 004A55D0 (one Read into a resized buffer)

    // Empties the stream: ok_ = 1, no buffer, no pending bits. Does not free the old
    // buffer (Clear, slot 6, is the same with the free).
    void Reset();
    // Size `size`, position 0; a larger buffer (1.5 x size + 0x10) keeps the contents
    // (the old size's bytes) and gets the pending bits byte (EndBits). Within the capacity the bits
    // state is left alone.
    void Resize(std::uint32_t size);
    // Size `size`, position 0; a larger buffer (exactly `size`) does not keep them and
    // only bitMask_ is reset (bitsIndex_ stays).
    void SetSize(std::uint32_t size);

    // The bit writer / reader of the delta code (net_delta.cpp).
    // Appends one bit (a new byte is started after eight).
    // Several writers have an inline copy of it (same effect).
    void PutBit(bool bit);
    // The readers' inline bit read: true when the next bit is set (reads a byte after eight).
    bool GetBit();
    // Stores bits_, appends a zero byte for the next bits (slot 2), bitMask_ = 1.
    void StartByte();
    // Stores bits_, no pending byte, bitMask_ = 0x80.
    void EndBits();
    // Writes the memory stream `from` into this stream (its slot 2): `from`'s
    // pending bits stored first (EndBits), then its size times two, in one byte when that is below
    // 0x100 (a signed compare), otherwise in four bytes with bit 0 set; then its bytes.
    void WriteStream(CMemoryStream* from);
    // Reads what WriteStream wrote (this stream's slot 1) into the memory stream
    // `to`: resized to the length (SetSize), filled, back at position 0, ok set. 0 for a length
    // over 100000 (nothing more read) or a short read, else 1.
    int ReadStream(CMemoryStream* to);
    // 0047D7F0 / 0047D970 (RET 8). The whole of this memory stream (a fixed view over its bytes)
    // compressed / decompressed by `coder` into a growing buffer, which then
    // replaces `out`'s contents; `out` is left at position 0. The coder keeps its window from call
    // to call (the client's and the server's message streams).
    void CompressTo(CoderState* coder, CMemoryStream* out);
    void DecompressTo(CoderState* coder, CMemoryStream* out);

    std::uint8_t bits_ = 0;         // byte being assembled by the bit writer
    std::int32_t bitMask_;          // bit of the last bit written (0x80: start a new byte)
    std::int32_t bitsIndex_;        // where bits_ goes in the data (-1: nowhere yet)
    std::uint8_t* data_;
    std::uint32_t capacity_;        // bytes allocated
    std::uint32_t position_;        // read / write position
    std::uint32_t size_;            // end of the data
};

// The game's save stream: the global CMemoryStream g_saveStream 007AFDF8 (world_game.cpp).
CMemoryStream* GameSaveStream();
// The stream the received packets are decompressed into (with the network queue's vtable; ReceiveMessages fills it):
// g_receivedMessages 007C83D0 (player_server_handlers.cpp).
CMemoryStream& ReceivedMessages();

// The harness's view of the memory streams' writes (verify builds; the save trace,
// tests/verify/harness_savetrace.cpp): the stream, the size and the slot's return address
// (`_AddressOfReturnAddress()`), before the bytes are written. game.exe's run has the harness take
// game.exe's slots instead.
extern void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame);

// 0047B940 (and its copy 004D9ED0). Allocates room for `count` pointers on a 32-byte boundary; the block returned by
// operator new is stored just before the aligned pointer (freed through it by 0047B620).
void** __fastcall AlignedPointerArrayAlloc(int count);

// The 0x2828-byte coder state (CoderState) is declared in net_coder.h; its constructor 0042FCA0
// is in core_streams.cpp.
