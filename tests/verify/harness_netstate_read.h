#pragma once

// The "netstate" step's reading (harness_netstate.cpp): the sessions' state as console lines, over our classes
// (image/harness_netstate_image.cpp). `Entry` is net::ServerEntry.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ei::verify::pair {

// The object a server list element holds: game.exe's views keep it in .ptr, the image's SharedRef gives Get().
template <class Held>
const void* HeldObject(const Held& held) {
    if constexpr (requires { held.ptr; }) return held.ptr;
    else return held.Get();
}

template <class Entry, class Server, class Client>
std::vector<std::string> NetLines(const Server* server, const Client* client) {
    std::vector<std::string> lines;
    char line[600];
    if (server) {
        unsigned connections = 0;
        for (auto it = server->clients.begin(); it != server->clients.end(); ++it) ++connections;
        std::snprintf(line, sizeof(line), "netstate: the server has %u connections", connections);
    } else {
        std::snprintf(line, sizeof(line), "netstate: no server");
    }
    lines.push_back(line);
    if (!client) {
        lines.push_back("netstate: no client");
        return lines;
    }
    const auto& servers = client->servers;
    unsigned listed = 0;
    for (auto it = servers.begin(); it != servers.end(); ++it) ++listed;
    std::snprintf(line, sizeof(line), "netstate: the client's join state %u, %u servers listed",
                  static_cast<unsigned>(client->joinState), listed);
    lines.push_back(line);
    for (const auto& held : servers) {
        const auto* entry = static_cast<const Entry*>(HeldObject(held));
        if (!entry) continue;
        const auto* ip = reinterpret_cast<const std::uint8_t*>(&entry->address.ip);
        const unsigned port = (entry->address.port >> 8 | entry->address.port << 8) & 0xFFFF;
        std::snprintf(line, sizeof(line),
                      "netstate: server %u.%u.%u.%u:%u state %u kind %u \"%s\" \"%s\" players %u/%u password %u foreign %u",
                      ip[0], ip[1], ip[2], ip[3], port, static_cast<unsigned>(entry->state),
                      static_cast<unsigned>(entry->kind), entry->name.data(), entry->description.data(),
                      static_cast<unsigned>(entry->players), static_cast<unsigned>(entry->maxPlayers),
                      static_cast<unsigned>(entry->password), static_cast<unsigned>(entry->foreign));
        lines.push_back(line);
    }
    return lines;
}

} // namespace ei::verify::pair
