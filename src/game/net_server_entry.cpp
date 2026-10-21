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
#include "game_data.h"

#include <cstring>

namespace net {

// 0041A7E0 (slot 15): makes `*out` a new server entry: the name, the kind, the current map's title
// as the description when a map message is held (else empty), the number of clients, the maximum
// and whether a password is set.
void ServerSession::DescribeImpl(RefPtr* out) {
    auto* entry = NewMessageMemory<ServerEntry>();
    if (entry) {
        entry->ConstructBase();
        stlp::StringElement::Default(&entry->name_);
        stlp::StringElement::Default(&entry->description_);
        ei::SetVtable(entry, ServerEntry::kVtableAddress);
        entry->name_.Assign(game::QuestionMarkText());
        entry->kind_ = 4;
        entry->description_.Assign(game::QuestionMarkText());
        entry->players_ = 0;
        entry->maxPlayers_ = 0;
        entry->password_ = 0;
    }
    if (&name_ != &entry->name_) entry->name_.AssignFrom(name_);
    entry->kind_ = kind_;
    stlp::string* description = &entry->description_;
    description->Assign(game::EmptyText());

    CRefObject* map = map_.ptr;
    if (map && !(map->flags7_ & CRefObject::kDetached)) {
        std::uint32_t title = 0;   // CString
        const char* text = *SessionMapNameInto(reinterpret_cast<mfc::CString*>(&title));
        description->Assign(text);   // inlined: overwrite, then cut or append
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
    if (announce) masterAddress_.Parse(ei::GameData<const char* const>(0x0079BED8), 0x6D64);
}

EI_REPLACE_METHOD(0x0041A7E0, ServerSession::DescribeImpl);
EI_REWRITTEN(0x00417E10, ServerSession::SetPublic);

} // namespace net
