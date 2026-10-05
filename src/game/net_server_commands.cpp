// The game server's command messages (0041B2A0, 0073BC40 {kind, text}) and the 0073BC58 objects a
// client hands in (0041B690), with the host check they use (00419F90). See net_classes.h and
// docs/notes/unmapped_410_470.md ("Eighth round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);   // refptr_copies.cpp

namespace net {

namespace {

// STLport (library): string equality and substr, the string destructor, the objects list
// (+0x259C) end / insert.
__declspec(naked) bool __fastcall StringEquals(const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00427FA0)
__declspec(naked) StringRep* __fastcall StringSubstr(const StringRep* /*s*/, void*, StringRep* /*out*/, std::size_t /*pos*/, std::size_t /*n*/) EI_ORIGINAL(0x0041F5F0)
__declspec(naked) void __fastcall StringDestroyA(StringRep* /*s*/) EI_ORIGINAL(0x0040C770)
__declspec(naked) void** __fastcall ObjectsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420610)
__declspec(naked) void** __fastcall ObjectsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004229B0)
__declspec(naked) void __fastcall WorldPrepareMap(void* /*world*/, void*, const char* /*name*/) EI_ORIGINAL(0x0057ED60)
__declspec(naked) int __cdecl GameAtoi(const char* /*text*/) EI_ORIGINAL(0x006E80D8)

constexpr std::uintptr_t kWorld = 0x007AFE20;
constexpr std::uintptr_t kEnterSuffix = 0x00783D70;   // "_1"
constexpr std::uintptr_t kLeaveSuffix = 0x00783D80;   // "_2"
constexpr std::uintptr_t kFinishSuffix = 0x00783D84;  // "_3"

enum CommandKind : std::uint32_t {
    kPreviewMap = 0,       // the host shows a map
    kZoneCommand = 1,      // the host picks a queued map message: "<zone>_1" / "_2" / "_3"
    kKickByName = 2,
    kBanByName = 3,
    kKickBySerial = 4,
    kBanBySerial = 5,
};

void* CurrentConnectionObject() { return *reinterpret_cast<void**>(0x0079B924); }
std::size_t StringNpos() { return *reinterpret_cast<const std::size_t*>(0x0073BDE8); }

bool Is(const StringRep& s, std::uintptr_t text) {
    const char* t = reinterpret_cast<const char*>(text);
    std::size_t length = std::strlen(t);
    return static_cast<std::size_t>(s.finish - s.start) == length && std::memcmp(s.start, t, length) == 0;
}

} // namespace

// 00419F90: whether `connection` is the host's (a binding of it with a live host player).
bool ServerSession::IsHostConnection(void* connection) {
    void** bindings = static_cast<void**>(bindings_);
    for (void** node = static_cast<void**>(bindings[0]); node != bindings; node = static_cast<void**>(node[0])) {
        auto* binding = static_cast<std::uint8_t*>(node[2]);
        void* bound;
        std::memcpy(&bound, binding + 0x10, 4);
        if (bound != connection) continue;
        CRefObject* player;
        std::memcpy(&player, binding + 0xC, 4);
        if (player && !(player->flags7_ & CRefObject::kDetached) && reinterpret_cast<std::uint8_t*>(player)[0x28]) return true;
    }
    return false;
}

// 0041B690 (0073BC58 objects): the message keeps its sender's connection (+0x10) and is listed on
// +0x259C (0041B720 drops it).
void __fastcall ServerOnObjectAdded(ServerSession* self, Message* message) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(message);
    auto* connection = static_cast<CRefObject*>(CurrentConnectionObject());
    CRefObject* previous;
    std::memcpy(&previous, bytes + 0x10, 4);
    std::memcpy(bytes + 0x10, &connection, 4);
    if (connection) ++connection->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) ei::VCall<void>(previous, 0, 1);
    }
    RefPtr raw;
    const RefPtr* value = RefPtrFromRaw(&raw, reinterpret_cast<CRefObject*>(message));
    void* objects = &self->fileRequests_;
    void* end;
    ObjectsEnd(objects, nullptr, &end);
    void* inserted;
    ObjectsInsert(objects, nullptr, &inserted, end, value);
}

// 0041B2A0 (0073BC40 commands, from the connection being read). Kinds 0-2 need the host's
// connection, 3-5 the same through IsHostConnection:
//   0: the world prepares map `text`;
//   1: when a queued map message (+0x25AC) is named `text`, its suffix picks SetMap (_1),
//      LeaveZone (_2) or CompleteZone (_3) with the name before it;
//   2 / 3: every guest whose name starts with `text` is kicked / banned;
//   4 / 5: the client with serial atoi(text) is kicked / banned.
void __fastcall ServerOnCommand(ServerSession* self, Message* message) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(message);
    std::uint32_t kind;
    std::memcpy(&kind, bytes + 0xC, 4);
    auto* text = reinterpret_cast<StringRep*>(bytes + 0x10);
    switch (kind) {
    case kPreviewMap:
        if (self->IsHostConnection(CurrentConnectionObject())) WorldPrepareMap(reinterpret_cast<void*>(kWorld), nullptr, text->start);
        return;
    case kZoneCommand: {
        if (!self->IsHostConnection(CurrentConnectionObject())) return;
        void** queued = static_cast<void**>(self->queuedMaps_);
        void** node = static_cast<void**>(queued[0]);
        for (; node != self->queuedMaps_; node = static_cast<void**>(node[0])) {
            auto* entry = static_cast<std::uint8_t*>(node[2]);
            if (StringEquals(reinterpret_cast<const StringRep*>(entry + 0xC), text)) break;
        }
        if (node == self->queuedMaps_) return;
        const std::size_t cut = static_cast<std::size_t>(text->finish - text->start) - 2;
        StringRep suffix, name;
        StringSubstr(text, nullptr, &suffix, cut, StringNpos());
        StringSubstr(text, nullptr, &name, 0, static_cast<std::size_t>(text->finish - text->start) - 2);
        if (Is(suffix, kEnterSuffix)) self->SetMap(&name);
        if (Is(suffix, kLeaveSuffix)) self->LeaveZone();
        if (Is(suffix, kFinishSuffix)) self->CompleteZone(&name);
        StringDestroyA(&name);
        StringDestroyA(&suffix);
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

namespace {
bool __fastcall IsHostConnectionHook(ServerSession* self, void*, void* connection) { return self->IsHostConnection(connection); }
} // namespace
EI_REPLACE(0x00419F90, IsHostConnectionHook);
EI_REPLACE(0x0041B690, ServerOnObjectAdded);
EI_REPLACE(0x0041B2A0, ServerOnCommand);

} // namespace net
