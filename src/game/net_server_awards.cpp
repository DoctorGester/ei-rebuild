// The game server's awards: the zone completion command (00418ED0, "<zone>_3"), the host's item
// awards (004191C0) and the queued award message (00419D90), plus the counted pointer copy they
// use (004263F0). See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#pragma float_control(precise, on)

#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// STLport strings and lists (library instantiations).
__declspec(naked) StringRep* __fastcall StringPlusText(StringRep* /*out*/, const StringRep* /*a*/, const char* /*b*/) EI_ORIGINAL(0x00427D60)
__declspec(naked) void __fastcall StringDestroyA(StringRep* /*s*/) EI_ORIGINAL(0x0040C770)
__declspec(naked) void __fastcall StringDestroyVector(StringRep* /*s*/) EI_ORIGINAL(0x00425040)
__declspec(naked) StringRep* __fastcall StringWithAlloc(StringRep* /*s*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004217E0)
__declspec(naked) void __fastcall StringRangeInit(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00428F10)
__declspec(naked) void __fastcall StringRangeInitTag(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, std::uint32_t /*tag*/) EI_ORIGINAL(0x0042A3D0)
__declspec(naked) void __fastcall StringAllocProxy(char** /*eos*/, void*, const void* /*alloc*/, char* const* /*value*/) EI_ORIGINAL(0x00425080)
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)
__declspec(naked) char* __fastcall CopyTrivial(const char* /*first*/, const char* /*last*/, char* /*result*/) EI_ORIGINAL(0x004193D0)
__declspec(naked) void __fastcall StringErase(StringRep* /*s*/, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppend(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, void* /*tag*/) EI_ORIGINAL(0x0042AFB0)
__declspec(naked) void __fastcall MessageStringConstruct(void* /*string*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void** __fastcall ClientsBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004219E0)
__declspec(naked) void** __fastcall ClientsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004219F0)
__declspec(naked) void __fastcall CountClients(int* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x00429040)
__declspec(naked) void __fastcall Element25ACDestroy(void* /*element*/, void*, int /*flags*/) EI_ORIGINAL(0x0042A5F0)
__declspec(naked) void __fastcall FreeNode(void* /*node*/, std::size_t /*n*/) EI_ORIGINAL(0x00426920)
__declspec(naked) void __fastcall List25B0Clear(void* /*list*/) EI_ORIGINAL(0x00427100)
__declspec(naked) void __fastcall List2598Clear(void* /*list*/) EI_ORIGINAL(0x004271C0)
// The pending awards (+0x25B4): push_back of {delay, award}.
__declspec(naked) void __fastcall AwardsPushBack(void* /*list*/, void*, const void* /*entry*/) EI_ORIGINAL(0x00422F70)

// The world (0x7AFE20), the text table (0x7C7D50), the map registry (0x7C87D4).
__declspec(naked) void* __fastcall WorldMapTitle(void* /*world*/, void*, void* /*out CString*/) EI_ORIGINAL(0x005814C0)
__declspec(naked) void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/) EI_ORIGINAL(0x005F8C90)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)
__declspec(naked) void** __fastcall MapRegistryFind(void* /*registry*/, void*, const StringRep* /*name*/) EI_ORIGINAL(0x004297A0)
constexpr std::uintptr_t kWorld = 0x007AFE20;
constexpr std::uintptr_t kTextTable = 0x007C7D50;
constexpr std::uintptr_t kMapRegistry = 0x007C87D4;
constexpr std::uintptr_t kRewardSuffix = 0x00783D84;   // "_3"
constexpr std::uintptr_t kEmptyText = 0x0079B298;      // ""

// The server's own steps (not rewritten yet).
__declspec(naked) void __fastcall ServerAnnounce(ServerSession* /*self*/, void*, const char* /*text*/, int /*flag*/) EI_ORIGINAL(0x0041A310)
__declspec(naked) void __fastcall ServerResetMapList(ServerSession* /*self*/) EI_ORIGINAL(0x00418530)

constexpr std::uintptr_t kAwardVtable = 0x0073BD48;
constexpr std::uint32_t kAwardDelay = 0x42700000;   // 60.0f

const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }
void Put32(void* p, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &v, 4); }
template <typename T>
T* GetPtr(const void* p, std::size_t offset) {
    T* v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
void SetVtable(void* p, std::uintptr_t v) { std::memcpy(p, &v, 4); }
bool Live(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

// RefPtr::Destroy as the callers inline it: the release is not null-checked after the shutdown.
void DestroyInline(RefPtr* p) {
    if (CRefObject* object = p->ptr) {
        if (object->refs_ != 1) {
            object->flags7_ |= CRefObject::kDetached;
            object->Shutdown();
        }
        p->ptr->Release();
    }
}

// An award's text: an empty string built the way the inline string(const char*) does.
void EmptyAwardText(StringRep* s) {
    s->start = nullptr;
    s->finish = nullptr;
    s->eos = nullptr;
    const char* first = Text(kEmptyText);
    const char* last = first + std::strlen(first);
    StringAllocateBlock(s, nullptr, static_cast<std::size_t>(last - first) + 1);
    char* end = CopyTrivial(first, last, s->start);
    s->finish = end;
    *end = 0;
}

} // namespace

// 004263F0: the counted pointer copy constructor (the same as Share with the other's object).
RefPtr* __fastcall RefPtrCopyConstruct(RefPtr* self, void*, const RefPtr* other) {
    self->Share(other->ptr);
    return self;
}
EI_REPLACE(0x004263F0, RefPtrCopyConstruct);

// 00419D90: queues an award message (vtable 0073BD48, 0x24 bytes: +0xC the connection (counted),
// +0x10 kind, +0x14 value, +0x18 text) on the pending awards (+0x25B4) with a 60 second delay.
// `text` is the caller's string, destroyed here.
void ServerSession::QueueAward(CRefObject* connection, int kind, float value, StringRep text) {
    auto* award = static_cast<std::uint8_t*>(gamecrt::operator_new(0x24));
    Put32(award, 4, 0);
    if (award) {
        RefValueConstructBase(award);
        SetVtable(award, kMessageVtable);
        Put32(award, 8, 1);
        Put32(award, 0xC, 0);
        MessageStringConstruct(award + 0x18);
        SetVtable(award, kAwardVtable);
    }
    CRefObject* previous = GetPtr<CRefObject>(award, 0xC);
    std::memcpy(award + 0xC, &connection, 4);
    if (connection) ++connection->refs_;
    if (previous) previous->Release();
    Put32(award, 0x10, static_cast<std::uint32_t>(kind));
    std::memcpy(award + 0x14, &value, 4);

    // The text, assigned in place (string::operator= inlined).
    auto* to = reinterpret_cast<StringRep*>(award + 0x18);
    const char* first = text.start;
    const char* last = text.finish;
    char* d = to->start;
    if (first == last) {
        StringErase(to, nullptr, d, to->finish);
    } else {
        for (;;) {
            if (d == to->finish) {
                if (first != last) StringAppend(to, nullptr, first, last, connection);
                else StringErase(to, nullptr, d, to->finish);
                break;
            }
            *d++ = *first++;
            if (first == last) {
                StringErase(to, nullptr, d, to->finish);
                break;
            }
        }
    }

    RefPtr shared;
    shared.Share(reinterpret_cast<CRefObject*>(award));
    struct {
        std::uint32_t delay;
        RefPtr award;
    } entry;
    entry.delay = kAwardDelay;
    RefPtrCopyConstruct(&entry.award, nullptr, &shared);
    AwardsPushBack(&list_25B4, nullptr, &entry);
    entry.award.Destroy();
    DestroyInline(&shared);
    StringDestroyVector(&text);
}

// 004191C0: finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (+0x14..+0x18)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const std::uint8_t* entry, bool withFinal) {
    void** sentinel = static_cast<void**>(list_25A4);
    void** node = static_cast<void**>(sentinel[0]);
    for (;;) {
        if (node == sentinel) return;
        auto* binding = static_cast<std::uint8_t*>(node[2]);
        auto* player = GetPtr<CRefObject>(binding, 0xC);
        if (Live(player) && reinterpret_cast<std::uint8_t*>(player)[0x28]) break;
        node = static_cast<void**>(node[0]);
    }
    auto* connection = GetPtr<CRefObject>(node[2], 0x10);
    if (!Live(connection)) return;

    char** const* items = reinterpret_cast<char** const*>(entry + 0x14);   // start, finish
    auto count = [items] { return static_cast<std::uint32_t>((items[1] - items[0])); };
    if (count() != 0) {
        std::uint32_t i = 0;
        if (count() != 0) {
            do {
                const char* item = items[0][i];
                StringRep text;
                text.start = nullptr;
                text.finish = nullptr;
                char* none = nullptr;
                std::uint8_t allocator = 0;
                StringAllocProxy(&text.eos, nullptr, &allocator, &none);
                StringRangeInitTag(&text, nullptr, item, item + std::strlen(item), withFinal);
                QueueAward(connection, 2, 0.0f, text);
                ++i;
            } while (i < count());
        }
    }
    if (withFinal) {
        StringRep text;
        EmptyAwardText(&text);
        QueueAward(connection, 4, 0.0f, text);
    }
}

// 00418ED0: the zone completion command ("<zone>_3"). Announces the current map's title (text 7)
// when a map message is held, then, when the map registry knows "zone_3", queues for every client
// its share of the entry's two values (+0xC as kind 0, +0x10 as kind 1, divided by the number of
// clients, at least 1) and the host's items with the final award. Then drops the map message and
// empties the queued messages (+0x25AC), +0x25B0 and +0x2598, and resets the map list (00418530).
void ServerSession::CompleteZone(const StringRep* zone) {
    auto* holder = reinterpret_cast<RefPtr*>(&unknown_25A8);
    if (Live(holder->ptr)) {
        std::uint32_t title = 0, text = 0;   // CStrings
        void* titleOut = WorldMapTitle(reinterpret_cast<void*>(kWorld), nullptr, &title);
        void* textOut = TextFormat(reinterpret_cast<void*>(kTextTable), nullptr, &text, 7, *static_cast<const char**>(titleOut));
        ServerAnnounce(this, nullptr, *static_cast<const char**>(textOut), 0);
        CStringDestroy(&text);
        CStringDestroy(&title);
    }

    StringRep key;
    StringPlusText(&key, zone, Text(kRewardSuffix));
    auto* entry = static_cast<std::uint8_t*>(*MapRegistryFind(reinterpret_cast<void*>(kMapRegistry), nullptr, &key));
    StringDestroyA(&key);
    if (entry) {
        void* clients = &base.clients;
        int count = 0;
        void* last;
        void* lastNode = *ClientsEnd(clients, nullptr, &last);
        void* first;
        void* firstNode = *ClientsBegin(clients, nullptr, &first);
        CountClients(&count, nullptr, firstNode, lastNode);
        int share = count < 1 ? 1 : count;
        float value0, value1;
        std::memcpy(&value0, entry + 0xC, 4);
        std::memcpy(&value1, entry + 0x10, 4);
        const double divisor = share;
        float award0 = static_cast<float>(value0 / divisor);
        float award1 = static_cast<float>(value1 / divisor);

        void** sentinel = static_cast<void**>(base.clients);
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
            auto* connection = static_cast<CRefObject*>(node[2]);
            std::uint8_t allocator = 0;
            StringRep text0;
            StringWithAlloc(&text0, nullptr, &allocator);
            const char* empty = Text(kEmptyText);
            StringRangeInit(&text0, nullptr, empty, empty + std::strlen(empty));
            QueueAward(connection, 0, award0, text0);
            StringRep text1;
            StringWithAlloc(&text1, nullptr, &allocator);
            StringRangeInitTag(&text1, nullptr, empty, empty + std::strlen(empty), static_cast<std::uint32_t>(share));
            QueueAward(connection, 1, award1, text1);
            node = static_cast<void**>(node[0]);
            sentinel = static_cast<void**>(base.clients);
        }
        AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    void** messages = static_cast<void**>(list_25AC);
    for (void** node = static_cast<void**>(messages[0]); node != list_25AC;) {
        void** next = static_cast<void**>(node[0]);
        Element25ACDestroy(node + 2, nullptr, 0);
        FreeNode(node, 1);
        node = next;
    }
    messages = static_cast<void**>(list_25AC);
    messages[0] = messages;
    static_cast<void**>(list_25AC)[1] = list_25AC;
    List25B0Clear(&list_25B0);
    List2598Clear(&list_2598);
    ServerResetMapList(this);
}

namespace {
void __fastcall QueueAwardHook(ServerSession* self, void*, CRefObject* connection, int kind, float value, StringRep text) {
    self->QueueAward(connection, kind, value, text);
}
void __fastcall AwardHostHook(ServerSession* self, void*, const std::uint8_t* entry, bool withFinal) {
    self->AwardHost(entry, withFinal);
}
void __fastcall CompleteZoneHook(ServerSession* self, void*, const StringRep* zone) { self->CompleteZone(zone); }
} // namespace
EI_REPLACE(0x00419D90, QueueAwardHook);
EI_REPLACE(0x004191C0, AwardHostHook);
EI_REPLACE(0x00418ED0, CompleteZoneHook);

} // namespace net
