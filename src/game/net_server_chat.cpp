// The game server's chat and system lines: kept on lines for the clients, at most 50. See net_classes.h and
// docs/notes/network.md.
#include "core_refcount.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "net_connection.h"
#include "net_messages.h"
#include "ui.h"
#include "world_data.h"

#include <cstring>
#include <string>

namespace net {

namespace {
constexpr std::uint32_t kMaxLines = 50;

} // namespace

// Appends a line (the next line number, the text, the connection of `player`'s binding,
// null for the server's own lines) and drops the oldest past 50.
void ServerSession::Announce(std::string_view text, RefObject* player) {
    auto* line = NewCounted<LineMessage>();
    lines.emplace_back(line);

    auto* added = lines.back().Get();
    added->text.assign(text);
    added->number = nextLine;
    ++nextLine;

    RefObject* connection = nullptr;
    if (player && !(player->flags7 & RefObject::kDetached)) {
        for (auto& node : bindings) {
            auto* binding = node.Get();
            if (binding->PlayerRef().Get() == player) {
                connection = binding->ConnectionRef().Get();
                break;
            }
        }
    }
    added->connection.Assign(connection);

    if (lines.size() > kMaxLines) lines.erase(lines.begin());
}

// Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int textKind, const char* text, RefObject* player) {
    Announce(g_interface.MessageText(textKind, text), player);
}

} // namespace net
