#pragma once

// Delta replication of object state. Every replicated object owns a DeltaState: up to 16 numbered fields, each
// a DeltaField holding, for two channels, the bytes last sent (the snapshot) and the bits and bytes to send now
// (the output). The object's serialiser calls BeginField(i) for each dirty field and writes its values with
// WriteDelta: a bit per 4-byte word says "unchanged", and only the changed words follow. Reading (the client)
// mirrors it through two globals set by BeginField in the read modes. See docs/notes/network.md.

#include "core_streams.h"

#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace delta {

// While writing, whether channel 0 / 1 keeps what it sent as the new snapshot.
inline int g_keepSnapshot1 = /*007A9780*/ {};
inline int g_keepSnapshot0 = /*007A9784*/ {};
// While reading, the snapshot read against and the received stream.
inline MemoryStream* g_readSnapshot = /*007A9794*/ {nullptr};
inline MemoryStream* g_received = /*007A9798*/ {nullptr};

// WriteDelta for a single channel.
void WriteDeltaChannel(MemoryStream* snapshot, const void* data, std::uint32_t size, int keepSnapshot,
                                  MemoryStream* out);
// Bits and changed words of `data` against `old` (no snapshot stream).
void WriteDiff(const std::uint8_t* old, const std::uint8_t* data, std::uint32_t size, MemoryStream* out);
// All of `data`: a 0 bit, then a 0 bit before every 4-byte word.
void WriteRaw(const std::uint8_t* data, std::uint32_t size, MemoryStream* out);

// Whether the snapshot read against (g_readSnapshot) is consumed to its end.
bool SnapshotConsumed();

// Reads what WriteDiff wrote: `data` = `old` when nothing changed (returns false),
// otherwise the changed words from `reader` (returns true).
bool ReadDiff(const std::uint8_t* old, void* data, std::uint32_t size, MemoryStream* reader);
} // namespace delta

namespace delta {
// The 16-bit float format of WriteFloat16 / ReadFloat16.
std::uint16_t PackFloat16(std::uint32_t floatBits);
std::uint32_t UnpackFloat16(std::uint16_t packed);
} // namespace delta

// The numbered fields of a map object's DeltaState (MapObject::MarkChanged, DeltaState::BeginField): the
// object's own, then the unit's. The numbers are the wire format. A lever and a torch have fields of their own
// at the same numbers.
enum class NetField : int {
    None = -1,          // MarkChanged: no field, only the object's changed flag
    Vitals = 0,         // the packed hit points and statistics
    Scale = 1,
    Placement = 2,      // the hidden flag, position and rotation
    Effects = 3,
    Stats = 4,          // a unit's parameters
    Equipment = 5,      // a unit's equipment ids
    Modes = 6,          // the movement modes and the active weapon
    Motion = 7,         // obstacle, dead, path time shift and motion mode bits
    Animation = 8,
    ScriptName = 9,
    Path = 10,
    Relations = 11,     // the hostility mask, side and player
    Exposure = 12,
    FightAtWill = 13,
    TorchLight = 4,     // a torch's field
    LeverState = 5,     // a lever's field
};

// One numbered field of a DeltaState.
struct DeltaField {
    MemoryStream snapshot[2];   // per channel, the bytes last sent
    MemoryStream out[2];        // per channel, what is sent now

    DeltaField() = default;
    DeltaField(DeltaField&&) noexcept = default;

    DeltaField* BeginWriteB(int channel1Only);
    DeltaField* BeginReadB(int second, MemoryStream* reader);
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
    void WriteStringBoth(std::string_view text);
    // Reads raw bytes from the reader; returns what the stream's Read returns.
    std::uint32_t ReadRaw(void* data, std::uint32_t size);
    // Reads a string from the reader (ByteStream::ReadString).
    std::string ReadString();
    // Reads a string (ReadString) into `text`, a buffer of `size` bytes: a
    // string of size - 1 characters or more leaves `text` empty instead.
    void ReadText(char* text, int size);
    // MemoryStream::WriteStream of `from` into both outputs (the pending bits stored before each).
    void WriteStreamBoth(MemoryStream* from);
    // MemoryStream::ReadStream from the reader into `to`.
    int ReadStream(MemoryStream* to);
    // Writes a position as three 16-bit values (WriteDelta of 2 bytes each): x and y as
    // (v + 16) * 120.46875, z as (v + 32) * 341.328125, rounded through a float, then to nearest.
    void WritePosition(const Vector3& position);
    // Reads what WritePosition wrote: x, y = q * 0.0083009 - 16; z = q * 0.0029297 - 32.
    // A changed value is also stored in snapshot[1].
    Vector3 ReadPosition();
    // The same for a 2D position: (v + 32) * 60.234375; q * 0.0166018 - 32.
    void WritePosition2D(Vector2 position);
    Vector2 ReadPosition2D();
    // A value in [min, max] as 16 bits: (v - min) * (65535 / (max - min)),
    // rounded through a float, then to nearest; read back as q * float((max - min) / 65535) + min.
    void WriteRanged(float value, float min, float max);
    float ReadRanged(float min, float max);
    // A float packed into 16 bits (sign, 7-bit exponent, 8-bit mantissa; see
    // PackFloat16). The writer takes the float's bits.
    void WriteFloat16(std::uint32_t floatBits);
    float ReadFloat16();
    // Three of them (each float is first copied through the FPU).
    void WriteFloat16x3(const float* values);
    Vector3 ReadFloat16x3();
    // The 16-bit read of the readers above (ReadDiff of 2 bytes, which some of them call instead, does the
    // same).
    std::uint16_t ReadWord();
};

// The fields of one object.
struct DeltaState {
    std::vector<DeltaField> fields;
    std::uint16_t sentMask = 0;
    std::uint16_t lastMask = 0;
    std::uint16_t mask = 0;      // fields to write / read
    MemoryStream* reader = nullptr;
    int mode = 4;            // 0 write all, 1 write delta, 2/3 read (against snapshot 0/1), 4 idle
    MemoryStream header[2];

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
    // Prepares field `index` for the current mode; null when it is not in mask.
    DeltaField* BeginField(NetField index);
    // Writing: 0 writes every field (mask = 0xFFFF), anything else only the dirty ones.
    void SetWriteMode(int deltaOnly);
    // Reading: takes the field mask from `reader`; returns `second`.
    int BeginRead(int second, MemoryStream* reader);
    // Ends a write or a read. After a delta write the written fields join
    // sentMask and become lastMask, and each header is rebuilt: its mask (sentMask for channel
    // 0, lastMask for channel 1), then the channel's output of every field in the mask. After a
    // full write the headers and the masks are emptied. The state goes idle without a reader.
    void Finish();
};
