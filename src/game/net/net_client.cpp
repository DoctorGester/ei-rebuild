// The game client session. See
// net/net_session.h and docs/notes/network.md.
#include "engine/core/core_refcount.h"

#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/net/net_session.h"
#include "engine/core/coder.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"

#include <cstring>

namespace net {

// The client base: the session base, its lobby fields, an unbound socket.
ClientBase::ClientBase() {
    host = NetAddress{};
    alwaysAsk = NetAddress{};
    socket.Open(-1);
}

ClientSession::ClientSession() {
    // The local player's login.
    ShareOwned(player, new LoginMessage());

    magic = kSessionMagic;

    parties[0].Create();
    parties[1].Create();
}

// What the server receives from this client: the local player's login, the timed objects and the file
// requests, and our trade offer with its members. All priority 1.
void ClientSession::CollectReplicated(const Connection*, std::vector<Replicated>* out) {
    PushReplicated(out, player.Get(), 1);
    for (auto& n : timers) PushReplicated(out, n.ref.Get(), 1);
    for (auto& n : fileRequests) PushReplicated(out, n.Get(), 1);
    if (PartyMessage* group = party.Get()) {
        PushReplicated(out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            const auto& members = party.Get()->members;
            if (i >= static_cast<std::uint32_t>(members.size())) break;
            PushReplicated(out, members[i].Get(), 1);
        }
    }
}

// --- Life --------------------------------------------------------------------------------------------

ClientBase::~ClientBase() {}

// --- Commands ----------------------------------------------------------------------------------------

// The game client's timed entries and commands: an object kept on the timers list, a
// command message for the server, the client's own server entry, the start of the game and the ratio of a
// received buffer. See net/net_session.h and docs/notes/network.md.

// Keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(Message* object, float seconds) {
    // The list's copy shares the entry's share: a clone, as sharing marked the object copy-on-share.
    TimedEntry<SharedRef<Message>> entry(seconds, SharedRef<Message>(object));
    timers.push_back(entry);
}

// Sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(CommandKind kind, std::string_view text) {
    auto* command = new CommandMessage();
    command->kind = kind;
    // The assign of a C string: copied over the current characters, the rest cut (the
    // terminator moved down) or appended.
    command->text.assign(text);
    AddTimed(command, kTimedEntryLifetime);
}

int ClientSession::PlayerLimit() const {
    const int present = static_cast<int>(players.size());
    return static_cast<int>(maxPlayers) > present ? static_cast<int>(maxPlayers) : present;
}

// Makes `*out` an empty server entry (no name or description, no kind, no players).
void ClientSession::CreateEntry(Ref<ServerEntry>* out) { ShareAssignOwned(*out, new ServerEntry()); }

// The game flow begins the multiplayer game.
void ClientSession::OnJoined() { g_gameFlow.BeginMultiplayerGame(); }

// Called by the transfer screen: a buffer's stream size (its base plus the highest written position, from
// `begin`) over the dword at its start (1 when 0), both as signed integers; 0 for fewer than 4 bytes. Reading
// the dword moves the read position to 4 (a Seek(0), then a 4-byte read as BufferRead does it).
float ClientSession::BufferRatio(GameBuffer* buffer) {
    if (buffer->Size() < 4) return 0.0f;
    buffer->Seek(0);
    std::uint32_t stored;
    buffer->Get(stored);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return static_cast<float>(buffer->Size() / divisor);
}

} // namespace net
