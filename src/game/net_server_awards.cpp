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
#include "ui.h"
#include "world.h"
#include "world_data.h"

#include <cstring>

namespace net {

namespace {

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
// second delay. `text` is the caller's string (made in the argument slot), destroyed here as the
// callee destroys a by-value argument.
void ServerSession::QueueAward(CRefObject* connection, int kind, float value, stlp::string text) {
    auto* award = NewMessageMemory<AwardMessage>();
    if (award) {
        RefValueConstructBase(award);
        ei::SetVtable(award, kMessageVtable);
        award->version_ = 1;
        award->connection_.ptr = nullptr;
        stlp::StringElement::Default(&award->text_);
        ei::SetVtable(award, AwardMessage::kVtableAddress);
    }
    CRefObject* previous = award->connection_.ptr;
    award->connection_.ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) previous->Release();
    award->kind_ = static_cast<std::uint32_t>(kind);
    std::memcpy(&award->value_, &value, 4);

    // The text, assigned in place (string::operator= inlined: overwrite, then cut or append).
    award->text_.AssignFrom(text);

    RefPtr shared;
    shared.Share(award);
    TimedEntry entry;
    entry.key = kAwardDelay;
    entry.ref.CopyShared(&shared);
    awards_.PushBack(entry);
    entry.ref.Destroy();
    DestroyInline(&shared);
}

// 004191C0: finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (+0x14..+0x18)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const std::uint8_t* entry, bool withFinal) {
    RefNode* sentinel = bindings_.End();
    RefNode* node = sentinel->next;
    for (;;) {
        if (node == sentinel) return;
        auto* binding = static_cast<BindingMessage*>(node->data.ptr);
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        if (Live(player) && player->host_) break;
        node = node->next;
    }
    CRefObject* connection = static_cast<BindingMessage*>(node->data.ptr)->ConnectionRef().ptr;
    if (!Live(connection)) return;

    char** const* items = reinterpret_cast<char** const*>(entry + 0x14);   // start, finish
    auto count = [items] { return static_cast<std::uint32_t>((items[1] - items[0])); };
    if (count() != 0) {
        std::uint32_t i = 0;
        if (count() != 0) {
            do {
                const char* item = items[0][i];
                QueueAward(connection, 2, 0.0f, stlp::string(item));
                ++i;
            } while (i < count());
        }
    }
    if (withFinal) QueueAward(connection, 4, 0.0f, stlp::string(game::EmptyText()));
}

// 00418ED0: the zone completion command ("<zone>_3"). Announces the current map's title (text 7)
// when a map message is held, then, when the map registry knows "zone_3", queues for every client
// its share of the entry's two values (+0xC as kind 0, +0x10 as kind 1, divided by the number of
// clients, at least 1) and the host's items with the final award. Then drops the map message and
// empties the queued messages (+0x25AC), +0x25B0 and +0x2598, and queues the following zones
// (00418530).
void ServerSession::CompleteZone(const stlp::string* zone) {
    auto* holder = &map_;
    if (Live(holder->ptr)) {
        std::uint32_t title = 0, text = 0;   // CStrings
        void* titleOut = SessionMapNameInto(reinterpret_cast<mfc::CString*>(&title));
        void* textOut = worlddata::InterfaceManager()->MessageText(&text, 7, *static_cast<const char**>(titleOut));
        Announce(*static_cast<const char**>(textOut), nullptr);
        CStringDestroy(&text);
        CStringDestroy(&title);
    }

    std::uint8_t* entry;
    {
        stlp::string key = stlp::Plus(*zone, game::ZoneSuffix3Text());
        entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&key));
    }
    if (entry) {
        int count = 0;
        for (ClientNode* node = clients_->next; node != clients_; node = node->next) ++count;
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
            const char* empty = game::EmptyText();
            QueueAward(connection, 0, award0, stlp::string(empty));
            QueueAward(connection, 1, award1, stlp::string(empty));
            node = node->next;
            sentinel = clients_;
        }
        AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    queuedMaps_.Clear();
    variables_.Clear();
    files_.Clear();
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
        void* titleOut = SessionMapNameInto(reinterpret_cast<mfc::CString*>(&title));
        void* textOut = worlddata::InterfaceManager()->MessageText(&text, 6, *static_cast<const char**>(titleOut));
        Announce(*static_cast<const char**>(textOut), nullptr);
        CStringDestroy(&text);
        CStringDestroy(&title);

        std::uint8_t* entry;
        {
            stlp::string key = stlp::Plus(static_cast<MapMessage*>(holder->ptr)->text_, game::ZoneSuffix1Text());
            stlp::string lookup(key.data());
            entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&lookup));
        }
        if (entry) AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    queuedMaps_.Clear();
    variables_.Clear();
    files_.Clear();
    QueueMapList();
    WorldPrepareMap(TheGameFlow(), nullptr, nullptr);
}

EI_REPLACE_METHOD(0x00419D90, ServerSession::QueueAward);
EI_REPLACE_METHOD(0x004191C0, ServerSession::AwardHost);
EI_REPLACE_METHOD(0x00418ED0, ServerSession::CompleteZone);
EI_REPLACE_METHOD(0x00418CC0, ServerSession::LeaveZone);

} // namespace net
