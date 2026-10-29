// A connection's tick: clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md. Float maths: the original's sums round at the main
// thread's 24-bit precision, so its double clocks hold float values (docs/notes/cleanup-tooling.md, "The x87 files
// to SSE2").
#include "net_connection.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace net {

namespace {

// The time from `then` to `now` on a connection's clock (doubles holding float values).
float Age(double now, double then) { return static_cast<float>(now) - static_cast<float>(then); }

// The reliable stream's size (the buffer's high mark follows its position first).
std::int32_t ReliableSize(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->PositionOf(b->high);
}

constexpr float kRttKeep = 0.93f;   // the share of the old average kept per sample

} // namespace

// Advances the idle time and the clock by `dt`. Packets older than the loss timeout (3 standard deviations
// above the average round trip, at least 0.1, at most the session's maxRoundTrip) are rolled back and move
// to the acknowledged list, where they stay for maxRoundTrip seconds. Sends an update every sendInterval
// (stopping early once the reliable stream and the header queue are empty). Every idReuseTime seconds the
// oldest batch of freed ids returns to the id pools. False once the peer was silent for the session's
// timeout or the reliable stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    g_currentConnection = this;
    const float step = static_cast<float>(dt);
    idleTime += step;
    clock = static_cast<float>(clock) + step;
    const float spread = AbsBits(roundTrip * roundTrip - roundTripSquare);
    float loss = std::sqrt(spread) * 3.0f + roundTrip;
    const float tenth = 0.1f;
    loss = loss >= tenth ? loss : tenth;
    const auto longest = static_cast<float>(g_currentSession->maxRoundTrip);
    loss = longest >= loss ? loss : longest;
    const auto kept = static_cast<float>(g_currentSession->maxRoundTrip);

    auto lost = history.begin();
    for (; lost != history.end(); ++lost) {
        const float age = Age(clock, lost->time);
        EI_NAN_AUDIT(loss, age);
        if (loss >= age) break;
        RollBack(&*lost);
    }
    acknowledged.insert(acknowledged.end(), std::make_move_iterator(history.begin()), std::make_move_iterator(lost));
    history.erase(history.begin(), lost);
    std::erase_if(acknowledged, [&](const SentPacket& packet) {
        return kept < Age(clock, packet.time);   // NaN: kept
    });

    sendTimer += step;
    if (sendTimer > sendInterval) {
        do {
            sendTimer -= sendInterval;
            if (SendUpdate(static_cast<int>(updateBudget)) && headers.empty()) {
                if (ReliableSize(&reliableOut) == 0) break;
            }
        } while (sendTimer > sendInterval);
    }
    while (sendTimer > sendInterval) sendTimer -= sendInterval;

    freedTimer += step;
    if (freedTimer > static_cast<float>(g_currentSession->idReuseTime)) {
        freedTimer = 0.0f;
        std::vector<FreedId>& oldest = freedIds.front();
        for (auto& e : oldest) {
            auto entryId = static_cast<std::uint16_t>(e.id);
            std::vector<std::uint16_t>& pool = idPools[e.type].free;
            pool.push_back(entryId);
        }
        oldest.clear();
        freedIds.erase(freedIds.begin());
        freedIds.emplace_back();
    }

    bool alive = false;
    if (static_cast<float>(g_currentSession->timeout) > idleTime) alive = ReliableSize(&reliableOut) < 32000;
    g_currentConnection = nullptr;
    return alive;
}

} // namespace net

namespace net {

void Connection::Delivered(SentPacket* packet) {
    const float sample = Age(clock, packet->time);
    roundTrip = (1.0f - kRttKeep) * sample + kRttKeep * roundTrip;
    roundTripSquare = (1.0f - kRttKeep) * sample * sample + kRttKeep * roundTripSquare;
    for (SentItem& sent : packet->items) {
        SentItem* item = &sent;
        auto action = static_cast<std::int32_t>(item->action);
        if (action == SentItem::kChangedReliable) {
            std::uint32_t version = item->version;
            if (item->entry->version > version) version = item->entry->version;
            item->entry->version = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            std::map<std::uint32_t, EntryRef>& tree = priorities[entry->flags >> 1];
            auto found = tree.find(entry->serial);
            if (found != tree.end()) {
                entry = item->entry;
                std::int32_t key = (static_cast<std::int32_t>(entry->type) << 16) + entry->id;
                bySentId.erase(key);
                FreedId value{};
                value.id = entry->id;
                value.type = entry->type;
                std::vector<FreedId>& batch = freedIds.back();    // the newest batch
                batch.push_back(value);
                tree.erase(found);
            }
        }
    }
}

} // namespace net
