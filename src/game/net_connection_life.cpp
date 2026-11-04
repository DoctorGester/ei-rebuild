// A connection's state and reset, its tick (clocks, lost packets, sending, delayed id reuse, the timeout) and what a
// delivered packet does. See net_connection.h and docs/notes/network.md. The clocks are doubles holding float
// values: their sums are float sums, as the original's were.
#include "net_connection.h"

#include "lib/float_select.h"
#include "platform/platform.h"

#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace net {

namespace {

// The time from `then` to `now` on a connection's clock (doubles holding float values).
float Age(double now, double then) { return static_cast<float>(now) - static_cast<float>(then); }

constexpr float kRttKeep = 0.93f;   // the share of the old average kept per sample

} // namespace

ConnectionState::ConnectionState(std::uint32_t newId)
    : sendSequence(static_cast<std::uint16_t>(platform::CoarseClock())), id(newId),
      roundTrip(kMaxRoundTrip) {}

void Connection::OnShutdown() { ConnectionState::operator=(ConnectionState(0)); }

// Advances the idle time and the clock by `dt`. Packets older than the loss timeout (3 standard deviations
// above the average round trip, at least 0.1, at most kMaxRoundTrip) are rolled back and move to the
// acknowledged list, where they stay for kMaxRoundTrip seconds. Sends an update every sendInterval (stopping
// early once the reliable stream and the header queue are empty). Every kIdReuseTime seconds the older batch
// of freed ids returns to the id pools. False once the peer was silent for kPeerTimeout or the reliable
// stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    const float step = static_cast<float>(dt);
    idleTime += step;
    clock = static_cast<float>(clock) + step;
    const float spread = AbsBits(roundTrip * roundTrip - roundTripSquare);
    float loss = std::sqrt(spread) * 3.0f + roundTrip;
    const float tenth = 0.1f;
    loss = MaxKeepB(loss, tenth);
    loss = MinKeepA(kMaxRoundTrip, loss);

    auto lost = history.begin();
    for (; lost != history.end(); ++lost) {
        const float age = Age(clock, lost->time);
        if (loss >= age) break;
        RollBack(&*lost);
    }
    acknowledged.insert(acknowledged.end(), std::make_move_iterator(history.begin()), std::make_move_iterator(lost));
    history.erase(history.begin(), lost);
    std::erase_if(acknowledged, [&](const SentPacket& packet) {
        return kMaxRoundTrip < Age(clock, packet.time);   // NaN: kept
    });

    sendTimer += step;
    if (sendTimer > sendInterval) {
        do {
            sendTimer -= sendInterval;
            if (SendUpdate(static_cast<int>(updateBudget)) && headers.empty()) {
                if (reliableOut.Size() == 0) break;
            }
        } while (sendTimer > sendInterval);
    }
    while (sendTimer > sendInterval) sendTimer -= sendInterval;

    freedTimer += step;
    if (freedTimer > kIdReuseTime) {
        freedTimer = 0.0f;
        for (const FreedId& freed : freedOlder) idPools[freed.type].free.push_back(freed.id);
        freedOlder = std::move(freedNewer);
        freedNewer.clear();
    }

    bool alive = false;
    if (kPeerTimeout > idleTime) alive = reliableOut.Size() < 32000;
    return alive;
}

void Connection::Delivered(SentPacket* packet) {
    const float sample = Age(clock, packet->time);
    roundTrip = (1.0f - kRttKeep) * sample + kRttKeep * roundTrip;
    roundTripSquare = (1.0f - kRttKeep) * sample * sample + kRttKeep * roundTripSquare;
    for (const SentItem& item : packet->items) {
        if (item.action != SentItem::Action::Removed) continue;
        ReplicaEntry* entry = item.entry;
        std::map<std::uint32_t, EntryRef>& tree = priorities[entry->priority];
        auto found = tree.find(entry->serial);
        if (found == tree.end()) continue;
        bySentId.erase(RecordKey(entry->type, entry->id));
        freedNewer.push_back({entry->id, entry->type});
        tree.erase(found);
    }
}

} // namespace net
