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

namespace libcontainers {

// ---- Lists ----------------------------------------------------------------------------------------

// list<CMemoryStream> (the loopback link's queue): an element is destroyed through its vtable.
struct QueuedStream {
    static void Destroy(CMemoryStream* p) { p->~CMemoryStream(); }
};
using StreamQueue = stlp::ListAbi<CMemoryStream, QueuedStream>;
static_assert(sizeof(StreamQueue::Node) == 0x30);

// An 8-byte key and two CStrings (the item screens' list), destroyed second first as a pair is.
struct KeyedTexts {
    std::uint32_t key[2];
    mfc::CString first;
    mfc::CString second;
};
struct KeyedTextsElement {
    static void Destroy(KeyedTexts* p) {
        std::destroy_at(&p->second);
        std::destroy_at(&p->first);
    }
};
using KeyedTextsNodes = stlp::ListAbi<KeyedTexts, KeyedTextsElement>;
static_assert(sizeof(KeyedTextsNodes::Node) == 0x18);

// A shown item of the briefing: its node made by copy-constructing the name, then the three pointers.
struct ShownItem {
    mfc::CString name;
    CUiMesh* item;
    CEngineObject* back;
    CEngineObject* frame;
};
struct ShownItemElement {
    static void Copy(ShownItem* to, const ShownItem& from) {
        std::construct_at(&to->name, from.name);
        to->item = from.item;
        to->back = from.back;
        to->frame = from.frame;
    }
};
using ShownItemNodes = stlp::ListAbi<ShownItem, ShownItemElement>;
static_assert(sizeof(ShownItemNodes::Node) == sizeof(BriefingShownItem));

// ---- Algorithms -----------------------------------------------------------------------------------

// _Construct(p, value) of a RefPtr: a copy with one more reference, nothing for a null p.
void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value);

// ---- CString's destructor and the unwind helpers ---------------------------------------------------

void __fastcall DestroyCString(mfc::CString* self);

// A record with a CString at +0x0C (the text tokenizer's word): its destructor.
struct TokenWord {
    std::uint8_t unused_00[0x0C];
    mfc::CString text;
};

// A local of the .mob loader whose third dword an unwind funclet clears.
struct LoaderLocal {
    std::uint8_t unused_00[8];
    std::uint32_t cleared;
};

// Two vectors of pointers, a local two unwind funclets end with: frees the second's storage, then the first's.
struct PointerVectorsPair {
    stlp::vector<void*> first;
    std::uint32_t unused_0C;
    stlp::vector<void*> second;
};


// ---- The party file entries' vector ------------------------------------------------------

// vector<MpHeroRecord> (the multiplayer heroes and the world's saved parties): copied as the party's copy
// constructor, then the name's.
struct HeroRecordElement {
    static void Construct(MpHeroRecord* to, const MpHeroRecord& from);
    static void Assign(MpHeroRecord& to, const MpHeroRecord& from) { to.Assign(&from); }
    static void Destroy(MpHeroRecord* p) { p->Destruct(); }
};
// _M_insert_overflow: a block of size + max(size, n) records (the node allocator's pool for one record); the old
// records destroyed in order, the old block freed.
using HeroRecordAbi = stlp::VectorAbi<MpHeroRecord, HeroRecordElement>;

// ---- The briefing's speakers --------------------------------------------------------------------

// The briefing's speakers vector: an element copied by _Construct (the padding is not copied).
struct SpeakerElement {
    static void Construct(BriefingSpeaker* to, const BriefingSpeaker& from);
    static void Destroy(BriefingSpeaker* p) { std::destroy_at(&p->name); }
};
// _Construct(p, value): nothing for a null p.
void __fastcall ConstructSpeaker(BriefingSpeaker* p, const BriefingSpeaker* value);
using SpeakerAbi = stlp::VectorAbi<BriefingSpeaker, SpeakerElement>;

// The server's banned addresses (find_if compares the host, not the port).

} // namespace libcontainers
