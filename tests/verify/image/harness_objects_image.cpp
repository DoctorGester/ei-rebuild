// The game's global objects and the steps' calls of the game in the image (image_side.h; harness_objects.h,
// harness.cpp).
#include "image_side.h"

#include "../../../src/platform/log.h"

#include "game/app/app.h"
#include "engine/scene/camera.h"
#include "game/console/console.h"
#include "engine/core/core_streams.h"
#include "engine/core/program_window.h"
#include "engine/scene/drawn_motion.h"
#include "game/app/game_flow.h"
#include "game/app/movies.h"
#include "game/app/singletons.h"
#include "engine/models/figure_gltf.h"
#include "engine/models/figure_manager.h"
#include "game/items/item.h"
#include "engine/scene/light.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/net/net_session.h"
#include "engine/models/link_loader.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/map/map_database.h"
#include "engine/data/text_manager.h"
#include "engine/data/options.h"
#include "game/players/party.h"
#include "game/players/party_manager.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/random.h"
#include "engine/data/resource.h"
#include "game/spells/spell.h"
#include "game/ui/ui.h"
#include "game/ui/ui_chat_overlay.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_screens.h"
#include "game/ui/ui_hud.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_drop_list.h"
#include "game/ui/ui_saveload.h"
#include "game/ui/ui_options.h"
#include "game/terrain/ground_print.h"
#include "game/objects/map_object_manager.h"
#include "game/units/acks_db.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"
#include "game/world/world.h"
#include "engine/particles/particle_data.h"
#include "game/effects/world_effect.h"
#include "engine/core/coder.h"
#include "game/sound/sound.h"
#include "engine/mods/maps_folder.h"
#include "game/terrain/terrain.h"
#include "lib/crt.h"
#include "game/ui/screen_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace ei::verify::image {

namespace {

void (*g_fill)(std::uint32_t* results) = nullptr;

void FixedSeed(IsaacRandom& random) {
    libcrt::Srand(12345);
    g_fill(random.results);
}

// The unit's slots (thiscall, RET 4) and a weapon's kind, for HudStateOf.
Item* UnitQuickItem(Unit* unit, std::uint32_t slot) { return unit->GetQuickItem(slot); }
Item* UnitSpell(Unit* unit, std::uint32_t slot) { return unit->GetSpell(slot); }
ItemWeapon* UnitWeapon(Unit* unit, std::uint32_t slot) { return unit->GetWeapon(slot); }
int WeaponTypeOf(ItemWeapon* weapon) { return static_cast<int>(weapon->GetType()); }

// "hudstate" (harness.cpp, HudState): the battle screen's state logged.
// Logs the pending action (pendingAction, pendingSlot, pendingItem), each party member's selection (flags 0x10)
// and quick items (slots 0-3), and the quick item panel's (quickItems) unit, entries shown and region items, the spell
// column's (spellColumn) count and highlight. With <minquick>, fails when the quick item panel shows fewer than that many of its unit's own quick items
// (regions 0-3).
std::string HudStateOf(void* window, const std::string& argument) {
    auto* battle = static_cast<BattleScreen*>(window);
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", window,
        static_cast<int>(battle->pendingAction), battle->pendingSlot, battle->pendingItem, static_cast<int>(std::ssize(battle->party)));
    for (int i = 0; i < std::ssize(battle->party) && i < 8; ++i) {
        Unit* member = battle->party[i];
        if (!member) continue;
        void* quick[4];
        for (int s = 0; s < 4; ++s) quick[s] = UnitQuickItem(member, s);
        Log("harness: hudstate: member %d %p flags %08X, quick items %p %p %p %p", i, member,
            member->flags, quick[0], quick[1], quick[2], quick[3]);
    }
    HudQuickItems* quickPanel = battle->quickItems;
    int own = 0;
    if (quickPanel) {
        for (int r = 0; r < 4; ++r) {
            int entry = quickPanel->regionItem[r];
            if (entry >= 0 && entry < std::ssize(quickPanel->entries) && quickPanel->entries[entry].own) ++own;
        }
        const int* items = quickPanel->regionItem;
        Log("harness: hudstate: quick panel unit %p, %d entries (%d own), highlight %d, regions %d %d %d %d %d %d %d %d",
            quickPanel->unit, static_cast<int>(std::ssize(quickPanel->entries)), own, quickPanel->selected, items[0],
            items[1], items[2], items[3], items[4], items[5], items[6], items[7]);
        // The player's quest items in the top row: the id a lever's lock matches (SetScience).
        for (int e = 0; e < std::ssize(quickPanel->entries); ++e) {
            const HudQuickItems::Entry& entry = quickPanel->entries[e];
            if (!entry.own && entry.item && entry.item->type == 0x3009)
                Log("harness: hudstate: quick panel entry %d: quest item id %u (object %d)", e,
                    static_cast<ItemQuest*>(entry.item)->questItemId, entry.id);
        }
    }
    if (HudSpellColumn* spells = battle->spellColumn) {
        Log("harness: hudstate: spell column unit %p, %d spells, highlight %d", static_cast<void*>(spells->unit), spells->count,
            spells->selected);
        // Each spell slot of the column's unit: the spell's prototype index and target flags.
        if (Unit* unit = spells->unit) {
            for (unsigned s = 0; s < 8; ++s) {
                Item* item = UnitSpell(unit, s);
                if (!item) continue;
                auto* spell = static_cast<Spell*>(item->spell);
                Log("harness: hudstate: spell slot %u item %p, prototype %d, flags %08X", s, item,
                    spell ? spell->prototypeIndex : -1, spell ? spell->targetFlags : 0);
            }
            // Its weapons (their kind: 5 bow, 6 crossbow) and the one in use.
            for (unsigned w = 0; w < 4; ++w)
                if (ItemWeapon* weapon = UnitWeapon(unit, w))
                    Log("harness: hudstate: weapon slot %u item %p kind %d%s", w, static_cast<void*>(weapon),
                        WeaponTypeOf(weapon), unit->activeWeapon == w ? " (in use)" : "");
        }
    }
    if (!argument.empty()) {
        int minimum = std::atoi(argument.c_str());
        if (own < minimum) return "hudstate: the quick item panel shows " + std::to_string(own) + " own items, wanted " +
                                  std::to_string(minimum);
    }
    return "";
}

} // namespace

void* MainWindow() { return g_mainWindow; }
std::uint8_t& InterfaceHasScreen() { return app::g_interfaceHasScreen; }
std::uint8_t& RecordingFrames() { return app::g_recordingFrames; }
bool AppActive() { return app::g_active != 0; }
int& FlowState() { return g_gameFlow.state; }
void SetLoadTarget(const char* slot) { g_gameFlow.requestTarget = slot; }
void ClearShowTutorial() { options::g_options.showTutorial = 0; }
void SetMasterHost(const char* host) { options::g_options.masterServerName = host; }
void SetShadows(bool on) { options::g_options.shadows = on; }

CursorState Cursor() {
    const ::Cursor* cursor = &g_cursor;
    CursorState state;
    state.imageShown = cursor->imageShown;
    if (const CursorImage* image = cursor->image) {
        state.image = true;
        state.width = image->width;
        state.height = image->height;
        state.frames = image->frames;
        state.hotX = image->hot.x;
        state.hotY = image->hot.y;

    }
    state.held = cursor->held;
    state.x = cursor->position.x;
    state.y = cursor->position.y;
    return state;
}

bool CloseAllPending() { return g_interface.closeAll; }
bool ScreenFade(bool* running, float* alpha) {
    const ::ScreenFade* fade = g_interface.fade;
    if (!fade) return false;
    *running = fade->active != 0;
    *alpha = fade->alpha;
    return true;
}
int CampaignMode() { return static_cast<int>(g_serverWorld.zoneMode); }
void* ClientWorldObject() { return &g_clientWorld; }
bool ClientPaused() { return g_clientWorld.paused != 0; }
bool ServerPaused() { return g_serverWorld.paused != 0; }
void* SceneDevice() { return g_sceneRenderer.device.Get(); }

void ServerUpdate(void* server) { static_cast<WorldServer*>(server)->Update(); }
bool ClientSegment(void* client) { return static_cast<WorldClient*>(client)->RunTick(); }
void CameraUpdate(void* camera, float seconds) { static_cast<::Camera*>(camera)->Update(seconds); }
void FrameStep(bool draw) { app::FrameStep(draw); }

void ConsoleExecute(const char* text) { console::Execute(text); }
void RequestGameState(int id) {
    g_interface.closeAll = 1;
    g_clientWorld.LocalView()->RequestGameState(id);
}
void SetConstructionVariable(float value) { g_serverWorld.SetConstructionVariable(value); }
void ObserveConsoleLines(void (*observer)(std::string_view line)) { console::g_consoleLineObserver = observer; }
unsigned& UpdatePeriod() { return reinterpret_cast<unsigned&>(app::g_updatePeriod); }
const char* DataFolder() { return g_dataFolder.c_str(); }
void SetFixedSeed(void (*fill)(std::uint32_t* results)) {
    g_fill = fill;
    IsaacRandom::SetSeedHook(&FixedSeed);
}
void ReseedRandom() {
    g_random.seeded = 0;
    g_random.Init();
    g_clientRandom.seeded = 0;
    g_clientRandom.InitFrom(g_random);
    g_particleRandom.seeded = 0;
    g_particleRandom.InitFrom(g_clientRandom);
}
int TextKey(const char* name) { return TextKeyFromName(name); }
void* LocalWorldPlayer(int player) { return g_serverWorld.GetPlayer(player); }
std::string HitUnit(int target, int attacker, float amount) {
    Unit* unit = g_serverWorld.FindUnit(target);
    if (!unit) return "no unit " + std::to_string(target);
    Unit* from = nullptr;
    if (attacker) {
        from = g_serverWorld.FindUnit(attacker);
        if (!from) return "no attacker " + std::to_string(attacker);
    }
    DamageInfo damage = {};
    damage.typeWeights[5] = 1.0f;
    damage.attackRoll = 100.0f;
    damage.amount = amount;
    damage.multiplier = 1.0f;
    damage.location = 1;
    unit->ReceiveHit(&damage, from, from);
    return "";
}
GroundPrintCounts CountGroundPrints() {
    const WorldClient& world = g_clientWorld;
    GroundPrintCounts counts;
    counts.blood = static_cast<int>(world.bloodPrints.size());
    counts.footprints = static_cast<int>(world.footprints.size());
    counts.scorch = static_cast<int>(world.scorchMarks.size());
    for (const BloodPrint& print : world.bloodPrints) counts.lifeLeft += print.life;
    for (const Footprint& print : world.footprints) {
        counts.lifeLeft += print.life;
        if (print.visible) ++counts.shownFootprints;
    }
    for (const GroundPrint& print : world.scorchMarks) counts.lifeLeft += print.life;
    return counts;
}

std::string GiveItem(int playerId, const char* name) {
    Player* player = g_serverWorld.GetPlayer(playerId);
    if (!player) return "giveitem: no player " + std::to_string(playerId);
    Item* item = g_serverWorld.CreateItemNamed(name);
    if (!item) return std::string("giveitem: no item ") + name;
    std::vector<Item*> items{item};
    player->GiveItems(items, 0);
    return "";
}
void* WorldHandle() { return g_serverWorld.worldHandle.Get(); }
const char* ServerZoneName() { return g_serverWorld.zoneName.c_str(); }
const char* LoadingMovieName() { return app::g_loadingMovieName; }
void SetPlayedMoviesPaced(bool on) { ::SetPlayedMoviesPaced(on); }

void* PickAt2(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<WorldClient*>(world)->Pick(p, HitTest::Parts);
}
void* UnitAt(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<EngineObject*>(static_cast<WorldClient*>(world)->UnitAt(p));
}
int ScreenRect(void* world, int id, Rect* rect) {
    int found = 0;
    for (const ScreenEntry& entry : static_cast<WorldClient*>(world)->screenEntries) {
        if (static_cast<int>(entry.objectId) != id) continue;
        *rect = Rect{entry.rect.left, entry.rect.top, entry.rect.right, entry.rect.bottom};
        found = 1;
    }
    return found;
}
int ObjectId(const void* object) { return static_cast<const EngineObject*>(object)->id; }
std::uint32_t ObjectFlags(const void* object) { return static_cast<const EngineObject*>(object)->flags; }
void* PlayerConnection(void* player) { return static_cast<Player*>(player)->connection; }
int ConnectionState(void* connection) {
    auto* base = static_cast<::PlayerConnection*>(connection);
    if (auto* server = dynamic_cast<PlayerServer*>(base)) return static_cast<int>(server->state);
    return static_cast<int>(static_cast<PlayerClient*>(base)->state);
}
bool CampItems(void* camp, int* page, std::vector<void*>* items, std::vector<unsigned>* types) {
    auto* pages = static_cast<CampPages*>(static_cast<UiWindow*>(camp));
    *page = pages->page;
    const std::vector<Item*>* source = pages->itemStrip ? pages->itemStrip->source : nullptr;
    if (!source) return false;
    for (int i = 0; i < static_cast<int>(std::ssize(*source)); ++i) {
        Item* item = (*source)[i];
        items->push_back(item);
        types->push_back(item ? static_cast<unsigned>(item->type) : 0);
    }
    return true;
}
void CampTakeItem(void* camp, void* item, int mode) {
    static_cast<CampPages*>(static_cast<UiWindow*>(camp))->TakeItem(static_cast<Item*>(item), mode);
}
bool TraderMagic(int type, std::vector<std::string>* items) {
    ClassTemplate* trader = g_partyManager.templates.Find(type);
    if (!trader) return false;
    for (Item* item : trader->SpellsParty()->items) {
        std::string text = "other";
        if (item && item->Kind() == ItemKind::Scroll && item->spell) {
            text = "scroll " + item->spell->GetPrototype()->code;
        } else if (item && item->Kind() == ItemKind::Loot) {
            const ItemLoot* loot = item->AsLoot();
            const itemdb::SpellRecord* record =
                loot->kind == LootKind::Record && loot->recordType == LootRecordType::Spell ? itemdb::SpellAt(loot->index)
                                                                                             : nullptr;
            if (record) text = "prototype " + record->code;
        }
        items->push_back(text);
    }
    return true;
}
void PickSurface(void* world, float* out, int x, int y) {
    *reinterpret_cast<Vector3*>(out) = static_cast<WorldClient*>(world)->PickSurface(x, y);
}
void LoadUnitFaces(void* world) { static_cast<WorldClient*>(world)->LoadUnitFaces(); }
void BlinkOpen(void* half) { static_cast<UnitClientSpecific*>(half)->BlinkOpen(true); }
void BlinkClose(void* half) { static_cast<UnitClientSpecific*>(half)->BlinkClose(true); }

bool UnitFaceHalves(std::vector<void*>* halves) {
    WorldClient* world = &g_clientWorld;
    if (!world) return false;
    for (MapObject* object : world->objects) {
        if (!object || object->GetClassId() != ClassId::Unit) continue;
        auto* half = reinterpret_cast<UnitClientSpecific*>(static_cast<Unit*>(object)->controller.Get());
        if (half && half->faceTexture) halves->push_back(half);
    }
    return true;
}

bool SetObjectSkins(int id, const char* first, const char* second) {
    for (MapObject* object : g_clientWorld.objects) {
        if (!object || object->id != id) continue;
        Object3DClientSpecific* half = object->ClientSpecific();
        if (!half) return false;
        const std::string secondName = second ? std::string(second) : half->secondSkinName;
        half->SetSkins(std::string_view(first), std::string_view(secondName));
        return true;
    }
    return false;
}

bool ObjectSkins(int id, std::string* first, std::string* second) {
    for (MapObject* object : g_clientWorld.objects) {
        if (!object || object->id != id) continue;
        Object3DClientSpecific* half = object->ClientSpecific();
        if (!half) return false;
        *first = half->skinName;
        *second = half->secondSkinName;
        return true;
    }
    return false;
}

const Object3D* ClientObject(int id) {
    for (MapObject* object : g_clientWorld.objects)
        if (object && object->id == id) return object;
    return nullptr;
}

int RegionCount(void* window) { return static_cast<int>(std::ssize(static_cast<UiWindow*>(window)->regions)); }
bool Region(void* window, int index, RegionInfo* region) {
    const UiRegion* found = static_cast<UiWindow*>(window)->regions[index].get();
    if (!found) return false;
    region->left = found->bounds.left;
    region->top = found->bounds.top;
    region->right = found->bounds.right;
    region->bottom = found->bounds.bottom;
    region->offsetX = found->offsetX;
    region->offsetY = found->offsetY;
    region->enabled = found->enabled;
    return true;
}
void ScaleToScreen(int* x, int* y) {
    Point point = screen_layout::ToScreen(Point{*x, *y});
    *x = point.x;
    *y = point.y;
}
void InterfaceSize(int* width, int* height) {
    *width = g_interface.width;
    *height = g_interface.height;
}
namespace {
UiDropList* FindDropList(UiWindow* window) {
    auto* container = dynamic_cast<UiContainer*>(window);
    if (!container) return nullptr;
    for (const auto& owned : container->children) {
        UiWindow* child = owned.get();
        if (auto* list = dynamic_cast<UiDropList*>(child)) return list;
        if (UiDropList* inner = FindDropList(child)) return inner;
    }
    return nullptr;
}
} // namespace
bool DropList(void* window, DropListInfo* info) {
    const UiDropList* list = FindDropList(static_cast<UiWindow*>(window));
    if (!list) return false;
    info->open = list->open;
    info->value = list->value;
    info->count = static_cast<int>(std::ssize(list->texts));
    info->first = list->FirstShown();
    info->shown = list->shown;
    info->active = list->active;
    info->text = info->count != 0 ? list->texts[list->value] : std::string();
    info->row = list->row;
    return true;
}
bool DropListEntry(void* window, int index, Point* centre) {
    UiDropList* list = FindDropList(static_cast<UiWindow*>(window));
    if (!list || !list->open || index < 0 || index >= std::ssize(list->texts)) return false;
    list->ScrollTo(index);
    const Rect entry = list->EntryRect(index);
    *centre = Point{(entry.left + entry.right) / 2, (entry.top + entry.bottom) / 2};
    return !entry.IsEmpty();
}
bool SaveLoadSliding(void* window) { return static_cast<SaveLoadWindow*>(window)->sliding; }
bool OptionsSliding(void* window) { return static_cast<OptionsWindow*>(window)->sliding; }
bool HasChatOverlay() { return g_interface.root != nullptr; }
void ChatKeyEnter() { g_interface.ChatKeyEnter(); }
void CloseChat() { g_interface.root->SetOpen(false); }
bool HasClassTemplate(int id) { return g_partyManager.templates.Find(id) != nullptr; }
const void* FindMap(const char* name) { return g_mapDatabase.FindMap(name); }
int MapKind(const void* record) { return static_cast<const MapRecord*>(record)->kind; }
void LeaveToZone(void* connection, const char* name, int exit) {
    static_cast<PlayerServer*>(connection)->LeaveToZone(name, exit);
}

bool RestoreSurfaces(long* result) {
    if (!g_sceneRenderer.device.Get()) return false;
    *result = 0;   // the image has no DirectDraw surfaces: nothing to restore
    return true;
}

int& GamePort() { return net::g_gamePort; }

const char* TextFolder() { return g_textFolder.c_str(); }

long ModFile(const char* path, const char* name) {
    MappedResource res;
    long got = -2;   // the archive did not open
    if (res.OpenResource(path)) {
        std::uint32_t size = 0;
        // Rebirth's ResOverride; as the texts read it, a texts.toml's text too (docs/notes/mods.md, "Records as TOML")
        bool utf8 = false;
        void* data = res.FindText(name, &size, &utf8);
        got = data ? static_cast<long>(size) : 0;
        if (data && !utf8) res.CloseFile(data);
    }
    return got;
}

long ModList(const char* path, const char* pattern) {
    MappedResource res;
    long count = -2;   // the archive did not open
    if (res.OpenResource(path)) {
        std::vector<std::string> names;
        res.ListFiles(&names, pattern);   // Rebirth's ResOverride: the mod's archives' names too
        count = static_cast<long>(names.size());
    }
    return count;
}

long GameText(const char* name, std::string* text) {
    TextManager* texts = g_clientWorld.texts.get();
    if (!texts) return -3;
    const std::optional<std::string> found = texts->FindString(name);
    if (!found) return 0;
    *text = *found;
    return 1;
}

long AckSound(const char* unit, const char* speech, std::string* sound) {
    acks::UnitAcks* record = g_ackDatabase.units.Find(unit);
    if (!record) return -3;
    for (const SpeechRecord& s : record->namedSpeeches.records) {
        if (s.name != speech) continue;
        *sound = s.sound;
        return 1;
    }
    return 0;
}

long RecordText(const char* kind, const char* name, std::string* text) {
    char line[256] = {};
    const std::string key = name;
    const std::string table = kind;
    if (table == "ack") {
        // "<unit>/<kind number>": the unit record's speeches of that ack kind.
        const std::size_t slash = key.rfind('/');
        if (slash == std::string::npos) return 0;
        acks::UnitAcks* record = g_ackDatabase.units.Find(key.substr(0, slash));
        if (!record) return 0;
        const auto found = record->head.speeches.find(std::atoi(key.c_str() + slash + 1));
        if (found == record->head.speeches.end() || !found->second) return 0;
        const std::vector<SpeechRecord>& list = found->second->records;
        std::snprintf(line, sizeof(line), "%d %s", static_cast<int>(list.size()),
                      list.empty() ? "-" : list.front().sound.c_str());
    } else if (table == "anim") {
        // "<figure>/<animation>": the animation's fields.
        const std::size_t slash = key.rfind('/');
        MapObjectManager* manager = g_clientWorld.GetMapObjectManager();
        if (!manager) return -3;
        const FigureAnimation* animation =
            slash == std::string::npos ? nullptr : manager->FindFigureAnimation(key.substr(0, slash), key.substr(slash + 1));
        if (!animation) return 0;
        std::snprintf(line, sizeof(line), "chance %u frames %u key %x flags %x moment %u", animation->chance,
                      animation->frameCount, animation->key, animation->flags, animation->moment);
    } else if (table == "track") {
        // "<figure>/<animation>/<part>": the part's track as the figure manager loads it (FindModel, FindAnimation).
        const std::size_t second = key.rfind('/');
        const std::size_t first = second == std::string::npos || second == 0 ? std::string::npos : key.rfind('/', second - 1);
        if (!g_clientWorld.figureManager) return -3;
        if (first == std::string::npos) return 0;
        FigureModel* model = g_clientWorld.figureManager->FindModel(key.substr(0, first));
        MappedResource* animation = model ? model->FindAnimation(key.substr(first + 1, second - first - 1)) : nullptr;
        std::uint32_t size = 0;
        void* data = animation ? animation->GetFile(key.substr(second + 1), &size) : nullptr;
        figure_gltf::Track track;
        if (!data || !figure_gltf::ReadTrack(std::string_view(static_cast<const char*>(data), size), &track)) return 0;
        std::snprintf(line, sizeof(line), "rotations %d positions %d frames %d slots %d",
                      static_cast<int>(track.rotations.size() / 4), static_cast<int>(track.positions.size() / 3),
                      track.frames, track.slots);
    } else {
        // blood, footprint or scorch: a print of the ground's surface by name.
        const PrintLook* dry = nullptr;
        const PrintLook* wet = nullptr;
        const auto find = [&](const auto& records) {
            for (const auto& record : records) {
                if (record.name != key) continue;
                dry = &record.dryLook;
                wet = &record.wetLook;
                return;
            }
        };
        if (table == "blood") find(g_printsDatabase.blood);
        else if (table == "scorch") find(g_printsDatabase.scorch);
        else find(g_printsDatabase.footprints);
        if (!dry) return 0;
        std::snprintf(line, sizeof(line), "dry %g %d %d wet %g %d %d", dry->alpha, dry->life, dry->lifetime, wet->alpha,
                      wet->life, wet->lifetime);
    }
    *text = line;
    return 1;
}

long LocalWeaponDamage(const char* weapon, float* minimum, float* maximum) {
    if (!g_serverWorld.players) return -3;
    Player* player = g_serverWorld.GetPlayer(0);
    if (!player) return -3;
    std::vector<Item*> items = player->items;
    if (Party* party = player->GetParty()) items.insert(items.end(), party->items.begin(), party->items.end());
    for (Item* item : items) {
        if (item->Kind() != ItemKind::Weapon) continue;
        const itemdb::WeaponRecord* record = itemdb::WeaponAt(PrototypeIndex(item->prototypeId));
        if (!record || record->name != weapon) continue;
        *minimum = WeaponMinDamage(item->prototypeId);
        *maximum = WeaponMaxDamage(item->prototypeId);
        return 1;
    }
    return 0;
}

std::string DataEffectStep(const std::string& argument) {
    std::istringstream words(argument);
    std::string name, verb;
    words >> name >> verb;
    if (name.empty() || (verb != "set" && verb != "fire" && verb != "check" && verb != "value"))
        return "effect " + argument +
               ": not <name> set <input> <value> | fire <event> | value <input> <value> | check <particles> [<stage>]";
    if (!g_clientWorld.particleManager) return "effect " + name + ": no client world";
    const std::int32_t id = effectdata::IdOf(name);
    std::vector<ParticleSource*> sources;
    ObjectListIterator it;
    for (EngineObject* object = it.First(g_clientWorld.particleManager.get()); object; object = it.Next()) {
        auto* source = static_cast<ParticleSource*>(object);
        if (source->type == id && source->data) sources.push_back(source);
    }
    if (sources.empty()) return "effect " + name + ": no source of it";
    if (verb == "set" || verb == "fire") {
        std::string input;
        words >> input;
        std::vector<std::string> values;
        for (std::string value; words >> value;) values.push_back(value);
        for (ParticleSource* source : sources) {
            bool done = false;
            char* end = nullptr;
            if (verb == "fire") done = effectdata::Fire(*source, input);
            else if (values.size() == 3)
                done = effectdata::SetInput(*source, input,
                                            Vector3{std::strtof(values[0].c_str(), nullptr),
                                                    std::strtof(values[1].c_str(), nullptr),
                                                    std::strtof(values[2].c_str(), nullptr)});
            else if (values.size() == 1 && (std::strtof(values[0].c_str(), &end), *end == '\0'))
                done = effectdata::SetInput(*source, input, std::strtof(values[0].c_str(), nullptr));
            else if (values.size() == 1) done = effectdata::SetInput(*source, input, std::string_view(values[0]));
            if (!done) return "effect " + name + ": no input " + input + " that takes " + verb + " " + argument;
        }
        Log("harness: effect %s %s %s on %zu sources", name.c_str(), verb.c_str(), input.c_str(), sources.size());
        return {};
    }
    if (verb == "value") {
        std::string input;
        float expected = 0.0f;
        words >> input >> expected;
        const effectdata::State& state = *sources.front()->data;
        const effectdata::Input* found = state.record->FindInput(input);
        if (!found) return "effect " + name + ": no input " + input;
        const float got = state.inputs[found->slot];
        Log("harness: effect %s: input %s is %g (expected %g)", name.c_str(), input.c_str(), got, expected);
        if (got != expected) return "effect " + name + ": input " + input + " is " + std::to_string(got);
        return {};
    }
    int least = 0;
    std::string stage;
    words >> least >> stage;
    std::size_t particles = 0;
    for (ParticleSource* source : sources) particles += source->particles.size();
    const effectdata::State& state = *sources.front()->data;
    const std::string now = state.stage >= 0 ? state.record->stages[state.stage].name : std::string("-");
    Log("harness: effect %s: %zu sources, %zu particles, stage %s, age %d (expected %d particles or more%s%s)",
        name.c_str(), sources.size(), particles, now.c_str(), state.age, least, stage.empty() ? "" : ", stage ",
        stage.c_str());
    if (static_cast<int>(particles) < least) return "effect " + name + ": " + std::to_string(particles) + " particles";
    if (!stage.empty() && stage != now) return "effect " + name + ": stage " + now + ", expected " + stage;
    return {};
}

long ModManagerFile(const char* which, const char* name) {
    const std::string manager = which;
    std::uint32_t size = 0;
    if (manager == "figures") {
        if (!g_clientWorld.figureManager) return -3;
        void* data = g_clientWorld.figureManager->GetFile(name, &size);
        if (data) g_clientWorld.figureManager->CloseFile(data);
        return data ? static_cast<long>(size) : 0;
    }
    if (manager == "textures") {
        TextureManager* textures = g_clientWorld.GetTextureManager();
        if (!textures) return -3;
        void* data = textures->GetFile(name, &size);
        if (data) textures->CloseFile(data);
        return data ? static_cast<long>(size) : 0;
    }
    if (manager == "links") {
        LinkFigureLoader* loader = g_clientWorld.FigureLoader();
        if (!loader) return -3;
        for (LinkArchive& archive : loader->archives) {
            if (void* data = archive.resource->GetFile(name, &size)) {
                archive.resource->CloseFile(data);
                return static_cast<long>(size);
            }
        }
        return 0;
    }
    return -4;
}

void PlayStream(const char* name) { g_clientWorld.sounds->RequestStream(name, StreamSwitch::Now); }

bool SwitchMod(const char* folder) { return app::RequestModSwitch(folder); }

bool ModSwitchPending() { return app::ModSwitchPending(); }

bool MapsFileExists(const char* name) { return MapFileExists(name); }

bool GroundHeights(float x, float y, float* client, float* server) {
    if (!g_clientWorld.terrain || !g_clientWorld.terrain->mapLoaded || !g_serverWorld.aiMap.loaded) return false;
    *client = g_clientWorld.terrain->HeightAt(x, y, terrain::Layer::Ground);
    *server = g_serverWorld.GridHeight(x, y);
    return true;
}

namespace {
void (*g_streamWrites)(void* stream, std::uint32_t size, void* const* frame) = nullptr;
void ObservedStreamWrite(MemoryStream* stream, std::uint32_t size, void* const* frame) { g_streamWrites(stream, size, frame); }
} // namespace

void ObserveStreamWrites(void (*observer)(void* stream, std::uint32_t size, void* const* frame)) {
    g_streamWrites = observer;
    g_memoryStreamWriteObserver = &ObservedStreamWrite;
}

namespace {

// A ViewPose as the camera trace samples it.
CameraPose PoseOf(const camera::ViewPose& v) {
    CameraPose pose;
    pose.focusX = v.focus.x;
    pose.focusY = v.focus.y;
    pose.focusZ = v.focus.z;
    pose.turnZ = v.heading.z;
    pose.turnW = v.heading.w;
    pose.distance = v.distance;
    pose.position[0] = v.position.x;
    pose.position[1] = v.position.y;
    pose.position[2] = v.position.z;
    pose.rotation[0] = v.rotation.w;
    pose.rotation[1] = v.rotation.x;
    pose.rotation[2] = v.rotation.y;
    pose.rotation[3] = v.rotation.z;
    return pose;
}

} // namespace

CameraState Camera(const void* camera) {
    const auto* c = static_cast<const ::Camera*>(camera);
    CameraState state;
    state.own = PoseOf(camera::ViewPose{c->localPosition, c->localRotation, c->focus, c->viewRotation, c->distance});
    state.drawn = PoseOf(camera::DrawnView(c));
    state.flags = c->flags;
    return state;
}

CameraView ViewCamera() {
    const ::Camera& c = g_camera;
    const float degrees = 1.0f / ei::kRadiansPerDegree;
    CameraView view{c.focus.x, c.focus.y, c.focus.z, 2.0f * std::atan2(c.viewRotation.z, c.viewRotation.w) * degrees,
                    c.distance, c.pitch * degrees};
    const camera::ViewPose& drawn = camera::FrameView();
    view.x = drawn.position.x;
    view.y = drawn.position.y;
    view.z = drawn.position.z;
    terrain::Terrain* ground = g_clientWorld.terrain.get();
    if (!ground) return view;
    const auto above = [ground](const Vector3& p) { return p.z - ground->CalmSurfaceHeight(p.x, p.y); };
    const float slope = std::tan(c.fieldOfView * 0.5f);
    const float halfX = c.nearDistance * slope / g_sceneRenderer.axisScale;
    const float halfY = c.nearDistance * slope / (4.0f / 3.0f * g_sceneRenderer.axisScaleY);
    const Vector3 across = drawn.rotation.AxisX(), up = drawn.rotation.AxisY();
    const Vector3 centre = drawn.position + drawn.rotation.AxisZ() * c.nearDistance;
    float least = above(drawn.position);
    for (int i = -4; i <= 4; ++i)
        for (int j = -4; j <= 4; ++j)
            least = std::min(least, above(centre + across * (halfX * i / 4.0f) + up * (halfY * j / 4.0f)));
    view.nearGround = least;
    return view;
}

void SetClimate(int climate) { g_serverWorld.SetClimate(climate); }

WeatherState Weather() {
    const RainSnow& layer = g_clientWorld.rainSnow;
    return WeatherState{g_serverWorld.weather, g_serverWorld.weatherStrength, g_serverWorld.weatherFactor,
                        layer.mode,           layer.targetMode,               layer.density};
}

CameraView PlaceCamera(const CameraPlacement& placement) {
    ::Camera& c = g_camera;
    c.SetTargetId(-1);
    c.flags &= ~::Camera::kFollowing;
    c.panTarget = camera::kZeroVector;
    c.panVelocity = camera::kZeroVector;
    if (placement.setDistance) c.distance = placement.distance;
    if (placement.setPitch) c.pitch = placement.pitch * ei::kRadiansPerDegree;
    c.SetFocus(Vector3{placement.x, placement.y, 0.0f});
    if (placement.setHeading) c.SetHeading(placement.heading * ei::kRadiansPerDegree);
    else c.ApplyView();
    return ViewCamera();
}

std::string HudState(void* screen, const std::string& argument) { return HudStateOf(screen, argument); }

namespace {

// The object's place at `pose`, or with `part` that part's place in the figure (the pose alone).
ObjectPlace PlaceAt(const FigureMesh* part, const drawn::Pose& pose) {
    if (part) return ObjectPlace{part->localPosition.x, part->localPosition.y, part->localPosition.z, 0.0f};
    const Quaternion& q = pose.rotation;
    return ObjectPlace{pose.position.x, pose.position.y, pose.position.z, 2.0f * std::atan2(q.z, q.w)};
}

// A light's place (no yaw).
ObjectPlace LightPlace(const Vector3& at) { return ObjectPlace{at.x, at.y, at.z, 0.0f}; }

} // namespace

void SetAnimationBlends(bool on) { FigureAnimationState::SetBlends(on); }

std::uint32_t WorldBlendsBegun() { return FigureAnimationState::WorldBlendsBegun(); }

bool ClientObject(int id, const char* part, ObjectState* out) {
    MapObject* found = nullptr;
    for (MapObject* object : g_clientWorld.objects) {
        if (!object) continue;
        if (id != 0 ? object->id == id : object->name == "efar1") {
            found = object;
            break;
        }
    }
    if (!found) return false;
    if (part && std::strcmp(part, "light") == 0) {
        MapObjectClientSpecific* side = found->companion && found->companion->GetClassId() == ClassId::MapObjectClientSpecific
                                            ? found->ClientCompanion()
                                            : nullptr;
        Light* light = side ? static_cast<Light*>(side->light) : nullptr;
        if (!light) return false;
        out->id = found->id;
        out->own = LightPlace(light->localPosition);
        out->drawn = LightPlace(drawn::LightPosition(light));
        return true;
    }
    const FigureMesh* piece = nullptr;
    if (part && *part && std::strcmp(part, "lastdraw") != 0) {
        piece = found->FindPart(part);
        if (!piece) return false;
    }
    out->id = found->id;
    if (FigureAnimationState* state = found->animation) {
        out->animation = state->current ? state->current->name : "";
        out->frame = state->time;
        out->blend = state->blendLength != 0 ? static_cast<float>(platform::FrameClock() - state->blendStart) /
                                                   static_cast<float>(state->blendLength)
                                             : 1.0f;
    }
    if (part && std::strcmp(part, "lastdraw") == 0) {
        // Where its last draw put it: the translation of the world matrix the draw set (row-major, the place in the
        // last column). After a frame, an interface figure's draw when one drew it last (the HUD unit panel's).
        const float* m = found->worldMatrix.m;
        out->own = out->drawn = ObjectPlace{m[3], m[7], m[11], 0.0f};
        return true;
    }
    out->own = PlaceAt(piece, drawn::OwnPose(found));
    out->drawn = PlaceAt(piece, drawn::PoseOf(found));
    return true;
}

bool ClientLight(int id, ObjectState* out) {
    bool found = false;
    ObjectListIterator walk;
    for (EngineObject* object = walk.First(g_clientWorld.TimedObjectManager()); object && !found; object = walk.Next()) {
        if (object->GetClassId() != ClassId::EffectPointLight) continue;
        auto* effect = static_cast<EffectPointLight*>(object);
        Light* light = effect->light;
        if (!light || (id != 0 ? effect->id != id : effect->moving == 0)) continue;
        out->id = effect->id;
        out->own = LightPlace(light->localPosition);
        out->drawn = LightPlace(drawn::LightPosition(light));
        found = true;
    }
    return found;
}

std::vector<std::string> ClientBolts() {
    std::vector<std::string> bolts;
    ObjectListIterator walk;
    for (EngineObject* object = walk.First(g_clientWorld.TimedObjectManager()); object; object = walk.Next()) {
        if (object->GetClassId() != ClassId::EffectLightning) continue;
        auto* bolt = static_cast<EffectLightning*>(object);
        char text[160];
        std::snprintf(text, sizeof(text), "%.2f %.2f %.2f - %.2f %.2f %.2f", bolt->from.x, bolt->from.y, bolt->from.z,
                      bolt->to.x, bolt->to.y, bolt->to.z);
        bolts.emplace_back(text);
    }
    return bolts;
}

std::vector<std::string> ClientHurt() {
    std::vector<std::string> hurt;
    for (MapObject* object : g_clientWorld.objects) {
        if (!object || object->GetClassId() != ClassId::Unit) continue;
        const UnitVitalsPacked& vitals = object->packedVitals;
        if (object->IsDead() || vitals.hitPoints >= vitals.maxHitPoints) continue;
        char text[96];
        std::snprintf(text, sizeof(text), "id %d hp %d of %d", object->id, vitals.hitPoints, vitals.maxHitPoints);
        hurt.emplace_back(text);
    }
    return hurt;
}

} // namespace ei::verify::image
