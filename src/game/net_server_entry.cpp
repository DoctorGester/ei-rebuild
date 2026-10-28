// The game server's description of itself: a server entry like those the
// client reads from the server list (net_server_list.cpp). See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "options.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "lib/crt.h"
#include "world.h"

#include <cstring>
#include <string>

namespace net {

// Makes `*out` a new server entry: the name, the kind, the current map's title as the description when a map
// message is held (else empty), the number of clients, the maximum and whether a password is set.
void ServerSession::Describe(RefPtr* out) {
    auto* entry = NewCounted<ServerEntry>();
    if (&name_ != &entry->name_) entry->name_ = name_;
    entry->kind_ = kind_;
    std::string* description = &entry->description_;
    description->assign(game::kEmptyText);

    CRefObject* map = map_.ptr;
    if (map && !(map->flags7_ & CRefObject::kDetached)) {
        std::string title;
        const char* text = (*SessionMapNameInto(&title)).c_str();
        description->assign(text);   // inlined: overwrite, then cut or append
    }

    entry->players_ = static_cast<std::uint8_t>(ClientCount());
    entry->maxPlayers_ = static_cast<std::uint8_t>(maxPlayers_);
    entry->password_ = password_[0] != 0;

    if (entry != out->ptr) {
        if (CRefObject* old = out->ptr) {
            if (old->refs_ != 1) {
                old->flags7_ |= CRefObject::kDetached;
                old->Shutdown();
            }
            out->ptr->Release();
        }
        out->Share(entry);
    }
}
// When announced, the master's address is set from the host name and port 28004.
void ServerSession::SetPublic(bool announce) {
    public_ = announce;
    if (announce) masterAddress_.Parse(options::g_options->masterServerName_.c_str(), 0x6D64);
}

} // namespace net
