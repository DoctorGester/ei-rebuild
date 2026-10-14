// The client's trade offer (0041DA50, called by the party screens after a change): one member
// message per party member, kept in our PartyMessage at +0x260C. See net_messages.h and
// docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_gameplay.h"

#include <cstring>

namespace net {

namespace {

// STLport vector<RefPtr> plumbing (library instantiations) and the message stream constructor.
void* __fastcall VectorGetAllocator(RefVector* a0, void*, void* a2) { return VectorGetAllocatorOriginal((const void*)a0, nullptr, a2); }
__declspec(naked) RefPtr* __fastcall VectorErase(RefVector* /*vector*/, void*, RefPtr* /*first*/, RefPtr* /*last*/) EI_ORIGINAL(0x00421ED0)
__declspec(naked) void __fastcall RefPtrConstructCopy(RefPtr* /*at*/, const RefPtr* /*from*/) EI_ORIGINAL(0x00429060)
__declspec(naked) void __fastcall VectorInsertOverflow(RefVector* /*vector*/, void*, RefPtr* /*pos*/, const RefPtr* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00426F00)
__declspec(naked) RefPtr* __fastcall RefPtrCopyRange(RefPtr* /*first*/, RefPtr* /*last*/, RefPtr* /*result*/) EI_ORIGINAL(0x00429080)
__declspec(naked) void __fastcall VectorDeallocate(RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426490)
__declspec(naked) void __fastcall RefPtrRaw(RefPtr* /*p*/, void*, CRefObject* /*object*/) EI_ORIGINAL(0x0041DD50)
__declspec(naked) void* __fastcall MemoryStreamConstructAt(CMemoryStream* /*stream*/) EI_ORIGINAL(0x0041DD70)

// vector<CEngineObject*> with counted elements (library), the +0x2614 list erase, the party's
// reset and add (00661050 class, outside this range).
__declspec(naked) void __fastcall ObjectVectorConstruct(void* /*vector*/, void*, const void* /*allocator*/) EI_ORIGINAL(0x00424B40)
__declspec(naked) void __fastcall ObjectPtrConstructCopy(void* /*at*/, void*, void* const* /*from*/) EI_ORIGINAL(0x004298F0)
__declspec(naked) void __fastcall ObjectVectorInsertOverflow(void* /*vector*/, void*, void** /*pos*/, void* const* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00428A20)
__declspec(naked) void __fastcall ObjectVectorDeallocate(void** /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x004268A0)
__declspec(naked) void** __fastcall List2614Erase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00429770)
inline void __fastcall PartyReset(void* a0, void*, int a2, int a3) { PartyResetOriginal((void*)a0, nullptr, a2, a3); }
inline int __fastcall PartyAdd(void* a0, void*, void* a2, int a3, int a4) { return (int)PartyAddItemOriginal((void*)a0, nullptr, a2, a3, a4); }

std::uint32_t Get32(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
void* GetPtr(const void* p, std::size_t offset) {
    void* v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}

void WriteDword(CStream* stream, std::uint32_t* scratch, std::uint32_t value) {
    *scratch = value;
    stream->Write(scratch, 4u);
}
CStream* StreamOf(CRefObject* member) { return &static_cast<PartyMemberMessage*>(member)->stream_; }

void PushBack(RefVector* v, const RefPtr* value) {
    if (v->finish != v->eos) {
        RefPtrConstructCopy(v->finish, value);
        ++v->finish;
    } else {
        VectorInsertOverflow(v, nullptr, v->finish, value, 1);
    }
}

} // namespace

// 00429FC0: the first element in [first, last) whose member stream holds the same bytes as
// `message`'s (sizes through slot 4, then a byte compare). The last argument is not used.
RefPtr* __fastcall FindSameMessage(RefPtr* first, RefPtr* last, PartyMemberMessage* message, std::uint32_t) {
    CStream* stream = &message->stream_;
    for (; first != last; ++first) {
        CStream* element = StreamOf(first->ptr);
        std::uint32_t size = element->Size();
        if (stream->Size() != size) continue;
        const void* a = static_cast<CMemoryStream*>(element)->data_;
        const void* b = message->stream_.data_;
        if (std::memcmp(a, b, stream->Size()) == 0) break;
    }
    return first;
}
EI_REPLACE(0x00429FC0, FindSameMessage);

// 0041DA50: rebuilds the party messages of the replicated object at +0x260C from the party at
// +0x2620 (members +0x40, count +0x44). Each member gives a member message with its
// attachment at +0x44 (fields +0xC, +0x10, +0x18 and its slot 10 serialisation, or -1 without one),
// its fields +0x10 and +0x18 and its own slot 10 serialisation. A message equal to one of the
// previous messages is replaced by that one (so unchanged members keep their object). Then the
// object's counters move on and its dirty flag is cleared.
void __fastcall ClientPublishParty(ClientSession* self) {
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    // Read again at each use, as the original does.
    auto state = [self] { return static_cast<PartyMessage*>(self->party_.ptr); };
    if (!state()) return;
    RefVector* messages = &state()->members_;

    // A copy of the old messages, then the list emptied.
    std::uint8_t allocator = 0;
    const void* allocatorCopy = VectorGetAllocator(messages, nullptr, &allocator);
    RefVector previous;
    VectorBaseConstruct(&previous, nullptr, static_cast<std::size_t>(messages->finish - messages->start), allocatorCopy);
    previous.finish = UninitializedCopy(messages->start, messages->finish, previous.start, 0);
    VectorErase(&state()->members_, nullptr, state()->members_.start, state()->members_.finish);
    state()->partyValue_ = Get32(base, 0x2670);

    std::uint32_t scratch = 0;
    for (int i = 0; i < static_cast<int>(Get32(base, 0x2664)); ++i) {
        auto* message = NewMessageMemory<PartyMemberMessage>();
        if (message) {
            MessageConstructBase(message);
            MemoryStreamConstructAt(&message->stream_);
            ei::SetVtable(message, PartyMemberMessage::kVtableAddress);
        }
        RefPtr ref;
        RefPtrRaw(&ref, nullptr, message);
        CStream* stream = StreamOf(ref.ptr);
        auto* member = static_cast<std::uint8_t*>(static_cast<void**>(GetPtr(base, 0x2660))[i]);
        stream->Clear();
        auto* attachment = static_cast<std::uint8_t*>(GetPtr(member, 0x44));
        if (attachment) {
            WriteDword(stream, &scratch, Get32(attachment, 0xC));
            WriteDword(stream, &scratch, Get32(attachment, 0x10));
            WriteDword(stream, &scratch, Get32(attachment, 0x18));
            ei::VCall<void>(attachment, 10, stream);
        } else {
            WriteDword(stream, &scratch, 0xFFFFFFFFu);
        }
        WriteDword(stream, &scratch, Get32(member, 0x10));
        WriteDword(stream, &scratch, Get32(member, 0x18));
        ei::VCall<void>(member, 10, stream);

        RefPtr* same = FindSameMessage(previous.start, previous.finish, static_cast<PartyMemberMessage*>(ref.ptr), scratch);
        PartyMessage* current = state();
        if (same != previous.finish) {
            PushBack(&current->members_, same);
            if (same + 1 != previous.finish) RefPtrCopyRange(same + 1, previous.finish, same);
            --previous.finish;
            RefPtrDestroyAt(previous.finish, nullptr, 0);
        } else {
            PushBack(&current->members_, &ref);
        }
        if (ref.ptr) ref.ptr->Release();
    }

    ++state()->revision_;
    state()->accepted_ = 0;
    ++state()->version_;
    for (RefPtr* p = previous.start; p != previous.finish; ++p)
        if (p->ptr) p->ptr->Release();
    VectorDeallocate(previous.start, static_cast<std::size_t>(previous.eos - previous.start));
}
EI_REPLACE(0x0041DA50, ClientPublishParty);

// 0041E220: the received offer (list +0x2610) that pairs with ours (+0x260C): its partner is our
// player and its player our partner, and it is live. Null without one.
void* __fastcall ClientFindParty(ClientSession* self) {
    auto* ours = static_cast<PartyMessage*>(self->party_.ptr);
    if (!ours) return nullptr;
    void** sentinel = static_cast<void**>(self->offers_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = static_cast<PartyMessage*>(node[2]);
        if (other->partner_.ptr != ours->player_.ptr || other->player_.ptr != ours->partner_.ptr) continue;
        if (other && !(other->flags7_ & CRefObject::kDetached)) return other;
    }
    return nullptr;
}
EI_REPLACE(0x0041E220, ClientFindParty);

// 0041E180: an accepted offer is unaccepted with its version moved on, so it is sent again.
void __fastcall ClientCommitParty(ClientSession* self) {
    auto* state = static_cast<PartyMessage*>(self->party_.ptr);
    if (!state || !state->accepted_) return;
    state->accepted_ = 0;
    ++static_cast<PartyMessage*>(self->party_.ptr)->version_;
}
EI_REPLACE(0x0041E180, ClientCommitParty);

// 0041DDC0: rebuilds the second party (+0x2690) from the paired offer, once every member message
// of it holds data. Each message gives (when its first dword is positive) an object made through the
// party's slot 22 factory (slot 23 with the stored prototype and that id; +0x18 and its slot 9
// load) and a member made through the slot 18 factory (slot 23 with id -1; +0x18 and slot 9 load).
// The members are added to the party (00661DB0 with 1, 1); entries of +0x2614 older than the
// offer's revision are dropped.
void __fastcall ClientApplyParty(ClientSession* self) {
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    auto* remote = static_cast<PartyMessage*>(ClientFindParty(self));
    if (!remote) return;
    auto count = [remote] { return static_cast<std::uint32_t>(remote->members_.finish - remote->members_.start); };
    for (std::uint32_t i = 0; i < count(); ++i) {
        CRefObject* message = remote->members_.start[i].ptr;
        if (!message || (message->flags7_ & CRefObject::kDetached)) return;
        if (StreamOf(message)->Size() == 0) return;
    }

    void* party = base + 0x2690;
    PartyReset(party, nullptr, 1, 0);
    std::memcpy(base + 0x26E0, &remote->partyValue_, 4);
    struct {
        void** start;
        void** finish;
        void** eos;
    } members;
    std::uint8_t allocator = 0;
    ObjectVectorConstruct(&members, nullptr, &allocator);
    for (std::uint32_t i = 0; i < count(); ++i) {
        CStream* stream = StreamOf(remote->members_.start[i].ptr);
        stream->Seek(0);
        std::int32_t id;
        std::uint32_t prototype, value;
        stream->Read(&id, 4u);
        if (id > 0) {
            stream->Read(&prototype, 4u);
            stream->Read(&value, 4u);
            void* factory = ei::VCall<void*>(party, 22);
            auto* object = ei::VCall<std::uint8_t*>(factory, 23, prototype, id);
            std::memcpy(object + 0x18, &value, 4);
            ei::VCall<int>(object, 9, stream);
        }
        stream->Read(&prototype, 4u);
        stream->Read(&value, 4u);
        void* factory = ei::VCall<void*>(party, 18);
        auto* member = ei::VCall<std::uint8_t*>(factory, 23, prototype, -1);
        std::memcpy(member + 0x18, &value, 4);
        ei::VCall<int>(member, 9, stream);
        void* held = member;
        if (member) ++*reinterpret_cast<std::int32_t*>(member + 4);
        if (members.finish != members.eos) {
            if (members.finish) ObjectPtrConstructCopy(members.finish, nullptr, &held);
            ++members.finish;
        } else {
            ObjectVectorInsertOverflow(&members, nullptr, members.finish, &held, 1);
        }
        if (held) ei::VCall<int>(held, 4);   // Release
    }
    for (std::uint32_t i = 0; i < count(); ++i) PartyAdd(party, nullptr, members.start[i], 1, 1);

    void* entries = &self->tradeRecords_;
    std::int32_t rebuilds = static_cast<std::int32_t>(remote->revision_);
    void** sentinel = static_cast<void**>(self->tradeRecords_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (reinterpret_cast<std::intptr_t>(node[2]) < rebuilds) {
            void* erased;
            List2614Erase(entries, nullptr, &erased, node);
        }
        node = next;
    }
    for (void** p = members.start; p != members.finish; ++p)
        if (*p) ei::VCall<int>(*p, 4);
    ObjectVectorDeallocate(members.start, static_cast<std::size_t>(members.eos - members.start));
}
EI_REPLACE(0x0041DDC0, ClientApplyParty);

} // namespace net
