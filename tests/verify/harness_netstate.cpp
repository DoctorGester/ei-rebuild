// The "netstate" step: the sessions' state as console lines (harness_pair.h, SV5d4).
#include "harness_pair.h"

#include "../../src/game/net_classes.h"

#include <cstdio>

namespace ei::verify::pair {

std::vector<std::string> NetState() {
    std::vector<std::string> lines;
    char line[600];
    if (net::ServerSession* server = net::GameServer()) {
        unsigned connections = 0;
        for (net::ClientNode* node = server->clients_->next; node != server->clients_; node = node->next) ++connections;
        std::snprintf(line, sizeof(line), "netstate: the server has %u connections", connections);
    } else {
        std::snprintf(line, sizeof(line), "netstate: no server");
    }
    lines.push_back(line);
    net::ClientSession* client = net::GameClient();
    if (!client) {
        lines.push_back("netstate: no client");
        return lines;
    }
    const stlp::SharedRefList& servers = client->servers_;
    unsigned listed = 0;
    for (net::RefNode* node = servers.First(); node != servers.End(); node = node->next) ++listed;
    std::snprintf(line, sizeof(line), "netstate: the client's join state %u, %u servers listed", client->joinState_, listed);
    lines.push_back(line);
    for (net::RefNode* node = servers.First(); node != servers.End(); node = node->next) {
        const auto* entry = static_cast<const net::ServerEntry*>(node->data.ptr);
        if (!entry) continue;
        const auto* ip = reinterpret_cast<const std::uint8_t*>(&entry->address_.ip);
        const unsigned port = (entry->address_.port >> 8 | entry->address_.port << 8) & 0xFFFF;
        std::snprintf(line, sizeof(line),
                      "netstate: server %u.%u.%u.%u:%u state %u kind %u \"%s\" \"%s\" players %u/%u password %u foreign %u",
                      ip[0], ip[1], ip[2], ip[3], port, entry->state_, entry->kind_, entry->name_.data(),
                      entry->description_.data(), entry->players_, entry->maxPlayers_, entry->password_, entry->foreign_);
        lines.push_back(line);
    }
    return lines;
}

} // namespace ei::verify::pair
