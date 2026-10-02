#pragma once

// Delta replication of object state (0047D7F0-0047FE90). Every replicated object (units at
// +0x1C4, ...) owns a DeltaState: up to 16 numbered fields, each a DeltaField holding, for two
// channels, the bytes last sent (the snapshot) and the bits and bytes to send now (the output).
// The object's serialiser (a virtual slot; 0050EAF0 for one family) calls BeginField(i) for each
// dirty field and writes its values with WriteDelta: a bit per 4-byte word says "unchanged", and
// only the changed words follow. Reading (the client) mirrors it through two globals set by
// BeginField in the read modes. Names provisional; see docs/notes/unmapped_410_470.md.

#include <cstddef>
#include <cstdint>

// A CMemoryStream (object.h) as the bit writer / reader of this code. Same layout; object.h names
// +0xC, +0x10 and +0x14 by their use elsewhere (its growBy_ is this mask).
struct BitStream {
    void* vtable_;
    std::uint8_t unknown_4;
    std::uint8_t ok_;               // +0x05
    std::uint8_t unknown_6[2];
    std::uint32_t limit_;           // +0x08
    std::uint8_t bits_;             // +0x0C byte being assembled
    std::uint8_t unknown_D[3];
    std::int32_t mask_;             // +0x10 bit of the last bit written (0x80: start a new byte)
    std::int32_t bitsIndex_;        // +0x14 where bits_ goes in the data (-1: nowhere yet)
    std::uint8_t* data_;            // +0x18
    std::uint32_t capacity_;        // +0x1C
    std::uint32_t position_;        // +0x20
    std::uint32_t size_;            // +0x24

    // Virtual slots (CMemoryStream: 004A5380, 004A53F0, 004A5490, 00417390, 004173A0).
    std::uint32_t Read(void* data, std::uint32_t size);          // slot 1
    void Write(const void* data, std::uint32_t size);            // slot 2
    void Seek(std::uint32_t position);                           // slot 3 (also flushes bits_)
    std::uint32_t Size();                                        // slot 4
    std::uint32_t Position();                                    // slot 5

    // 0047DFC0. Appends one bit (a new byte is started after eight).
    // Several writers have an inline copy of it (same effect).
    void PutBit(bool bit);
    // The readers' inline bit read: true when the next bit is set (reads a byte after eight).
    bool GetBit();

    // Not rewritten (004A5000-004A6000).
    void StartByte();                       // 004A5610: stores bits_, appends a zero byte for the next bits
    void EndBits();                         // 004A5650: stores bits_, no pending byte
    void Resize(std::uint32_t size);        // 004A54D0: size = size, position 0 (grows the buffer)
    void ReadString(void* cstring);         // 00461190: CString up to a zero byte
};
static_assert(offsetof(BitStream, mask_) == 0x10);
static_assert(offsetof(BitStream, data_) == 0x18);
static_assert(sizeof(BitStream) == 0x28);

namespace delta {

// 007A9784 / 007A9780: while writing, whether channel 0 / 1 keeps what it sent as the new snapshot.
inline int& KeepSnapshot0() { return *reinterpret_cast<int*>(0x007A9784); }
inline int& KeepSnapshot1() { return *reinterpret_cast<int*>(0x007A9780); }
// 007A9794 / 007A9798: while reading, the snapshot read against and the received stream.
inline BitStream*& ReadSnapshot() { return *reinterpret_cast<BitStream**>(0x007A9794); }
inline BitStream*& Reader() { return *reinterpret_cast<BitStream**>(0x007A9798); }

// Writers of one channel (__fastcall, the first two arguments in ECX / EDX).
// 0047E740. WriteDelta for a single channel. Returns nothing.
void __fastcall WriteDeltaChannel(BitStream* snapshot, const void* data, std::uint32_t size, int keepSnapshot,
                                  BitStream* out);
// 0047E5E0. Bits and changed words of `data` against `old` (no snapshot stream).
void __fastcall WriteDiff(const void* old, const void* data, std::uint32_t size, BitStream* out);
// 0047E6D0. All of `data`: a 0 bit, then a 0 bit before every 4-byte word.
void __fastcall WriteRaw(const void* data, std::uint32_t size, BitStream* out);

} // namespace delta

// One numbered field of a DeltaState (0xA0 bytes).
struct DeltaField {
    BitStream snapshot[2];   // +0x00, +0x28: per channel, the bytes last sent
    BitStream out[2];        // +0x50, +0x78: per channel, what is sent now

    // 0047DB70 / 0047FA40. Rewinds the snapshots, empties the outputs; both channels keep their
    // snapshots unless `channel1Only`. 0047DB70 returns `channel1Only` or 1, 0047FA40 this.
    int BeginWriteA(int channel1Only);
    DeltaField* BeginWriteB(int channel1Only);
    // 0047DBD0 / 0047FAA0. Rewinds the snapshots and reads `reader` against snapshot 0 or 1.
    // 0047DBD0 returns `second` or the reader, 0047FAA0 this.
    std::uintptr_t BeginReadA(int second, BitStream* reader);
    DeltaField* BeginReadB(int second, BitStream* reader);
    // 0047DC20 / 0047FAF0. Ends the field: when reading, finishes the reader's bits; when writing,
    // finishes both outputs and truncates the kept snapshots at their positions.
    void EndField();
    // 0047DCA0. Writes `size` bytes on both channels against their snapshots.
    void WriteDelta(const void* data, std::uint32_t size);
    // 0047E000. Reads `size` bytes written by WriteDelta (also stores them in snapshot[1]).
    void ReadDelta(void* data, std::uint32_t size);
    // 0047E140. Writes the bytes to both outputs as they are.
    void WriteBoth(const void* data, std::uint32_t size);
    // 0047E2D0. Writes the string with its terminator to both outputs.
    void WriteStringBoth(const char* text);
    // 0047E170. Reads raw bytes from the reader; returns what the stream's Read returns.
    std::uint32_t ReadRaw(void* data, std::uint32_t size);
    // 0047E310. Reads a CString from the reader; returns `cstring`.
    void* ReadString(void* cstring);
};
static_assert(offsetof(DeltaField, out) == 0x50);
static_assert(sizeof(DeltaField) == 0xA0);

// The fields of one object.
struct DeltaState {
    std::uint32_t unknown_0;
    DeltaField* fields_;     // +0x04
    int fieldCount_;         // +0x08
    std::uint32_t unknown_C;
    std::uint32_t unknown_10;
    std::uint16_t sentMask_; // +0x14
    std::uint16_t lastMask_; // +0x16
    std::uint16_t mask_;     // +0x18 fields to write / read
    std::uint16_t unknown_1A;
    BitStream* reader_;      // +0x1C
    int mode_;               // +0x20 0 write all, 1 write delta, 2/3 read (against snapshot 0/1), 4 idle
    BitStream header_[2];    // +0x24, +0x4C

    enum Mode { kWriteFull = 0, kWriteDelta = 1, kRead0 = 2, kRead1 = 3, kIdle = 4 };

    // 0047FB70. Empties every field's four streams and the two headers; mode idle.
    void Reset();
    // 0047FBF0. Prepares field `index` for the current mode; null when it is not in mask_.
    DeltaField* BeginField(int index);
    // 0047FD50. Writing: 0 writes every field (mask_ = 0xFFFF), anything else only the dirty ones.
    void SetWriteMode(int deltaOnly);
    // 0047FD80. Reading: takes the field mask from `reader`; returns `second`.
    int BeginRead(int second, BitStream* reader);
};
static_assert(offsetof(DeltaState, mask_) == 0x18);
static_assert(offsetof(DeltaState, header_) == 0x24);
static_assert(sizeof(DeltaState) == 0x74);
