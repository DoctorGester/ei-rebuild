// The game server's description of itself (0041A7E0, slot 15): a server entry like those the
// client reads from the server list (net_server_list.cpp). See net_classes.h and
// docs/notes/network.md.
#include "net_classes.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_world.h"
#include "world.h"

#include <cstring>

namespace net {

namespace {



} // namespace

// 0041A7E0 (slot 15): makes `*out` a new server entry: the name, the kind, the current map's title
// as the description when a map message is held (else empty), the number of clients, the maximum
// and whether a password is set.
void ServerSession::DescribeImpl(RefPtr* out) {
    auto* entry = NewMessageMemory<ServerEntry>();
    if (entry) {
        entry->ConstructBase();
        std::uint8_t allocator = 0;
        StringReserveOriginal(&entry->name_, nullptr, &allocator, 8);
        *entry->name_.finish = 0;
        StringInitEmpty(&entry->description_);
        ei::SetVtable(entry, ServerEntry::kVtableAddress);
        StringAssignCStrOriginal(&entry->name_, nullptr, game::QuestionMarkText());
        entry->kind_ = 4;
        StringAssignCStrOriginal(&entry->description_, nullptr, game::QuestionMarkText());
        entry->players_ = 0;
        entry->maxPlayers_ = 0;
        entry->password_ = 0;
    }
    StringRep* name = &entry->name_;
    if (&name_ != name) StringAssignTaggedOriginal(name, nullptr, name_.start, name_.finish, out);
    entry->kind_ = kind_;
    StringRep* description = &entry->description_;
    const char* empty = game::EmptyText();
    StringAssignRangeOriginal(description, nullptr, empty, empty + std::strlen(empty));

    CRefObject* map = map_.ptr;
    if (map && !(map->flags7_ & CRefObject::kDetached)) {
        std::uint32_t title = 0;   // CString
        const char* text = *static_cast<const char**>(WorldMapTitle(TheGameFlow(), nullptr, &title));
        const char* end = text + std::strlen(text);
        std::size_t length = static_cast<std::size_t>(end - text);
        std::size_t size = static_cast<std::size_t>(description->finish - description->start);
        if (length <= size) {
            std::memcpy(description->start, text, length);
            StringEraseOriginal(description, nullptr, description->start + length, description->finish);
        } else {
            std::memcpy(description->start, text, size);
            StringAppendRangeOriginal(description, nullptr, text + (description->finish - description->start), end, out);
        }
        CStringDestroy(&title);
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
// 00417E10: when announced, the master's address is set from the host name at 0079BED8 and port
// 28004.
void ServerSession::SetPublic(bool announce) {
    public_ = announce;
    if (announce) masterAddress_.Parse(*reinterpret_cast<const char* const*>(0x0079BED8), 0x6D64);
}

namespace {
} // namespace
EI_REPLACE_METHOD(0x0041A7E0, ServerSession::DescribeImpl);
EI_REPLACE_METHOD(0x00417E10, ServerSession::SetPublic);

} // namespace net
