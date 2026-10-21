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

void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value) {
    if (p) p->FromRawCounted(value->ptr);
}

CEngineObject** __fastcall ConstructHeldObject(CEngineObject** p, void*, CEngineObject* const* value) {
    *p = *value;
    if (*p) ++(*p)->refCount_;
    return p;
}

void __fastcall DestroyCString(mfc::CString* self) { self->~CString(); }

void __fastcall SetRootVtable(void* self) { ei::SetVtable(self, CLinkMesh::kRootVtableAddress); }


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

} // namespace libcontainers

namespace mfc::impl {
CTime* CTime::MinusSpan(CTime* out, long span) const {
    out->time = static_cast<long>(static_cast<unsigned long>(time) - static_cast<unsigned long>(span));   // wraps, as SUB
    return out;
}
} // namespace mfc::impl

using namespace libcontainers;

