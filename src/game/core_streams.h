#pragma once

// The engine's byte streams (CStream, CMemoryStream), the constructors and destructors of the reference-counted
// base classes, and two small allocation helpers. See docs/notes/core.md.

#include "core_refcount.h"
#include "engine_object.h"
#include "net_coder.h"

#include "lib/containers.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// The engine's abstract byte stream. The save game, the network messages and the chunk archives are memory
// streams behind it; the message recordings are file streams.
class CStream {
public:
    CStream();
    CStream(const CStream&) = delete;
    CStream& operator=(const CStream&) = delete;
    virtual ~CStream() {}
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) = 0;   // returns the bytes read
    virtual void Write(const void* buffer, std::uint32_t size) = 0;
    virtual void Seek(std::uint32_t position) = 0;
    virtual std::uint32_t Size() = 0;
    virtual std::uint32_t Position() = 0;
    virtual void Clear() = 0;                                           // memory streams empty, file streams rewind
    virtual void CopyFrom(CStream* source);                             // all of `source`, 1 KB at a time

    // One value as its bytes (sizeof(T), in the host byte order: the saves and the messages are little-endian
    // x86 images of the fields). A read or a write of another size (a four-byte field in a wider variable, a
    // byte in an int) stays a Read / Write with its size written out.
    template <class T>
    void ReadValue(T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "a stream value is plain bytes");
        Read(&value, sizeof(T));
    }
    // A flag saved as one byte (0 / 1); any other byte reads as set. A short read keeps the flag.
    void ReadValue(bool& value) {
        std::uint8_t byte = value;
        Read(&byte, 1);
        value = byte != 0;
    }
    template <class T>
    void WriteValue(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "a stream value is plain bytes");
        Write(&value, sizeof(T));
    }


    // Text in the streams (the saves, the network's messages and recordings) is in the asset code page, as the
    // original's: converted to UTF-8 where it is read and back where it is written (asset_text.h).
    // Bytes up to a zero or a failed read (in UTF-8).
    std::string ReadString();
    // `text` up to its first zero, in the asset code page (a character it has no byte for as '?'), and a zero.
    void WriteString(std::string_view text);

    // Reads a block WriteSized wrote (a two-byte size word, two more bytes when its bit 0 is set; the size is
    // the word / 2) into `to`: `to` cleared, sought to 0, then given the block.
    void ReadSized(CStream* to);
    // Writes all of `from` with its size in front: size * 2 in two bytes, or
    // size * 2 + 1 in four bytes above 30000 (a signed compare). `from` is sought to 0 and read.
    void WriteSized(CStream* from);
    // The save game's difference coding. A difference is a run header byte (the run length, 0..255) followed
    // by alternating runs: equal bytes (only the length) and changed bytes (the length and the new bytes),
    // starting with an equal run; a full run (255) is followed by an empty run of the other kind; a zero byte
    // ends it. Writes the difference from `base` to `current` (same size, else nothing); both are read from
    // their start.
    void WriteDiff(CStream* base, CStream* current);
    // Rebuilds into this stream (from its start) `base` with the difference read
    // from `diff` (at its position), until base's size is reached or `diff` runs out.
    void ApplyDiff(CStream* base, CStream* diff);
    // Tagged blocks (a four-byte tag, the size including these eight bytes, then the content). Reads the next
    // block's tag into *tag and its content into `out` (emptied, reading cleared, written, sought to 0).
    // False when the tag, the size or the content cannot be read (`out` is then already emptied once the size
    // was read).
    bool ReadTagged(std::uint32_t* tag, CStream* out);
    // From the start (reading set), reads blocks until one has tag `id`;
    // when none has, `out` is emptied and the result is false.
    bool FindTagged(std::uint32_t id, CStream* out);
    // Reads bytes into `buffer` until a zero or `capacity` bytes; buffer[capacity - 1] is always set to zero. The
    // text then in UTF-8, cut at a character's end when it no longer fits.
    void ReadText(char* buffer, int capacity);
    // Reads the magic number and the version (four bytes each, into one variable); true and version set when
    // the magic matches and oldest <= version <= newest (signed).
    bool ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion);
    void WriteHeader(std::uint32_t magic, std::uint32_t version);

    void Rewind() {
        ok = 1;
        Seek(0);
        reading = 1;
    }

    std::uint8_t reading = 0;       // 1 reading, 0 writing (CopyFrom clears it; never read)
    std::uint8_t ok;                // cleared when a read runs past the end
    std::uint32_t version;          // format version (the save game's); 2000000000 when constructed
};

// A growable memory stream that can also pack bits. The bit writer assembles bits in bits and stores the
// byte at data[bitsIndex] when a new one starts; Seek, Resize and SetSize flush and reset that state.
class CMemoryStream : public CStream {
public:
    CMemoryStream();
    ~CMemoryStream() override;
    // The buffer and the bit state move; `other` is left empty.
    CMemoryStream(CMemoryStream&& other) noexcept;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // a short read zero-fills, clears ok
    // Grows to (capacity + size) * 2. The harness's observer (verify builds) is told first.
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // then EndBits
    std::uint32_t Size() override { return size; }
    std::uint32_t Position() override { return position; }
    void Clear() override;
    void CopyFrom(CStream* source) override;                           // one Read into a resized buffer

    // Size `size`, position 0; a larger buffer (1.5 x size + 0x10) keeps the contents (the old size's bytes)
    // and gets the pending bits byte (EndBits). Within the capacity the bits state is left alone.
    void Resize(std::uint32_t size);
    // Size `size`, position 0; a larger buffer (exactly `size`) does not keep them and
    // only bitMask is reset (bitsIndex stays).
    void SetSize(std::uint32_t size);

    // The bit writer / reader of the delta code. Appends one bit (a new byte is started after eight).
    void PutBit(bool bit);
    // The readers' inline bit read: true when the next bit is set (reads a byte after eight).
    bool GetBit();
    // Stores bits, appends a zero byte for the next bits, bitMask = 1.
    void StartByte();
    // Stores bits, no pending byte, bitMask = 0x80.
    void EndBits();
    // `from`'s pending bits stored first (EndBits), then its size times two, in one byte when that is below
    // 0x100 (a signed compare), otherwise in four bytes with bit 0 set; then its bytes.
    void WriteStream(CMemoryStream* from);
    // Reads what WriteStream wrote into `to`: resized to the length (SetSize), filled, back at position 0, ok
    // set. 0 for a length over 100000 (nothing more read) or a short read, else 1.
    int ReadStream(CMemoryStream* to);
    // The whole of this memory stream (a fixed view over its bytes) compressed / decompressed by `coder` into
    // a growing buffer, which then replaces `out`'s contents; `out` is left at position 0. The coder keeps
    // its window from call to call (the client's and the server's message streams).
    void CompressTo(CoderState* coder, CMemoryStream* out);
    void DecompressTo(CoderState* coder, CMemoryStream* out);

    std::uint8_t bits = 0;          // byte being assembled by the bit writer
    std::int32_t bitMask;           // bit of the last bit written (0x80: start a new byte)
    std::int32_t bitsIndex;         // where bits goes in the data (-1: nowhere yet)
    std::vector<std::uint8_t> data;     // the buffer: its size is the capacity
    std::uint32_t position;         // read / write position
    std::uint32_t size;             // end of the data
};

// The saved game's stream (world_game.cpp).
extern CMemoryStream g_saveStream;
// The stream the received packets are decompressed into (ReceiveMessages fills it; player_server_handlers.cpp).
extern CMemoryStream g_receivedMessages;

// The harness's view of the memory streams' writes (verify builds; the save trace): the stream, the size and
// the slot's return address, before the bytes are written. A reference run has the harness take the original's
// slots instead.
extern void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame);

