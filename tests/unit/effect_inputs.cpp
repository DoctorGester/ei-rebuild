// The inputs a server sets on a data effect's world object (src/game/effects/effect_inputs.cpp, MF15j2): packed into a
// delta field and read back, first whole and then as a change against the last write, and saved and loaded.
// ei_unit_check (unit_check.cpp) and the POSIX port check run it.
#include "../../src/game/effects/effect_inputs.h"
#include "../../src/engine/core/core_streams.h"
#include "../../src/game/net/net_delta.h"

#include <cstdio>

namespace {

int failures = 0;

void Expect(const char* what, bool got) {
    if (got) return;
    ++failures;
    std::printf("ei_unit_check: effect inputs: %s\n", what);
}

bool SameEntries(const effectinputs::Entries& a, const effectinputs::Entries& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].set != b[i].set || !effectinputs::Same(a[i].value, b[i].value)) return false;
    return true;
}

// One write of `entries` into the server's field and its read into the client's on `channel`; whether the read
// ended where the write did.
bool Send(DeltaField& server, DeltaField& client, const effectinputs::Entries& entries, int channel,
          effectinputs::Entries* got) {
    DeltaWriter writer(server, DeltaWrite::Changes);
    effectinputs::Pack(writer, entries);
    writer.End();
    MemoryStream sent;
    sent.Write(server.out[channel].data.data(), server.out[channel].Size());
    sent.Seek(0);
    DeltaReader reader(client, channel, &sent);
    *got = effectinputs::Unpack(reader);
    const bool consumed = reader.SnapshotConsumed();
    reader.End();
    return consumed;
}

} // namespace

int RunEffectInputsChecks() {
    // Three inputs: the second set (a float), the third an event fired twice; the first not set.
    effectinputs::Entries entries;
    effectinputs::At(entries, 1) = {1, {0.75f, 0.0f, 0.0f}};
    effectinputs::Entry& pulse = effectinputs::At(entries, 2);
    pulse.set = 1;
    pulse.value.x += 2.0f;
    Expect("an entry past the end is made with those before it", effectinputs::At(entries, 4).set == 0 &&
                                                                     entries.size() == 5);
    entries.resize(3);

    DeltaField server, client;
    effectinputs::Entries got;
    Expect("the first write reads whole", Send(server, client, entries, 1, &got) && SameEntries(got, entries));
    // A new client reads the server's first channel against an empty snapshot: the entries come whole all the same.
    DeltaField late;
    Expect("a new client reads them whole", Send(server, late, entries, 0, &got) && SameEntries(got, entries));

    // A vector set into the first and the event fired once more: the change against the last write.
    effectinputs::At(entries, 0) = {1, {1.0f, -2.0f, 3.5f}};
    effectinputs::At(entries, 2).value.x += 1.0f;
    Expect("a change reads against the last write", Send(server, client, entries, 1, &got) &&
                                                        SameEntries(got, entries) && got[2].value.x == 3.0f);
    Expect("nothing changed reads the same", Send(server, client, entries, 1, &got) && SameEntries(got, entries));

    MemoryStream saved;
    effectinputs::Save(&saved, entries);
    saved.Seek(0);
    Expect("a save loads the same", SameEntries(effectinputs::Load(&saved), entries));
    MemoryStream empty;
    effectinputs::Save(&empty, {});
    empty.Seek(0);
    Expect("an effect without inputs saves its count alone", empty.Size() == 4 && effectinputs::Load(&empty).empty());

    std::printf("ei_unit_check: effect inputs: %d failed\n", failures);
    return failures;
}
