#pragma once

// The connection's STLport lists whose elements own something (docs/notes/standalone.md): the send history, the
// freed-id batches and the reliable chunk lists, with their element policies; the session's clients' find_if by
// peer address and the counted pointer's init.

#include "net_connection.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

namespace netlists {

// ---- list<SentItem> (a packet's items, 20-byte nodes from the pool) --------------------------------

using SentRecord = net::SentItem;

// _M_create_node's copy: the action and version, the entry with one more reference (its 16-bit count; a null
// entry stays null). Destroyed with the entry's release. insert(pos) copies a default item: the game leaves
// action and version as its stack had them (every caller overwrites them); ours are zero.
struct SentRecordElement {
    static void Copy(SentRecord* to, const SentRecord& from) {
        to->action = from.action;
        to->version = from.version;
        to->entry = from.entry;
        if (from.entry) ++from.entry->refs_;
    }
    static void Destroy(SentRecord* p) {
        if (p->entry) p->entry->Release();
    }
    static void Default(SentRecord* p) {
        p->action = 0;
        p->version = 0;
        p->entry = nullptr;
    }
};

// ---- list<SentPacket> (the send history and the acknowledged packets; 0x120-byte nodes from
// operator new) -------------------------------------------------------------------------------------

using PacketRecord = net::SentPacket;

// _M_create_node's copy: the time, the sequence (not the word after it), the items by
// the list's copy constructor (a sentinel, then a copy of each item), the header's 0x104 bytes and
// the rolled-back byte (not the three after it). Destroyed by the packet's destructor: the items cleared (each
// entry released, the node back to the pool), then the items' sentinel. insert(pos) copies a
// default packet: an empty item list and rolledBack 0; the time, sequence and header
// were the game's stack bytes (the caller overwrites them), ours are zero.
struct PacketRecordElement {
    static void Copy(PacketRecord* to, const PacketRecord& from);
    static void Destroy(PacketRecord* p) { p->items.Release<SentRecordElement>(); }
    static void Default(PacketRecord* p);
};

// ---- list<vector<FreedId>> (the freed-id batches, 20-byte nodes from the pool) ---------------------

using FreedIds = net::FreedIds;

// _M_create_node's copy: vector's copy constructor (room for exactly size() ids, none when empty). Destroyed
// with its storage back to the pool, by its capacity. insert(pos) copies an empty vector.
struct FreedIdsElement {
    static void Copy(FreedIds* to, const FreedIds& from) {
        to->CopyConstruct<stlp::PlainElement<net::FreedId>>(from);
    }
    static void Destroy(FreedIds* p) { p->FreeStorage(); }
    static void Default(FreedIds* p) { p->ConstructEmpty(); }
};

// ---- list<ReliableChunk> (the waiting and out-of-order chunks; 0x10C-byte nodes from operator new)

using Chunks = stlp::list<net::ReliableChunk>;

// The chunk's bytes copied. insert(pos) copies a default chunk: the game's stack
// bytes (the caller writes the sequence, size and data); ours are zero.
struct ChunkElement {
    static void Copy(net::ReliableChunk* to, const net::ReliableChunk& from) { *to = from; }
    static void Destroy(net::ReliableChunk*) {}
    static void Default(net::ReliableChunk* p) { *p = net::ReliableChunk{}; }
};

// ---- The members -----------------------------------------------------------------------------------

using SentItemAbi = stlp::ListAbi<SentRecord, SentRecordElement>;
using SentPacketAbi = stlp::ListAbi<PacketRecord, PacketRecordElement>;
using FreedBatchAbi = stlp::ListAbi<FreedIds, FreedIdsElement>;
using ChunkAbi = stlp::ListAbi<net::ReliableChunk, ChunkElement>;

// ---- The session's clients (list<Connection*>, counted) ---------------------------------------------

struct ClientsNode {
    ClientsNode* next;
    ClientsNode* prev;
    net::Connection* connection;
};
static_assert(sizeof(ClientsNode) == sizeof(net::ClientNode));

// An iterator of the clients' list (its node), as find_if returns it.
struct ClientsIterator {
    ClientsNode* node;

    // find_if(first, last, same peer address) into this; returns this.
    ClientsIterator* FindByAddress(ClientsNode* first, ClientsNode* last, const net::NetAddress* address);
};
static_assert(sizeof(ClientsIterator) == 4);

// The clients' counted pointer made from a raw connection: stored without a reference; returns the pointer.
net::Connection** __fastcall InitClientRef(net::Connection** ref, net::Connection* connection);

} // namespace netlists
