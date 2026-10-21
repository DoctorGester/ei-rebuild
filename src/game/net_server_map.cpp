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
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>

namespace net {

namespace {

// The world (0x7AFE20) and the text table (0x7C7D50), the map registry (0x7C87D4).
// The game flow at 0x7AFE20 (world_briefing.cpp, world_game.cpp): loading a zone's quests.
// vector<string> (library): the block free.
void __fastcall NamesFree(stlp::string* a0, std::size_t a1) { stlp::deallocate(a0, a1 * 12); }


std::uint32_t* VariableCounter() { return ei::GamePointer<std::uint32_t>(0x0079B608); }

} // namespace

// 004186F0: SetMap(name). Empties the two lists at +0x25AC / +0x25B0, queues a map message (name +
// suffix) on the first, replaces the counted message at +0x25A8 by a fresh 0073BD00 one and copies
// the name into it and into +0x25D4, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's
// names (inline, through FindOrAddVariable) and for two paths built from the map name
// (SetVariable), and queues the host's item awards (004191C0, without the final award).
void ServerSession::SetMap(const stlp::string* name) {
    queuedMaps_.Clear();
    variables_.Clear();

    {
        stlp::string messageName = stlp::Plus(*name, game::ZoneSuffix2Text());
        auto* memory = static_cast<ZoneMessage*>(NewRefObject(sizeof(ZoneMessage)));
        ZoneMessage* message = memory ? memory->Construct() : nullptr;
        message->text_.AssignFrom(messageName);
        RefPtr ref;
        queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));   // 00427D50
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
    void* textOut = TheInterface()->MessageText(&text, 5, *static_cast<const char**>(titleOut));
    Announce(*static_cast<const char**>(textOut), nullptr);
    CStringDestroy(&text);
    CStringDestroy(&title);

    std::uint8_t* entry;
    {
        stlp::string registryName = stlp::Plus(*name, game::ZoneSuffix1Text());
        entry = stlp::NamedSlot<std::uint8_t>(worlddata::QuestBriefings(), registryName);
    }
    if (!entry) return;

    for (auto** it = *reinterpret_cast<const char***>(entry + 0x2C); it != *reinterpret_cast<const char***>(entry + 0x30); ++it) {
        stlp::string resourceName(*it);
        RefNode* added;
        FindOrAddVariable(&added, &resourceName);
        auto* record = static_cast<VariableMessage*>(added->data.ptr);
        const std::uint32_t one = 0x3F800000;   // 1.0f
        std::memcpy(&record->value_, &one, 4);
        record->serial_ = *VariableCounter();
        ++*VariableCounter();
        ++record->version_;
    }

    // "<prefix><name>.<name>" and the same with the path suffix; the temporaries freed in reverse.
    {
        stlp::string prefix(flowdata::kQuestVariablePrefix);
        stlp::string a = stlp::Plus(prefix, *name);
        stlp::string b = stlp::Plus(a, flowdata::kDot);
        stlp::string path = stlp::Plus(b, *name);
        SetVariable(&path, 1.0f);
    }
    {
        stlp::string prefix(flowdata::kQuestVariablePrefix);
        stlp::string a = stlp::Plus(prefix, *name);
        stlp::string b = stlp::Plus(a, flowdata::kDot);
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
    zones.eos = none;   // the allocator proxy's constructor
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
            queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));   // 00427D50
            RefPtrClear(&ref);   // 00421360
        }
        FlowLoadQuestsOriginal(zone->data());
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
    queuedMaps_.Clear();

    stlp::string messageName = stlp::Plus(*name, game::ZoneSuffix3Text());
    auto* message = NewMessageMemory<ZoneMessage>();
    if (message) {
        MessageConstructBase(message);
        stlp::StringElement::Default(&message->text_);
        ei::SetVtable(message, ZoneMessage::kVtableAddress);
    }
    message->text_.AssignFrom(messageName);
    RefPtr ref;
    queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));   // 00427D50
    RefPtrClear(&ref);   // 00421360
    return true;
}

EI_REPLACE_METHOD(0x004186F0, ServerSession::SetMap);
EI_REPLACE_METHOD(0x00418530, ServerSession::QueueMapList);
EI_REPLACE_METHOD(0x004193F0, ServerSession::FinishMap);

} // namespace net
