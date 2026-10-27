#pragma once

// The engine's byte streams (CStream, CMemoryStream), the constructors and destructors of the
// reference-counted base classes, and two small allocation helpers.
// See docs/notes/core.md. Names are provisional.

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

// The engine's abstract byte stream. The save game, the network messages and the
// chunk archives are memory streams behind it; the message recordings are file streams
//.
class CStream {
public:
    CStream();
    CStream(const CStream&) = delete;
    CStream& operator=(const CStream&) = delete;
    virtual ~CStream() {}                                               // deleting
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) = 0;   // returns the bytes read
    virtual void Write(const void* buffer, std::uint32_t size) = 0;
    virtual void Seek(std::uint32_t position) = 0;
    virtual std::uint32_t Size() = 0;
    virtual std::uint32_t Position() = 0;
    virtual void Clear() = 0;                                           // memory streams empty, file streams rewind
    virtual void CopyFrom(CStream* source);                             // all of `source`, 1 KB at a time

    // Streams live on the game's heap (core_streams.cpp: gamecrt::operator_new / delete).
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    // Reads bytes up to a zero or a failed read into a CString constructed at `out`;
    // returns `out`.
    mfc::CString* ReadString(void* out);
    // A text (mfc::CString or std::string) read as ReadString reads it.
    template <typename Text>
    void ReadInto(Text* out);

    // Reads a block WriteSized wrote (a two-byte size word, two more bytes when its bit 0 is set; the size is
    // the word / 2) into `to`: `to` cleared (slot 6), sought to 0, then given the block (slot 2).
    void ReadSized(CStream* to);
    // Writes all of `from` with its size in front: size * 2 in two bytes, or
    // size * 2 + 1 in four bytes above 30000 (a signed compare). `from` is sought to 0 and read.
    void WriteSized(CStream* from);
    // The save game's difference coding. A difference is a run
    // header byte (the run length, 0..255) followed by alternating runs: equal bytes (only the
    // length) and changed bytes (the length and the new bytes), starting with an equal run; a
    // full run (255) is followed by an empty run of the other kind; a zero byte ends it.
    // Writes the difference from `base` to `current` (same size, else nothing);
    // both are read from their start.
    void WriteDiff(CStream* base, CStream* current);
    // Rebuilds into this stream (from its start) `base` with the difference read
    // from `diff` (at its position), until base's size is reached or `diff` runs out.
    void ApplyDiff(CStream* base, CStream* diff);
    // Tagged blocks (a four-byte tag, the size including these eight bytes, then the content). Reads the next
    // block's tag into *tag and its content into `out` (emptied, reading_ cleared, written, sought to 0).
    // False when the tag, the size or the content cannot be read (`out` is then already emptied once the size
    // was read).
    bool ReadTagged(std::uint32_t* tag, CStream* out);
    // From the start (reading_ set), reads blocks until one has tag `id`;
    // when none has, `out` is emptied and the result is false.
    bool FindTagged(std::uint32_t id, CStream* out);
    // Called only by Rebirth's EI_Plugin.asi. Copies this stream's tagged
    // blocks, from its position, to `out` (reading_ cleared), the block with tag `id` replaced by all
    // of `replacement` (sought to 0, read); without one, `replacement` is appended under `id`.
    void ReplaceTagged(std::uint32_t id, CStream* replacement, CStream* out);
    // Reads bytes into `buffer` until a zero or `capacity` bytes;
    // buffer[capacity - 1] is always set to zero.
    void ReadText(char* buffer, int capacity);
    // Reads the magic number and the version (four bytes each, into one variable); true and version_ set when
    // the magic matches and oldest <= version <= newest (signed).
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

// A growable memory stream that can also pack bits. The bit writer assembles bits in bits_ and stores the
// byte at data_[bitsIndex_] when a new one starts; Seek, Resize and SetSize flush and reset that state.
class CMemoryStream : public CStream {
public:
    CMemoryStream();
    ~CMemoryStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // a short read zero-fills, clears ok_
    // 2: grows to (capacity + size) * 2. The harness's observer (verify builds) is told first.
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // then EndBits
    std::uint32_t Size() override { return size_; }
    std::uint32_t Position() override { return position_; }
    void Clear() override;
    void CopyFrom(CStream* source) override;                           // one Read into a resized buffer

    // Empties the stream: ok_ = 1, no buffer, no pending bits. Does not free the old
    // buffer (Clear, slot 6, is the same with the free).
    void Reset();
    // Size `size`, position 0; a larger buffer (1.5 x size + 0x10) keeps the contents (the old size's bytes)
    // and gets the pending bits byte (EndBits). Within the capacity the bits state is left alone.
    void Resize(std::uint32_t size);
    // Size `size`, position 0; a larger buffer (exactly `size`) does not keep them and
    // only bitMask_ is reset (bitsIndex_ stays).
    void SetSize(std::uint32_t size);

    // The bit writer / reader of the delta code.
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
    // The whole of this memory stream (a fixed view over its bytes) compressed / decompressed by `coder` into
    // a growing buffer, which then replaces `out`'s contents; `out` is left at position 0. The coder keeps
    // its window from call to call (the client's and the server's message streams).
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

CMemoryStream* GameSaveStream();
// The stream the received packets are decompressed into (with the network queue's vtable; ReceiveMessages fills it):
// g_receivedMessages.
CMemoryStream& ReceivedMessages();

// The harness's view of the memory streams' writes (verify builds; the save trace,
// tests/verify/harness_savetrace.cpp): the stream, the size and the slot's return address
// (`_AddressOfReturnAddress()`), before the bytes are written. A reference run has the harness take
// the original's slots instead.
extern void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame);

// Allocates room for `count` pointers on a 32-byte boundary; the block returned by
// operator new is stored just before the aligned pointer (freed through it by a companion function).
void** __fastcall AlignedPointerArrayAlloc(int count);

// The 0x2828-byte coder state (CoderState) is declared in net_coder.h; its constructor
// is in core_streams.cpp.
