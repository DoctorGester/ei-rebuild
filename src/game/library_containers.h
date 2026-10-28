#pragma once

// Library code over game types (docs/notes/standalone.md): STLport members whose elements are the game's, the
// CString destructor's out-of-line copies and the compiler's unwind helpers.

#include "core_refcount.h"
#include "core_streams.h"
#include "engine_object.h"
#include "model.h"
#include "net_classes.h"
#include "ui_briefing.h"
#include "ui_screens.h"

#include "lib/mfc.h"
#include "lib/mfc_time.h"
#include "lib/stlp.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace libcontainers {

// ---- Algorithms -----------------------------------------------------------------------------------

// _Construct(p, value) of a RefPtr: a copy with one more reference, nothing for a null p.
void ConstructRefPtr(RefPtr* p, const RefPtr* value);

// ---- CString's destructor and the unwind helpers ---------------------------------------------------

// A local of the .mob loader whose third dword an unwind funclet clears.
struct LoaderLocal {
    std::uint8_t unused_00[8];
    std::uint32_t cleared;
};

// Two vectors of pointers, a local two unwind funclets end with: frees the second's storage, then the first's.
struct PointerVectorsPair {
    std::vector<void*> first;
    std::uint32_t unused_0C;
    std::vector<void*> second;
};


// ---- The party file entries' vector ------------------------------------------------------

// vector<MpHeroRecord> (the multiplayer heroes and the world's saved parties): copied as the party's copy
// constructor, then the name's. Its InsertOverflow<HeroRecordElement> allocates a block of size + max(size, n)
// records and destroys the old records in order.
struct HeroRecordElement {
    static void Construct(MpHeroRecord* to, const MpHeroRecord& from);
    static void Assign(MpHeroRecord& to, const MpHeroRecord& from) { to.Assign(&from); }
    static void Destroy(MpHeroRecord* p) { p->Destruct(); }
};

// ---- The briefing's speakers --------------------------------------------------------------------

// The briefing's speakers vector: an element copied by _Construct (the padding is not copied).
struct SpeakerElement {
    static void Construct(BriefingSpeaker* to, const BriefingSpeaker& from);
    static void Destroy(BriefingSpeaker* p) { std::destroy_at(&p->name); }
};
// _Construct(p, value): nothing for a null p.
void ConstructSpeaker(BriefingSpeaker* p, const BriefingSpeaker* value);

// The server's banned addresses (find_if compares the host, not the port).

} // namespace libcontainers
