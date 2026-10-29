// Connections of the network sessions: object replication. See net_connection.h.
#include "net_connection.h"

#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"
#include "net_coder.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace net {

namespace {

// "text\n" and a flush to the network log, when there is one.
void LogLine(const char* text) {
    if (console::Stream* log = g_netLog) console::EndLine(*log << text);
}

// Inlined helper of the buffers: `high` follows `cur` upwards.
void UpdateHigh(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
}

// Bytes in a buffer (written so far).
std::int32_t BufferedSize(GameBuffer* b) {
    UpdateHigh(b);
    return b->PositionOf(b->high);
}

// The handlers registered for message `type` on `channel` (called with the session and the message). The end
// is read again after each call.
void CallHandlers(SessionBase* session, int channel, std::uint32_t type, RefObject* message) {
    std::vector<MessageHandler>& v = session->handlers[channel][type & 0xFF];
    for (auto& h : v) h(session, message);
}

// A message's virtual Write and Read over a byte cursor.
void ReadObject(RefObject* object, BufferCursor* cursor) { static_cast<Message*>(object)->Read(cursor); }

} // namespace

std::uint16_t IdPool::Allocate() {
    if (free.begin() == free.end()) return ++next;
    std::uint16_t id = free.back();
    free.pop_back();
    return id;
}

void Connection::WriteAcks(std::uint8_t** p) {
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
    std::memcpy(*p, &base, 2);
    *p += 2;
    lib::StoreBytes(*p, mask);
    *p += 4;
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
        int available = BufferedSize(&reliableOut);
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence, 2);
        std::memcpy(chunk + 3, reliableOut.begin, static_cast<std::size_t>(size));
        int remaining = BufferedSize(&reliableOut) - size;
        std::memmove(reliableOut.begin, reliableOut.begin + size, static_cast<std::size_t>(remaining));
        reliableOut.cur = reliableOut.begin;
        reliableOut.high = reliableOut.begin + remaining;
        if (reliableOut.high > reliableOut.end) reliableOut.Grow(static_cast<std::uint32_t>(remaining));
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
    headers.splice(headers.begin(), headers, best);
}

void Connection::RollBack(SentPacket* packet) {
    if (packet->rolledBack) return;
    for (SentItem& item : packet->items) {
        // Unreliable sends are forgotten (the entry is sent again); a reliable change only when
        // the entry still carries the version this packet sent.
        if (item.action == SentItem::kChanged) {
            if (item.entry->version == item.version) item.entry->version = 0;
        } else if (item.action == SentItem::kRemoved) {
            item.entry->version = 0;
        }
    }
    if (packet->header.Size() != 0) headers.push_back(packet->header);
    packet->rolledBack = 1;
}

void Connection::AttachRecord(RefObject* object, std::uint32_t type, std::int32_t key) {
    AttachedId& newAttached = attached[object];
    newAttached.id = static_cast<std::uint16_t>(key);
    newAttached.type = static_cast<std::uint8_t>(type);
}

bool Connection::SendUpdate(int budget) {
    bool complete = true;
    CollectChanges();
    BufferCursor c;
    c.Reset();
    c.sink = nullptr;
    GameBuffer* out = &g_sendBuffer;
    out->Seek(0);
    c.BeginWrite(out, budget + 0x400);

    *c.p = 0;   // packet kind: update
    std::uint8_t* payload = c.p + 1;
    std::uint16_t sequence = sendSequence++;
    c.p = payload;
    std::memcpy(c.p, &sequence, 2);
    c.p += 2;
    WriteAcks(&c.p);
    QueueHeader(budget - 0x10);

    // The reliable chunk at the front of the queue goes into the packet and into its history entry.
    const ReliableChunk& chunk = headers.front();
    std::size_t chunkSize = static_cast<std::size_t>(chunk.Size()) + 3;
    std::memcpy(c.p, chunk.bytes, chunkSize);
    c.p += chunkSize;
    SentPacket& sent = history.emplace_back();
    sent.time = clock;
    sent.sequence = sequence;
    sent.header = chunk;
    headers.pop_front();

    std::uint8_t* count = c.p++;
    *count = 0;
    for (int priority = 10; priority >= 0; --priority) {
        // In the order the entries were tracked (the original: by the entry's address).
        std::map<std::uint32_t, EntryRef>& map = priorities[priority];
        for (auto& it : map) {
            ReplicaEntry* entry = it.second.Get();
            Message* object = entry->Object();
            bool alive;
            if (object && !(object->flags7 & RefObject::kDetached)) {
                alive = true;
                if (entry->version == object->version) continue;
                if (entry->id == 0) {
                    entry->id = idPools[entry->type].Allocate();
                    std::int32_t key = (static_cast<std::int32_t>(entry->type) << 16) + entry->id;
                    bySentId[key] = entry;
                }
                c.PutBits(1, 1);
                std::uint32_t entryId = entry->id;
                *c.p = entry->type;
                ++c.p;
                c.WriteBytes(&entryId, 2);
                entry->Object()->Write(&c);
            } else {
                alive = false;
                if (entry->id == 0) continue;
                if (entry->object.Get()) {
                    RefObject* key = entry->object.Get();
                    byObject.erase(key);
                    entry->object.Assign(nullptr);
                    entry->version = 0;
                }
                if (entry->version != 0) continue;
                c.PutBits(0, 1);
                std::uint32_t entryId = entry->id;
                *c.p = entry->type;
                ++c.p;
                c.WriteBytes(&entryId, 2);
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
            if (entry->flags & 1) {
                if (alive) {
                    item.action = SentItem::kChangedReliable;
                    item.version = entry->Object()->version;
                } else {
                    item.action = SentItem::kRemovedReliable;
                }
            } else if (alive) {
                entry->version = entry->Object()->version;
                item.action = SentItem::kChanged;
                item.version = entry->Object()->version;
            } else {
                entry->version = 1;
                item.action = SentItem::kRemoved;
            }
        }
    }
    c.End();

    if (g_currentSession->socket.SendTo(&address, out)) return complete;
    // Not sent: undo what the history entry recorded and drop it.
    RollBack(&history.back());
    history.pop_back();
    return false;
}

bool Connection::ReadRecord(BufferCursor* cursor, std::uint32_t version) {
    std::uint32_t created = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t objectId = 0;
    cursor->ReadBytes(&objectId, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (objectId & 0xFFFF));
    auto found = records.find(key);
    SessionBase* session = g_currentSession;

    if (!created) {
        // Removal.
        if (found == records.end()) {
            records[key].version = version;
            return true;
        }
        if (found->second.version >= version) {
            LogLine(game::nettexts::SkipRemove());
            return true;
        }
        Ref<RefObject>* slot = &found->second.object;
        if (slot->Get()) {
            RefObject* object = slot->Get();
            attached.erase(object);
            object = slot->Get();
            session = g_currentSession;
            CallHandlers(session, 2, type, object);
        }
        DestroyOwned(*slot);
        found->second.version = version;
        return true;
    }

    if (found == records.end()) {
        // A new object.
        Record& record = records[key];
        RefObject* object = session->types.Create(type & 0xFF);
        if (!object) return false;
        ShareAssignOwned(record.object, object);
        record.version = 0;
        AttachRecord(object, type, key);
        ReadObject(object, cursor);
        record.version = version;
        CallHandlers(g_currentSession, 0, type, object);
        return true;
    }

    if (found->second.version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine(game::nettexts::SkipUpdate());
        SharedRef<RefObject> scratch(g_currentSession->types.Create(type & 0xFF));
        ReadObject(scratch, cursor);
        return true;
    }

    Ref<RefObject>* slot = &found->second.object;
    if (!slot->Get()) {
        RefObject* object = g_currentSession->types.Create(type & 0xFF);
        if (object) {
            ShareAssignOwned(*slot, object);
            found->second.version = 0;
            AttachRecord(object, type, key);
        }
        ShareAssignOwned(*slot, object);
        if (!slot->Get()) return false;
    }
    ReadObject(slot->Get(), cursor);
    RefObject* object = slot->Get();
    CallHandlers(g_currentSession, found->second.version == 0 ? 0 : 1, type, object);
    found->second.version = version;
    return true;
}

void Connection::Track(Replicated* item) {
    Message* object = item->object;
    if (!object || (object->flags7 & RefObject::kDetached)) return;
    auto found = g_currentSession->types.byVtable.find(TypeRegistry::KeyOf(object));
    std::int32_t type = found != g_currentSession->types.byVtable.end() ? static_cast<std::int32_t>(found->second) : -1;

    // A new entry with no version and no id yet.
    auto* entry = NewCounted<ReplicaEntry>();

    entry->serial = ++entriesTracked;
    byObject[item->object] = entry;
    priorities[static_cast<std::int8_t>(item->priority)][entry->serial] = entry;
    entry->type = static_cast<std::uint8_t>(type);
    entry->object.Assign(item->object);
    entry->flags = static_cast<std::uint8_t>((entry->flags & 1) | (item->priority << 1));
    std::uint8_t reliable = g_currentSession->reliable[type];
    entry->flags = static_cast<std::uint8_t>(((reliable ^ entry->flags) & 1) ^ entry->flags);
}

void Connection::CollectChanges() {
    std::vector<Replicated> items;
    std::vector<Replicated>* target = &items;
    g_currentSession->CollectReplicated(&target);
    // The original sorts the list by the object's address and merges it with byObject (also by address), so
    // it tracks new objects, and its entries get their addresses, in address order. Ours goes by the list's
    // order (the session's lists), so entries are tracked, and their serials handed out, in an order no
    // allocation decides. An object listed twice would be tracked once (the original's merge tracks the second
    // listing again); the sessions list each object once.
    std::unordered_set<RefObject*> listed;
    for (const Replicated& item : items) listed.insert(item.object);

    // Tracked but no longer listed: never sent -> forgotten; sent -> marked gone. In serial order.
    std::vector<ReplicaEntry*> gone;
    for (const auto& [object, entry] : byObject)
        if (!listed.contains(object)) gone.push_back(entry.Get());
    std::sort(gone.begin(), gone.end(), [](const ReplicaEntry* a, const ReplicaEntry* b) { return a->serial < b->serial; });
    for (ReplicaEntry* entry : gone) {
        auto node = byObject.find(entry->object.Get());
        if (entry->id == 0) {
            priorities[entry->flags >> 1].erase(entry->serial);
        } else {
            entry->object.Assign(nullptr);
            entry->version = 0;
        }
        byObject.erase(node);
    }

    for (Replicated& item : items)
        if (!byObject.contains(item.object)) Track(&item);
}

} // namespace net

void BufferCursor::PutBit(std::uint32_t bit) {
    if (bits == 0) {
        bits = 1;
        bytePtr = p;
        ++p;
        acc = bit;
    } else {
        acc += bit << bits;
        ++bits;
    }
    if (bits > 8) {
        *bytePtr = static_cast<std::uint8_t>(acc);
        bits = static_cast<std::uint8_t>(bits - 8);
        bytePtr = p;
        acc >>= 8;
        ++p;
    }
}

namespace net {

void Connection::WriteObjectRef(BufferCursor* cursor, RefObject* object) {
    if (IsLive(object)) {
        auto found = byObject.find(object);
        if (found != byObject.end()) {
            ReplicaEntry* entry = found->second.Get();
            if (entry->id == 0) {
                entry->id = idPools[entry->type].Allocate();
                std::int32_t key = (static_cast<std::int32_t>(entry->type) << 16) + entry->id;
                bySentId[key] = entry;
            }
            std::uint16_t entryId = entry->id;
            std::uint8_t type = entry->type;
            cursor->PutBit(0);
            cursor->WriteBytes(&type, 1);
            cursor->WriteBytes(&entryId, 2);
            return;
        }
        auto node = attached.find(object);
        if (node != attached.end()) {
            std::uint16_t secondId = node->second.id;
            std::uint8_t type = node->second.type;
            cursor->PutBit(1);
            *cursor->p++ = type;
            std::memcpy(cursor->p, &secondId, 2);
            cursor->p += 2;
            return;
        }
    }
    std::uint32_t zero = 0;
    cursor->PutBit(0);
    *cursor->p++ = 0;
    cursor->WriteBytes(&zero, 2);
}

// The original reads the type and id into dwords whose upper bytes it masks off (and passes the
// type dword on to AttachRecord, which keeps only its low byte in use); they start as zero here.
RefObject* Connection::ReadObjectRef(BufferCursor* cursor) {
    std::uint32_t sent = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t objectId = 0;
    cursor->ReadBytes(&objectId, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (objectId & 0xFFFF));
    if ((objectId & 0xFFFF) == 0) return nullptr;
    if (sent) {
        auto node = bySentId.find(key);
        if (node == bySentId.end()) return nullptr;
        return node->second->object.Get();
    }
    auto found = records.find(key);
    if (found == records.end()) {
        Record* record = &records[key];
        RefObject* object = g_currentSession->types.Create(type & 0xFF);
        if (!object) return nullptr;
        ShareAssignOwned(record->object, object);
        record->version = 0;
        AttachRecord(object, type & 0xFF, key);
        return object;
    }
    if (IsLive(found->second.object.Get())) return found->second.object.Get();
    std::uint32_t typeKey = type & 0xFF;
    MessageFactory factory = g_currentSession->types.byType[typeKey];
    RefObject* object = factory ? factory() : nullptr;
    if (!object) return nullptr;
    Ref<RefObject>* slot = &found->second.object;
    ShareAssignOwned(*slot, object);
    found->second.version = 0;
    AttachedId* attachedId = &attached[object];
    attachedId->id = static_cast<std::uint16_t>(key);
    attachedId->type = static_cast<std::uint8_t>(type);
    return object;
}

} // namespace net

namespace net {

void Connection::Acknowledge(std::uint16_t sequence) {
    for (std::list<SentPacket>* packets : {&history, &acknowledged}) {
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
    std::memcpy(&sequence, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t mask;
    lib::LoadBytes(mask, cursor->p);
    cursor->p += 4;
    std::int32_t moved = static_cast<std::int32_t>((static_cast<std::uint32_t>(sequence) - ackBase) & 0xFFFF);
    if (moved > 0x8000) moved -= 0x10000;
    if (moved <= 0) {
        if (moved < -0x20) ackMask = 0;
        else ackMask >>= (-moved) & 31;    // SHR by CL: a shift of 32 leaves it as it is
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
        if (console::Stream* log = g_netLog) console::EndLine(*log << game::nettexts::TooOld() << newest - sequence);
        return false;
    }
    if (!(newest > sequence)) newest = sequence;
    lastReceived = newest;
    for (auto& it : received) {
        if (it == static_cast<std::int32_t>(sequence)) {
            if (g_netLog) {
                std::string peer = address.ToString();
                const char* text = peer.data();
                console::EndLine(*g_netLog << game::nettexts::Duplicate() << text);
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
                GameBuffer* in = &reliableIn;
                if (in->cur > in->high) in->high = in->cur;
                in->Seek(in->PositionOf(in->high));
                std::uint32_t size = node->Size();
                const std::uint8_t* from = node->bytes + 3;
                if (in->cur + size > in->end) {
                    in->WriteMore(from, size);
                } else {
                    in->flags |= 1;
                    std::memcpy(in->cur, from, size);
                    in->cur += size;
                }
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
    g_currentConnection = this;
    idleTime = 0.0f;
    std::uint16_t low;
    std::memcpy(&low, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t sequence = low + sequenceHigh;
    if (lastReceived == 0) lastReceived = sequence;
    std::int32_t step = static_cast<std::int32_t>(sequence - lastReceived);
    if (step > 0x8000) {
        sequence -= 0x10000;
    } else if (step < -0x8000) {
        sequence += 0x10000;
        sequenceHigh += 0x10000;
    }
    if (!Accept(sequence)) {
        g_currentConnection = nullptr;
        return true;
    }
    ReadAcks(cursor);
    ReliableChunk header;
    std::memcpy(header.bytes, cursor->p, 3);
    std::uint8_t* from = cursor->p + 3;
    cursor->p = from;
    std::memcpy(header.bytes + 3, from, header.Size());
    cursor->p = from + header.Size();
    if (static_cast<std::uint16_t>(header.Sequence() - incomingSequence) <= 0x8000 && header.Size() != 0) {
        incoming.push_back(header);
        ProcessHeaders();
    }
    std::uint8_t count = *cursor->p++;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (!ReadRecord(cursor, sequence)) {
            g_currentConnection = nullptr;
            return false;
        }
    }
    g_currentConnection = nullptr;
    return true;
}

void Connection::Close() {
    g_currentConnection = this;
    for (auto& it : records) {
        Ref<RefObject>* slot = &it.second.object;
        if (RefObject* object = slot->Get()) {
            SessionBase* session = g_currentSession;
            auto found = session->types.byVtable.find(TypeRegistry::KeyOf(object));
            std::uint32_t type = found != session->types.byVtable.end() ? found->second : 0xFFFFFFFFu;
            CallHandlers(session, 2, type, object);
        }
        DestroyOwned(*slot);
    }
    g_currentConnection = nullptr;
}

} // namespace net
