// Library code over game types (library_containers.h), registered at the exe's
// addresses so the standalone image has it.
#include "library_containers.h"

#include "core_refcount.h"
#include "link_figure.h"
#include "party.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp_list_refs.h"

#include <memory>
#include <new>

namespace libcontainers {

void __fastcall ConstructRefPtr(RefPtr* p, const RefPtr* value) {
    if (p) p->FromRawCounted(value->ptr);
}

} // namespace libcontainers

// (_Construct of a held engine object is HeldObject::Construct, engine_object.h.)

namespace libcontainers {

void __fastcall DestroyCString(mfc::CString* self) { std::destroy_at(self); }



void HeroRecordElement::Construct(MpHeroRecord* to, const MpHeroRecord& from) {
    new (to->Party()) CParty(*const_cast<MpHeroRecord&>(from).Party());   // reads the source non-const
    std::construct_at(&to->file, from.file);
}

void SpeakerElement::Construct(BriefingSpeaker* to, const BriefingSpeaker& from) {
    to->unit = from.unit;
    std::construct_at(&to->name, from.name);
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

EI_REWRITTEN(0x0060AB00, ConstructSpeaker);
EI_REWRITTEN(0x005E0550, mfc::impl::CTime::MinusSpan);
EI_REWRITTEN(0x005F9680, DestroyCString);
EI_REWRITTEN(0x005FBE20, DestroyCString);
EI_REWRITTEN(0x0060AA80, ConstructRefPtr);
EI_REWRITTEN(0x004FB680, HeldEngineObject::Construct);
EI_REWRITTEN(0x00429060, ConstructRefPtr);
EI_REWRITTEN(0x004298F0, HeldEngineObject::Construct);
