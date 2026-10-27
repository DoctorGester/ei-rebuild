#pragma once

// Library code over game types (docs/notes/standalone.md): STLport members of
// lists, vectors and algorithms whose elements are the game's (streams, CStrings, counted pointers,
// the briefing's records), the CString destructor's out-of-line copies and the compiler's unwind
// helpers. Registered in library_containers.cpp.
// Thiscalls are __fastcall with an unused EDX.

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

// list<CMemoryStream> (the loopback link's queue, net_link.h QueueNode): an element is destroyed
// through its vtable (slot 0 with flags 0).
struct QueuedStream {
    static void Destroy(CMemoryStream* p) { p->~CMemoryStream(); }
};
using StreamQueue = stlp::ListAbi<CMemoryStream, QueuedStream>;
static_assert(sizeof(StreamQueue::Node) == 0x30);

// An 8-byte key and two CStrings (the item screens' list): destroyed
// as a pair destroys them, the second first.
struct KeyedTexts {
    std::uint32_t key[2];
    mfc::CString first;    // +0x08
    mfc::CString second;   // +0x0C
};
struct KeyedTextsElement {
    static void Destroy(KeyedTexts* p) {
        std::destroy_at(&p->second);
        std::destroy_at(&p->first);
    }
};
using KeyedTextsNodes = stlp::ListAbi<KeyedTexts, KeyedTextsElement>;
static_assert(sizeof(KeyedTextsNodes::Node) == 0x18);

// A shown item of the briefing (the value of ui_briefing.h's BriefingShownItem node): its node made
// by copy-constructing the name, then the three pointers.
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

// _Construct(p, value) of a RefPtr (ECX p, EDX value): a copy with one more reference,
// nothing for a null p.
void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value);
// (_Construct(p, value) of a held engine object is HeldEngineObject::Construct.)

// ---- CString's destructor and the unwind helpers ---------------------------------------------------

// CString's destructor (two copies, each a jump to the library's).
void __fastcall DestroyCString(mfc::CString* self);

// A record with a CString at +0x0C (the text tokenizer's word): its destructor.
struct TokenWord {
    std::uint8_t unused_00[0x0C];
    mfc::CString text;   // +0x0C
};

// A local of the .mob loader whose third dword an unwind funclet clears.
struct LoaderLocal {
    std::uint8_t unused_00[8];
    std::uint32_t cleared;   // +0x08
};

// Two vectors of pointers (+0x00, +0x10), a local two unwind funclets end with: frees the second's
// storage, then the first's.
struct PointerVectorsPair {
    stlp::vector<void*> first;
    std::uint32_t unused_0C;
    stlp::vector<void*> second;   // +0x10
};


// CTime::operator-(CTimeSpan) (library.csv: a pointer iterator's operator-(n)): the time
// less a span of seconds into `out` (thiscall; called after a CTime
// constructor); returns out.

// ---- The party file entries' vector ------------------------------------------------------

// vector<MpHeroRecord> (0x74 bytes: a CParty and its file name; the multiplayer heroes and
// the world's saved parties, world_game.cpp): an element is copied as the party's copy constructor
// (by address) and then the name's (CString's copy), destroyed as MpHeroRecord::Destruct
// (the name, then the party) and assigned as MpHeroRecord::Assign.
struct HeroRecordElement {
    static void Construct(MpHeroRecord* to, const MpHeroRecord& from);
    static void Assign(MpHeroRecord& to, const MpHeroRecord& from) { to.Assign(&from); }
    static void Destroy(MpHeroRecord* p) { p->Destruct(); }
};
// _M_insert_overflow(pos, x, n): a block of size + max(size, n) records (the
// node allocator's pool for one record), [start, pos), n copies of x, [pos, finish); the old
// records destroyed in order, the old block freed.
// (Registered as HeroRecordAbi::InsertOverflow.)
using HeroRecordAbi = stlp::VectorAbi<MpHeroRecord, HeroRecordElement>;

// ---- The last library code our rewrites called ------------------------------------------------

// The briefing's speakers vector (CBriefingScreen +0x294): an element copied by _Construct (the
// unit, CString's copy constructor, the flag byte; the padding is not copied) and destroyed with the
// name's destructor.
struct SpeakerElement {
    static void Construct(BriefingSpeaker* to, const BriefingSpeaker& from);
    static void Destroy(BriefingSpeaker* p) { std::destroy_at(&p->name); }
};
using Speakers = stlp::vector<BriefingSpeaker>;
// _Construct(p, value) (ECX p, EDX value, plain return): nothing for a null p.
void __fastcall ConstructSpeaker(BriefingSpeaker* p, const BriefingSpeaker* value);
// _M_insert_overflow(pos, x, n).
// (Registered as SpeakerAbi::InsertOverflow.)
using SpeakerAbi = stlp::VectorAbi<BriefingSpeaker, SpeakerElement>;

// find_if(first, last, pred) over the server's banned addresses (out in ECX): the
// first node whose address has value's host (the port is not compared), else last.
using AddressList = stlp::list<net::NetAddress>;

} // namespace libcontainers
