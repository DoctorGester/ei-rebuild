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
#include "world_data.h"

#include <cstring>

namespace net {

namespace {
// The lines list (+0x2594): a counted pointer from a raw one without a reference, end, insert,
// node count, erase (library).
__declspec(naked) RefPtr* __fastcall LineRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428030)
__declspec(naked) void** __fastcall LinesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420450)
__declspec(naked) void** __fastcall LinesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004225B0)
__declspec(naked) void __fastcall CountLines(std::uint32_t* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x004290F0)
__declspec(naked) void** __fastcall LinesErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00420460)

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
        StringInitEmpty(&line->text_);
        RefNullBOriginal(&line->connection_);
        ei::SetVtable(line, LineMessage::kVtableAddress);
    }
    void* lines = &lines_;
    RefPtr ref;
    const RefPtr* value = LineRefFromRaw(&ref, line);
    void* end;
    LinesEnd(lines, nullptr, &end);
    void* inserted;
    LinesInsert(lines, nullptr, &inserted, end, value);

    void** sentinel = static_cast<void**>(lines_);
    auto* added = static_cast<LineMessage*>(static_cast<void**>(sentinel[1])[2]);
    StringAssignRangeOriginal(&added->text_, nullptr, text, text + std::strlen(text));
    added->number_ = nextLine_;
    ++nextLine_;

    CRefObject* connection = nullptr;
    if (player && !(player->flags7_ & CRefObject::kDetached)) {
        void** bindings = static_cast<void**>(bindings_);
        for (void** node = static_cast<void**>(bindings[0]); node != bindings; node = static_cast<void**>(node[0])) {
            auto* binding = static_cast<BindingMessage*>(node[2]);
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

    std::uint32_t count = 0;
    CountLines(&count, nullptr, static_cast<void**>(lines_)[0], lines_);
    if (count > kMaxLines) {
        void* erased;
        LinesErase(lines, nullptr, &erased, static_cast<void**>(lines_)[0]);
    }
}

// 0041A490: Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int kind, const char* text, CRefObject* player) {
    std::uint32_t line = 0;   // CString
    void* formatted = TextFormat(worlddata::InterfaceManager(), nullptr, &line, kind, text);
    Announce(*static_cast<const char**>(formatted), player);
    CStringDestroy(&line);
}

EI_REPLACE_METHOD(0x0041A310, ServerSession::Announce);
EI_REPLACE_METHOD(0x0041A490, ServerSession::AnnounceText);

} // namespace net
