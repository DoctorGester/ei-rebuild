#pragma once

// The inputs a server sets on a data effect's world object, which every client gives its own source
// (docs/notes/effects.md, "Inputs the server sets"): one entry per input of the effect's record, in its order, sent
// after the object's fields and saved after them only for a data effect's id, so an original type's packets and
// saves stay as they are.

#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

class ByteStream;
class DeltaReader;
class DeltaWriter;

namespace effectinputs {

struct Entry {
    std::int32_t set = 0;   // 1 once the server set it (an event: fired)
    Vector3 value;          // a float's, bool's or enum index's in x, a vector's; an event's x: the times it fired
};
// Entry i is the record's input i (those after the last set one left out).
using Entries = std::vector<Entry>;

// The entry of input `index`, made (with those before it) when there is none.
Entry& At(Entries& entries, int index);
// The same value, bit for bit.
bool Same(const Vector3& a, const Vector3& b);

// The count, then each entry's flag and value, as they are (WriteBoth, not against the snapshots): a new client's
// object compares a delta with the snapshot its own creation wrote, which has no entries, so a list that grew since
// the server's first write cannot be read as unchanged words.
void Pack(DeltaWriter& field, const Entries& entries);
Entries Unpack(DeltaReader& field);
void Save(ByteStream* stream, const Entries& entries);
Entries Load(ByteStream* stream);

} // namespace effectinputs
