// The game server's chat and system lines (0041A310, 0041A490): kept on +0x2594 for the clients,
// at most 50. See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall RefNull(void* /*ref*/) EI_ORIGINAL(0x0041FF70)
__declspec(naked) void __fastcall StringAssignRange(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00426A80)
// The lines list (+0x2594): a counted pointer from a raw one without a reference, end, insert,
// node count, erase (library).
__declspec(naked) RefPtr* __fastcall LineRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428030)
__declspec(naked) void** __fastcall LinesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420450)
__declspec(naked) void** __fastcall LinesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004225B0)
__declspec(naked) void __fastcall CountLines(std::uint32_t* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x004290F0)
__declspec(naked) void** __fastcall LinesErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00420460)
__declspec(naked) void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/) EI_ORIGINAL(0x005F8C90)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)

constexpr std::uintptr_t kLineVtable = 0x0073BC88;   // 0x20 bytes
constexpr std::uintptr_t kTextTable = 0x007C7D50;
constexpr std::uint32_t kMaxLines = 50;

void SetVtable(void* p, std::uintptr_t v) { std::memcpy(p, &v, 4); }

} // namespace

// 0041A310: appends a line (vtable 0073BC88: +0xC the next line number (+0x258C), +0x10 the text,
// +0x1C the connection of `player`'s binding, null for the server's own lines) and drops the
// oldest past 50.
void ServerSession::Announce(const char* text, CRefObject* player) {
    auto* line = static_cast<std::uint8_t*>(gamecrt::operator_new(0x20));
    std::uint32_t zero = 0;
    std::memcpy(line + 4, &zero, 4);
    if (line) {
        RefValueConstructBase(line);
        SetVtable(line, kMessageVtable);
        std::uint32_t one = 1;
        std::memcpy(line + 8, &one, 4);
        StringConstruct(reinterpret_cast<StringRep*>(line + 0x10));
        RefNull(line + 0x1C);
        SetVtable(line, kLineVtable);
    }
    void* lines = &lines_;
    RefPtr ref;
    const RefPtr* value = LineRefFromRaw(&ref, line);
    void* end;
    LinesEnd(lines, nullptr, &end);
    void* inserted;
    LinesInsert(lines, nullptr, &inserted, end, value);

    void** sentinel = static_cast<void**>(lines_);
    auto* added = static_cast<std::uint8_t*>(static_cast<void**>(sentinel[1])[2]);
    StringAssignRange(reinterpret_cast<StringRep*>(added + 0x10), nullptr, text, text + std::strlen(text));
    std::memcpy(added + 0xC, &nextLine_, 4);
    ++nextLine_;

    CRefObject* connection = nullptr;
    if (player && !(player->flags7_ & CRefObject::kDetached)) {
        void** bindings = static_cast<void**>(bindings_);
        for (void** node = static_cast<void**>(bindings[0]); node != bindings; node = static_cast<void**>(node[0])) {
            auto* binding = static_cast<std::uint8_t*>(node[2]);
            CRefObject* bound;
            std::memcpy(&bound, binding + 0xC, 4);
            if (bound == player) {
                std::memcpy(&connection, binding + 0x10, 4);
                break;
            }
        }
    }
    CRefObject* previous;
    std::memcpy(&previous, added + 0x1C, 4);
    std::memcpy(added + 0x1C, &connection, 4);
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
    void* formatted = TextFormat(reinterpret_cast<void*>(kTextTable), nullptr, &line, kind, text);
    Announce(*static_cast<const char**>(formatted), player);
    CStringDestroy(&line);
}

namespace {
void __fastcall AnnounceHook(ServerSession* self, void*, const char* text, CRefObject* player) { self->Announce(text, player); }
void __fastcall AnnounceTextHook(ServerSession* self, void*, int kind, const char* text, CRefObject* player) {
    self->AnnounceText(kind, text, player);
}
} // namespace
EI_REPLACE(0x0041A310, AnnounceHook);
EI_REPLACE(0x0041A490, AnnounceTextHook);

} // namespace net
