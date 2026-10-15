// The game server's command messages (0041B2A0, CommandMessage) and the file requests a client
// hands in (0041B690), with the host check they use (00419F90). See net_messages.h and
// docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"
#include "originals_world.h"
#include "world.h"
#include "game_strings.h"

#include <cstring>

RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);   // refptr_copies.cpp

namespace net {

namespace {

// STLport (library): string equality and substr, the string destructor, the objects list
// (+0x259C) end / insert.
__declspec(naked) StringRep* __fastcall StringSubstr(const StringRep* /*s*/, void*, StringRep* /*out*/, std::size_t /*pos*/, std::size_t /*n*/) EI_ORIGINAL(0x0041F5F0)
__declspec(naked) void** __fastcall ObjectsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420610)
__declspec(naked) void** __fastcall ObjectsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004229B0)


enum CommandKind : std::uint32_t {
    kPreviewMap = 0,       // the host shows a map
    kZoneCommand = 1,      // the host picks a queued map message: "<zone>_1" / "_2" / "_3"
    kKickByName = 2,
    kBanByName = 3,
    kKickBySerial = 4,
    kBanBySerial = 5,
};

void* CurrentConnectionObject() { return CurrentConnection(); }
std::size_t StringNpos() { return *reinterpret_cast<const std::size_t*>(0x0073BDE8); }

bool Is(const StringRep& s, const char* t) {
    std::size_t length = std::strlen(t);
    return static_cast<std::size_t>(s.finish - s.start) == length && std::memcmp(s.start, t, length) == 0;
}

} // namespace

// 00419F90: whether `connection` is the host's (a binding of it with a live host player).
bool ServerSession::IsHostConnection(void* connection) {
    void** bindings = static_cast<void**>(bindings_);
    for (void** node = static_cast<void**>(bindings[0]); node != bindings; node = static_cast<void**>(node[0])) {
        auto* binding = static_cast<BindingMessage*>(node[2]);
        if (binding->ConnectionRef().ptr != connection) continue;
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        if (player && !(player->flags7_ & CRefObject::kDetached) && player->host_) return true;
    }
    return false;
}

// 0041B690 (file requests): the request keeps its sender's connection and is listed on +0x259C
// (0041B720 drops it).
void __fastcall ServerOnObjectAdded(ServerSession* self, FileRequestMessage* message) {
    auto* connection = static_cast<CRefObject*>(CurrentConnectionObject());
    CRefObject* previous = message->ConnectionRef().ptr;
    message->ConnectionRef().ptr = connection;
    if (connection) ++connection->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
    RefPtr raw;
    const RefPtr* value = RefPtrFromRaw(&raw, message);
    void* objects = &self->fileRequests_;
    void* end;
    ObjectsEnd(objects, nullptr, &end);
    void* inserted;
    ObjectsInsert(objects, nullptr, &inserted, end, value);
}

// 0041B2A0 (commands, from the connection being read). Kinds 0-2 need the host's
// connection, 3-5 the same through IsHostConnection:
//   0: the world prepares map `text`;
//   1: when a queued map message (+0x25AC) is named `text`, its suffix picks SetMap (_1),
//      LeaveZone (_2) or CompleteZone (_3) with the name before it;
//   2 / 3: every guest whose name starts with `text` is kicked / banned;
//   4 / 5: the client with serial atoi(text) is kicked / banned.
void __fastcall ServerOnCommand(ServerSession* self, CommandMessage* message) {
    StringRep* text = &message->text_;
    switch (message->kind_) {
    case kPreviewMap:
        if (self->IsHostConnection(CurrentConnectionObject())) WorldPrepareMap(TheGameFlow(), nullptr, text->start);
        return;
    case kZoneCommand: {
        if (!self->IsHostConnection(CurrentConnectionObject())) return;
        void** queued = static_cast<void**>(self->queuedMaps_);
        void** node = static_cast<void**>(queued[0]);
        for (; node != self->queuedMaps_; node = static_cast<void**>(node[0])) {
            auto* entry = static_cast<ZoneMessage*>(node[2]);
            if (StringEquals(&entry->text_, text)) break;
        }
        if (node == self->queuedMaps_) return;
        const std::size_t cut = static_cast<std::size_t>(text->finish - text->start) - 2;
        StringRep suffix, name;
        StringSubstr(text, nullptr, &suffix, cut, StringNpos());
        StringSubstr(text, nullptr, &name, 0, static_cast<std::size_t>(text->finish - text->start) - 2);
        if (Is(suffix, game::ZoneSuffix1Text())) self->SetMap(&name);
        if (Is(suffix, game::ZoneSuffix2Text())) self->LeaveZone();
        if (Is(suffix, game::ZoneSuffix3Text())) self->CompleteZone(&name);
        StringRepDestroy(&name);
        StringRepDestroy(&suffix);
        return;
    }
    case kKickByName:
        if (!self->IsHostConnection(CurrentConnectionObject())) return;
        for (;;) {
            auto* connection = static_cast<CRefObject*>(self->FindGuestConnection(text));
            if (!connection || (connection->flags7_ & CRefObject::kDetached)) return;
            self->Kick(connection);
        }
    case kBanByName:
        if (!self->IsHostConnection(CurrentConnectionObject())) return;
        for (;;) {
            auto* connection = static_cast<CRefObject*>(self->FindGuestConnection(text));
            if (!connection || (connection->flags7_ & CRefObject::kDetached)) return;
            self->Ban(connection);
        }
    case kKickBySerial:
        if (self->IsHostConnection(CurrentConnectionObject())) self->Kick(self->FindConnectionBySerial(GameAtoi(text->start)));
        return;
    case kBanBySerial:
        if (self->IsHostConnection(CurrentConnectionObject())) self->Ban(self->FindConnectionBySerial(GameAtoi(text->start)));
        return;
    default:
        return;
    }
}

EI_REPLACE_METHOD(0x00419F90, ServerSession::IsHostConnection);
EI_REPLACE(0x0041B690, ServerOnObjectAdded);
EI_REPLACE(0x0041B2A0, ServerOnCommand);

} // namespace net
