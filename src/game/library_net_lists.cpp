// The clients' find and the counted pointer's init (library_net_lists.h).
#include "library_net_lists.h"

#include "ei/hook.h"

#include <cstring>

namespace netlists {

void PacketRecordElement::Copy(PacketRecord* to, const PacketRecord& from) {
    to->time = from.time;
    to->sequence = from.sequence;
    to->items.Construct();
    stlp::list<SentRecord>::InsertRange<SentRecordElement>(to->items.head, from.items.First(), from.items.End());
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

ClientsIterator* ClientsIterator::FindByAddress(ClientsNode* first, ClientsNode* last, const net::NetAddress* address) {
    while (first != last && std::memcmp(&first->connection->address_, address, sizeof(net::NetAddress)) != 0)
        first = first->next;
    node = first;
    return this;
}

net::Connection** __fastcall InitClientRef(net::Connection** ref, net::Connection* connection) {
    *ref = connection;
    return ref;
}

} // namespace netlists

using namespace netlists;

EI_REWRITTEN(0x0043D780, ClientsIterator::FindByAddress);
EI_REWRITTEN(0x0043D7D0, InitClientRef);
