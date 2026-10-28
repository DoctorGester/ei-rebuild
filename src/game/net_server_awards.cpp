// The game server's awards: the zone completion command ("<zone>_3"), the host's item awards and the queued
// award message, plus the counted pointer copy they use. See net_classes.h and docs/notes/network.md.

#include "net_messages.h"
#include "quest_records.h"
#include "game_strings.h"
#include "net_connection.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "lib/crt.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "game_singletons.h"

#include <cstring>
#include <string>

namespace net {

namespace {

constexpr std::uint32_t kAwardDelay = 0x42700000;   // 60.0f

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

// The counted pointer copy constructor (the same as Share with the other's object).
RefPtr* RefPtr::CopyShared(const RefPtr* other) {
    Share(other->ptr);
    return this;
}

namespace net {

// Queues an award message for `connection` on the pending awards with a 60 second delay. `text` is the
// caller's string (made in the argument slot), destroyed here as the callee destroys a by-value argument.
void ServerSession::QueueAward(CRefObject* connection, int kind, float value, std::string text) {
    auto* award = NewCounted<AwardMessage>();
    CRefObject* previous = award->connection_.ptr;
    award->connection_.ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) previous->Release();
    award->kind_ = static_cast<std::uint32_t>(kind);
    std::memcpy(&award->value_, &value, 4);

    // The text, assigned in place (string::operator= inlined: overwrite, then cut or append).
    award->text_ = text;

    RefPtr shared;
    shared.Share(award);
    TimedEntry entry;
    entry.key = kAwardDelay;
    entry.ref.CopyShared(&shared);
    awards_.PushBack(entry);
    entry.ref.Destroy();
    DestroyInline(&shared);
}

// Finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (its given items)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const QuestBriefingRecord* entry, bool withFinal) {
    RefNode* sentinel = bindings_.End();
    RefNode* node = sentinel->next;
    for (;;) {
        if (node == sentinel) return;
        auto* binding = bindings_.At(node);
        auto* player = binding->Player();
        if (IsLive(player) && player->host_) break;
        node = node->next;
    }
    CRefObject* connection = bindings_.At(node)->ConnectionRef().ptr;
    if (!IsLive(connection)) return;

    const stlp::vector<std::string>& items = entry->lists[0];   // the items given
    auto count = [&items] { return static_cast<std::uint32_t>(items.end() - items.begin()); };
    if (count() != 0) {
        std::uint32_t i = 0;
        if (count() != 0) {
            do {
                QueueAward(connection, 2, 0.0f, items.begin()[i]);
                ++i;
            } while (i < count());
        }
    }
    if (withFinal) QueueAward(connection, 4, 0.0f, std::string(game::EmptyText()));
}

// The zone completion command ("<zone>_3"). Announces the current map's title (text 7)
// when a map message is held, then, when the map registry knows "zone_3", queues for every client
// its share of the entry's two values (exp as kind 0, money as kind 1, divided by the number of
// clients, at least 1) and the host's items with the final award. Then drops the map message and
// empties queuedMaps_, variables_ and files_, and queues the following zones
void ServerSession::CompleteZone(const std::string* zone) {
    auto* holder = &map_;
    if (IsLive(holder->ptr)) {
        {
            std::string title, text;
            const char* mapTitle = (*SessionMapNameInto(&title)).c_str();
            Announce((*TheInterface()->MessageText(&text, 7, mapTitle)).c_str(), nullptr);
        }
    }

    QuestBriefingRecord* entry;
    {
        std::string key = *zone + game::ZoneSuffix3Text();
        entry = stlp::NamedSlot<QuestBriefingRecord>(worlddata::QuestBriefings(), key);
    }
    if (entry) {
        int count = 0;
        for (ClientNode* node = clients_->next; node != clients_; node = node->next) ++count;
        int share = count < 1 ? 1 : count;
        float value0, value1;
        std::memcpy(&value0, &entry->exp, 4);
        std::memcpy(&value1, &entry->money, 4);
        const double divisor = share;
        float award0 = static_cast<float>(value0 / divisor);
        float award1 = static_cast<float>(value1 / divisor);

        ClientNode* sentinel = clients_;
        for (ClientNode* node = sentinel->next; node != sentinel;) {
            CRefObject* connection = node->connection;
            const char* empty = game::EmptyText();
            QueueAward(connection, 0, award0, std::string(empty));
            QueueAward(connection, 1, award1, std::string(empty));
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

// The zone leaving command ("<zone>_2"). When a map message is held: announces the map's title (text 6) and,
// when the map registry knows "<map>_1", queues the host's items with the final award. Then drops the map
// message, empties queuedMaps_, variables_ and files_, queues the following zones and clears the
// world's map (with null).
void ServerSession::LeaveZone() {
    auto* holder = &map_;
    if (IsLive(holder->ptr)) {
        {
            std::string title, text;
            const char* mapTitle = (*SessionMapNameInto(&title)).c_str();
            Announce((*TheInterface()->MessageText(&text, 6, mapTitle)).c_str(), nullptr);
        }

        QuestBriefingRecord* entry;
        {
            std::string key = holder->Get()->text_ + game::ZoneSuffix1Text();
            std::string lookup(key.data());
            entry = stlp::NamedSlot<QuestBriefingRecord>(worlddata::QuestBriefings(), lookup);
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
    TheGameFlow()->SetNetworkZone(nullptr);
}

} // namespace net
