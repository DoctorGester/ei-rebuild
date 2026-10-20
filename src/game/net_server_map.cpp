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
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "game_flow_data.h"

#include <cstring>

namespace net {

namespace {

// List teardown of +0x25AC / +0x25B0: element destructors and node deallocation.

// The queued message (vtable 0073BCD0, built by 00418C30): the list push_back (the counted pointer
// helpers are RefPtrFromRaw / RefPtrClear, core_refcount.h).
__declspec(naked) void __fastcall List25ACPushBack(void* /*list*/, void*, const void* /*ref*/) EI_ORIGINAL(0x004207C0)

// The world (0x7AFE20) and the text table (0x7C7D50), the map registry (0x7C87D4).
// The game flow at 0x7AFE20 (world_briefing.cpp, world_game.cpp): loading a zone's quests.
inline void __fastcall FlowLoadZoneQuests(void*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }
// vector<string> (library): the allocator proxy constructor and the block free; the string
// buffer free the string destructor inlines.
__declspec(naked) void __fastcall NamesProxyConstruct(void* /*eos*/, void*, const void* /*alloc*/, stlp::string* const* /*value*/) EI_ORIGINAL(0x004263D0)
void __fastcall NamesFree(stlp::string* a0, std::size_t a1) { NodeFree12Original((void*)a0, a1); }
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
void ServerSession::SetMap(const stlp::string* name) {
    ClearList(&queuedMaps_, Element25ACDestroy, List25ACFree);
    ClearList(&variables_, Element25B0Destroy, List25B0FreeNode);

    {
        stlp::string messageName = stlp::Plus(*name, game::ZoneSuffix2Text());
        auto* memory = static_cast<ZoneMessage*>(NewRefObject(sizeof(ZoneMessage)));
        ZoneMessage* message = memory ? memory->Construct() : nullptr;
        message->text_.AssignFrom(messageName);
        RefPtr ref;
        List25ACPushBack(&queuedMaps_, nullptr, RefPtrFromRaw(&ref, message));   // 00427D50
        RefPtrClear(&ref);   // 00421360
    }

    auto* mapMessage = NewMessageMemory<MapMessage>();
    if (mapMessage) {
        MessageConstructBase(mapMessage);
        stlp::StringElement::Default(&mapMessage->text_);
        ei::SetVtable(mapMessage, MapMessage::kVtableAddress);
    }
    RefPtr* holder = &map_;
    if (mapMessage != holder->ptr) {
        holder->Destroy();
        holder->Share(mapMessage);
    }
    auto* current = static_cast<MapMessage*>(holder->ptr);
    if (name != &current->text_) current->text_.AssignFrom(*name);
    if (name != &mapName_) mapName_.AssignFrom(*name);

    void* world = TheGameFlow();
    static_cast<GameFlow*>(world)->RegisterQuests(name->data());
    WorldPrepareMap(world, nullptr, name->data());
    std::uint32_t title = 0, text = 0;   // CStrings
    void* titleOut = SessionMapNameInto(reinterpret_cast<mfc::CString*>(&title));
    void* textOut = worlddata::InterfaceManager()->MessageText(&text, 5, *static_cast<const char**>(titleOut));
    Announce(*static_cast<const char**>(textOut), nullptr);
    CStringDestroy(&text);
    CStringDestroy(&title);

    std::uint8_t* entry;
    {
        stlp::string registryName = stlp::Plus(*name, game::ZoneSuffix1Text());
        entry = static_cast<std::uint8_t*>(*(void**)MapRegistryFindOriginal((void*)worlddata::QuestBriefings(), nullptr, (const void*)&registryName));
    }
    if (!entry) return;

    for (auto** it = *reinterpret_cast<const char***>(entry + 0x2C); it != *reinterpret_cast<const char***>(entry + 0x30); ++it) {
        stlp::string resourceName(*it);
        void* added;
        FindOrAddVariable(&added, &resourceName);
        auto* record = static_cast<VariableMessage*>(static_cast<void**>(added)[2]);
        const std::uint32_t one = 0x3F800000;   // 1.0f
        std::memcpy(&record->value_, &one, 4);
        record->serial_ = *VariableCounter();
        ++*VariableCounter();
        ++record->version_;
    }

    // "<prefix><name>.<name>" and the same with the path suffix; the temporaries freed in reverse.
    {
        stlp::string prefix(game::Text(flowdata::kQuestVariablePrefix));
        stlp::string a = stlp::Plus(prefix, *name);
        stlp::string b = stlp::Plus(a, game::Text(flowdata::kDot));
        stlp::string path = stlp::Plus(b, *name);
        SetVariable(&path, 1.0f);
    }
    {
        stlp::string prefix(game::Text(flowdata::kQuestVariablePrefix));
        stlp::string a = stlp::Plus(prefix, *name);
        stlp::string b = stlp::Plus(a, game::Text(flowdata::kDot));
        stlp::string c = stlp::Plus(b, *name);
        stlp::string path = stlp::Plus(c, game::PathSuffixText());
        SetVariable(&path, 1.0f);
    }

    AwardHost(entry, false);
}
// 00418530: unloads the flow's quest texts, then queues a map message ("zone" + "_1", vtable
// 0073BCD0) on +0x25AC for each zone the flow picks for the server's kind (+0x25E4) after the
// current map (+0x25D4), loading each zone's quests.
void ServerSession::QueueMapList() {
    void* flow = TheGameFlow();
    UnloadQuestTexts();
    struct {
        stlp::string* start;
        stlp::string* finish;
        stlp::string* eos;
    } zones;
    zones.start = nullptr;
    zones.finish = nullptr;
    stlp::string* none = nullptr;
    std::uint8_t allocator = 0;
    NamesProxyConstruct(&zones.eos, nullptr, &allocator, &none);
    PickZones(&zones, kind_, mapName_.data());
    for (stlp::string* zone = zones.start; zone != zones.finish; ++zone) {
        {
            stlp::string key = stlp::Plus(*zone, game::ZoneSuffix1Text());
            auto* message = NewMessageMemory<ZoneMessage>();
            if (message) {
                MessageConstructBase(message);
                stlp::StringElement::Default(&message->text_);
                ei::SetVtable(message, ZoneMessage::kVtableAddress);
            }
            message->text_.AssignFrom(key);
            RefPtr ref;
            List25ACPushBack(&queuedMaps_, nullptr, RefPtrFromRaw(&ref, message));   // 00427D50
            RefPtrClear(&ref);   // 00421360
        }
        FlowLoadZoneQuests(flow, nullptr, zone->data());
    }
    for (stlp::string* zone = zones.start; zone != zones.finish; ++zone) zone->~string();
    NamesFree(zones.start, static_cast<std::size_t>(zones.eos - zones.start));
}

// 004193F0: when the held map message names `name`: drops it, empties the queued messages
// (+0x25AC) and queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const stlp::string* name) {
    auto* holder = &map_;
    CRefObject* map = holder->ptr;
    if (!map || (map->flags7_ & CRefObject::kDetached)) return false;
    const stlp::string* held = &static_cast<MapMessage*>(map)->text_;
    if (!(*held == *name)) return false;

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

    stlp::string messageName = stlp::Plus(*name, game::ZoneSuffix3Text());
    auto* message = NewMessageMemory<ZoneMessage>();
    if (message) {
        MessageConstructBase(message);
        stlp::StringElement::Default(&message->text_);
        ei::SetVtable(message, ZoneMessage::kVtableAddress);
    }
    message->text_.AssignFrom(messageName);
    RefPtr ref;
    List25ACPushBack(&queuedMaps_, nullptr, RefPtrFromRaw(&ref, message));   // 00427D50
    RefPtrClear(&ref);   // 00421360
    return true;
}

EI_REPLACE_METHOD(0x004186F0, ServerSession::SetMap);
EI_REPLACE_METHOD(0x00418530, ServerSession::QueueMapList);
EI_REPLACE_METHOD(0x004193F0, ServerSession::FinishMap);

} // namespace net
