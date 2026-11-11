// The inputs a server sets on a data effect's world object. See effect_inputs.h and docs/notes/effects.md.

#include "game/effects/effect_inputs.h"

#include "engine/core/core_streams.h"
#include "game/net/net_delta.h"

#include <algorithm>
#include <bit>

namespace effectinputs {

namespace {

// More than any effect declares: a count past it is a broken packet or save, read no further.
constexpr int kMostEntries = 256;

} // namespace

Entry& At(Entries& entries, int index) {
    if (std::ssize(entries) <= index) entries.resize(static_cast<std::size_t>(index) + 1);
    return entries[static_cast<std::size_t>(index)];
}

bool Same(const Vector3& a, const Vector3& b) {
    return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
           std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
           std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

void Pack(DeltaWriter& field, const Entries& entries) {
    const std::int32_t count = static_cast<std::int32_t>(entries.size());
    field.WriteBoth(&count, 4);
    for (const Entry& entry : entries) {
        field.WriteBoth(&entry.set, 4);
        field.WriteBoth(&entry.value, 12);
    }
}

Entries Unpack(DeltaReader& field) {
    std::int32_t count = 0;
    field.ReadRaw(&count, 4);
    Entries entries(static_cast<std::size_t>(std::clamp(count, 0, kMostEntries)));
    for (Entry& entry : entries) {
        field.ReadRaw(&entry.set, 4);
        field.ReadRaw(&entry.value, 12);
    }
    return entries;
}

void Save(ByteStream* stream, const Entries& entries) {
    stream->WriteCount(entries.size());
    for (const Entry& entry : entries) {
        stream->WriteValue(entry.set);
        stream->WriteValue(entry.value);
    }
}

Entries Load(ByteStream* stream) {
    Entries entries(static_cast<std::size_t>(std::min(stream->ReadCount(), kMostEntries)));
    for (Entry& entry : entries) {
        stream->ReadValue(entry.set);
        stream->ReadValue(entry.value);
    }
    return entries;
}

} // namespace effectinputs
