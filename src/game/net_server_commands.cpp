// The game server's command messages (CommandMessage) and the file requests a client
// hands in, with the host check they use. See net_messages.h and docs/notes/network.md.
#include "net_messages.h"
#include "net_connection.h"
#include "core_refcount.h"

#include "lib/crt.h"
#include "world.h"
#include "game_strings.h"

#include <cstdlib>
#include <cstring>


namespace net {

namespace {


enum CommandKind : std::uint32_t {
    kPreviewMap = 0,       // the host shows a map
    kZoneCommand = 1,      // the host picks a queued map message: "<zone>_1" / "_2" / "_3"
    kKickByName = 2,
    kBanByName = 3,
    kKickBySerial = 4,
    kBanBySerial = 5,
};

std::size_t StringNpos() { return 0xFFFFFFFFu /*0073BDE8*/; }

bool Is(const std::string& s, const char* t) {
    std::size_t length = std::strlen(t);
    return s.size() == length && std::memcmp(s.data(), t, length) == 0;
}

} // namespace

// Whether `connection` is the host's (a binding of it with a live host player).
bool ServerSession::IsHostConnection(const Connection* connection) {
    for (auto node = bindings_.begin(); node != bindings_.end(); ++node) {
        auto* binding = node->Get();
        if (binding->ConnectionRef().ptr != connection) continue;
        auto* player = binding->Player();
        if (IsLive(player) && player->host_) return true;
    }
    return false;
}

// File requests: the request keeps its sender's connection and is listed on fileRequests_ (dropped again on
// removal).
void ServerOnObjectAdded(ServerSession* self, FileRequestMessage* message) {
    CRefObject* connection = g_currentConnection;
    CRefObject* previous = message->ConnectionRef().ptr;
    message->ConnectionRef().ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
    self->fileRequests_.emplace_back(message);
}

// Commands, from the connection being read. Kinds 0-2 need the host's
// connection, 3-5 the same through IsHostConnection:
//   0: the world prepares map `text`;
//   1: when a queued map message is named `text`, its suffix picks SetMap (_1),
//      LeaveZone (_2) or CompleteZone (_3) with the name before it;
//   2 / 3: every guest whose name starts with `text` is kicked / banned;
//   4 / 5: the client with serial atoi(text) is kicked / banned.
void ServerOnCommand(ServerSession* self, CommandMessage* message) {
    std::string* text = &message->text_;
    switch (message->kind_) {
    case kPreviewMap:
        if (self->IsHostConnection(g_currentConnection)) TheGameFlow()->SetNetworkZone(text->data());
        return;
    case kZoneCommand: {
        if (!self->IsHostConnection(g_currentConnection)) return;
        auto node = self->queuedMaps_.begin();
        for (; node != self->queuedMaps_.end(); ++node) {
            auto* entry = node->Get();
            if (entry->text_ == *text) break;
        }
        if (node == self->queuedMaps_.end()) return;
        const std::size_t cut = text->size() - 2;
        std::string suffix = text->substr(cut, StringNpos());
        std::string name = text->substr(0, text->size() - 2);
        if (Is(suffix, game::ZoneSuffix1Text())) self->SetMap(&name);
        if (Is(suffix, game::ZoneSuffix2Text())) self->LeaveZone();
        if (Is(suffix, game::ZoneSuffix3Text())) self->CompleteZone(&name);
        return;
    }
    case kKickByName:
        if (!self->IsHostConnection(g_currentConnection)) return;
        for (;;) {
            Connection* connection = self->FindGuestConnection(text);
            if (!IsLive(connection)) return;
            self->Kick(connection);
        }
    case kBanByName:
        if (!self->IsHostConnection(g_currentConnection)) return;
        for (;;) {
            Connection* connection = self->FindGuestConnection(text);
            if (!IsLive(connection)) return;
            self->Ban(connection);
        }
    case kKickBySerial:
        if (self->IsHostConnection(g_currentConnection)) self->Kick(self->FindConnectionBySerial(std::atoi(text->data())));
        return;
    case kBanBySerial:
        if (self->IsHostConnection(g_currentConnection)) self->Ban(self->FindConnectionBySerial(std::atoi(text->data())));
        return;
    default:
        return;
    }
}

} // namespace net
