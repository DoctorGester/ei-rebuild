// Connections of the network sessions: object replication, then the state and tick of a connection. See
// net_connection.h.
#include "game/net/net_connection.h"

#include "game/console/console.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "engine/core/coder.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <vector>

namespace net {

namespace {

// "text\n" and a flush to the network log, when there is one.
void LogLine(const char* text) {
    if (console::Stream* log = g_netLog) console::EndLine(*log << text);
}

// An object reference on the wire: a flag bit, the type and the id.
struct WireId {
    std::uint32_t flag = 0;
    std::uint8_t type = 0;
    std::uint16_t id = 0;
};

void PutWireId(BufferCursor* cursor, const WireId& ref) {
    cursor->PutBits(ref.flag, 1);
    cursor->Put(ref.type);
    cursor->Put(ref.id);
}

WireId TakeWireId(BufferCursor* cursor) {
    WireId ref{};
    ref.flag = cursor->GetBit();
    cursor->Take(ref.type);
    cursor->Take(ref.id);
    return ref;
}

// The time from `then` to `now` on a connection's clock (doubles holding float values).
float Age(double now, double then) { return static_cast<float>(now) - static_cast<float>(then); }

constexpr float kRttKeep = 0.93f;   // the share of the old average kept per sample

} // namespace

// --- Object replication ---------------------------------------------------------------------------

std::uint16_t IdPool::Allocate() {
    if (free.begin() == free.end()) return ++next;
    std::uint16_t id = free.back();
    free.pop_back();
    return id;
}

void Connection::WriteAcks(BufferCursor* cursor) {
    std::uint16_t base = static_cast<std::uint16_t>(lastReceived);
    std::uint32_t mask = 0;
    std::int32_t* end = received.data() + received.size();
    std::int32_t target = static_cast<std::int32_t>(lastReceived);
    for (int k = 1; k <= 32; ++k) {
        --target;
        std::int32_t* it = received.data();
        while (it != end && *it != target) ++it;
        if (it != end) mask |= 1u << (k - 1);
    }
    cursor->Put(base);
    cursor->Put(mask);
    // Forget sequences more than 32 behind (from the back, shifting the rest down).
    for (std::int32_t i = static_cast<std::int32_t>(received.size()) - 1; i >= 0; --i) {
        std::int32_t* at = received.data() + i;
        if (static_cast<std::uint32_t>(*at) + 0x20 < lastReceived) {
            received.erase(received.begin() + i);
        }
    }
}

void Connection::QueueHeader(int budget) {
    if (headers.empty()) {
        int size = budget < 0xFF ? budget : 0xFF;
        std::uint8_t* chunk = headers.emplace_back().bytes;
        int available = reliableOut.Size();
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence, 2);
        std::memcpy(chunk + 3, reliableOut.begin, static_cast<std::size_t>(size));
        int remaining = reliableOut.Size() - size;
        std::memmove(reliableOut.begin, reliableOut.begin + size, static_cast<std::size_t>(remaining));
        reliableOut.SetSize(static_cast<std::uint32_t>(remaining));
        headerSequence = static_cast<std::uint16_t>(headerSequence + size);
        return;
    }
    // The oldest waiting chunk (sequence order with wrap-around) moves to the front.
    auto best = headers.begin();
    std::uint16_t bestSeq = best->Sequence();
    for (auto n = best; n != headers.end(); ++n) {
        std::uint16_t seq = n->Sequence();
        if (static_cast<std::uint16_t>(seq - bestSeq) > 0x8000) {
            best = n;
            bestSeq = seq;
        }
    }
    if (best == headers.begin()) return;
    std::rotate(headers.begin(), best, best + 1);
}

void Connection::RollBack(SentPacket* packet) {
    if (packet->rolledBack) return;
    for (SentItem& item : packet->items) {
        // The entry is sent again: a removal always, a change unless a later packet sent a newer version.
        if (item.action == SentItem::Action::Removed || item.entry->version == item.version) item.entry->version = 0;
    }
    if (packet->header.Size() != 0) headers.push_back(packet->header);
    packet->rolledBack = 1;
}

void Connection::AssignId(ReplicaEntry* entry) {
    if (entry->id != 0) return;
    entry->id = idPools[entry->type].Allocate();
    bySentId[RecordKey(entry->type, entry->id)] = entry;
}

Message* Connection::CreateRecord(Record& record, std::uint8_t type, std::uint16_t objectId) {
    Message* object = CreateMessage(type);
    if (!object) return nullptr;
    ShareAssignOwned(record.object, object);
    record.version = 0;
    attached[object] = {objectId, type};
    return object;
}

bool Connection::SendUpdate(int budget) {
    bool complete = true;
    CollectChanges();
    BufferCursor c;
    GameBuffer* out = &g_sendBuffer;
    out->Seek(0);
    c.BeginWrite(out, budget + 0x400);

    c.Put(static_cast<std::uint8_t>(PacketKind::Update));
    const std::uint8_t* payload = c.p;
    std::uint16_t sequence = sendSequence++;
    c.Put(sequence);
    WriteAcks(&c);
    QueueHeader(budget - 0x10);

    // The reliable chunk at the front of the queue goes into the packet and into its history entry.
    const ReliableChunk& chunk = headers.front();
    c.WriteBytes(chunk.bytes, static_cast<std::uint32_t>(chunk.Size()) + 3);
    SentPacket& sent = history.emplace_back();
    sent.time = clock;
    sent.sequence = sequence;
    sent.header = chunk;
    headers.erase(headers.begin());

    std::uint8_t* count = c.p;   // the number of entries, counted up below
    c.Put(std::uint8_t{0});
    for (int priority = 10; priority >= 0; --priority) {
        // In the order the entries were tracked (the original: by the entry's address).
        std::map<std::uint32_t, EntryRef>& map = priorities[priority];
        for (auto& it : map) {
            ReplicaEntry* entry = it.second.Get();
            Message* object = entry->object.Get();
            const bool alive = IsLive(object);
            if (alive) {
                if (entry->version == object->version) continue;
                AssignId(entry);
                PutWireId(&c, {1, entry->type, entry->id});
                object->Write(this, &c);
            } else {
                if (entry->id == 0) continue;
                if (object) {
                    byObject.erase(object);
                    entry->object.Reset();
                    entry->version = 0;
                }
                if (entry->version != 0) continue;
                PutWireId(&c, {0, entry->type, entry->id});
            }
            if (budget < static_cast<int>(c.p - payload)) {
                // Over the budget: this priority's other entries wait (the entry just written
                // stays in the packet, uncounted).
                complete = false;
                break;
            }
            ++*count;
            SentItem& item = sent.items.emplace_back();
            item.entry = entry;
            if (alive) {
                entry->version = object->version;
                item.version = object->version;
            } else {
                entry->version = 1;
                item.action = SentItem::Action::Removed;
            }
        }
    }
    c.End();

    if (session.socket.SendTo(&address, out)) return complete;
    // Not sent: undo what the history entry recorded and drop it.
    RollBack(&history.back());
    history.pop_back();
    return false;
}

bool Connection::ReadRecord(BufferCursor* cursor, std::uint32_t version) {
    const WireId ref = TakeWireId(cursor);
    const std::int32_t key = RecordKey(ref.type, ref.id);
    auto found = records.find(key);

    if (!ref.flag) {
        // Removal.
        if (found == records.end()) {
            records[key].version = version;
            return true;
        }
        if (found->second.version >= version) {
            LogLine("SKIP remove, newer version of object is already here");
            return true;
        }
        if (Message* object = found->second.object.Get()) {
            attached.erase(object);
            session.OnMessage(this, MessageEvent::Removed, object);
        }
        DestroyOwned(found->second.object);
        found->second.version = version;
        return true;
    }

    if (found != records.end() && found->second.version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine("SKIP update, newer version of object is already here");
        SharedRef<Message> scratch(CreateMessage(ref.type));
        scratch->Read(this, cursor);
        return true;
    }

    // A new record or one without an object gets a new object (version 0: created).
    Record& record = found != records.end() ? found->second : records[key];
    if (!record.object.Get() && !CreateRecord(record, ref.type, ref.id)) return false;
    Message* object = record.object.Get();
    object->Read(this, cursor);
    const MessageEvent event = record.version == 0 ? MessageEvent::Created : MessageEvent::Changed;
    record.version = version;
    session.OnMessage(this, event, object);
    return true;
}

void Connection::Track(const Replicated& item) {
    Message* object = item.object;
    if (!IsLive(object)) return;

    // A new entry with no version and no id yet.
    auto* entry = new ReplicaEntry();
    entry->serial = ++entriesTracked;
    entry->type = static_cast<std::uint8_t>(object->Type());
    entry->priority = item.priority;
    entry->object.Assign(object);
    byObject[object] = entry;
    priorities[entry->priority][entry->serial] = entry;
}

void Connection::CollectChanges() {
    std::vector<Replicated> items;
    session.CollectReplicated(this, &items);
    // The original sorts the list by the object's address and merges it with byObject (also by address), so
    // it tracks new objects, and its entries get their addresses, in address order. Ours goes by the list's
    // order (the session's lists), so entries are tracked, and their serials handed out, in an order no
    // allocation decides. An object listed twice would be tracked once (the original's merge tracks the second
    // listing again); the sessions list each object once.
    std::unordered_set<Message*> listed;
    for (const Replicated& item : items) listed.insert(item.object);

    // Tracked but no longer listed: never sent -> forgotten; sent -> marked gone. In serial order.
    std::vector<ReplicaEntry*> gone;
    for (const auto& [object, entry] : byObject)
        if (!listed.contains(object)) gone.push_back(entry.Get());
    std::sort(gone.begin(), gone.end(), [](const ReplicaEntry* a, const ReplicaEntry* b) { return a->serial < b->serial; });
    for (ReplicaEntry* entry : gone) {
        auto node = byObject.find(entry->object.Get());
        if (entry->id == 0) {
            priorities[entry->priority].erase(entry->serial);
        } else {
            entry->object.Reset();
            entry->version = 0;
        }
        byObject.erase(node);
    }

    for (const Replicated& item : items)
        if (!byObject.contains(item.object)) Track(item);
}

// The flag says whose id it is: 0 this side's (or none), 1 the peer's.
void Connection::WriteObjectRef(BufferCursor* cursor, Message* object) {
    if (IsLive(object)) {
        if (auto found = byObject.find(object); found != byObject.end()) {
            ReplicaEntry* entry = found->second.Get();
            AssignId(entry);
            PutWireId(cursor, {0, entry->type, entry->id});
            return;
        }
        if (auto node = attached.find(object); node != attached.end()) {
            PutWireId(cursor, {1, node->second.type, node->second.id});
            return;
        }
    }
    PutWireId(cursor, {0, 0, 0});
}

// The flag of a reference is the writer's: 1 names an object this side sent.
Message* Connection::ReadObjectRef(BufferCursor* cursor) {
    const WireId ref = TakeWireId(cursor);
    if (ref.id == 0) return nullptr;
    const std::int32_t key = RecordKey(ref.type, ref.id);
    if (ref.flag) {
        auto node = bySentId.find(key);
        return node != bySentId.end() ? node->second->object.Get() : nullptr;
    }
    Record& record = records[key];
    if (IsLive(record.object.Get())) return record.object.Get();
    return CreateRecord(record, ref.type, ref.id);
}

void Connection::Acknowledge(std::uint16_t sequence) {
    for (std::vector<SentPacket>* packets : {&history, &acknowledged}) {
        for (auto packet = packets->begin(); packet != packets->end(); ++packet) {
            if (packet->sequence == sequence) {
                Delivered(&*packet);
                packets->erase(packet);
                return;
            }
        }
    }
}

// ackMask shifts along when the peer's newest acknowledged sequence moves.
void Connection::ReadAcks(BufferCursor* cursor) {
    std::uint16_t sequence;
    cursor->Take(sequence);
    std::uint32_t mask;
    cursor->Take(mask);
    std::int32_t moved = static_cast<std::int32_t>((static_cast<std::uint32_t>(sequence) - ackBase) & 0xFFFF);
    if (moved > 0x8000) moved -= 0x10000;
    if (moved <= 0) {
        if (moved < -0x20) ackMask = 0;
        else ackMask >>= (-moved) & 31;    // a shift of 32 leaves it as it is
    } else {
        std::uint32_t shifted = (ackMask << 1) | 1;
        ackMask = shifted;
        if (moved - 1 < 0x20) ackMask = shifted << ((moved - 1) & 31);
        else ackMask = 0;
    }
    std::uint32_t fresh = ~ackMask & mask;
    ackBase = sequence;
    ackMask |= fresh;
    Acknowledge(sequence);
    std::uint16_t older = sequence;
    for (int k = 1; k <= 32; ++k) {
        --older;
        if (fresh & (1u << (k - 1))) Acknowledge(older);
    }
}

bool Connection::Accept(std::uint32_t sequence) {
    std::uint32_t newest = lastReceived;
    if (sequence + 0x20 < newest) {
        if (console::Stream* log = g_netLog) console::EndLine(*log << "SKIP too old update, dif=" << newest - sequence);
        return false;
    }
    if (!(newest > sequence)) newest = sequence;
    lastReceived = newest;
    for (auto& it : received) {
        if (it == static_cast<std::int32_t>(sequence)) {
            if (g_netLog) {
                std::string peer = address.ToString();
                const char* text = peer.data();
                console::EndLine(*g_netLog << "DUPLICATE update received from " << text);
            }
            return false;
        }
    }
    auto value = static_cast<std::int32_t>(sequence);
    received.push_back(value);
    return true;
}

void Connection::ProcessHeaders() {
    bool progress;
    do {
        progress = false;
        auto node = incoming.begin();
        if (node == incoming.end()) return;
        do {
            if (node->Sequence() == incomingSequence) {
                reliableIn.Seek(reliableIn.Size());
                reliableIn.Write(node->bytes + 3, node->Size());
                incomingSequence = static_cast<std::uint16_t>(incomingSequence + node->Size());
                node = incoming.erase(node);
                progress = true;
            } else {
                ++node;
            }
        } while (node != incoming.end());
    } while (progress);
}

bool Connection::ReadUpdate(BufferCursor* cursor) {
    idleTime = 0.0f;
    std::uint16_t low;
    cursor->Take(low);
    std::uint32_t sequence = low + sequenceHigh;
    if (lastReceived == 0) lastReceived = sequence;
    std::int32_t step = static_cast<std::int32_t>(sequence - lastReceived);
    if (step > 0x8000) {
        sequence -= 0x10000;
    } else if (step < -0x8000) {
        sequence += 0x10000;
        sequenceHigh += 0x10000;
    }
    if (!Accept(sequence)) return true;
    ReadAcks(cursor);
    ReliableChunk header;
    cursor->ReadBytes(header.bytes, 3);
    cursor->ReadBytes(header.bytes + 3, header.Size());
    if (static_cast<std::uint16_t>(header.Sequence() - incomingSequence) <= 0x8000 && header.Size() != 0) {
        incoming.push_back(header);
        ProcessHeaders();
    }
    std::uint8_t count;
    cursor->Take(count);
    for (std::uint8_t i = 0; i < count; ++i)
        if (!ReadRecord(cursor, sequence)) return false;
    return true;
}

void Connection::Close() {
    for (auto& it : records) {
        Ref<Message>* slot = &it.second.object;
        if (Message* object = slot->Get()) session.OnMessage(this, MessageEvent::Removed, object);
        DestroyOwned(*slot);
    }
}

// --- A connection's state and its tick ----------------------------------------------------------
// A connection's state and reset, its tick (clocks, lost packets, sending, delayed id reuse, the timeout) and what a
// delivered packet does. See net_connection.h and docs/notes/network.md. The clocks are doubles holding float
// values: their sums are float sums, as the original's were.

ConnectionState::ConnectionState(std::uint32_t newId)
    : sendSequence(static_cast<std::uint16_t>(platform::CoarseClock())), id(newId) {}

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
