#pragma once

// Delta replication of object state (0047D7F0-0047FE90). Every replicated object (units at
// +0x1C4, ...) owns a DeltaState: up to 16 numbered fields, each a DeltaField holding, for two
// channels, the bytes last sent (the snapshot) and the bits and bytes to send now (the output).
// The object's serialiser (a virtual slot; 0050EAF0 for one family) calls BeginField(i) for each
// dirty field and writes its values with WriteDelta: a bit per 4-byte word says "unchanged", and
// only the changed words follow. Reading (the client) mirrors it through two globals set by
// BeginField in the read modes. Names provisional; see docs/notes/unmapped_410_470.md.

#include "core_streams.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

struct CoderState;   // net_coder.h

// The bit writer / reader of this code is the CMemoryStream of core_streams.h (PutBit, GetBit,
// WriteStream, ReadStream, CompressTo and DecompressTo are its members, defined in net_delta.cpp).
using BitStream = CMemoryStream;

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

// 0047EAE0. BitStream::GetBit as a function: 0 or 1 in EAX.
int __fastcall GetBit(BitStream* reader);
// 0047EB20. Reads what WriteDiff wrote: `data` = `old` when nothing changed (returns false),
// otherwise the changed words from `reader` (returns true; the result is in AL only).
bool __fastcall ReadDiff(const void* old, void* data, std::uint32_t size, BitStream* reader);

} // namespace delta

namespace delta {
// The 16-bit float format of WriteFloat16 / ReadFloat16 (inline in the original).
std::uint16_t PackFloat16(std::uint32_t floatBits);
std::uint32_t UnpackFloat16(std::uint16_t packed);
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
    // 0047E190 (RET 4). BitStream::WriteStream of `from` into both outputs (inlined twice; the
    // pending bits stored before each).
    void WriteStreamBoth(BitStream* from);
    // 0047E240 (RET 4). BitStream::ReadStream from the reader into `to` (inlined; `this` unused).
    int ReadStream(BitStream* to);
    // 0047E3A0. Writes a position as three 16-bit values (WriteDelta of 2 bytes each): x and y as
    // (v + 16) * 120.46875, z as (v + 32) * 341.328125, rounded through a float and FISTP.
    void WritePosition(const Vector3* position);
    // 0047E930. Reads what WritePosition wrote: x, y = q * 0.0083009 - 16; z = q * 0.0029297 - 32.
    // A changed value is also stored in snapshot[1].
    void ReadPosition(Vector3* position);
    // 0047EBF0 / 0047EE00. The same for a 2D position: (v + 32) * 60.234375; q * 0.0166018 - 32.
    void WritePosition2D(const float* position);
    void ReadPosition2D(float* position);
    // 0047EF80 / 0047F110. A value in [min, max] as 16 bits: (v - min) * (65535 / (max - min)),
    // rounded through a float and FISTP; read back as q * float((max - min) / 65535) + min.
    void WriteRanged(float value, float min, float max);
    void ReadRanged(float* value, float min, float max);
    // 0047F1E0 / 0047F3D0. A float packed into 16 bits (sign, 7-bit exponent, 8-bit mantissa; see
    // PackFloat16). The writer takes the float's bits (a stack argument).
    void WriteFloat16(std::uint32_t floatBits);
    void ReadFloat16(float* value);
    // 0047F4D0 / 0047F7F0. Three of them (each float is first copied through the FPU).
    void WriteFloat16x3(const float* values);
    void ReadFloat16x3(float* values);
    // The 16-bit read of the readers above (inline in the original; ReadDiff of 2 bytes, which
    // some of them call instead, does the same).
    std::uint16_t ReadWord();
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
