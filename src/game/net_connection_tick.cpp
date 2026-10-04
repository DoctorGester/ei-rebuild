// A connection's tick (00433BD0): clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/unmapped_410_470.md ("Sixth round").
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>

#pragma float_control(precise, on)

namespace net {

namespace {

inline double D(float f) { return f; }

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The history / acknowledged lists (STLport list<HistoryNode>): end, splice of one node before
// `pos`, erase.
__declspec(naked) void** __fastcall HistoryEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00437F20)
__declspec(naked) void __fastcall HistorySplice(void* /*list*/, void*, void* /*pos*/, void* /*from*/, void* /*node*/) EI_ORIGINAL(0x004380C0)
__declspec(naked) void* __fastcall HistoryEraseAt(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00437F30)
// The freed-id queue (list of vector<{uint16 id, uint8 type}>): erase, insert a default element.
__declspec(naked) void* __fastcall FreedErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x004390C0)
__declspec(naked) void* __fastcall FreedInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00439000)
// vector<uint16_t> pieces of the id pools: construct in place, insert when full; vector copy of
// 4-byte elements (returns the end of the copy).
__declspec(naked) void __fastcall IdConstruct(std::uint16_t* /*at*/, const void* /*value*/) EI_ORIGINAL(0x0043D7E0)
__declspec(naked) void __fastcall IdInsertOverflow(IdPool* /*pool*/, void*, std::uint16_t* /*pos*/, const void* /*value*/,
                                                   std::uint32_t /*n*/) EI_ORIGINAL(0x0043B530)
__declspec(naked) std::uint8_t* __fastcall CopyFreed(std::uint8_t* /*first*/, std::uint8_t* /*last*/, std::uint8_t* /*dest*/) EI_ORIGINAL(0x0043DC80)

SessionBase*& SessionRef() { return *reinterpret_cast<SessionBase**>(0x0079B920); }
Connection*& ConnectionRef() { return *reinterpret_cast<Connection**>(0x0079B924); }
int SessionInt(std::size_t offset) {
    int v;
    std::memcpy(&v, reinterpret_cast<std::uint8_t*>(SessionRef()) + offset, 4);
    return v;
}

double Get(const std::uint32_t* d) {
    double v;
    std::memcpy(&v, d, 8);
    return v;
}
void Set(std::uint32_t* d, double v) { std::memcpy(d, &v, 8); }

// The reliable stream's size (the buffer's high mark follows its position first).
std::int32_t ReliableSize(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

// Float bit select as the original writes min / max: `pick` when the flag holds, else `other`.
float Select(bool flag, float pick, float other) { return flag ? pick : other; }

} // namespace

// 00433BD0: advances the idle time and the clock by `dt`. Packets older than the loss timeout
// (3 standard deviations above the average round trip, at least 0.1, at most the session's +0x10)
// are rolled back and move to the acknowledged list, where they stay for +0x10 seconds. Sends an
// update every sendInterval (stopping early once the reliable stream and the header queue are
// empty). Every +0x14 seconds the oldest batch of freed ids returns to the id pools. False once
// the peer was silent for +0x0C seconds or the reliable stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    ConnectionRef() = this;
    idleTime = Store(D(idleTime) + dt);
    Set(clock, dt + Get(clock));
    float spread = Store(D(roundTrip) * D(roundTrip) - D(roundTripSquare));
    std::uint32_t bits;
    std::memcpy(&bits, &spread, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&spread, &bits, 4);
    float loss = Store(x87::fsqrt(spread) * D(GameFloat(0x0073BAA4)) + D(roundTrip));
    float tenth;
    const std::uint32_t tenthBits = 0x3DCCCCCD;   // 0.1f
    std::memcpy(&tenth, &tenthBits, 4);
    loss = Select(!(loss >= tenth), tenth, loss);
    float longest = Store(static_cast<double>(SessionInt(0x10)));
    loss = Select(!(longest >= loss), longest, loss);
    float kept = Store(static_cast<double>(SessionInt(0x10)));

    for (auto* node = *static_cast<HistoryNode**>(history); node != history;) {
        double age = Get(clock) - Get(node->time);
        if (!(D(loss) < age)) break;
        RollBack(reinterpret_cast<std::uint8_t*>(node) + 8);
        HistoryNode* next = node->next;
        void* end;
        HistoryEnd(&acknowledged, nullptr, &end);
        HistorySplice(&acknowledged, nullptr, end, &history, node);
        node = next;
        if (node == history) break;
    }
    for (auto* node = *static_cast<HistoryNode**>(acknowledged); node != acknowledged;) {
        double age = Get(clock) - Get(node->time);
        if (!(D(kept) < age)) {
            node = node->next;
        } else {
            void* after;
            HistoryEraseAt(&acknowledged, nullptr, &after, node);
            node = static_cast<HistoryNode*>(after);
        }
    }

    double timer = D(sendTimer) + dt;
    sendTimer = Store(timer);
    if (timer > D(sendInterval)) {
        do {
            sendTimer = Store(D(sendTimer) - D(sendInterval));
            if (SendUpdate(static_cast<int>(updateBudget)) && *static_cast<void**>(headers) == headers) {
                if (ReliableSize(&reliableOut) == 0) break;
            }
        } while (sendTimer > sendInterval);
    }
    if (sendTimer > sendInterval) {
        double rest;
        do {
            rest = D(sendTimer) - D(sendInterval);
            sendTimer = Store(rest);
        } while (rest > D(sendInterval));
    }

    double freedTime = D(freedTimer) + dt;
    freedTimer = Store(freedTime);
    if (freedTime > static_cast<double>(SessionInt(0x14))) {
        freedTimer = 0.0f;
        auto* first = *static_cast<std::uint8_t**>(freedIds);
        auto** ids = reinterpret_cast<std::uint8_t**>(first + 8);   // vector: begin, end, eos
        for (std::uint8_t* e = ids[0]; e != ids[1]; e += 4) {
            std::uint32_t value = 0;
            std::memcpy(&value, e, 2);
            IdPool& pool = idPools[e[2]];
            if (pool.end != pool.eos) {
                IdConstruct(pool.end, &value);
                ++pool.end;
            } else {
                IdInsertOverflow(&pool, nullptr, pool.end, &value, 1);
            }
        }
        ids[1] = CopyFreed(ids[1], ids[1], ids[0]);
        void* out;
        FreedErase(&freedIds, nullptr, &out, *static_cast<void**>(freedIds));
        FreedInsert(&freedIds, nullptr, &out, freedIds);
    }

    bool alive = false;
    if (static_cast<double>(SessionInt(0x0C)) > D(idleTime)) alive = ReliableSize(&reliableOut) < 32000;
    ConnectionRef() = nullptr;
    return alive;
}
namespace {
bool __fastcall TickHook(Connection* self, void*, double dt) { return self->Tick(dt); }
} // namespace
EI_REPLACE(0x00433BD0, TickHook);

} // namespace net

namespace net {

namespace {
__declspec(naked) void* __fastcall PriorityFind(RbTree* /*tree*/, void*, void** /*out*/, ReplicaEntry* const* /*key*/) EI_ORIGINAL(0x0043C9A0)
__declspec(naked) void __fastcall PriorityEraseAt(RbTree* /*tree*/, void*, void* /*position*/) EI_ORIGINAL(0x00437EF0)
__declspec(naked) std::uint32_t __fastcall BySentIdErase(void* /*map*/, void*, const std::uint32_t* /*key*/) EI_ORIGINAL(0x0043BE60)
// vector<{uint16 id, uint8 type}> of a freed-id batch: construct in place (4 bytes), insert when full.
__declspec(naked) void __fastcall FreedConstruct(std::uint8_t* /*at*/, const void* /*value*/) EI_ORIGINAL(0x0043D7F0)
__declspec(naked) void __fastcall FreedInsertOverflow(std::uint8_t** /*vector*/, void*, std::uint8_t* /*pos*/, const void* /*value*/,
                                                      std::uint32_t /*n*/) EI_ORIGINAL(0x0043B6C0)
#define EI_RTT_KEEP GameFloat(0x0073BF30)   // the share of the old average kept per sample
#define EI_ONE GameFloat(0x0073B84C)        // 1.0f
} // namespace

void Connection::Delivered(std::uint8_t* historyData) {
    auto* node = reinterpret_cast<HistoryNode*>(historyData - 8);
    double sample = Get(clock) - Get(node->time);
    double kept = D(EI_RTT_KEEP) * D(roundTrip);
    roundTrip = Store(kept);
    roundTrip = Store((D(EI_ONE) - D(EI_RTT_KEEP)) * sample + kept);
    double keptSquare = D(EI_RTT_KEEP) * D(roundTripSquare);
    roundTripSquare = Store(keptSquare);
    roundTripSquare = Store((D(EI_ONE) - D(EI_RTT_KEEP)) * sample * sample + keptSquare);
    for (SentItem* item = node->items->next; item != node->items; item = item->next) {
        auto action = static_cast<std::int32_t>(item->action);
        if (action == 1) {
            std::uint32_t version = item->version;
            if (item->entry->version > version) version = item->entry->version;
            item->entry->version = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            RbTree* tree = &priorities[entry->flags >> 1];
            void* found;
            PriorityFind(tree, nullptr, &found, &entry);
            if (found != tree->header) {
                entry = item->entry;
                std::uint32_t key = (static_cast<std::uint32_t>(entry->type) << 16) + entry->id;
                BySentIdErase(bySentId, nullptr, &key);
                std::uint8_t value[4] = {};
                std::memcpy(value, &entry->id, 2);
                value[2] = entry->type;
                auto* batch = static_cast<std::uint8_t**>(freedIds)[1];   // the newest batch
                auto** ids = reinterpret_cast<std::uint8_t**>(batch + 8);  // begin, end, eos
                if (ids[1] != ids[2]) {
                    FreedConstruct(ids[1], value);
                    ids[1] += 4;
                } else {
                    FreedInsertOverflow(ids, nullptr, ids[1], value, 1);
                }
                PriorityEraseAt(tree, nullptr, found);
            }
        }
    }
}
EI_REPLACE_METHOD(0x00433FA0, Connection::Delivered);

} // namespace net
