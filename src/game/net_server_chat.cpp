// The game server's chat and system lines: kept on lines_ for the clients, at most 50. See net_classes.h and
// docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "lib/crt.h"
#include "ui.h"
#include "world_data.h"
#include "game_singletons.h"

#include <cstring>

namespace net {

namespace {
constexpr std::uint32_t kMaxLines = 50;

} // namespace

// Appends a line (the next line number, the text, the connection of `player`'s binding,
// null for the server's own lines) and drops the oldest past 50.
void ServerSession::Announce(const char* text, CRefObject* player) {
    auto* line = NewCounted<LineMessage>();
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, line);
    lines_.Insert(lines_.End(), *value);

    auto* added = lines_.Back();
    added->text_.assign(text);
    added->number_ = nextLine_;
    ++nextLine_;

    CRefObject* connection = nullptr;
    if (player && !(player->flags7_ & CRefObject::kDetached)) {
        for (RefNode* node = bindings_.First(); node != bindings_.End(); node = node->next) {
            auto* binding = bindings_.At(node);
            if (binding->PlayerRef().ptr == player) {
                connection = binding->ConnectionRef().ptr;
                break;
            }
        }
    }
    CRefObject* previous = added->connection_.ptr;
    added->connection_.ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) previous->Release();

    int count = 0;
    stlp::SharedRefList::Distance(lines_.First(), lines_.End(), count);
    if (static_cast<std::uint32_t>(count) > kMaxLines) lines_.Erase(lines_.First());
}

// Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int kind, const char* text, CRefObject* player) {
    mfc::CString line(mfc::CString::kUnset);   // constructed by the call
    mfc::CString* formatted = TheInterface()->MessageText(&line, kind, text);
    Announce(formatted->GetString(), player);
}

} // namespace net
