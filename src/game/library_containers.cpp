// Library code over game types in S12a's ranges (library_containers.h), registered at the exe's
// addresses so the standalone image has it.
#include "library_containers.h"

#include "link_figure.h"
#include "originals_gameplay.h"
#include "party.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp_list_refs.h"

#include <new>

namespace libcontainers {

BriefingSpeaker* __fastcall CopySpeakers(const BriefingSpeaker* first, const BriefingSpeaker* last,
                                         BriefingSpeaker* result) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++result) {
        result->unit = first->unit;
        result->name = first->name;
        result->spareFlag = first->spareFlag;
    }
    return result;
}

void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value) {
    if (p) p->FromRawCounted(value->ptr);
}

RefPtr* __fastcall CopyBackwardRefPtrs(const RefPtr* first, const RefPtr* last, RefPtr* result) {
    for (std::ptrdiff_t n = last - first; n > 0; --n) {
        --last;
        --result;
        RefPtr old = *result;
        result->FromRawCounted(last->ptr);
        RefPtrRelease(&old);
    }
    return result;
}

CEngineObject** __fastcall ConstructHeldObject(CEngineObject** p, void*, CEngineObject* const* value) {
    *p = *value;
    if (*p) ++(*p)->refCount_;
    return p;
}

void __fastcall DestroyCString(mfc::CString* self) { self->~CString(); }

void* __fastcall DeleteCString(void* self, void*, unsigned flags) {
    DestroyCString(static_cast<mfc::CString*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

void __fastcall DestroyTokenWord(TokenWord* self) { self->text.~CString(); }

void __fastcall ClearLoaderLocal(LoaderLocal* self) { self->cleared = 0; }

void __fastcall DestroyPointerVectors(PointerVectorsPair* self) {
    self->second.FreeStorage();
    self->first.FreeStorage();
}

void __fastcall SetRootVtable(void* self) { ei::SetVtable(self, CLinkMesh::kRootVtableAddress); }


void __fastcall DestroyGameInfoRecord(net::GameInfoRecord* self) {
    self->description.~string();
    self->name.~string();
}

void __fastcall DestroyPrintName(PrintRecord* self) { self->name.~string(); }

void HeroRecordElement::Construct(MpHeroRecord* to, const MpHeroRecord& from) {
    to->Party()->CopyConstruct(const_cast<MpHeroRecord&>(from).Party());   // 006613C0 (reads the source non-const)
    new (&to->file) mfc::CString(from.file);
}

void SpeakerElement::Construct(BriefingSpeaker* to, const BriefingSpeaker& from) {
    to->unit = from.unit;
    new (&to->name) mfc::CString(from.name);
    to->spareFlag = from.spareFlag;
}

void __fastcall ConstructSpeaker(BriefingSpeaker* p, const BriefingSpeaker* value) {
    if (p) SpeakerElement::Construct(p, *value);
}

void __fastcall DestroyTradeRecord(net::TradeRecord* self) {
    self->parties[2].Party()->DestructBody();
    self->parties[1].Party()->DestructBody();
    self->parties[0].Party()->DestructBody();
}

AddressList::Node** __fastcall FindAddressByHost(AddressList::Node** out, void*, AddressList::Node* first,
                                                 AddressList::Node* last, const net::NetAddress* value) {
    while (first != last && first->data.ip != value->ip) first = first->next;
    *out = first;
    return out;
}

} // namespace libcontainers

namespace mfc::impl {
CTime* CTime::MinusSpan(CTime* out, long span) const {
    out->time = static_cast<long>(static_cast<unsigned long>(time) - static_cast<unsigned long>(span));   // wraps, as SUB
    return out;
}
} // namespace mfc::impl

using namespace libcontainers;

// ~list<CMemoryStream> (only unwind funclets call it; the queue's owner inlines its own).
EI_REPLACE(0x00478F10, StreamQueue::Destruct);
// ~list of an 8-byte key and two CStrings (only unwind funclets call it).
EI_REPLACE(0x005F9570, KeyedTextsNodes::Destruct);
// The briefing's shown items: _M_create_node (called by the list's insert 00609EB0).
EI_REPLACE(0x0060A010, ShownItemNodes::CreateNode);

EI_REPLACE(0x0060AB60, CopySpeakers);          // from the speakers' erase (00608450)
EI_REPLACE(0x0060AA80, ConstructRefPtr);       // same instructions as 00429060
EI_REPLACE(0x0060AAA0, CopyBackwardRefPtrs);   // same instructions as 00429DB0
EI_REPLACE(0x004FB680, ConstructHeldObject);   // same instructions as 004298F0; called by 004F77C0
EI_REPLACE(0x00429060, ConstructRefPtr);       // S12g: the client's parties
EI_REPLACE(0x004298F0, ConstructHeldObject);   // S12g

EI_REPLACE(0x005F9680, DestroyCString);   // library.csv: exception::~exception
EI_REPLACE(0x005FBE20, DestroyCString);
EI_REPLACE(0x005FBCB0, DeleteCString);
EI_REPLACE(0x0060ABE0, DeleteCString);

// Unwind helpers (only exception unwind funclets call them; 004F69F0 also 004F6A00).
EI_REPLACE(0x00445FC0, DestroyTokenWord);
EI_REPLACE(0x00482E90, ClearLoaderLocal);
EI_REPLACE(0x005F60B0, DestroyPointerVectors);
EI_REPLACE(0x004F69F0, SetRootVtable);

EI_REPLACE_METHOD(0x005E0550, mfc::impl::CTime::MinusSpan);

EI_REPLACE(0x0041AC30, DestroyGameInfoRecord);   // the server list's records (S12e)
EI_REPLACE(0x006D29A0, DestroyPrintName);        // the print records' arrays (S12e)
EI_REPLACE(0x006D29E0, DestroyPrintName);
EI_REPLACE(0x00619920, HeroRecordAbi::InsertOverflow);   // the heroes and saved parties (S12f)
EI_REPLACE(0x0060AB00, ConstructSpeaker);             // S12g: the briefing's speakers
EI_REPLACE(0x0060A660, SpeakerAbi::InsertOverflow);
EI_REPLACE(0x00424DB0, DestroyTradeRecord);           // S12g: the client's trade records
EI_REPLACE(0x00427FF0, FindAddressByHost);            // S12g: the server's banned addresses
