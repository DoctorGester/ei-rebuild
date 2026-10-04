// The client's party description (0041DA50, called by the party screens after a change): one
// message per party member, kept in the replicated object at +0x260C. See net_classes.h and
// docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// STLport vector<RefPtr> plumbing (library instantiations) and the message stream constructor.
__declspec(naked) void* __fastcall VectorGetAllocator(void* /*vector*/, void*, void* /*out*/) EI_ORIGINAL(0x00421EB0)
__declspec(naked) void __fastcall VectorBaseConstruct(void* /*vector*/, void*, std::size_t /*n*/, const void* /*allocator*/) EI_ORIGINAL(0x00421F60)
__declspec(naked) RefPtr* __fastcall UninitializedCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/) EI_ORIGINAL(0x0042AF80)
__declspec(naked) RefPtr* __fastcall VectorErase(void* /*vector*/, void*, RefPtr* /*first*/, RefPtr* /*last*/) EI_ORIGINAL(0x00421ED0)
__declspec(naked) void __fastcall RefPtrConstructCopy(RefPtr* /*at*/, const RefPtr* /*from*/) EI_ORIGINAL(0x00429060)
__declspec(naked) void __fastcall VectorInsertOverflow(void* /*vector*/, void*, RefPtr* /*pos*/, const RefPtr* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00426F00)
__declspec(naked) RefPtr* __fastcall RefPtrCopyRange(RefPtr* /*first*/, RefPtr* /*last*/, RefPtr* /*result*/) EI_ORIGINAL(0x00429080)
__declspec(naked) void* __fastcall RefPtrDestroyAt(RefPtr* /*p*/, void*, unsigned /*flags*/) EI_ORIGINAL(0x004292A0)
__declspec(naked) void __fastcall VectorDeallocate(RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426490)
__declspec(naked) void __fastcall RefPtrRaw(RefPtr* /*p*/, void*, CRefObject* /*object*/) EI_ORIGINAL(0x0041DD50)
__declspec(naked) void* __fastcall MemoryStreamConstructAt(void* /*stream*/) EI_ORIGINAL(0x0041DD70)

// vector<CEngineObject*> with counted elements (library), the +0x2614 list erase, the party's
// reset and add (00661050 class, outside this range).
__declspec(naked) void __fastcall ObjectVectorConstruct(void* /*vector*/, void*, const void* /*allocator*/) EI_ORIGINAL(0x00424B40)
__declspec(naked) void __fastcall ObjectPtrConstructCopy(void* /*at*/, void*, void* const* /*from*/) EI_ORIGINAL(0x004298F0)
__declspec(naked) void __fastcall ObjectVectorInsertOverflow(void* /*vector*/, void*, void** /*pos*/, void* const* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00428A20)
__declspec(naked) void __fastcall ObjectVectorDeallocate(void** /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x004268A0)
__declspec(naked) void** __fastcall List2614Erase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00429770)
__declspec(naked) void __fastcall PartyReset(void* /*party*/, void*, int /*a*/, int /*b*/) EI_ORIGINAL(0x00661700)
__declspec(naked) void __fastcall PartyAdd(void* /*party*/, void*, void* /*object*/, int /*a*/, int /*b*/) EI_ORIGINAL(0x00661DB0)

constexpr std::uintptr_t kMemberMessageVtable = 0x0073BD78;
constexpr std::size_t kMemberMessageSize = 0x34;   // Message header, CMemoryStream at +0xC

// STLport vector<RefPtr>: start, finish, end of storage.
struct RefVector {
    RefPtr* start;
    RefPtr* finish;
    RefPtr* eos;
};

// The replicated party description (a CRefObject; names provisional).
struct PartyState {
    std::uint8_t header[8];
    std::uint32_t version;        // +0x08 bumped on every rebuild
    RefVector messages;           // +0x0C one message per party member
    std::uint32_t partyValue;     // +0x18 the party's +0x50
    std::uint8_t dirty;           // +0x1C
    std::uint8_t unknown_1D[7];
    std::uint32_t rebuilds;       // +0x24 counts the rebuilds (the client drops older +0x2614 entries)
    std::int32_t idA;             // +0x28 paired with the other side's +0x2C (0041E220)
    std::int32_t idB;             // +0x2C paired with the other side's +0x28
};
static_assert(offsetof(PartyState, messages) == 0x0C);
static_assert(offsetof(PartyState, rebuilds) == 0x24);
static_assert(offsetof(PartyState, idB) == 0x2C);

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

void WriteDword(void* stream, std::uint32_t* scratch, std::uint32_t value) {
    *scratch = value;
    ei::VCall<int>(stream, 2, scratch, 4u);   // Write
}

void PushBack(RefVector* v, const RefPtr* value) {
    if (v->finish != v->eos) {
        RefPtrConstructCopy(v->finish, value);
        ++v->finish;
    } else {
        VectorInsertOverflow(v, nullptr, v->finish, value, 1);
    }
}

} // namespace

// 00429FC0: the first element in [first, last) whose message stream holds the same bytes as
// `message`'s (sizes through slot 4, then a byte compare). The last argument is not used.
RefPtr* __fastcall FindSameMessage(RefPtr* first, RefPtr* last, Message* message, std::uint32_t) {
    auto* stream = reinterpret_cast<std::uint8_t*>(message) + 0xC;
    for (; first != last; ++first) {
        auto* element = reinterpret_cast<std::uint8_t*>(first->ptr) + 0xC;
        std::uint32_t size = ei::VCall<std::uint32_t>(element, 4);
        if (ei::VCall<std::uint32_t>(stream, 4) != size) continue;
        const void* a = GetPtr(element, 0x18);
        const void* b = GetPtr(stream, 0x18);
        if (std::memcmp(a, b, ei::VCall<std::uint32_t>(stream, 4)) == 0) break;
    }
    return first;
}
EI_REPLACE(0x00429FC0, FindSameMessage);

// 0041DA50: rebuilds the party messages of the replicated object at +0x260C from the party at
// +0x2620 (members +0x40, count +0x44). Each member gives a message (vtable 0073BD78) with its
// attachment at +0x44 (fields +0xC, +0x10, +0x18 and its slot 10 serialisation, or -1 without one),
// its fields +0x10 and +0x18 and its own slot 10 serialisation. A message equal to one of the
// previous messages is replaced by that one (so unchanged members keep their object). Then the
// object's counters move on and its dirty flag is cleared.
void __fastcall ClientPublishParty(ClientSession* self) {
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    // Read again at each use, as the original does.
    auto state = [self] { return static_cast<PartyState*>(self->unknown_260C); };
    if (!state()) return;
    RefVector* messages = &state()->messages;

    // A copy of the old messages, then the list emptied.
    std::uint8_t allocator = 0;
    const void* allocatorCopy = VectorGetAllocator(messages, nullptr, &allocator);
    RefVector previous;
    VectorBaseConstruct(&previous, nullptr, static_cast<std::size_t>(messages->finish - messages->start), allocatorCopy);
    previous.finish = UninitializedCopy(messages->start, messages->finish, previous.start, 0);
    VectorErase(&state()->messages, nullptr, state()->messages.start, state()->messages.finish);
    state()->partyValue = Get32(base, 0x2670);

    std::uint32_t scratch = 0;
    for (int i = 0; i < static_cast<int>(Get32(base, 0x2664)); ++i) {
        auto* message = static_cast<std::uint8_t*>(gamecrt::operator_new(kMemberMessageSize));
        std::uint32_t zero = 0;
        std::memcpy(message + 4, &zero, 4);
        if (message) {
            MessageConstructBase(reinterpret_cast<Message*>(message));
            MemoryStreamConstructAt(message + 0xC);
            std::memcpy(message, &kMemberMessageVtable, 4);
        }
        RefPtr ref;
        RefPtrRaw(&ref, nullptr, reinterpret_cast<CRefObject*>(message));
        auto* stream = reinterpret_cast<std::uint8_t*>(ref.ptr) + 0xC;
        auto* member = static_cast<std::uint8_t*>(static_cast<void**>(GetPtr(base, 0x2660))[i]);
        ei::VCall<void>(stream, 6);   // Clear
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

        RefPtr* same = FindSameMessage(previous.start, previous.finish, reinterpret_cast<Message*>(ref.ptr), scratch);
        PartyState* current = state();
        if (same != previous.finish) {
            PushBack(&current->messages, same);
            if (same + 1 != previous.finish) RefPtrCopyRange(same + 1, previous.finish, same);
            --previous.finish;
            RefPtrDestroyAt(previous.finish, nullptr, 0);
        } else {
            PushBack(&current->messages, &ref);
        }
        if (ref.ptr) ref.ptr->Release();
    }

    ++state()->rebuilds;
    state()->dirty = 0;
    ++state()->version;
    for (RefPtr* p = previous.start; p != previous.finish; ++p)
        if (p->ptr) p->ptr->Release();
    VectorDeallocate(previous.start, static_cast<std::size_t>(previous.eos - previous.start));
}
EI_REPLACE(0x0041DA50, ClientPublishParty);

// 0041E220: the received party description (list +0x2610) that pairs with ours (+0x260C): its
// +0x2C equals our +0x28 and its +0x28 our +0x2C, and it is live. Null without one.
void* __fastcall ClientFindParty(ClientSession* self) {
    auto* ours = static_cast<PartyState*>(self->unknown_260C);
    if (!ours) return nullptr;
    void** sentinel = static_cast<void**>(self->list_2610);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = static_cast<PartyState*>(node[2]);
        if (other->idB != ours->idA || other->idA != ours->idB) continue;
        if (other && !(reinterpret_cast<CRefObject*>(other)->flags7_ & CRefObject::kDetached)) return other;
    }
    return nullptr;
}
EI_REPLACE(0x0041E220, ClientFindParty);

// 0041DDC0: rebuilds the second party (+0x2690) from the paired description, once every message of
// it holds data. Each message gives (when its first dword is positive) an object made through the
// party's slot 22 factory (slot 23 with the stored prototype and that id; +0x18 and its slot 9
// load) and a member made through the slot 18 factory (slot 23 with id -1; +0x18 and slot 9 load).
// The members are added to the party (00661DB0 with 1, 1); entries of +0x2614 older than the
// description's rebuild count are dropped.
void __fastcall ClientApplyParty(ClientSession* self) {
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    auto* remote = static_cast<PartyState*>(ClientFindParty(self));
    if (!remote) return;
    auto count = [remote] { return static_cast<std::uint32_t>(remote->messages.finish - remote->messages.start); };
    for (std::uint32_t i = 0; i < count(); ++i) {
        CRefObject* message = remote->messages.start[i].ptr;
        if (!message || (message->flags7_ & CRefObject::kDetached)) return;
        if (ei::VCall<std::uint32_t>(reinterpret_cast<std::uint8_t*>(message) + 0xC, 4) == 0) return;   // Size
    }

    void* party = base + 0x2690;
    PartyReset(party, nullptr, 1, 0);
    std::memcpy(base + 0x26E0, &remote->partyValue, 4);
    struct {
        void** start;
        void** finish;
        void** eos;
    } members;
    std::uint8_t allocator = 0;
    ObjectVectorConstruct(&members, nullptr, &allocator);
    for (std::uint32_t i = 0; i < count(); ++i) {
        auto* stream = reinterpret_cast<std::uint8_t*>(remote->messages.start[i].ptr) + 0xC;
        ei::VCall<int>(stream, 3, 0);   // Seek
        std::int32_t id;
        std::uint32_t prototype, value;
        ei::VCall<int>(stream, 1, &id, 4u);   // Read
        if (id > 0) {
            ei::VCall<int>(stream, 1, &prototype, 4u);
            ei::VCall<int>(stream, 1, &value, 4u);
            void* factory = ei::VCall<void*>(party, 22);
            auto* object = ei::VCall<std::uint8_t*>(factory, 23, prototype, id);
            std::memcpy(object + 0x18, &value, 4);
            ei::VCall<int>(object, 9, stream);
        }
        ei::VCall<int>(stream, 1, &prototype, 4u);
        ei::VCall<int>(stream, 1, &value, 4u);
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

    void* entries = &self->list_2614;
    std::int32_t rebuilds = static_cast<std::int32_t>(remote->rebuilds);
    void** sentinel = static_cast<void**>(self->list_2614);
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
