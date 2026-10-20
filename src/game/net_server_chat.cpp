// The game server's chat and system lines (0041A310, 0041A490): kept on +0x2594 for the clients,
// at most 50. See net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "ui.h"
#include "world_data.h"

#include <cstring>

namespace net {

namespace {
constexpr std::uint32_t kMaxLines = 50;

} // namespace

// 0041A310: appends a line (the next line number, the text, the connection of `player`'s binding,
// null for the server's own lines) and drops the oldest past 50.
void ServerSession::Announce(const char* text, CRefObject* player) {
    auto* line = NewMessageMemory<LineMessage>();
    if (line) {
        RefValueConstructBase(line);
        ei::SetVtable(line, kMessageVtable);
        line->version_ = 1;
        stlp::StringElement::Default(&line->text_);
        line->connection_.ptr = nullptr;   // RefPtr() (0041FF70)
        ei::SetVtable(line, LineMessage::kVtableAddress);
    }
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, line);   // 00428030
    lines_.Insert(lines_.End(), *value);

    auto* added = static_cast<LineMessage*>(lines_.End()->prev->data.ptr);
    added->text_.Assign(text);
    added->number_ = nextLine_;
    ++nextLine_;

    CRefObject* connection = nullptr;
    if (player && !(player->flags7_ & CRefObject::kDetached)) {
        for (RefNode* node = bindings_.First(); node != bindings_.End(); node = node->next) {
            auto* binding = static_cast<BindingMessage*>(node->data.ptr);
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

// 0041A490: Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int kind, const char* text, CRefObject* player) {
    std::uint32_t line = 0;   // CString
    void* formatted = worlddata::InterfaceManager()->MessageText(&line, kind, text);
    Announce(*static_cast<const char**>(formatted), player);
    CStringDestroy(&line);
}

EI_REPLACE_METHOD(0x0041A310, ServerSession::Announce);
EI_REPLACE_METHOD(0x0041A490, ServerSession::AnnounceText);

} // namespace net
