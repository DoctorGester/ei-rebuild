#pragma once

// Delta replication of object state. Every replicated object owns a DeltaState: up to 16 numbered fields, each
// a DeltaField holding, for two channels, the bytes last sent (the snapshot) and the bits and bytes to send now
// (the output). The object's serialiser takes a DeltaWriter for each dirty field (DeltaState::WriteField) and
// writes its values with WriteDelta: a bit per 4-byte word says "unchanged", and only the changed words follow.
// Reading (the client) mirrors it through a DeltaReader. See docs/notes/network.md.

#include "engine/core/core_streams.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The numbered fields of a map object's DeltaState (MapObject::MarkChanged, DeltaState::WriteField): the
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

// One numbered field of a DeltaState. Channel 0 is written against the last full write, channel 1 against the
// last write of any kind.
struct DeltaField {
    MemoryStream snapshot[2];   // per channel, the bytes last sent
    MemoryStream out[2];        // per channel, what is sent now

    DeltaField() = default;
    DeltaField(DeltaField&&) noexcept = default;

    // The four streams emptied.
    void Empty();
};

// What a write keeps as the snapshots the next write compares with: channel 1's only (Changes; channel 0 stays
// against the last full write), or both (Full).
enum class DeltaWrite : bool { Changes, Full };

// Writes one field: every value on both channels, each against its channel's snapshot.
class DeltaWriter {
public:
    // Rewinds both snapshots and empties both outputs.
    DeltaWriter(DeltaField& target, DeltaWrite kind);

    // Writes `size` bytes on both channels against their snapshots. Without a snapshot to compare with (it is
    // shorter than its position + size) everything is sent and appended to the snapshot. Otherwise a 1 bit
    // means "all unchanged"; else a 0 bit and, per 4-byte word, 1 (unchanged) or 0 and the word, then the
    // remaining bytes as they are. A kept snapshot takes the bytes in place; it advances past them either way.
    void WriteDelta(const void* data, std::uint32_t size);
    // Writes the bytes to both outputs as they are.
    void WriteBoth(const void* data, std::uint32_t size);
    // Writes the string with its terminator to both outputs.
    void WriteStringBoth(std::string_view text);
    // MemoryStream::WriteStream of `from` into both outputs (the pending bits stored before each).
    void WriteStreamBoth(MemoryStream* from);
    // Writes a position as three 16-bit values (WriteDelta of 2 bytes each): x and y as
    // (v + 16) * 120.46875, z as (v + 32) * 341.328125, rounded through a float, then to nearest.
    void WritePosition(const Vector3& position);
    // The same for a 2D position: (v + 32) * 60.234375.
    void WritePosition2D(Vector2 position);
    // A value in [min, max] as 16 bits: (v - min) * (65535 / (max - min)), rounded through a float, then to
    // nearest.
    void WriteRanged(float value, float min, float max);
    // A float packed into 16 bits: the sign, exponent - 0x40 in 7 bits (0 below 0x40, wrapping above 0xBF) and
    // the top 8 mantissa bits rounded (a carry out of them is dropped, not added to the exponent).
    void WriteFloat16(float value);
    void WriteFloat16x3(const Vector3& values);
    // Ends the field: both outputs' pending bits stored, each kept snapshot truncated at its position.
    void End();

private:
    DeltaField* field = nullptr;
    bool keepSnapshot0 = false;  // channel 1 always keeps what it sends
};

// Reads one field from a received stream against the snapshots of the channel it was written for.
class DeltaReader {
public:
    // Rewinds both snapshots.
    DeltaReader(DeltaField& target, int channel, MemoryStream* from);

    // Reads `size` bytes written by WriteDelta: from the snapshot when unchanged, else the changed words from
    // the received stream, and then the whole value is also stored in snapshot[1] at the same place (even
    // when reading against snapshot 0).
    void ReadDelta(void* data, std::uint32_t size);
    // Reads raw bytes from the received stream.
    void ReadRaw(void* data, std::uint32_t size);
    // Reads a string from the received stream (ByteStream::ReadString).
    std::string ReadString();
    // Reads a string (ReadString) into `text`, a buffer of `size` bytes: a string of size - 1 characters or
    // more leaves `text` empty instead.
    void ReadText(char* text, int size);
    // MemoryStream::ReadStream from the received stream into `to`.
    int ReadStream(MemoryStream* to);
    // Reads what WritePosition wrote: x, y = q * 0.0083009 - 16; z = q * 0.0029297 - 32.
    Vector3 ReadPosition();
    // Reads what WritePosition2D wrote: q * 0.0166018 - 32.
    Vector2 ReadPosition2D();
    // Reads what WriteRanged wrote: q * float((max - min) / 65535) + min.
    float ReadRanged(float min, float max);
    float ReadFloat16();
    Vector3 ReadFloat16x3();
    // Whether the snapshot read against is consumed to its end.
    bool SnapshotConsumed() const;
    // Ends the field: the received stream's pending bits dropped.
    void End();

private:
    // One quantised 16-bit value (ReadDelta of 2 bytes).
    std::uint16_t ReadQuantised();

    DeltaField* field = nullptr;
    MemoryStream* snapshot = nullptr;  // the one read against
    MemoryStream* received = nullptr;
};

// The fields of one object.
struct DeltaState {
    enum class Mode { WriteFull, WriteChanges, Read0, Read1, Idle };   // Read0 / Read1: against snapshot 0 / 1

    std::vector<DeltaField> fields;
    std::uint16_t sentMask = 0;
    std::uint16_t lastMask = 0;
    std::uint16_t mask = 0;      // fields to write / read
    MemoryStream* reader = nullptr;
    Mode mode = Mode::Idle;
    MemoryStream header[2];

    // No fields, idle.
    DeltaState() = default;
    DeltaState(const DeltaState&) = delete;
    DeltaState& operator=(const DeltaState&) = delete;

    // Empties every field's four streams and the two headers; mode idle.
    void Reset();
    // New fields constructed, dropped ones destroyed.
    void SetFieldCount(int count);
    // Writing: a full write writes every field (mask = 0xFFFF), a write of the changes only the dirty ones.
    void SetWriteMode(DeltaWrite kind);
    // Reading: takes the field mask from `reader`, read against `channel`'s snapshots.
    void BeginRead(int channel, MemoryStream* reader);
    // The writer of field `index` for the current write; none when it is not in mask.
    std::optional<DeltaWriter> WriteField(NetField index);
    // The reader of field `index` for the current read; none when it is not in mask.
    std::optional<DeltaReader> ReadField(NetField index);
    // Ends a write or a read. After a write of the changes the written fields join
    // sentMask and become lastMask, and each header is rebuilt: its mask (sentMask for channel
    // 0, lastMask for channel 1), then the channel's output of every field in the mask. After a
    // full write the headers and the masks are emptied. The state goes idle without a reader.
    void Finish();
};
