// A connection's tick: clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md. Floating point follows the original's x87 code:
// `double` for values the original keeps on the FPU stack, static_cast<float> where it stores to a float.
#include "net_connection.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <iterator>
#include <vector>

namespace net {

namespace {

inline double D(float f) { return f; }


// The clocks are doubles read and written as their 8 bytes.
double Get(const double* d) {
    double v;
    std::memcpy(&v, d, 8);
    return v;
}
void Set(double* d, double v) { std::memcpy(d, &v, 8); }

// The reliable stream's size (the buffer's high mark follows its position first).
std::int32_t ReliableSize(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->PositionOf(b->high);
}

float RttKeep() { return 0.93f /*0073BF30*/; }   // the share of the old average kept per sample
float One() { return 1.0f /*0073B84C*/; }

} // namespace

// Advances the idle time and the clock by `dt`. Packets older than the loss timeout (3 standard deviations
// above the average round trip, at least 0.1, at most the session's maxRoundTrip_) are rolled back and move
// to the acknowledged list, where they stay for maxRoundTrip_ seconds. Sends an update every sendInterval_
// (stopping early once the reliable stream and the header queue are empty). Every idReuseTime_ seconds the
// oldest batch of freed ids returns to the id pools. False once the peer was silent for the session's
// timeout_ or the reliable stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    g_currentConnection = this;
    idleTime_ = static_cast<float>(D(idleTime_) + dt);
    Set(&clock_, dt + Get(&clock_));
    float spread = static_cast<float>(D(roundTrip_) * D(roundTrip_) - D(roundTripSquare_));
    std::uint32_t bits;
    std::memcpy(&bits, &spread, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&spread, &bits, 4);
    float loss = static_cast<float>(std::sqrt(spread) * D(3.0f /*0073BAA4*/) + D(roundTrip_));
    float tenth;
    const std::uint32_t tenthBits = 0x3DCCCCCD;   // 0.1f
    std::memcpy(&tenth, &tenthBits, 4);
    loss = loss >= tenth ? loss : tenth;
    float longest = static_cast<float>(static_cast<double>(g_currentSession->maxRoundTrip_));
    loss = longest >= loss ? loss : longest;
    float kept = static_cast<float>(static_cast<double>(g_currentSession->maxRoundTrip_));

    for (auto node = history_.begin(); node != history_.end();) {
        double age = Get(&clock_) - Get(&node->time);
        EI_NAN_AUDIT(loss, age);
        if (D(loss) >= age) break;
        RollBack(&*node);
        auto next = std::next(node);
        acknowledged_.splice(acknowledged_.end(), history_, node);
        node = next;
    }
    for (auto node = acknowledged_.begin(); node != acknowledged_.end();) {
        double age = Get(&clock_) - Get(&node->time);
        if (D(kept) < age) {
            node = acknowledged_.erase(node);
        } else {
            ++node;   // also NaN
        }
    }

    double timer = D(sendTimer_) + dt;
    sendTimer_ = static_cast<float>(timer);
    if (timer > D(sendInterval_)) {
        do {
            sendTimer_ = static_cast<float>(D(sendTimer_) - D(sendInterval_));
            if (SendUpdate(static_cast<int>(updateBudget_)) && headers_.empty()) {
                if (ReliableSize(&reliableOut_) == 0) break;
            }
        } while (sendTimer_ > sendInterval_);
    }
    if (sendTimer_ > sendInterval_) {
        double rest;
        do {
            rest = D(sendTimer_) - D(sendInterval_);
            sendTimer_ = static_cast<float>(rest);
        } while (rest > D(sendInterval_));
    }

    double freedTime = D(freedTimer_) + dt;
    freedTimer_ = static_cast<float>(freedTime);
    if (freedTime > static_cast<double>(g_currentSession->idReuseTime_)) {
        freedTimer_ = 0.0f;
        std::vector<FreedId>& oldest = freedIds_.front();
        for (FreedId* e = oldest.data(); e != oldest.data() + oldest.size(); ++e) {
            auto id = static_cast<std::uint16_t>(e->id);
            std::vector<std::uint16_t>& pool = idPools_[e->type].free;
            pool.push_back(id);
        }
        oldest.clear();
        freedIds_.pop_front();
        freedIds_.emplace_back();
    }

    bool alive = false;
    if (static_cast<double>(g_currentSession->timeout_) > D(idleTime_)) alive = ReliableSize(&reliableOut_) < 32000;
    g_currentConnection = nullptr;
    return alive;
}

} // namespace net

namespace net {

void Connection::Delivered(SentPacket* packet) {
    double sample = Get(&clock_) - Get(&packet->time);
    double kept = D(RttKeep()) * D(roundTrip_);
    roundTrip_ = static_cast<float>(kept);
    roundTrip_ = static_cast<float>((D(One()) - D(RttKeep())) * sample + kept);
    double keptSquare = D(RttKeep()) * D(roundTripSquare_);
    roundTripSquare_ = static_cast<float>(keptSquare);
    roundTripSquare_ = static_cast<float>((D(One()) - D(RttKeep())) * sample * sample + keptSquare);
    for (SentItem& sent : packet->items) {
        SentItem* item = &sent;
        auto action = static_cast<std::int32_t>(item->action);
        if (action == SentItem::kChangedReliable) {
            std::uint32_t version = item->version;
            if (item->entry->version_ > version) version = item->entry->version_;
            item->entry->version_ = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            std::map<std::uint32_t, EntryRef>& tree = priorities_[entry->flags_ >> 1];
            auto found = tree.find(entry->serial_);
            if (found != tree.end()) {
                entry = item->entry;
                std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                bySentId_.erase(key);
                FreedId value{};
                value.id = entry->id_;
                value.type = entry->type_;
                std::vector<FreedId>& batch = freedIds_.back();   // the newest batch
                batch.push_back(value);
                tree.erase(found);
            }
        }
    }
}

} // namespace net
