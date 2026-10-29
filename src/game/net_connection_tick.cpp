// A connection's tick: clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md. Floating point follows the original's x87 code:
// `double` for values the original keeps on the FPU stack, static_cast<float> where it stores to a float.
#include "net_connection.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cstring>
#include <iterator>
#include <vector>

namespace net {

namespace {

inline double D(float f) { return f; }


// The clocks are doubles read and written as their 8 bytes.
double Get(const double* d) {
    double v;
    v = *d;
    return v;
}
void Set(double* d, double v) { *d = v; }

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
    idleTime = static_cast<float>(D(idleTime) + dt);
    Set(&clock, dt + Get(&clock));
    float spread = static_cast<float>(D(roundTrip) * D(roundTrip) - D(roundTripSquare));
    spread = AbsBits(spread);
    float loss = static_cast<float>(std::sqrt(spread) * D(3.0f) + D(roundTrip));
    float tenth;
    tenth = 0.1f;
    loss = loss >= tenth ? loss : tenth;
    float longest = static_cast<float>(static_cast<double>(g_currentSession->maxRoundTrip));
    loss = longest >= loss ? loss : longest;
    float kept = static_cast<float>(static_cast<double>(g_currentSession->maxRoundTrip));

    for (auto node = history.begin(); node != history.end();) {
        double age = Get(&clock) - Get(&node->time);
        EI_NAN_AUDIT(loss, age);
        if (D(loss) >= age) break;
        RollBack(&*node);
        auto next = std::next(node);
        acknowledged.splice(acknowledged.end(), history, node);
        node = next;
    }
    for (auto node = acknowledged.begin(); node != acknowledged.end();) {
        double age = Get(&clock) - Get(&node->time);
        if (D(kept) < age) {
            node = acknowledged.erase(node);
        } else {
            ++node;   // also NaN
        }
    }

    double timer = D(sendTimer) + dt;
    sendTimer = static_cast<float>(timer);
    if (timer > D(sendInterval)) {
        do {
            sendTimer = static_cast<float>(D(sendTimer) - D(sendInterval));
            if (SendUpdate(static_cast<int>(updateBudget)) && headers.empty()) {
                if (ReliableSize(&reliableOut) == 0) break;
            }
        } while (sendTimer > sendInterval);
    }
    if (sendTimer > sendInterval) {
        double rest;
        do {
            rest = D(sendTimer) - D(sendInterval);
            sendTimer = static_cast<float>(rest);
        } while (rest > D(sendInterval));
    }

    double freedTime = D(freedTimer) + dt;
    freedTimer = static_cast<float>(freedTime);
    if (freedTime > static_cast<double>(g_currentSession->idReuseTime)) {
        freedTimer = 0.0f;
        std::vector<FreedId>& oldest = freedIds.front();
        for (auto& e : oldest) {
            auto entryId = static_cast<std::uint16_t>(e.id);
            std::vector<std::uint16_t>& pool = idPools[e.type].free;
            pool.push_back(entryId);
        }
        oldest.clear();
        freedIds.pop_front();
        freedIds.emplace_back();
    }

    bool alive = false;
    if (static_cast<double>(g_currentSession->timeout) > D(idleTime)) alive = ReliableSize(&reliableOut) < 32000;
    g_currentConnection = nullptr;
    return alive;
}

} // namespace net

namespace net {

void Connection::Delivered(SentPacket* packet) {
    double sample = Get(&clock) - Get(&packet->time);
    double kept = D(kRttKeep) * D(roundTrip);
    roundTrip = static_cast<float>(kept);
    roundTrip = static_cast<float>((1.0 - D(kRttKeep)) * sample + kept);
    double keptSquare = D(kRttKeep) * D(roundTripSquare);
    roundTripSquare = static_cast<float>(keptSquare);
    roundTripSquare = static_cast<float>((1.0 - D(kRttKeep)) * sample * sample + keptSquare);
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
