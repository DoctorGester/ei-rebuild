// The game server's awards: the zone completion command ("<zone>_3"), the host's item awards and the queued
// award message, plus the counted pointer copy they use. See net_classes.h and docs/notes/network.md.

#include "core_refcount.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_connection.h"
#include "net_messages.h"
#include "quest_records.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"

#include "lib/crt.h"

#include "ei/x87.h"

#include <cstring>
#include <string>
#include <vector>

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

namespace net {

// Queues an award message for `connection` on the pending awards with a 60 second delay. `text` is the
// caller's string (made in the argument slot), destroyed here as the callee destroys a by-value argument.
void ServerSession::QueueAward(CRefObject* connection, int kind, float value, std::string text) {
    auto* award = NewCounted<AwardMessage>();
    CRefObject* previous = award->connection_.ptr;
    award->connection_.ptr = connection;
    if (connection) connection->AddRef();
    if (previous) previous->Release();
    award->kind_ = static_cast<std::uint32_t>(kind);
    x87::CopyBits(award->value_, value);

    // The text, assigned in place (string::operator= inlined: overwrite, then cut or append).
    award->text_ = text;

    // Shared three times over: the list keeps a clone of the entry's clone (each share marks the object
    // copy-on-share); the entry's and then the first share are destroyed.
    SharedRefPtr shared(award);
    TimedEntry<SharedRefPtr> entry(kAwardDelay, shared);
    awards_.push_back(entry);
}

// Finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (its given items)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const QuestBriefingRecord* entry, bool withFinal) {
    auto sentinel = bindings_.end();
    auto node = bindings_.begin();
    for (;;) {
        if (node == sentinel) return;
        auto* binding = node->Get();
        auto* player = binding->Player();
        if (IsLive(player) && player->host_) break;
        ++node;
    }
    CRefObject* connection = node->Get()->ConnectionRef().ptr;
    if (!IsLive(connection)) return;

    const std::vector<std::string>& items = entry->lists[0];   // the items given
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
    if (withFinal) QueueAward(connection, 4, 0.0f, std::string(game::kEmptyText));
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
            std::string title;
            const char* mapTitle = (*SessionMapNameInto(&title)).c_str();
            Announce(g_interface.MessageText(7, mapTitle), nullptr);
        }
    }

    QuestBriefingRecord* entry;
    {
        std::string key = *zone + game::ZoneSuffix3Text();
        entry = worlddata::QuestBriefings()[key];
    }
    if (entry) {
        int count = static_cast<int>(clients_.size());
        int share = count < 1 ? 1 : count;
        float value0, value1;
        x87::CopyBits(value0, entry->exp);
        x87::CopyBits(value1, entry->money);
        const double divisor = share;
        float award0 = static_cast<float>(value0 / divisor);
        float award1 = static_cast<float>(value1 / divisor);

        for (auto& node : clients_) {
            CRefObject* connection = node.ptr;
            const char* empty = game::kEmptyText;
            QueueAward(connection, 0, award0, std::string(empty));
            QueueAward(connection, 1, award1, std::string(empty));
        }
        AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    queuedMaps_.clear();
    variables_.clear();
    files_.clear();
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
            std::string title;
            const char* mapTitle = (*SessionMapNameInto(&title)).c_str();
            Announce(g_interface.MessageText(6, mapTitle), nullptr);
        }

        QuestBriefingRecord* entry;
        {
            std::string key = holder->Get()->text_ + game::ZoneSuffix1Text();
            std::string lookup(key.data());
            entry = worlddata::QuestBriefings()[lookup];
        }
        if (entry) AwardHost(entry, true);
    }

    if (holder->ptr) {
        DestroyInline(holder);
        holder->Share(nullptr);
    }
    queuedMaps_.clear();
    variables_.clear();
    files_.clear();
    QueueMapList();
    g_gameFlow->SetNetworkZone(nullptr);
}

} // namespace net
