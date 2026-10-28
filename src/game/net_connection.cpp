// Connections of the network sessions: object replication. See net_connection.h.
#include "net_coder.h"
#include "net_connection.h"
#include "net_connection_trees.h"
#include "library_net_lists.h"
#include "game_strings.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_tree_abi.h"

#include <cstring>
#include "stl_sorts.h"
#include "game_data.h"

RefPtr* __fastcall RefPtrNull(RefPtr* /*p*/);
namespace net {

namespace {

using ObjectMap = trees::Objects;
using SentIdMap = trees::SentIds;
using AttachedMap = trees::Attached;
using EntryMap = Connection::EntryMap;
using RecordMap = Connection::RecordMap;

// --- STLport container instantiations (library_net_lists.h, net_connection_trees.h) -------------
// The history's, the chunk lists' and a packet's items' element policies.
using netlists::ChunkElement;
using netlists::PacketRecordElement;
using netlists::SentRecordElement;
using Chunks = stlp::list<ReliableChunk>;
using Packets = stlp::list<SentPacket>;
// CollectChanges's static vector of {object, priority}; its exit function frees the storage.
constinit stlp::vector<Replicated> g_replicated = /*0079B8D0*/ {};
std::uint8_t g_replicatedGuard = /*0079B8DC*/ {};
void __cdecl FreeReplicated() { g_replicated.FreeStorage(); }

// "text\n" and a flush to the network log, when there is one.
void LogLine(const char* text) {
    if (LogStream* log = g_netLog) log->Put(text)->PutChar('\n')->Flush();
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

// A tree iterator moved on (the library's ++).
template <typename Node>
void Increment(Node*& it) {
    it = Node::From(stlp::rb::Next(it->Base()));
}

// The handlers registered for message `type` on `channel` (__fastcall: session, message). The end
// is read again after each call.
void CallHandlers(SessionBase* session, int channel, std::uint32_t type, CRefObject* message) {
    HandlerVector& v = session->handlers_[channel][type & 0xFF];
    for (MessageHandler* h = v.begin(); h != v.end(); ++h) (*h)(session, message);
}

// A message's virtual Write and Read over a byte cursor.
void ReadObject(CRefObject* object, BufferCursor* cursor) { static_cast<Message*>(object)->Read(cursor); }

// records_.insert(hint, pair(key, Record())) as ReadRecord inlines it (the pair constructor, the
// insert); returns the new node.
RecordMap::Node* InsertRecord(RecordMap& records, RecordMap::Node* hint, std::int32_t key) {
    Record empty;
    ::RefPtrNull(&empty.object);
    empty.version = 0;   // left uninitialised by the original; overwritten by every caller
    RecordMap::ValueAbi pair;
    pair.MakeValue(key, empty);
    RecordMap::Node* inserted;
    records.InsertHint(&inserted, hint, pair);
    pair.second.object.Destroy();
    empty.object.Destroy();
    return inserted;
}

// The node for `key` in `records`, inserted when missing (the lower_bound + hinted insert idiom).
RecordMap::Node* RecordAt(RecordMap& records, std::int32_t key) {
    RecordMap::Node* lb;
    records.LowerBound(&lb, key);
    if (lb == records.end() || key < lb->first) lb = InsertRecord(records, lb, key);
    return lb;
}

} // namespace

std::uint16_t IdPool::Allocate() {
    if (free.begin() == free.end()) return ++next;
    --free.Finish();
    return *free.end();
}

void Connection::WriteAcks(std::uint8_t** p) {
    std::uint16_t base = static_cast<std::uint16_t>(lastReceived_);
    std::uint32_t mask = 0;
    std::int32_t* end = received_.end();
    std::int32_t target = static_cast<std::int32_t>(lastReceived_);
    for (int k = 1; k <= 32; ++k) {
        --target;
        std::int32_t* it = received_.begin();
        while (it != end && *it != target) ++it;
        if (it != end) mask |= 1u << (k - 1);
    }
    std::memcpy(*p, &base, 2);
    *p += 2;
    std::memcpy(*p, &mask, 4);
    *p += 4;
    // Forget sequences more than 32 behind (from the back, shifting the rest down).
    for (std::int32_t i = static_cast<std::int32_t>(received_.size()) - 1; i >= 0; --i) {
        std::int32_t* at = received_.begin() + i;
        if (static_cast<std::uint32_t>(*at) + 0x20 < lastReceived_) {
            for (std::int32_t* q = at; q + 1 < received_.end(); ++q) q[0] = q[1];
            --received_.Finish();
        }
    }
}

void Connection::QueueHeader(int budget) {
    if (headers_.Empty()) {
        int size = budget < 0xFF ? budget : 0xFF;
        Chunks::InsertDefault<ChunkElement>(headers_.End());
        std::uint8_t* chunk = headers_.End()->prev->data.bytes;
        int available = BufferedSize(&reliableOut_);
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence_, 2);
        std::memcpy(chunk + 3, reliableOut_.begin, static_cast<std::size_t>(size));
        int remaining = BufferedSize(&reliableOut_) - size;
        std::memmove(reliableOut_.begin, reliableOut_.begin + size, static_cast<std::size_t>(remaining));
        reliableOut_.cur = reliableOut_.begin;
        reliableOut_.high = reliableOut_.begin + remaining;
        if (reliableOut_.high > reliableOut_.end) reliableOut_.Grow(static_cast<std::uint32_t>(remaining));
        headerSequence_ = static_cast<std::uint16_t>(headerSequence_ + size);
        return;
    }
    // The oldest waiting chunk (sequence order with wrap-around) moves to the front.
    HeaderNode* head = headers_.End();
    HeaderNode* best = head->next;
    std::uint16_t bestSeq = best->data.Sequence();
    for (HeaderNode* n = best; n != head; n = n->next) {
        std::uint16_t seq = n->data.Sequence();
        if (static_cast<std::uint16_t>(seq - bestSeq) > 0x8000) {
            best = n;
            bestSeq = seq;
        }
    }
    if (head->next == best) return;
    Chunks::SpliceRange(headers_.First(), best, best->next);
}

void Connection::RollBack(SentPacket* packet) {
    if (packet->rolledBack) return;
    for (SentItemNode* node = packet->items.First(); node != packet->items.End(); node = node->next) {
        // Unreliable sends are forgotten (the entry is sent again); a reliable change only when
        // the entry still carries the version this packet sent.
        SentItem* item = &node->data;
        if (item->action == SentItem::kChanged) {
            if (item->entry->version_ == item->version) item->entry->version_ = 0;
        } else if (item->action == SentItem::kRemoved) {
            item->entry->version_ = 0;
        }
    }
    if (packet->header.Size() != 0) Chunks::Insert<ChunkElement>(headers_.End(), packet->header);
    packet->rolledBack = 1;
}

void Connection::AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key) {
    AttachedMap::Node* lb;
    attached_.LowerBound(&lb, object);
    if (lb == attached_.end() || object < lb->first) {
        // The pair holds the type dword in the id's place (both fields are written below).
        AttachedMap::Value pair{object, {static_cast<std::uint16_t>(type), static_cast<std::uint8_t>(type >> 16),
                                         static_cast<std::uint8_t>(type >> 24)}};
        AttachedMap::Node* inserted;
        attached_.InsertHint(&inserted, lb, pair);
        lb = inserted;
    }
    lb->second.id = static_cast<std::uint16_t>(key);
    lb->second.type = static_cast<std::uint8_t>(type);
}

bool Connection::SendUpdate(int budget) {
    bool complete = true;
    CollectChanges();
    BufferCursor c;
    c.Reset(nullptr, 0, 0);
    c.sink = nullptr;
    GameBuffer* out = SendBuffer();
    out->Seek(0);
    c.BeginWrite(out, budget + 0x400);

    *c.p = 0;   // packet kind: update
    std::uint8_t* payload = c.p + 1;
    std::uint16_t sequence = sendSequence_++;
    c.p = payload;
    std::memcpy(c.p, &sequence, 2);
    c.p += 2;
    WriteAcks(&c.p);
    QueueHeader(budget - 0x10);

    // The reliable chunk at the front of the queue goes into the packet and into its history entry.
    const ReliableChunk& chunk = headers_.First()->data;
    std::size_t chunkSize = static_cast<std::size_t>(chunk.Size()) + 3;
    std::memcpy(c.p, chunk.bytes, chunkSize);
    c.p += chunkSize;
    Packets::InsertDefault<PacketRecordElement>(history_.End());
    HistoryNode* sent = history_.End()->prev;
    std::memcpy(&sent->data.time, &clock_, sizeof(sent->data.time));
    sent->data.sequence = sequence;
    sent->data.header = chunk;
    Chunks::Erase<ChunkElement>(headers_.First());

    std::uint8_t* count = c.p++;
    *count = 0;
    for (int priority = 10; priority >= 0; --priority) {
        EntryMap& map = priorities_[priority];
        EntryMap::Node* it = map.begin();
        while (it != map.end()) {
            ReplicaEntry* entry = it->second.Get();
            Message* object = entry->Object();
            bool alive;
            if (object && !(object->flags7_ & CRefObject::kDetached)) {
                alive = true;
                if (entry->version_ == object->version_) {
                    Increment(it);
                    continue;
                }
                if (entry->id_ == 0) {
                    entry->id_ = idPools_[entry->type_].Allocate();
                    std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                    bySentId_.Subscript(key)->Assign(entry);
                }
                c.PutBits(1, 1);
                std::uint32_t id = entry->id_;
                *c.p = entry->type_;
                ++c.p;
                c.WriteBytes(&id, 2);
                entry->Object()->Write(&c);
            } else {
                alive = false;
                if (entry->id_ == 0) {
                    Increment(it);
                    continue;
                }
                if (entry->object_.ptr) {
                    CRefObject* key = entry->object_.ptr;
                    byObject_.EraseKey(key);
                    entry->object_.Assign(nullptr);
                    entry->version_ = 0;
                }
                if (entry->version_ != 0) {
                    Increment(it);
                    continue;
                }
                c.PutBits(0, 1);
                std::uint32_t id = entry->id_;
                *c.p = entry->type_;
                ++c.p;
                c.WriteBytes(&id, 2);
            }
            if (budget < static_cast<int>(c.p - payload)) {
                // Over the budget: this priority's other entries wait (the entry just written
                // stays in the packet, uncounted).
                complete = false;
                break;
            }
            ++*count;
            stlp::list<SentItem>::InsertDefault<SentRecordElement>(sent->data.items.End());
            SentItem* item = &sent->data.items.End()->prev->data;
            ReplicaEntry* old = item->entry;
            item->entry = entry;
            ++entry->refs_;
            if (old) old->Release();
            if (entry->flags_ & 1) {
                if (alive) {
                    item->action = SentItem::kChangedReliable;
                    item->version = entry->Object()->version_;
                } else {
                    item->action = SentItem::kRemovedReliable;
                }
            } else if (alive) {
                entry->version_ = entry->Object()->version_;
                item->action = SentItem::kChanged;
                item->version = entry->Object()->version_;
            } else {
                entry->version_ = 1;
                item->action = SentItem::kRemoved;
            }
            Increment(it);
        }
    }
    c.End();

    if (g_currentSession->socket_.SendTo(&address_, out)) return complete;
    // Not sent: undo what the history entry recorded and drop it.
    RollBack(&history_.End()->prev->data);
    Packets::Erase<PacketRecordElement>(history_.End()->prev);
    return false;
}

bool Connection::ReadRecord(BufferCursor* cursor, std::uint32_t version) {
    std::uint32_t created = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t id = 0;
    cursor->ReadBytes(&id, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (id & 0xFFFF));
    RecordMap::Node* found;
    records_.Find(&found, key);
    SessionBase* session = g_currentSession;

    if (!created) {
        // Removal.
        if (found == records_.end()) {
            RecordAt(records_, key)->second.version = version;
            return true;
        }
        if (found->second.version >= version) {
            LogLine(game::nettexts::SkipRemove());
            return true;
        }
        RefPtr* slot = &found->second.object;
        if (slot->ptr) {
            CRefObject* object = slot->ptr;
            attached_.EraseKey(object);
            object = slot->ptr;
            session = g_currentSession;
            CallHandlers(session, 2, type, object);
        }
        if (slot->ptr) {
            slot->Destroy();
            slot->Share(nullptr);
        }
        found->second.version = version;
        return true;
    }

    if (found == records_.end()) {
        // A new object.
        RecordMap::Node* node = RecordAt(records_, key);
        CRefObject* object = session->types_.Create(type & 0xFF);
        if (!object) return false;
        node->second.object.AssignObject(object);
        node->second.version = 0;
        AttachRecord(object, type, key);
        ReadObject(object, cursor);
        node->second.version = version;
        CallHandlers(g_currentSession, 0, type, object);
        return true;
    }

    if (found->second.version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine(game::nettexts::SkipUpdate());
        RefPtr scratch;
        scratch.Share(g_currentSession->types_.Create(type & 0xFF));
        ReadObject(scratch.ptr, cursor);
        scratch.Destroy();
        return true;
    }

    RefPtr* slot = &found->second.object;
    if (!slot->ptr) {
        CRefObject* object = g_currentSession->types_.Create(type & 0xFF);
        if (object) {
            slot->AssignObject(object);
            found->second.version = 0;
            AttachRecord(object, type, key);
        }
        if (object != slot->ptr) {
            slot->Destroy();
            slot->Share(object);
        }
        if (!slot->ptr) return false;
    }
    ReadObject(slot->ptr, cursor);
    CRefObject* object = slot->ptr;
    CallHandlers(g_currentSession, found->second.version == 0 ? 0 : 1, type, object);
    found->second.version = version;
    return true;
}

void Connection::Track(Replicated* item) {
    Message* object = item->object;
    if (!object || (object->flags7_ & CRefObject::kDetached)) return;
    TypeRegistry::ByVtable::Node* found = g_currentSession->types_.byVtable.Find(TypeRegistry::KeyOf(object));
    std::int32_t type = found ? static_cast<std::int32_t>(found->value) : -1;

    // A new entry with no version and no id yet.
    auto* entry = NewCounted<ReplicaEntry>();

    CRefObject* key = item->object;
    ObjectMap::Node* lb;
    byObject_.LowerBound(&lb, key);
    if (lb == byObject_.end() || item->object < lb->first) {
        RefPtrTo<ReplicaEntry> none;
        ::RefPtrNull(&none);
        ObjectMap::ValueAbi pair;
        pair.MakeValue(key, none);
        ObjectMap::Node* inserted;
        byObject_.InsertHint(&inserted, lb, pair);
        lb = inserted;
        if (pair.second.ptr) pair.second.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    CRefObject*& held = lb->second.ptr;
    CRefObject* old = held;
    held = entry;
    if (entry) ++held->refs_;
    if (old) old->Release();

    EntryMap& map = priorities_[static_cast<std::int8_t>(item->priority)];
    EntryMap::Node* lb2;
    map.LowerBound(&lb2, entry);
    if (lb2 == map.end() || entry < lb2->first) {
        RefPtrTo<ReplicaEntry> none;
        ::RefPtrNull(&none);
        EntryMap::ValueAbi pair;
        pair.MakeValue(entry, none);
        EntryMap::Node* inserted;
        map.InsertHint(&inserted, lb2, pair);
        lb2 = inserted;
        if (pair.second.ptr) pair.second.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    lb2->second.Assign(entry);
    entry->type_ = static_cast<std::uint8_t>(type);
    CRefObject* previous = entry->object_.ptr;
    entry->object_.ptr = item->object;
    if (entry->object_.ptr) ++entry->object_.ptr->refs_;
    if (previous) previous->Release();
    entry->flags_ = static_cast<std::uint8_t>((entry->flags_ & 1) | (item->priority << 1));
    std::uint8_t reliable = g_currentSession->reliable_[type];
    entry->flags_ = static_cast<std::uint8_t>(((reliable ^ entry->flags_) & 1) ^ entry->flags_);
}

void Connection::CollectChanges() {
    ReplicatedVector& items = g_replicated;
    if (!(g_replicatedGuard & 1)) {
        g_replicatedGuard |= 1;
        items.ConstructEmpty();
        libcrt::AtExit(FreeReplicated);
    }
    items.EraseRange<stlp::PlainElement<Replicated>>(items.begin(), items.end());
    ReplicatedVector* target = &items;
    g_currentSession->CollectReplicated(&target);
    if (items.begin() != items.end()) {
        int depth = 0;
        for (std::int32_t n = static_cast<std::int32_t>(items.size()); n != 1; n >>= 1) ++depth;
        stl_sorts::ReplicatedIntroSort(items.begin(), items.end(), nullptr, depth * 2, target);
        stl_sorts::ReplicatedFinalSort(items.begin(), items.end(), target);
    }

    // Merge the sorted list with byObject_ (both ordered by object pointer).
    Replicated* item = items.begin();
    Replicated* end = items.end();
    ObjectMap::Node* header = byObject_.end();
    ObjectMap::Node* node = byObject_.begin();
    auto entryAt = [](ObjectMap::Node* n) { return n->second.Get(); };
    if (item != end) {
        do {
            if (node == header) {
                for (; item < end; ++item) Track(item);
                return;
            }
            ReplicaEntry* entry = entryAt(node);
            CRefObject* listed = item->object;
            CRefObject* tracked = entry->object_.ptr;
            if (listed == tracked) {
                Increment(node);
                ++item;
            } else if (listed < tracked) {
                Track(item);
                ++item;
            } else {
                // Tracked but no longer listed: never sent -> forgotten; sent -> marked gone.
                ObjectMap::Node* gone = node;
                Increment(node);
                entry = entryAt(gone);
                if (entry->id_ == 0) {
                    priorities_[entry->flags_ >> 1].EraseKey(entry);
                } else {
                    CRefObject* old = entry->object_.ptr;
                    entry->object_.ptr = nullptr;
                    if (old) old->Release();
                    entry->version_ = 0;
                }
                byObject_.EraseAt(gone);
            }
        } while (item != end);
    }
    while (node != header) {
        ObjectMap::Node* gone = node;
        Increment(node);
        ReplicaEntry* entry = entryAt(gone);
        if (entry->id_ == 0) {
            priorities_[entry->flags_ >> 1].EraseKey(entry);
        } else {
            entry->object_.Assign(nullptr);
            entry->version_ = 0;
        }
        byObject_.EraseAt(gone);
    }
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

namespace {
// The entries' 16-bit reference count, as the original inlines RefPtr<ReplicaEntry> assignment:
// the old entry is deleted when its count reaches zero.
void AssignEntry(RefPtr* slot, ReplicaEntry* entry) {
    CRefObject* old = slot->ptr;
    slot->ptr = entry;
    if (entry) ++entry->refs_;
    if (old && --old->refs_ == 0) delete old;
}
} // namespace

void Connection::WriteObjectRef(BufferCursor* cursor, CRefObject* object) {
    if (IsLive(object)) {
        ObjectMap::Node* found;
        byObject_.Find(&found, object);
        if (found != byObject_.end()) {
            auto* entry = found->second.Get();
            if (entry->id_ == 0) {
                entry->id_ = idPools_[entry->type_].Allocate();
                std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                AssignEntry(bySentId_.Subscript(key), entry);
            }
            std::uint16_t id = entry->id_;
            std::uint8_t type = entry->type_;
            cursor->PutBit(0);
            cursor->WriteBytes(&type, 1);
            cursor->WriteBytes(&id, 2);
            return;
        }
        AttachedMap::Node* node;
        attached_.Find(&node, object);
        if (node != attached_.end()) {
            std::uint16_t id = node->second.id;
            std::uint8_t type = node->second.type;
            cursor->PutBit(1);
            *cursor->p++ = type;
            std::memcpy(cursor->p, &id, 2);
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
CRefObject* Connection::ReadObjectRef(BufferCursor* cursor) {
    std::uint32_t sent = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t id = 0;
    cursor->ReadBytes(&id, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (id & 0xFFFF));
    if ((id & 0xFFFF) == 0) return nullptr;
    if (sent) {
        SentIdMap::Node* node;
        bySentId_.Find(&node, key);
        if (node == bySentId_.end()) return nullptr;
        return node->second.Get()->object_.ptr;
    }
    RecordMap::Node* found;
    records_.Find(&found, key);
    if (found == records_.end()) {
        Record* record = records_.Subscript(key);
        CRefObject* object = g_currentSession->types_.Create(type & 0xFF);
        if (!object) return nullptr;
        if (object != record->object.ptr) {
            record->object.Destroy();
            record->object.Share(object);
        }
        record->version = 0;
        AttachRecord(object, type & 0xFF, key);
        return object;
    }
    if (IsLive(found->second.object.ptr)) return found->second.object.ptr;
    std::uint32_t typeKey = type & 0xFF;
    MessageFactory factory = g_currentSession->types_.byType[typeKey];
    CRefObject* object = factory ? factory() : nullptr;
    if (!object) return nullptr;
    RefPtr* slot = &found->second.object;
    if (object != slot->ptr) {
        slot->Destroy();
        slot->Share(object);
    }
    found->second.version = 0;
    AttachedId* attachedId = attached_.Subscript(object);
    attachedId->id = static_cast<std::uint16_t>(key);
    attachedId->type = static_cast<std::uint8_t>(type);
    return object;
}

} // namespace net

namespace net {

void Connection::Acknowledge(std::uint16_t sequence) {
    for (HistoryNode* node = history_.First(); node != history_.End(); node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            Packets::Erase<PacketRecordElement>(node);
            return;
        }
    }
    for (HistoryNode* node = acknowledged_.First(); node != acknowledged_.End(); node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            Packets::Erase<PacketRecordElement>(node);
            return;
        }
    }
}

// ackMask_ shifts along when the peer's newest acknowledged sequence moves.
void Connection::ReadAcks(BufferCursor* cursor) {
    std::uint16_t sequence;
    std::memcpy(&sequence, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t mask;
    std::memcpy(&mask, cursor->p, 4);
    cursor->p += 4;
    std::int32_t moved = static_cast<std::int32_t>((static_cast<std::uint32_t>(sequence) - ackBase_) & 0xFFFF);
    if (moved > 0x8000) moved -= 0x10000;
    if (moved <= 0) {
        if (moved < -0x20) ackMask_ = 0;
        else ackMask_ >>= (-moved) & 31;   // SHR by CL: a shift of 32 leaves it as it is
    } else {
        std::uint32_t shifted = (ackMask_ << 1) | 1;
        ackMask_ = shifted;
        if (moved - 1 < 0x20) ackMask_ = shifted << ((moved - 1) & 31);
        else ackMask_ = 0;
    }
    std::uint32_t fresh = ~ackMask_ & mask;
    ackBase_ = sequence;
    ackMask_ |= fresh;
    Acknowledge(sequence);
    std::uint16_t older = sequence;
    for (int k = 1; k <= 32; ++k) {
        --older;
        if (fresh & (1u << (k - 1))) Acknowledge(older);
    }
}

bool Connection::Accept(std::uint32_t sequence) {
    std::uint32_t newest = lastReceived_;
    if (sequence + 0x20 < newest) {
        if (LogStream* log = g_netLog)
            log->Put(game::nettexts::TooOld())->PutInt(static_cast<int>(newest - sequence))->PutChar('\n')->Flush();
        return false;
    }
    if (!(newest > sequence)) newest = sequence;
    lastReceived_ = newest;
    for (std::int32_t* it = received_.begin(); it != received_.end(); ++it) {
        if (*it == static_cast<std::int32_t>(sequence)) {
            if (g_netLog) {
                stlp::string peer;
                const char* text = address_.ToString(&peer)->data();
                g_netLog->Put(game::nettexts::Duplicate())->Put(text)->PutChar('\n')->Flush();
            }
            return false;
        }
    }
    auto value = static_cast<std::int32_t>(sequence);
    if (received_.end() == received_.EndOfStorage()) {
        received_.InsertOverflow<stlp::PlainElement<std::int32_t>>(received_.end(), value, 1);
    } else {
        if (received_.end()) *received_.end() = value;
        ++received_.Finish();
    }
    return true;
}

void Connection::ProcessHeaders() {
    bool progress;
    do {
        progress = false;
        HeaderNode* node = incoming_.First();
        if (node == incoming_.End()) return;
        do {
            if (node->data.Sequence() == incomingSequence_) {
                GameBuffer* in = &reliableIn_;
                if (in->cur > in->high) in->high = in->cur;
                in->Seek(in->PositionOf(in->high));
                std::uint32_t size = node->data.Size();
                const std::uint8_t* from = node->data.bytes + 3;
                if (in->cur + size > in->end) {
                    in->WriteMore(from, size);
                } else {
                    in->flags |= 1;
                    std::memcpy(in->cur, from, size);
                    in->cur += size;
                }
                incomingSequence_ = static_cast<std::uint16_t>(incomingSequence_ + node->data.Size());
                node = Chunks::Erase<ChunkElement>(node);
                progress = true;
            } else {
                node = node->next;
            }
        } while (node != incoming_.End());
    } while (progress);
}

bool Connection::ReadUpdate(BufferCursor* cursor) {
    g_currentConnection = this;
    idleTime_ = 0.0f;
    std::uint16_t low;
    std::memcpy(&low, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t sequence = low + sequenceHigh_;
    if (lastReceived_ == 0) lastReceived_ = sequence;
    std::int32_t step = static_cast<std::int32_t>(sequence - lastReceived_);
    if (step > 0x8000) {
        sequence -= 0x10000;
    } else if (step < -0x8000) {
        sequence += 0x10000;
        sequenceHigh_ += 0x10000;
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
    if (static_cast<std::uint16_t>(header.Sequence() - incomingSequence_) <= 0x8000 && header.Size() != 0) {
        Chunks::Insert<ChunkElement>(incoming_.End(), header);
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
    RecordMap::Node* it = records_.begin();
    while (it != records_.end()) {
        RefPtr* slot = &it->second.object;
        if (CRefObject* object = slot->ptr) {
            SessionBase* session = g_currentSession;
            TypeRegistry::ByVtable::Node* found = session->types_.byVtable.Find(TypeRegistry::KeyOf(object));
            std::uint32_t type = found ? found->value : 0xFFFFFFFFu;
            CallHandlers(session, 2, type, object);
        }
        if (slot->ptr) {
            slot->Destroy();
            slot->ptr = nullptr;
        }
        Increment(it);
    }
    g_currentConnection = nullptr;
}

} // namespace net
