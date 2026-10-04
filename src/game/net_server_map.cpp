// The game server's map change (004186F0). A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// List teardown of +0x25AC / +0x25B0: element destructors and node deallocation.
__declspec(naked) void __fastcall Element25ACDestroy(void* /*element*/, void*, int /*flags*/) EI_ORIGINAL(0x0042A5F0)
__declspec(naked) void __fastcall List25ACFreeNode(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425A40)
__declspec(naked) void __fastcall Element25B0Destroy(void* /*element*/, void*, int /*flags*/) EI_ORIGINAL(0x004292C0)
__declspec(naked) void __fastcall List25B0FreeNode(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00422390)

// STLport strings (library): operator+(string, const char*), operator+(string, string),
// string(const char*, alloc), string(alloc), range assign, assign(first, last, tag), destructors.
__declspec(naked) StringRep* __fastcall StringPlusText(StringRep* /*out*/, const StringRep* /*a*/, const char* /*b*/) EI_ORIGINAL(0x00427D60)
__declspec(naked) StringRep* __fastcall StringPlusString(StringRep* /*out*/, const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00427E80)
__declspec(naked) StringRep* __fastcall StringFromText(StringRep* /*s*/, void*, const char* /*text*/, const void* /*alloc*/) EI_ORIGINAL(0x0040C5A0)
__declspec(naked) StringRep* __fastcall StringWithAlloc(StringRep* /*s*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004217E0)
__declspec(naked) void __fastcall StringRangeInit(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00428F10)
__declspec(naked) void __fastcall StringAssign(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, void* /*tag*/) EI_ORIGINAL(0x0042A4F0)
__declspec(naked) void __fastcall StringDestroyA(StringRep* /*s*/) EI_ORIGINAL(0x0040C770)
__declspec(naked) void __fastcall StringDestroyB(StringRep* /*s*/) EI_ORIGINAL(0x0040C720)

// The queued message (vtable 0073BCD0, built by 00418C30) and its string member assignment, the
// counted pointer helpers, the list push_back.
__declspec(naked) void __fastcall MessageStringAssign(void* /*string*/, void*, const StringRep* /*from*/) EI_ORIGINAL(0x0041F4E0)
__declspec(naked) void* __fastcall RefFromRaw(void* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00427D50)
__declspec(naked) void __fastcall RefClear(void* /*ref*/) EI_ORIGINAL(0x00421360)
__declspec(naked) void __fastcall List25ACPushBack(void* /*list*/, void*, const void* /*ref*/) EI_ORIGINAL(0x004207C0)
__declspec(naked) void __fastcall MessageStringConstruct(void* /*string*/) EI_ORIGINAL(0x0041F460)
constexpr std::uintptr_t kMapMessageVtable = 0x0073BD00;

// The world (0x7AFE20) and the text table (0x7C7D50), the map registry (0x7C87D4).
__declspec(naked) void __fastcall WorldLoadMap(void* /*world*/, void*, const char* /*name*/) EI_ORIGINAL(0x005810A0)
__declspec(naked) void __fastcall WorldPrepareMap(void* /*world*/, void*, const char* /*name*/) EI_ORIGINAL(0x0057ED60)
__declspec(naked) void* __fastcall WorldMapTitle(void* /*world*/, void*, void* /*out CString*/) EI_ORIGINAL(0x005814C0)
__declspec(naked) void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/) EI_ORIGINAL(0x005F8C90)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)
__declspec(naked) void** __fastcall MapRegistryFind(void* /*registry*/, void*, const StringRep* /*name*/) EI_ORIGINAL(0x004297A0)
constexpr std::uintptr_t kWorld = 0x007AFE20;
constexpr std::uintptr_t kTextTable = 0x007C7D50;
constexpr std::uintptr_t kMapRegistry = 0x007C87D4;

// The server's own steps (not rewritten yet).
__declspec(naked) void* __fastcall ServerAddResource(ServerSession* /*self*/, void*, void** /*out*/, const StringRep* /*name*/) EI_ORIGINAL(0x004195D0)
__declspec(naked) void __fastcall ServerAddFile(ServerSession* /*self*/, void*, const StringRep* /*path*/, float /*weight*/) EI_ORIGINAL(0x00419730)

constexpr std::uintptr_t kMapSuffix = 0x00783D80;      // appended to the map name for the message
constexpr std::uintptr_t kRegistrySuffix = 0x00783D70; // appended for the registry lookup
constexpr std::uintptr_t kPathPrefix = 0x00783D7C;
constexpr std::uintptr_t kPathMiddle = 0x00783D78;
constexpr std::uintptr_t kPathSuffix = 0x00783D74;

const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }
std::uint32_t* ResourceCounter() { return reinterpret_cast<std::uint32_t*>(0x0079B608); }

// Frees every node of a list (sentinel kept and linked to itself): element destructor, then the node.
template <typename DestroyElement, typename FreeNode>
void ClearList(void* list, DestroyElement destroy, FreeNode freeNode) {
    void** sentinel = *static_cast<void***>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != *static_cast<void***>(list);) {
        void** next = static_cast<void**>(node[0]);
        destroy(node + 2, nullptr, 0);
        freeNode(list, nullptr, node, 1);
        node = next;
    }
    sentinel = *static_cast<void***>(list);
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
}

} // namespace

// 004186F0: SetMap(name). Empties the two lists at +0x25AC / +0x25B0, queues a map message (name +
// suffix) on the first, replaces the counted message at +0x25A8 by a fresh 0073BD00 one and copies
// the name into it and into +0x25D4, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", registers each of the map's resource names (weight
// 1, numbered by 0079B608), adds the map's two data files and queues the host's item awards
// (004191C0, without the final award).
void ServerSession::SetMap(const StringRep* name) {
    std::uint8_t* self = reinterpret_cast<std::uint8_t*>(this);
    ClearList(self + 0x25AC, Element25ACDestroy, List25ACFreeNode);
    ClearList(self + 0x25B0, Element25B0Destroy, List25B0FreeNode);

    StringRep messageName;
    StringPlusText(&messageName, name, Text(kMapSuffix));
    auto* memory = static_cast<Message*>(NewRefObject(0x18));
    Message* message = memory ? ConstructMessage73BCD0(memory) : nullptr;
    MessageStringAssign(reinterpret_cast<std::uint8_t*>(message) + 0xC, nullptr, &messageName);
    void* ref;
    List25ACPushBack(self + 0x25AC, nullptr, RefFromRaw(&ref, message));
    RefClear(&ref);
    StringDestroyA(&messageName);

    auto* mapMessage = static_cast<std::uint8_t*>(gamecrt::operator_new(0x18));
    std::uint32_t zero = 0;
    std::memcpy(mapMessage + 4, &zero, 4);
    if (mapMessage) {
        MessageConstructBase(reinterpret_cast<Message*>(mapMessage));
        MessageStringConstruct(mapMessage + 0xC);
        std::uintptr_t vtable = kMapMessageVtable;
        std::memcpy(mapMessage, &vtable, 4);
    }
    auto* holder = reinterpret_cast<RefPtr*>(self + 0x25A8);
    if (reinterpret_cast<CRefObject*>(mapMessage) != holder->ptr) {
        holder->Destroy();
        holder->Share(reinterpret_cast<CRefObject*>(mapMessage));
    }
    auto* current = reinterpret_cast<std::uint8_t*>(holder->ptr);
    if (name != reinterpret_cast<const StringRep*>(current + 0xC))
        StringAssign(reinterpret_cast<StringRep*>(current + 0xC), nullptr, name->start, name->finish, mapMessage);
    if (name != &string_25D4) StringAssign(&string_25D4, nullptr, name->start, name->finish, mapMessage);

    void* world = reinterpret_cast<void*>(kWorld);
    WorldLoadMap(world, nullptr, name->start);
    WorldPrepareMap(world, nullptr, name->start);
    std::uint32_t title = 0, text = 0;   // CStrings
    void* titleOut = WorldMapTitle(world, nullptr, &title);
    void* textOut = TextFormat(reinterpret_cast<void*>(kTextTable), nullptr, &text, 5, *static_cast<const char**>(titleOut));
    Announce(*static_cast<const char**>(textOut), nullptr);
    CStringDestroy(&text);
    CStringDestroy(&title);

    StringRep registryName;
    StringPlusText(&registryName, name, Text(kRegistrySuffix));
    auto* entry = static_cast<std::uint8_t*>(*MapRegistryFind(reinterpret_cast<void*>(kMapRegistry), nullptr, &registryName));
    StringDestroyA(&registryName);
    if (!entry) return;

    for (auto** it = *reinterpret_cast<const char***>(entry + 0x2C); it != *reinterpret_cast<const char***>(entry + 0x30); ++it) {
        const char* resource = *it;
        StringRep resourceName;
        std::uint32_t alloc;
        StringWithAlloc(&resourceName, nullptr, &alloc);
        StringRangeInit(&resourceName, nullptr, resource, resource + std::strlen(resource));
        void* added;
        ServerAddResource(this, nullptr, &added, &resourceName);
        auto* record = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(added) + 8);
        const std::uint32_t one = 0x3F800000;   // 1.0f
        std::memcpy(record + 0x18, &one, 4);
        std::memcpy(record + 0x1C, ResourceCounter(), 4);
        ++*ResourceCounter();
        ++*reinterpret_cast<std::int32_t*>(record + 8);
        StringDestroyB(&resourceName);
    }

    std::uint32_t alloc;
    StringRep prefix, a, b, path;
    StringFromText(&prefix, nullptr, Text(kPathPrefix), &alloc);
    StringPlusString(&a, &prefix, name);
    StringPlusText(&b, &a, Text(kPathMiddle));
    StringPlusString(&path, &b, name);
    ServerAddFile(this, nullptr, &path, 1.0f);
    StringDestroyB(&path);
    StringDestroyB(&b);
    StringDestroyB(&a);
    StringDestroyB(&prefix);

    StringRep prefix2, a2, b2, c2, path2;
    StringFromText(&prefix2, nullptr, Text(kPathPrefix), &alloc);
    StringPlusString(&a2, &prefix2, name);
    StringPlusText(&b2, &a2, Text(kPathMiddle));
    StringPlusString(&c2, &b2, name);
    StringPlusText(&path2, &c2, Text(kPathSuffix));
    ServerAddFile(this, nullptr, &path2, 1.0f);
    StringDestroyB(&path2);
    StringDestroyB(&c2);
    StringDestroyB(&b2);
    StringDestroyB(&a2);
    StringDestroyB(&prefix2);

    AwardHost(entry, false);
}
namespace {
void __fastcall SetMapHook(ServerSession* self, void*, const StringRep* name) { self->SetMap(name); }
} // namespace
EI_REPLACE(0x004186F0, SetMapHook);

} // namespace net
