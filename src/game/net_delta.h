#pragma once

// Delta replication of object state. Every replicated object owns a DeltaState: up to 16 numbered fields, each
// a DeltaField holding, for two channels, the bytes last sent (the snapshot) and the bits and bytes to send now
// (the output). The object's serialiser calls BeginField(i) for each dirty field and writes its values with
// WriteDelta: a bit per 4-byte word says "unchanged", and only the changed words follow. Reading (the client)
// mirrors it through two globals set by BeginField in the read modes. See docs/notes/network.md.

#include "core_streams.h"
#include "maths.h"
#include "game_data.h"
#include <cstddef>
#include <cstdint>
#include <string>


// The bit writer / reader of this code is the CMemoryStream of core_streams.h (PutBit, GetBit,
// WriteStream, ReadStream, CompressTo and DecompressTo are its members, defined in net_delta.cpp).
using BitStream = CMemoryStream;

namespace delta {

// While writing, whether channel 0 / 1 keeps what it sent as the new snapshot.
inline int g_keepSnapshot1 = /*007A9780*/ {};
inline int g_keepSnapshot0 = /*007A9784*/ {};
// While reading, the snapshot read against and the received stream.
inline BitStream* g_readSnapshot = /*007A9794*/ {nullptr};
inline BitStream* g_received = /*007A9798*/ {nullptr};

// WriteDelta for a single channel.
void WriteDeltaChannel(BitStream* snapshot, const void* data, std::uint32_t size, int keepSnapshot,
                                  BitStream* out);
// Bits and changed words of `data` against `old` (no snapshot stream).
void WriteDiff(const std::uint8_t* old, const std::uint8_t* data, std::uint32_t size, BitStream* out);
// All of `data`: a 0 bit, then a 0 bit before every 4-byte word.
void WriteRaw(const std::uint8_t* data, std::uint32_t size, BitStream* out);

// Reads what WriteDiff wrote: `data` = `old` when nothing changed (returns false),
// otherwise the changed words from `reader` (returns true).
bool ReadDiff(const std::uint8_t* old, void* data, std::uint32_t size, BitStream* reader);

} // namespace delta

namespace delta {
// The 16-bit float format of WriteFloat16 / ReadFloat16 (inline in the original).
std::uint16_t PackFloat16(std::uint32_t floatBits);
std::uint32_t UnpackFloat16(std::uint16_t packed);
} // namespace delta

// One numbered field of a DeltaState.
struct DeltaField {
    BitStream snapshot[2];   // per channel, the bytes last sent
    BitStream out[2];        // per channel, what is sent now

    DeltaField() = default;
    DeltaField(DeltaField&&) noexcept = default;

    DeltaField* BeginWriteB(int channel1Only);
    DeltaField* BeginReadB(int second, BitStream* reader);
    // Ends the field: when reading, finishes the reader's bits; when writing,
    // finishes both outputs and truncates the kept snapshots at their positions.
    void EndField();
    // Writes `size` bytes on both channels against their snapshots.
    void WriteDelta(const void* data, std::uint32_t size);
    // Reads `size` bytes written by WriteDelta (also stores them in snapshot[1]).
    void ReadDelta(void* data, std::uint32_t size);
    // Writes the bytes to both outputs as they are.
    void WriteBoth(const void* data, std::uint32_t size);
    // Writes the string with its terminator to both outputs.
    void WriteStringBoth(const char* text);
    // Reads raw bytes from the reader; returns what the stream's Read returns.
    std::uint32_t ReadRaw(void* data, std::uint32_t size);
    // Reads a string from the reader into `text` (CStream::ReadString); returns it.
    std::string* ReadString(std::string* text);
    // Reads a string (ReadString) into `text`, a buffer of `size` bytes: a
    // string of size - 1 characters or more leaves `text` empty instead.
    void ReadText(char* text, int size);
    // BitStream::WriteStream of `from` into both outputs (inlined twice; the
    // pending bits stored before each).
    void WriteStreamBoth(BitStream* from);
    // BitStream::ReadStream from the reader into `to` (inlined; `this` unused).
    int ReadStream(BitStream* to);
    // Writes a position as three 16-bit values (WriteDelta of 2 bytes each): x and y as
    // (v + 16) * 120.46875, z as (v + 32) * 341.328125, rounded through a float and FISTP.
    void WritePosition(const Vector3* position);
    // Reads what WritePosition wrote: x, y = q * 0.0083009 - 16; z = q * 0.0029297 - 32.
    // A changed value is also stored in snapshot[1].
    void ReadPosition(Vector3* position);
    // The same for a 2D position: (v + 32) * 60.234375; q * 0.0166018 - 32.
    void WritePosition2D(const float* position);
    void ReadPosition2D(float* position);
    // A value in [min, max] as 16 bits: (v - min) * (65535 / (max - min)),
    // rounded through a float and FISTP; read back as q * float((max - min) / 65535) + min.
    void WriteRanged(float value, float min, float max);
    void ReadRanged(float* value, float min, float max);
    // A float packed into 16 bits (sign, 7-bit exponent, 8-bit mantissa; see
    // PackFloat16). The writer takes the float's bits (a stack argument).
    void WriteFloat16(std::uint32_t floatBits);
    void ReadFloat16(float* value);
    // Three of them (each float is first copied through the FPU).
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
    std::vector<DeltaField> fields_;
    std::uint16_t sentMask_ = 0;
    std::uint16_t lastMask_ = 0;
    std::uint16_t mask_ = 0;     // fields to write / read
    std::uint16_t unused_1A = 0;
    BitStream* reader_ = nullptr;
    int mode_ = 4;           // 0 write all, 1 write delta, 2/3 read (against snapshot 0/1), 4 idle
    BitStream header_[2];

    // No fields, idle; the field array's vtable in place. The destructor destroys the fields.
    DeltaState();
    ~DeltaState();
    DeltaState(const DeltaState&) = delete;
    DeltaState& operator=(const DeltaState&) = delete;

    enum Mode { kWriteFull = 0, kWriteDelta = 1, kRead0 = 2, kRead1 = 3, kIdle = 4 };

    // Empties every field's four streams and the two headers; mode idle.
    void Reset();
    // The fields' SetSize(count, -1) (new fields constructed, dropped ones destroyed;
    // recordarrays::DeltaFields).
    void SetFieldCount(int count);
    // Prepares field `index` for the current mode; null when it is not in mask_.
    DeltaField* BeginField(int index);
    // Writing: 0 writes every field (mask_ = 0xFFFF), anything else only the dirty ones.
    void SetWriteMode(int deltaOnly);
    // Reading: takes the field mask from `reader`; returns `second`.
    int BeginRead(int second, BitStream* reader);
    // Ends a write or a read. After a delta write the written fields join
    // sentMask_ and become lastMask_, and each header is rebuilt: its mask (sentMask_ for channel
    // 0, lastMask_ for channel 1), then the channel's output of every field in the mask. After a
    // full write the headers and the masks are emptied. The state goes idle without a reader.
    void Finish();
};
