#pragma once

// The "netstate" step's reading (harness_netstate.cpp): the sessions' state as console lines, over either side's
// classes. game.exe's runs read game.exe's sessions through their views (exe_objects.h); the image reads ours
// (image/harness_netstate_image.cpp). `Entry` is the side's net::ServerEntry.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ei::verify::pair {

template <class Entry, class Server, class Client>
std::vector<std::string> NetLines(const Server* server, const Client* client) {
    std::vector<std::string> lines;
    char line[600];
    if (server) {
        unsigned connections = 0;
        for (auto* node = server->clients_->next; node != server->clients_; node = node->next) ++connections;
        std::snprintf(line, sizeof(line), "netstate: the server has %u connections", connections);
    } else {
        std::snprintf(line, sizeof(line), "netstate: no server");
    }
    lines.push_back(line);
    if (!client) {
        lines.push_back("netstate: no client");
        return lines;
    }
    const auto& servers = client->servers_;
    unsigned listed = 0;
    for (auto* node = servers.First(); node != servers.End(); node = node->next) ++listed;
    std::snprintf(line, sizeof(line), "netstate: the client's join state %u, %u servers listed",
                  static_cast<unsigned>(client->joinState_), listed);
    lines.push_back(line);
    for (auto* node = servers.First(); node != servers.End(); node = node->next) {
        const auto* entry = static_cast<const Entry*>(node->data.ptr);
        if (!entry) continue;
        const auto* ip = reinterpret_cast<const std::uint8_t*>(&entry->address_.ip);
        const unsigned port = (entry->address_.port >> 8 | entry->address_.port << 8) & 0xFFFF;
        std::snprintf(line, sizeof(line),
                      "netstate: server %u.%u.%u.%u:%u state %u kind %u \"%s\" \"%s\" players %u/%u password %u foreign %u",
                      ip[0], ip[1], ip[2], ip[3], port, static_cast<unsigned>(entry->state_),
                      static_cast<unsigned>(entry->kind_), entry->name_.data(), entry->description_.data(),
                      static_cast<unsigned>(entry->players_), static_cast<unsigned>(entry->maxPlayers_),
                      static_cast<unsigned>(entry->password_), static_cast<unsigned>(entry->foreign_));
        lines.push_back(line);
    }
    return lines;
}

} // namespace ei::verify::pair
