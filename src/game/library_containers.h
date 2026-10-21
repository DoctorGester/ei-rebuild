#pragma once

// Library code over game types in S12a's ranges (S12c, docs/notes/standalone.md): STLport members of
// lists, vectors and algorithms whose elements are the game's (streams, CStrings, counted pointers,
// the briefing's records), the CString destructor's out-of-line copies and the compiler's unwind
// helpers. Registered in library_containers.cpp; checked in game by verify_library_containers.cpp until S21d.
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

namespace libcontainers {

// ---- Lists ----------------------------------------------------------------------------------------

// list<CMemoryStream> (the loopback link's queue, net_link.h QueueNode): an element is destroyed
// through its vtable (slot 0 with flags 0).
struct QueuedStream {
    static void Destroy(CMemoryStream* p) { p->~CMemoryStream(); }
};
using StreamQueue = stlp::ListAbi<CMemoryStream, QueuedStream>;
static_assert(sizeof(StreamQueue::Node) == 0x30);

// An 8-byte key and two CStrings (the item screens' list whose destructor is 005F9570): destroyed
// as a pair destroys them, the second first.
struct KeyedTexts {
    std::uint32_t key[2];
    mfc::CString first;    // +0x08
    mfc::CString second;   // +0x0C
};
struct KeyedTextsElement {
    static void Destroy(KeyedTexts* p) {
        p->second.~CString();
        p->first.~CString();
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
        new (&to->name) mfc::CString(from.name);
        to->item = from.item;
        to->back = from.back;
        to->frame = from.frame;
    }
};
using ShownItemNodes = stlp::ListAbi<ShownItem, ShownItemElement>;
static_assert(sizeof(ShownItemNodes::Node) == sizeof(BriefingShownItem));

// ---- Algorithms -----------------------------------------------------------------------------------

// copy(first, last, result) of the briefing's speakers (0060AB60; ECX first, EDX last, RET 4): each
// assigned in order (the unit, the name by CString's operator=, the flag byte); the end of the copies.
BriefingSpeaker* __fastcall CopySpeakers(const BriefingSpeaker* first, const BriefingSpeaker* last,
                                         BriefingSpeaker* result);
// _Construct(p, value) of a RefPtr (0060AA80; ECX p, EDX value): a copy with one more reference,
// nothing for a null p.
void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value);
// copy_backward(first, last, result) of RefPtrs (0060AAA0; ECX first, EDX last, RET 4): from the
// back, each assigned (the new object's count raised, then the old one released); the start of the
// copies.
RefPtr* __fastcall CopyBackwardRefPtrs(const RefPtr* first, const RefPtr* last, RefPtr* result);
// _Construct(p, value) of a held engine object (004FB680; thiscall on p, RET 4): the pointer copied,
// its count raised when not null; returns p.
CEngineObject** __fastcall ConstructHeldObject(CEngineObject** p, void*, CEngineObject* const* value);

// ---- CString's destructor and the unwind helpers ---------------------------------------------------

// CString's destructor (005F9680, 005FBE20: a jump to 00700970).
void __fastcall DestroyCString(mfc::CString* self);
// Its scalar deleting destructor (005FBCB0, 0060ABE0; RET 4): returns self.
void* __fastcall DeleteCString(void* self, void*, unsigned flags);

// A record with a CString at +0x0C (the text tokenizer's word): 00445FC0 destroys it.
struct TokenWord {
    std::uint8_t unused_00[0x0C];
    mfc::CString text;   // +0x0C
};
void __fastcall DestroyTokenWord(TokenWord* self);

// A local of the .mob loader whose third dword an unwind funclet clears (00482E90).
struct LoaderLocal {
    std::uint8_t unused_00[8];
    std::uint32_t cleared;   // +0x08
};
void __fastcall ClearLoaderLocal(LoaderLocal* self);

// Two vectors of pointers (+0x00, +0x10), a local two unwind funclets end with: 005F60B0 frees the second's
// storage, then the first's.
struct PointerVectorsPair {
    stlp::vector<void*> first;
    std::uint32_t unused_0C;
    stlp::vector<void*> second;   // +0x10
};
void __fastcall DestroyPointerVectors(PointerVectorsPair* self);

// 004F69F0: the counted root's vtable (0073C368) stored (unwind funclets and the deleting destructor
// 004F6A00).
void __fastcall SetRootVtable(void* self);

// 005E0550 (library.csv: a pointer iterator's operator-(n)): CTime::operator-(CTimeSpan), the time
// less a span of seconds into `out` (thiscall, RET 8; called from 005E0400 after a CTime
// constructor); returns out.

// ---- Vector members destroyed inside an owner (S12e) ------------------------------------------------

// The server list's game record (net_classes.h; 0041AC30): the description's block back to the pool,
// then the name's.
void __fastcall DestroyGameInfoRecord(net::GameInfoRecord* self);
// The print records' name (model.h; 006D29A0 for PrintRecord, 006D29E0 for PrintRecord2, whose name
// is at the same +0x04): its block back to the pool.
void __fastcall DestroyPrintName(PrintRecord* self);

// ---- The party file entries' vector (S12f) ------------------------------------------------------

// vector<MpHeroRecord> (0x74 bytes: a CParty and its file name; the multiplayer heroes 007C82A4 and
// the world's saved parties, world_game.cpp): an element is copied as the party's copy constructor
// (006613C0, by address) and then the name's (CString's copy), destroyed as MpHeroRecord::Destruct
// (the name, then 006611D0) and assigned as MpHeroRecord::Assign.
struct HeroRecordElement {
    static void Construct(MpHeroRecord* to, const MpHeroRecord& from);
    static void Assign(MpHeroRecord& to, const MpHeroRecord& from) { to.Assign(&from); }
    static void Destroy(MpHeroRecord* p) { p->Destruct(); }
};
using HeroRecords = stlp::vector<MpHeroRecord>;
// _M_insert_overflow(pos, x, n) (00619920; RET 0xC): a block of size + max(size, n) records (the
// node allocator's pool for one record), [start, pos), n copies of x, [pos, finish); the old
// records destroyed in order, the old block freed.
// (Registered as HeroRecordAbi::InsertOverflow.)
using HeroRecordAbi = stlp::VectorAbi<MpHeroRecord, HeroRecordElement>;

// ---- S12g: the last library code our rewrites called ----------------------------------------------

// The briefing's speakers vector (CBriefingScreen +0x294): an element copied as 0060AB00 does (the
// unit, CString's copy constructor, the flag byte; the padding is not copied) and destroyed with the
// name's destructor.
struct SpeakerElement {
    static void Construct(BriefingSpeaker* to, const BriefingSpeaker& from);
    static void Destroy(BriefingSpeaker* p) { p->name.~CString(); }
};
using Speakers = stlp::vector<BriefingSpeaker>;
// _Construct(p, value) (0060AB00; ECX p, EDX value, plain RET): nothing for a null p.
void __fastcall ConstructSpeaker(BriefingSpeaker* p, const BriefingSpeaker* value);
// _M_insert_overflow(pos, x, n) (0060A660; RET 0xC).
// (Registered as SpeakerAbi::InsertOverflow.)
using SpeakerAbi = stlp::VectorAbi<BriefingSpeaker, SpeakerElement>;

// The client's trade record's destructor (00424DB0): the three parties' CParty destructor
// (006611D0), the traded party first.
void __fastcall DestroyTradeRecord(net::TradeRecord* self);

// find_if(first, last, pred) over the server's banned addresses (00427FF0; out in ECX, RET 0xC): the
// first node whose address has value's host (the port is not compared), else last.
using AddressList = stlp::list<net::NetAddress>;
AddressList::Node** __fastcall FindAddressByHost(AddressList::Node** out, void*, AddressList::Node* first,
                                                 AddressList::Node* last, const net::NetAddress* value);

} // namespace libcontainers
