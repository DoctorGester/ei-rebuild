// The game server's awards: the zone completion command (00418ED0, "<zone>_3"), the host's item
// awards (004191C0) and the queued award message (00419D90), plus the counted pointer copy they
// use (004263F0). See net_classes.h and docs/notes/network.md.

#include "net_messages.h"
#include "game_strings.h"
#include "net_connection.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world.h"
#include "world_data.h"

#include <cstring>

namespace net {

namespace {

// STLport strings and lists (library instantiations).
inline void __fastcall StringRangeInitTag(StringRep* a0, void*, const char* a2, const char* a3, std::uint32_t a4) { StringInitRangeOriginal((void*)a0, nullptr, a2, a3, (const void*)a4); }
__declspec(naked) char* __fastcall CopyTrivial(const char* /*first*/, const char* /*last*/, char* /*result*/) EI_ORIGINAL(0x004193D0)
__declspec(naked) void** __fastcall ClientsBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004219E0)
__declspec(naked) void** __fastcall ClientsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004219F0)
__declspec(naked) void __fastcall CountClients(int* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x00429040)
__declspec(naked) void __fastcall FreeNode(void* /*node*/, std::size_t /*n*/) EI_ORIGINAL(0x00426920)
// The pending awards (+0x25B4): push_back of {delay, award}.
__declspec(naked) void __fastcall AwardsPushBack(void* /*list*/, void*, const void* /*entry*/) EI_ORIGINAL(0x00422F70)

// The world (0x7AFE20), the text table (0x7C7D50), the map registry (0x7C87D4).

constexpr std::uint32_t kAwardDelay = 0x42700000;   // 60.0f

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
    const char* first = game::EmptyText();
    const char* last = first + std::strlen(first);
    StringAllocateBlock(s, nullptr, static_cast<std::size_t>(last - first) + 1);
    char* end = CopyTrivial(first, last, s->start);
    s->finish = end;
    *end = 0;
}

} // namespace

} // namespace net

// 004263F0: the counted pointer copy constructor (the same as Share with the other's object).
RefPtr* RefPtr::CopyShared(const RefPtr* other) {
    Share(other->ptr);
    return this;
}

namespace net {
EI_REPLACE_METHOD(0x004263F0, RefPtr::CopyShared);
EI_REPLACE_METHOD(0x004250A0, RefPtr::CopyShared);   // byte-identical copy

// 00419D90: queues an award message for `connection` on the pending awards (+0x25B4) with a 60
// second delay. `text` is the caller's string, destroyed here.
void ServerSession::QueueAward(CRefObject* connection, int kind, float value, StringRep text) {
    auto* award = NewMessageMemory<AwardMessage>();
    if (award) {
        RefValueConstructBase(award);
        ei::SetVtable(award, kMessageVtable);
        award->version_ = 1;
        award->connection_.ptr = nullptr;
        StringInitEmpty(&award->text_);
        ei::SetVtable(award, AwardMessage::kVtableAddress);
    }
    CRefObject* previous = award->connection_.ptr;
    award->connection_.ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) previous->Release();
    award->kind_ = static_cast<std::uint32_t>(kind);
    std::memcpy(&award->value_, &value, 4);

    // The text, assigned in place (string::operator= inlined).
    StringRep* to = &award->text_;
    const char* first = text.start;
    const char* last = text.finish;
    char* d = to->start;
    if (first == last) {
        StringEraseOriginal(to, nullptr, d, to->finish);
    } else {
        for (;;) {
            if (d == to->finish) {
                if (first != last) StringAppend(to, nullptr, first, last, connection);
                else StringEraseOriginal(to, nullptr, d, to->finish);
                break;
            }
            *d++ = *first++;
            if (first == last) {
                StringEraseOriginal(to, nullptr, d, to->finish);
                break;
            }
        }
    }

    RefPtr shared;
    shared.Share(award);
    TimedEntry entry;
    entry.secondsBits = kAwardDelay;
    entry.object.CopyShared(&shared);
    AwardsPushBack(&awards_, nullptr, &entry);
    entry.object.Destroy();
    DestroyInline(&shared);
    StringBufferFree(&text);
}

// 004191C0: finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (+0x14..+0x18)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const std::uint8_t* entry, bool withFinal) {
    void** sentinel = static_cast<void**>(bindings_);
    void** node = static_cast<void**>(sentinel[0]);
    for (;;) {
        if (node == sentinel) return;
        auto* binding = static_cast<BindingMessage*>(node[2]);
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        if (Live(player) && player->host_) break;
        node = static_cast<void**>(node[0]);
    }
    CRefObject* connection = static_cast<BindingMessage*>(node[2])->ConnectionRef().ptr;
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
                StringEndOfStorageOriginal(&text.eos, nullptr, &allocator, &none);
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
// empties the queued messages (+0x25AC), +0x25B0 and +0x2598, and queues the following zones
// (00418530).
void ServerSession::CompleteZone(const StringRep* zone) {
    auto* holder = &map_;
    if (Live(holder->ptr)) {
        std::uint32_t title = 0, text = 0;   // CStrings
        void* titleOut = WorldMapTitle(TheGameFlow(), nullptr, &title);
        void* textOut = TextFormat(worlddata::InterfaceManager(), nullptr, &text, 7, *static_cast<const char**>(titleOut));
        Announce(*static_cast<const char**>(textOut), nullptr);
        CStringDestroy(&text);
        CStringDestroy(&title);
    }

    StringRep key;
    StringPlusText(&key, zone, game::ZoneSuffix3Text());
    auto* entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&key));
    StringRepDestroy(&key);
    if (entry) {
        void* clients = &clients_;
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

        ClientNode* sentinel = clients_;
        for (ClientNode* node = sentinel->next; node != sentinel;) {
            CRefObject* connection = node->connection;
            std::uint8_t allocator = 0;
            StringRep text0;
            StringInitWithAllocator(&text0, nullptr, &allocator);
            const char* empty = game::EmptyText();
            StringAssignSpan(&text0, nullptr, empty, empty + std::strlen(empty));
            QueueAward(connection, 0, award0, text0);
            StringRep text1;
            StringInitWithAllocator(&text1, nullptr, &allocator);
            StringRangeInitTag(&text1, nullptr, empty, empty + std::strlen(empty), static_cast<std::uint32_t>(share));
            QueueAward(connection, 1, award1, text1);
            node = node->next;
            sentinel = clients_;
        }
        AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    void** messages = static_cast<void**>(queuedMaps_);
    for (void** node = static_cast<void**>(messages[0]); node != queuedMaps_;) {
        void** next = static_cast<void**>(node[0]);
        Element25ACDestroy(node + 2, nullptr, 0);
        FreeNode(node, 1);
        node = next;
    }
    messages = static_cast<void**>(queuedMaps_);
    messages[0] = messages;
    static_cast<void**>(queuedMaps_)[1] = queuedMaps_;
    List25B0Clear(&variables_);
    List2598Clear(&files_);
    QueueMapList();
}

// 00418CC0: the zone leaving command ("<zone>_2"). When a map message is held: announces the
// map's title (text 6) and, when the map registry knows "<map>_1", queues the host's items with
// the final award. Then drops the map message, empties the queued messages (+0x25AC), +0x25B0
// and +0x2598, queues the following zones (00418530) and clears the world's map (0057ED60 with
// null).
void ServerSession::LeaveZone() {
    auto* holder = &map_;
    if (Live(holder->ptr)) {
        std::uint32_t title = 0, text = 0;   // CStrings
        void* titleOut = WorldMapTitle(TheGameFlow(), nullptr, &title);
        void* textOut = TextFormat(worlddata::InterfaceManager(), nullptr, &text, 6, *static_cast<const char**>(titleOut));
        Announce(*static_cast<const char**>(textOut), nullptr);
        CStringDestroy(&text);
        CStringDestroy(&title);

        StringRep key;
        StringPlusText(&key, &static_cast<MapMessage*>(holder->ptr)->text_, game::ZoneSuffix1Text());
        const char* keyText = key.start;
        StringRep lookup;
        std::uint8_t allocator = 0;
        StringInitWithAllocator(&lookup, nullptr, &allocator);
        StringAssignSpan(&lookup, nullptr, keyText, keyText + std::strlen(keyText));
        auto* entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&lookup));
        StlStringDestroy(&lookup);
        libcrt::NodeDeallocateCheckedStd(key.start, static_cast<std::size_t>(key.eos - key.start));
        if (entry) AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    void** messages = static_cast<void**>(queuedMaps_);
    for (void** node = static_cast<void**>(messages[0]); node != queuedMaps_;) {
        void** next = static_cast<void**>(node[0]);
        Element25ACDestroy(node + 2, nullptr, 0);
        FreeNode(node, 1);
        node = next;
    }
    static_cast<void**>(queuedMaps_)[0] = queuedMaps_;
    static_cast<void**>(queuedMaps_)[1] = queuedMaps_;
    void** queued = static_cast<void**>(variables_);
    for (void** node = static_cast<void**>(queued[0]); node != variables_;) {
        void** next = static_cast<void**>(node[0]);
        Element25B0Destroy(node + 2, nullptr, 0);
        List25B0FreeNode(&variables_, nullptr, node, 1);
        node = next;
    }
    static_cast<void**>(variables_)[0] = variables_;
    static_cast<void**>(variables_)[1] = variables_;
    List2598Clear(&files_);
    QueueMapList();
    WorldPrepareMap(TheGameFlow(), nullptr, nullptr);
}

EI_REPLACE_METHOD(0x00419D90, ServerSession::QueueAward);
EI_REPLACE_METHOD(0x004191C0, ServerSession::AwardHost);
EI_REPLACE_METHOD(0x00418ED0, ServerSession::CompleteZone);
EI_REPLACE_METHOD(0x00418CC0, ServerSession::LeaveZone);

} // namespace net
