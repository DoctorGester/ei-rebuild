// The connection's owning lists and the clients' find (library_net_lists.h), registered at the exe's
// addresses so the standalone image has them (S12e).
#include "library_net_lists.h"

#include "ei/hook.h"

#include <cstring>

namespace netlists {

void PacketRecordElement::Copy(PacketRecord* to, const PacketRecord& from) {
    to->time = from.time;
    to->sequence = from.sequence;
    to->items.Construct();
    SentItems::InsertRange<SentRecordElement>(to->items.head, from.items.First(), from.items.End());
    to->header = from.header;
    to->rolledBack = from.rolledBack;
}

void PacketRecordElement::Default(PacketRecord* p) {
    p->time = 0.0;
    p->sequence = 0;
    p->items.Construct();
    p->header = net::ReliableChunk{};
    p->rolledBack = 0;
}

ClientsNode** __fastcall FindClientByAddress(ClientsNode** out, void*, ClientsNode* first, ClientsNode* last,
                                             const net::NetAddress* address) {
    while (first != last && std::memcmp(&first->connection->address_, address, sizeof(net::NetAddress)) != 0)
        first = first->next;
    *out = first;
    return out;
}

net::Connection** __fastcall InitClientRef(net::Connection** ref, net::Connection* connection) {
    *ref = connection;
    return ref;
}

} // namespace netlists

using namespace netlists;

