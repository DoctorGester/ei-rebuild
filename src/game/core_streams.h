#pragma once

// The engine's byte streams (ByteStream, MemoryStream), the constructors and destructors of the reference-counted
// base classes, and two small allocation helpers. See docs/notes/core.md.

#include "core_refcount.h"
#include "engine_object.h"
#include "net_coder.h"

#include "lib/bytes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// The engine's abstract byte stream. The save game, the network messages and the chunk archives are memory
// streams behind it.
class ByteStream {
public:
    ByteStream();
    ByteStream(const ByteStream&) = delete;
    ByteStream& operator=(const ByteStream&) = delete;
    virtual ~ByteStream() {}
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) = 0;   // returns the bytes read
    virtual void Write(const void* buffer, std::uint32_t size) = 0;
    virtual void Seek(std::uint32_t position) = 0;
    virtual std::uint32_t Size() = 0;
    virtual std::uint32_t Position() = 0;
    virtual void Clear() = 0;                                           // memory streams empty, file streams rewind
    virtual void CopyFrom(ByteStream* source);                             // all of `source`, 1 KB at a time

    // One value as its bytes (sizeof(T), in the host byte order: the saves and the messages are little-endian
    // x86 images of the fields). A read or a write of another size (a four-byte field in a wider variable, a
    // byte in an int) stays a Read / Write with its size written out.
    template <class T>
    void ReadValue(T& value) {
        static_assert(lib::kFormatValue<T>, "a stream value is plain bytes of one size in every build");
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
        static_assert(lib::kFormatValue<T>, "a stream value is plain bytes of one size in every build");
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
    void ReadSized(ByteStream* to);
    // Writes all of `from` with its size in front: size * 2 in two bytes, or
    // size * 2 + 1 in four bytes above 30000 (a signed compare). `from` is sought to 0 and read.
    void WriteSized(ByteStream* from);
    // The save game's difference coding. A difference is a run header byte (the run length, 0..255) followed
    // by alternating runs: equal bytes (only the length) and changed bytes (the length and the new bytes),
    // starting with an equal run; a full run (255) is followed by an empty run of the other kind; a zero byte
    // ends it. Writes the difference from `base` to `current` (same size, else nothing); both are read from
    // their start.
    void WriteDiff(ByteStream* base, ByteStream* current);
    // Rebuilds into this stream (from its start) `base` with the difference read
    // from `diff` (at its position), until base's size is reached or `diff` runs out.
    void ApplyDiff(ByteStream* base, ByteStream* diff);
    // Tagged blocks (a four-byte tag, the size including these eight bytes, then the content). Reads the next
    // block's tag into *tag and its content into `out` (emptied, reading cleared, written, sought to 0).
    // False when the tag, the size or the content cannot be read (`out` is then already emptied once the size
    // was read).
    bool ReadTagged(std::uint32_t* tag, ByteStream* out);
    // From the start (reading set), reads blocks until one has tag `id`;
    // when none has, `out` is emptied and the result is false.
    bool FindTagged(std::uint32_t id, ByteStream* out);
    // Reads bytes into `buffer` until a zero or `capacity` bytes; buffer[capacity - 1] is always set to zero. The
    // text then in UTF-8, cut at a character's end when it no longer fits.
    void ReadText(char* buffer, int capacity);
    // Reads the magic number and the version (four bytes each, into one variable); true and version set when
    // the magic matches and oldest <= version <= newest (signed).
    bool ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion);
    void WriteHeader(std::uint32_t magic, std::uint32_t version);

    // The format version, compared signed.
    int Version() const { return static_cast<int>(version); }

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
class MemoryStream : public ByteStream {
public:
    MemoryStream();
    ~MemoryStream() override;
    // The buffer and the bit state move; `other` is left empty.
    MemoryStream(MemoryStream&& other) noexcept;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // a short read zero-fills, clears ok
    // Grows to (capacity + size) * 2. The harness's observer (verify builds) is told first.
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // then EndBits
    std::uint32_t Size() override { return size; }
    std::uint32_t Position() override { return position; }
    void Clear() override;
    void CopyFrom(ByteStream* source) override;                           // one Read into a resized buffer

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
    void WriteStream(MemoryStream* from);
    // Reads what WriteStream wrote into `to`: resized to the length (SetSize), filled, back at position 0, ok
    // set. 0 for a length over 100000 (nothing more read) or a short read, else 1.
    int ReadStream(MemoryStream* to);
    // The whole of this memory stream (a fixed view over its bytes) compressed / decompressed by `coder` into
    // a growing buffer, which then replaces `out`'s contents; `out` is left at position 0. The coder keeps
    // its window from call to call (the client's and the server's message streams).
    void CompressTo(CoderState* coder, MemoryStream* out);
    void DecompressTo(CoderState* coder, MemoryStream* out);

    std::uint8_t bits = 0;          // byte being assembled by the bit writer
    std::int32_t bitMask;           // bit of the last bit written (0x80: start a new byte)
    std::int32_t bitsIndex;         // where bits goes in the data (-1: nowhere yet)
    std::vector<std::uint8_t> data;     // the buffer: its size is the capacity
    std::uint32_t position;         // read / write position
    std::uint32_t size;             // end of the data
};

// The saved game's stream (world_game.cpp).
extern MemoryStream g_saveStream;
// The stream the received packets are decompressed into (ReceiveMessages fills it; player_server_handlers.cpp).
extern MemoryStream g_receivedMessages;

// The harness's view of the memory streams' writes (verify builds; the save trace): the stream, the size and
// the slot's return address, before the bytes are written. A reference run has the harness take the original's
// slots instead.
extern void (*g_memoryStreamWriteObserver)(MemoryStream* stream, std::uint32_t size, void* const* frame);

