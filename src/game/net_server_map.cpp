// The game server's map change (004186F0). A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world.h"
#include "world_data.h"
#include "game_flow_data.h"

#include <cstring>

namespace net {

namespace {

// List teardown of +0x25AC / +0x25B0: element destructors and node deallocation.

// STLport strings (library): operator+(string, const char*), operator+(string, string),
// string(const char*, alloc), string(alloc), range assign, assign(first, last, tag), destructors.
__declspec(naked) StringRep* __fastcall StringPlusString(StringRep* /*out*/, const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00427E80)
static StringRep* StringFromText(StringRep* s, void*, const char* text, const void* alloc) { return static_cast<StringRep*>(::StringInitFromChars(s, nullptr, text, alloc)); }

// The queued message (vtable 0073BCD0, built by 00418C30) and its string member assignment, the
// counted pointer helpers, the list push_back.
__declspec(naked) void __fastcall MessageStringAssign(StringRep* /*string*/, void*, const StringRep* /*from*/) EI_ORIGINAL(0x0041F4E0)
__declspec(naked) void* __fastcall RefFromRaw(void* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00427D50)
__declspec(naked) void __fastcall RefClear(void* /*ref*/) EI_ORIGINAL(0x00421360)
__declspec(naked) void __fastcall List25ACPushBack(void* /*list*/, void*, const void* /*ref*/) EI_ORIGINAL(0x004207C0)

// The world (0x7AFE20) and the text table (0x7C7D50), the map registry (0x7C87D4).
// The game flow at 0x7AFE20 (world_briefing.cpp, world_game.cpp): unloading the quest texts,
// picking the zones that follow, loading a zone's quests.
__declspec(naked) void __fastcall FlowUnloadQuestTexts(void* /*flow*/) EI_ORIGINAL(0x00580F10)
__declspec(naked) void __fastcall FlowPickZones(void* /*flow*/, void*, void* /*out vector<string>*/, std::uint32_t /*kind*/, const char* /*current*/) EI_ORIGINAL(0x0057F850)
inline void __fastcall FlowLoadZoneQuests(void*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }
// vector<string> (library): the allocator proxy constructor and the block free; the string
// buffer free the string destructor inlines.
__declspec(naked) void __fastcall NamesProxyConstruct(void* /*eos*/, void*, const void* /*alloc*/, StringRep* const* /*value*/) EI_ORIGINAL(0x004263D0)
void __fastcall NamesFree(StringRep* a0, std::size_t a1) { NodeFree12Original((void*)a0, a1); }
void __fastcall NodeFree(void* a0, std::size_t a1) { StlNodeFreeOriginal(a0, a1); }


std::uint32_t* VariableCounter() { return reinterpret_cast<std::uint32_t*>(0x0079B608); }

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
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's
// names (inline, through FindOrAddVariable) and for two paths built from the map name
// (SetVariable), and queues the host's item awards (004191C0, without the final award).
void ServerSession::SetMap(const StringRep* name) {
    ClearList(&queuedMaps_, Element25ACDestroy, List25ACFree);
    ClearList(&variables_, Element25B0Destroy, List25B0FreeNode);

    StringRep messageName;
    StringPlusText(&messageName, name, game::ZoneSuffix2Text());
    auto* memory = static_cast<ZoneMessage*>(NewRefObject(sizeof(ZoneMessage)));
    ZoneMessage* message = memory ? memory->Construct() : nullptr;
    MessageStringAssign(&message->text_, nullptr, &messageName);
    void* ref;
    List25ACPushBack(&queuedMaps_, nullptr, RefFromRaw(&ref, message));
    RefClear(&ref);
    StringRepDestroy(&messageName);

    auto* mapMessage = NewMessageMemory<MapMessage>();
    if (mapMessage) {
        MessageConstructBase(mapMessage);
        StringInitEmpty(&mapMessage->text_);
        ei::SetVtable(mapMessage, MapMessage::kVtableAddress);
    }
    RefPtr* holder = &map_;
    if (mapMessage != holder->ptr) {
        holder->Destroy();
        holder->Share(mapMessage);
    }
    auto* current = static_cast<MapMessage*>(holder->ptr);
    if (name != &current->text_) StringAssignTaggedOriginal(&current->text_, nullptr, name->start, name->finish, mapMessage);
    if (name != &mapName_) StringAssignTaggedOriginal(&mapName_, nullptr, name->start, name->finish, mapMessage);

    void* world = TheGameFlow();
    FlowRegisterQuestsOriginal(world, nullptr, name->start);
    WorldPrepareMap(world, nullptr, name->start);
    std::uint32_t title = 0, text = 0;   // CStrings
    void* titleOut = WorldMapTitle(world, nullptr, &title);
    void* textOut = TextFormat(worlddata::InterfaceManager(), nullptr, &text, 5, *static_cast<const char**>(titleOut));
    Announce(*static_cast<const char**>(textOut), nullptr);
    CStringDestroy(&text);
    CStringDestroy(&title);

    StringRep registryName;
    StringPlusText(&registryName, name, game::ZoneSuffix1Text());
    auto* entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&registryName));
    StringRepDestroy(&registryName);
    if (!entry) return;

    for (auto** it = *reinterpret_cast<const char***>(entry + 0x2C); it != *reinterpret_cast<const char***>(entry + 0x30); ++it) {
        const char* variableName = *it;
        StringRep resourceName;
        std::uint32_t alloc;
        StringInitWithAllocator(&resourceName, nullptr, &alloc);
        StringAssignSpan(&resourceName, nullptr, variableName, variableName + std::strlen(variableName));
        void* added;
        FindOrAddVariable(&added, &resourceName);
        auto* record = static_cast<VariableMessage*>(static_cast<void**>(added)[2]);
        const std::uint32_t one = 0x3F800000;   // 1.0f
        std::memcpy(&record->value_, &one, 4);
        record->serial_ = *VariableCounter();
        ++*VariableCounter();
        ++record->version_;
        StlStringDestroy(&resourceName);
    }

    std::uint32_t alloc;
    StringRep prefix, a, b, path;
    StringFromText(&prefix, nullptr, game::Text(flowdata::kQuestVariablePrefix), &alloc);
    StringPlusString(&a, &prefix, name);
    StringPlusText(&b, &a, game::Text(flowdata::kDot));
    StringPlusString(&path, &b, name);
    SetVariable(&path, 1.0f);
    StlStringDestroy(&path);
    StlStringDestroy(&b);
    StlStringDestroy(&a);
    StlStringDestroy(&prefix);

    StringRep prefix2, a2, b2, c2, path2;
    StringFromText(&prefix2, nullptr, game::Text(flowdata::kQuestVariablePrefix), &alloc);
    StringPlusString(&a2, &prefix2, name);
    StringPlusText(&b2, &a2, game::Text(flowdata::kDot));
    StringPlusString(&c2, &b2, name);
    StringPlusText(&path2, &c2, game::PathSuffixText());
    SetVariable(&path2, 1.0f);
    StlStringDestroy(&path2);
    StlStringDestroy(&c2);
    StlStringDestroy(&b2);
    StlStringDestroy(&a2);
    StlStringDestroy(&prefix2);

    AwardHost(entry, false);
}
// 00418530: unloads the flow's quest texts, then queues a map message ("zone" + suffix, vtable
// 0073BCD0) on +0x25AC for each zone the flow picks for the server's kind (+0x25E4) after the
// current map (+0x25D4), loading each zone's quests.
void ServerSession::QueueMapList() {
    void* flow = TheGameFlow();
    FlowUnloadQuestTexts(flow);
    struct {
        StringRep* start;
        StringRep* finish;
        StringRep* eos;
    } zones;
    zones.start = nullptr;
    zones.finish = nullptr;
    StringRep* none = nullptr;
    std::uint8_t allocator = 0;
    NamesProxyConstruct(&zones.eos, nullptr, &allocator, &none);
    FlowPickZones(flow, nullptr, &zones, kind_, mapName_.start);
    for (StringRep* zone = zones.start; zone != zones.finish; ++zone) {
        StringRep key;
        StringPlusText(&key, zone, game::ZoneSuffix2Text());
        auto* message = NewMessageMemory<ZoneMessage>();
        if (message) {
            MessageConstructBase(message);
            StringInitEmpty(&message->text_);
            ei::SetVtable(message, ZoneMessage::kVtableAddress);
        }
        MessageStringAssign(&message->text_, nullptr, &key);
        void* ref;
        List25ACPushBack(&queuedMaps_, nullptr, RefFromRaw(&ref, message));
        RefClear(&ref);
        StringDeallocateOriginal(key.start, static_cast<std::size_t>(key.eos - key.start));
        FlowLoadZoneQuests(flow, nullptr, zone->start);
    }
    for (StringRep* zone = zones.start; zone != zones.finish; ++zone) StringRepDestroy(zone);
    NamesFree(zones.start, static_cast<std::size_t>(zones.eos - zones.start));
}

// 004193F0: when the held map message names `name`: drops it, empties the queued messages
// (+0x25AC) and queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const StringRep* name) {
    auto* holder = &map_;
    CRefObject* map = holder->ptr;
    if (!map || (map->flags7_ & CRefObject::kDetached)) return false;
    const StringRep* held = &static_cast<MapMessage*>(map)->text_;
    const std::size_t length = static_cast<std::size_t>(held->finish - held->start);
    if (length != static_cast<std::size_t>(name->finish - name->start) || std::memcmp(held->start, name->start, length) != 0)
        return false;

    if (CRefObject* object = holder->ptr) {
        if (object->refs_ != 1) {
            object->flags7_ |= CRefObject::kDetached;
            object->Shutdown();
        }
        holder->ptr->Release();
        holder->Share(nullptr);
    }
    for (void** node = static_cast<void**>(static_cast<void**>(queuedMaps_)[0]); node != queuedMaps_;) {
        void** current = node;
        node = static_cast<void**>(node[0]);
        reinterpret_cast<RefPtr*>(current + 2)->Destroy();
        if (current) NodeFree(current, 0xC);
    }
    static_cast<void**>(queuedMaps_)[0] = queuedMaps_;
    static_cast<void**>(queuedMaps_)[1] = queuedMaps_;

    StringRep messageName;
    StringPlusText(&messageName, name, game::ZoneSuffix3Text());
    auto* message = NewMessageMemory<ZoneMessage>();
    if (message) {
        MessageConstructBase(message);
        StringInitEmpty(&message->text_);
        ei::SetVtable(message, ZoneMessage::kVtableAddress);
    }
    MessageStringAssign(&message->text_, nullptr, &messageName);
    void* ref;
    List25ACPushBack(&queuedMaps_, nullptr, RefFromRaw(&ref, message));
    RefClear(&ref);
    StringBufferFree(&messageName);
    return true;
}

EI_REPLACE_METHOD(0x004186F0, ServerSession::SetMap);
EI_REPLACE_METHOD(0x00418530, ServerSession::QueueMapList);
EI_REPLACE_METHOD(0x004193F0, ServerSession::FinishMap);

} // namespace net
