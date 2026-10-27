// The game server's talk with the master server and a chat handler: the announcement (from the tick), the
// master server's answer and a client's chat line (registered for SayMessage by ServerSession's constructor).
// All run from the session's tick on the main thread (net::Pump). Rebirth's EI_Plugin.asi patches the
// announcement and the master answer (NewMaster; both reproduced: the answer's NOPs and the announcement's
// copy of the announcement to the second master server, net_second_master.h) and addon.dll's ChatLog has a
// site in the chat handler (disabled in this install). See docs/notes/network.md and rebirth.md.
#include "net_classes.h"

#include "core_refcount.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_messages.h"
#include "net_second_master.h"
#include "world.h"             // TheGameFlow

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace net {

namespace {
// The next dword of the packet (read through the cursor's pointer, as the original inlines it).
std::uint32_t TakeDword(BufferCursor* cursor) {
    std::uint32_t value;
    std::memcpy(&value, cursor->p, sizeof value);
    cursor->p += sizeof value;
    return value;
}


} // namespace

// The record is built on the stack (two strings with room for 8 bytes first, as their
// constructors make them), written into a growing buffer and sent from the session's socket to
// the master's address; the result of the send is not looked at.
void ServerSession::AnnounceToMaster() {
    GrowingBuffer buffer;
    GameInfoRecord info;
    stlp::string& name = info.name;
    stlp::string& description = info.description;
    stlp::string::Reserve(name, 8);
    stlp::string::Reserve(description, 8);

    name.AssignFrom(name_);
    info.hasPassword = password_[0] != 0;
    info.masterKey = masterKey_;
    info.kind = static_cast<std::uint8_t>(kind_);
    const char* empty = game::EmptyText();
    description.Assign(empty);
    CRefObject* map = map_.ptr;
    if (map && !(map->flags7_ & CRefCounted::kDetached)) {
        mfc::CString title(mfc::CString::kUnset);
        const char* text = *SessionMapNameInto(&title);
        description.Assign(text);
    }
    info.players = static_cast<std::uint8_t>(ClientCount());
    info.maxPlayers = static_cast<std::uint8_t>(maxPlayers_);
    info.masterToken = masterToken_;
    info.magic = kSessionMagic;
    info.Write(&buffer);
    socket_.SendTo(&masterAddress_, &buffer);
    // EI_Plugin's NewMaster (its call sends as SendTo does, then the copy).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster)) second_master::CopyAnnouncement(socket_.socket, buffer);

}
EI_REWRITTEN(0x0041AA10, ServerSession::AnnounceToMaster);

// A packet of kind -1: only from the master server's address (all 16 bytes compared; from any address with
// EI_Plugin's NewMaster, which NOPs a jump so the answer of its second master server counts). Its first dword
// must be the session's +0x25FC; the second is then kept (the next announcement sends it back). The first
// dword is consumed either way.
void ServerSession::OnMaster(const NetAddress* from, BufferCursor* cursor) {
    if (std::memcmp(from, &masterAddress_, sizeof(NetAddress)) != 0 &&
        !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        return;
    if (TakeDword(cursor) != masterKey_) return;
    masterToken_ = TakeDword(cursor);
}
EI_REWRITTEN(0x0041ACA0, ServerSession::OnMaster);

// A chat line from the connection being read
// (g_currentConnection; nothing when it is gone or detached): the first binding of that connection names the
// player (nothing without one, or when the binding is detached), and the line is announced to
// everybody as "^<number><name>^7: <text>".
void __fastcall ServerOnSay(ServerSession* server, SayMessage* message) {
    Connection* connection = g_currentConnection;
    if (!connection || (connection->flags7_ & CRefCounted::kDetached)) return;
    const auto& bindings = server->bindings_;
    for (auto* node = bindings.First(); node != bindings.End(); node = node->next) {
        auto* binding = bindings.At(node);
        if (binding->ConnectionRef().ptr != connection) continue;
        // (The original also tests the binding for null here, after having read it.)
        if (binding->flags7_ & CRefCounted::kDetached) return;
        auto* player = binding->Player();
        std::string line;
        line = str::Format("^%i%s^7: %s", player->number_, player->name_.data(), message->text_.data());
        server->Announce(line.c_str(), nullptr);
        return;
    }
}
EI_REWRITTEN(0x0041B1C0, ServerOnSay);

// The handler of the party members' channels 0 and 1 (ServerSession::Construct). The member message's version
// moves on, so the connections send it again.
void __fastcall ServerOnMemberChanged(ServerSession*, Message* message) { ++message->version_; }
EI_REWRITTEN(0x0041BA20, ServerOnMemberChanged);

} // namespace net
