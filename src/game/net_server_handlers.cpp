// The game server's talk with the master server and a chat handler (A-E1): the announcement
// (0041AA10, from the tick), the master server's answer (0041ACA0, slot 10) and a client's chat
// line (0041B1C0, registered for SayMessage by ServerSession's constructor). All run from the
// session's tick on the main thread (net::Pump). Rebirth's EI_Plugin.asi patches 0041AA10 and
// 0041ACA0 (NewMaster, at 0041ABDA and 0041ACB7) and addon.dll's ChatLog has a site in 0041B1C0
// (0041B25B, disabled in this install): these are the vanilla functions, and the installer leaves
// a function with an active site alone. See docs/notes/network.md.
#include "net_classes.h"

#include "core_refcount.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_messages.h"
#include "originals_world.h"   // WorldMapTitle
#include "world.h"             // TheGameFlow

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace net {

namespace {
// The next dword of the packet (read through the cursor's pointer, as the original inlines it).
std::uint32_t TakeDword(BufferCursor* cursor) {
    std::uint32_t value;
    std::memcpy(&value, cursor->p, sizeof value);
    cursor->p += sizeof value;
    return value;
}

// The record's strings as the STLport strings they are.
stlp::string& AsString(StringRep& text) { return *reinterpret_cast<stlp::string*>(&text); }

// The bindings (+0x25A4): an STLport list of counted pointers to BindingMessage.
const stlp::list<RefPtr>& Bindings(ServerSession* server) {
    return *reinterpret_cast<const stlp::list<RefPtr>*>(&server->bindings_);
}
} // namespace

// 0041AA10. The record is built on the stack (two strings with room for 8 bytes first, as their
// constructors make them), written into a growing buffer and sent from the session's socket to
// the master's address; the result of the send is not looked at.
void ServerSession::AnnounceToMaster() {
    GameBuffer buffer;
    buffer.ConstructGrowing();
    GameInfoRecord info;
    stlp::string& name = AsString(info.name);
    stlp::string& description = AsString(info.description);
    stlp::string::Reserve(name, 8);
    stlp::string::Reserve(description, 8);

    name.Assign(name_.start, name_.finish);
    info.hasPassword = password_[0] != 0;
    info.masterKey = masterKey_;
    info.kind = static_cast<std::uint8_t>(kind_);
    const char* empty = game::EmptyText();
    description.Assign(empty, empty + std::strlen(empty));
    CRefObject* map = map_.ptr;
    if (map && !(map->flags7_ & CRefCounted::kDetached)) {
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        auto* title = static_cast<mfc::CString*>(WorldMapTitle(TheGameFlow(), nullptr, storage));
        const char* text = *title;
        description.Assign(text, text + std::strlen(text));
        title->~CString();
    }
    info.players = static_cast<std::uint8_t>(ClientCount());
    info.maxPlayers = static_cast<std::uint8_t>(maxPlayers_);
    info.masterToken = masterToken_;
    info.magic = kSessionMagic;
    info.Write(&buffer);
    socket_.SendTo(&masterAddress_, &buffer);

    description.~string();
    name.~string();
    buffer.DestructGrowing();
}
EI_REPLACE_METHOD(0x0041AA10, ServerSession::AnnounceToMaster);

// 0041ACA0 (slot 10, RET 8). A packet of kind -1: only from the master server's address (all 16
// bytes compared). Its first dword must be the session's +0x25FC; the second is then kept at
// +0x2578 (the next announcement sends it back). The first dword is consumed either way.
void ServerSession::OnMasterImpl(const NetAddress* from, BufferCursor* cursor) {
    if (std::memcmp(from, &masterAddress_, sizeof(NetAddress)) != 0) return;
    if (TakeDword(cursor) != masterKey_) return;
    masterToken_ = TakeDword(cursor);
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x0041ACA0, ServerSession::OnMasterImpl);

// 0041B1C0 (the server in ECX, the message in EDX). A chat line from the connection being read
// (0079B924; nothing when it is gone or detached): the first binding of that connection names the
// player (nothing without one, or when the binding is detached), and the line is announced to
// everybody as "^<number><name>^7: <text>".
void __fastcall ServerOnSay(ServerSession* server, SayMessage* message) {
    Connection* connection = CurrentConnection();
    if (!connection || (connection->flags7_ & CRefCounted::kDetached)) return;
    const stlp::list<RefPtr>& bindings = Bindings(server);
    for (auto* node = bindings.First(); node != bindings.End(); node = node->next) {
        auto* binding = static_cast<BindingMessage*>(node->data.ptr);
        if (binding->ConnectionRef().ptr != connection) continue;
        // (The original also tests the binding for null here, after having read it.)
        if (binding->flags7_ & CRefCounted::kDetached) return;
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        mfc::CString line;
        line.Format("^%i%s^7: %s", player->number_, player->name_.start, message->text_.start);
        server->Announce(line, nullptr);
        return;
    }
}
EI_REPLACE(0x0041B1C0, ServerOnSay);

} // namespace net
