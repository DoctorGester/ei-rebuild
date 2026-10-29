// The game server's description of itself: a server entry like those the
// client reads from the server list (net_server_list.cpp). See net_classes.h and docs/notes/network.md.
#include "core_refcount.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_messages.h"
#include "options.h"
#include "world.h"

#include "lib/crt.h"

#include <cstring>
#include <string>

namespace net {

// Makes `*out` a new server entry: the name, the kind, the current map's title as the description when a map
// message is held (else empty), the number of clients, the maximum and whether a password is set.
void ServerSession::Describe(Ref<ServerEntry>* out) {
    auto* entry = NewCounted<ServerEntry>();
    entry->name = name;
    entry->kind = kind;
    std::string* description = &entry->description;
    description->assign(game::kEmptyText);

    RefObject* newMap = map.Get();
    if (newMap && !(newMap->flags7 & RefObject::kDetached)) description->assign(SessionMapName());    // overwrite, then cut or append

    entry->players = static_cast<std::uint8_t>(ClientCount());
    entry->maxPlayers = static_cast<std::uint8_t>(maxPlayers);
    entry->password = password[0] != 0;

    ShareAssignOwned(*out, entry);
}
// When announced, the master's address is set from the host name and port 28004.
void ServerSession::SetPublic(bool announce) {
    announced = announce;
    if (announce) masterAddress.Parse(options::g_options.masterServerName.c_str(), 0x6D64);
}

} // namespace net
