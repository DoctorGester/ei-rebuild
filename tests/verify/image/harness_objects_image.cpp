// The game's global objects and the steps' calls of the game in the image (image_side.h; harness_objects.h,
// harness.cpp).
#include "image_side.h"

#include "../../../src/platform/log.h"

#include "game/app.h"
#include "game/camera.h"
#include "game/console.h"
#include "game/core_streams.h"
#include "game/drawn_motion.h"
#include "game/game_singletons.h"
#include "game/figure_manager.h"
#include "game/item.h"
#include "game/map_object_figures.h"
#include "game/map_object_shadow.h"
#include "game/net_classes.h"
#include "game/model.h"
#include "game/object_tables.h"
#include "game/options.h"
#include "game/party_manager.h"
#include "game/player_client.h"
#include "game/player_messages.h"
#include "game/random.h"
#include "game/resource.h"
#include "game/spell.h"
#include "game/texture.h"
#include "game/ui.h"
#include "game/ui_camp.h"
#include "game/ui_cursor.h"
#include "game/ui_screens.h"
#include "game/unit.h"
#include "game/unit_client_models.h"
#include "game/world.h"
#include "game/world_data.h"
#include "game/world_effect.h"
#include "game/net_coder.h"
#include "game/sound.h"
#include "game/world_map_files.h"
#include "lib/crt.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
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
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", window, battle->pendingAction,
        battle->pendingSlot, battle->pendingItem, static_cast<int>(std::ssize(battle->party)));
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

void* MainWindow() { return app::g_mainWindow; }
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

void* QueuedWindow() { return g_interface.queuedWindow; }
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
void* SceneDevice() { return g_sceneRenderer.device.Get(); }

int ServerUpdate(void* server) {
    static_cast<WorldServer*>(server)->Update();
    return 1;
}
int ClientSegment(void* client) { return static_cast<WorldClient*>(client)->RunTick() ? 1 : 0; }
void CameraUpdate(void* camera, float seconds) { static_cast<::Camera*>(camera)->Update(seconds); }
void FrameStep(bool draw) { app::FrameStep(draw); }

void ConsoleExecute(const char* text) { console::Execute(text); }
void RequestGameState(int id) {
    g_interface.closeAll = 1;
    g_clientWorld.LocalView()->RequestGameState(id);
}
void SetConstructionVariable(std::uint32_t value) { g_serverWorld.SetConstructionVariable(value); }
void ObserveConsoleLines(void (*observer)(const char* line)) { console::g_consoleLineObserver = observer; }
unsigned& UpdatePeriod() { return reinterpret_cast<unsigned&>(worlddata::g_updatePeriod); }
const char* DataFolder() { return DataFolderText(); }
void SetFixedSeed(void (*fill)(std::uint32_t* results)) {
    g_fill = fill;
    IsaacRandom::SetSeedHook(&FixedSeed);
}
void ReseedRandom() {
    g_random.seeded = 0;
    g_random.Init();
    g_clientRandom.seeded = 0;
    g_clientRandom.InitFrom(g_random);
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
void* WorldHandle() { return g_serverWorld.worldHandle.Get(); }
const char* ServerZoneName() { return g_serverWorld.zoneName.c_str(); }
const char* LoadingMovieName() { return app::g_loadingMovieName; }

void* PickAt2(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<WorldClient*>(world)->PickAt2(&p);
}
void* UnitAt(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<EngineObject*>(static_cast<WorldClient*>(world)->UnitAt(&p));
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
    if (PlayerServer* server = base->ServerSide()) return static_cast<int>(server->state);
    return static_cast<int>(base->ClientSide()->state);
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
        auto* half = reinterpret_cast<UnitClientSpecific*>(static_cast<Unit*>(object)->controller);
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

const void* ClientObject(int id) {
    for (MapObject* object : g_clientWorld.objects)
        if (object && object->id == id) return object;
    return nullptr;
}

int RegionCount(void* window) { return static_cast<int>(std::ssize(static_cast<UiWindow*>(window)->regions)); }
bool Region(void* window, int index, RegionInfo* region) {
    const UiRegion* found = static_cast<UiWindow*>(window)->regions[index];
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
    Point point{*x, *y};
    g_interface.ScalePointToScreen(&point);
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
    for (UiWindow* child : container->children) {
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
    void* memory = ::operator new(sizeof(MappedResource));
    std::memset(memory, 0, sizeof(MappedResource));
    auto* res = new (memory) MappedResource();
    long got = -2;   // the archive did not open
    if (res->MappedResource::OpenResource(path)) {
        std::uint32_t size = 0;
        void* data = res->GetFile(name, &size);   // slot 5 (00671B40, Rebirth's ResOverride)
        got = data ? static_cast<long>(size) : 0;
        if (data) res->CloseFile(data);
    }
    res->MappedResource::Destroy();
    ::operator delete(memory);
    return got;
}

long ModList(const char* path, const char* pattern) {
    void* memory = ::operator new(sizeof(MappedResource));
    std::memset(memory, 0, sizeof(MappedResource));
    auto* res = new (memory) MappedResource();
    long count = -2;   // the archive did not open
    if (res->MappedResource::OpenResource(path)) {
        std::vector<std::string> names;
        res->ListFiles(&names, pattern);   // Rebirth's ResOverride: the mod's archives' names too
        count = static_cast<long>(names.size());
    }
    res->MappedResource::Destroy();
    ::operator delete(memory);
    return count;
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

void PlayStream(const char* name) { g_clientWorld.sounds->RequestStream(name, 4); }

bool MapsFileExists(const char* name) { return MapFileExists(name); }

void WriteMapBytes(const char* name) {
    static std::uint8_t bytes[] = {'S', '1', '9', 'd', 10};
    BufferView view(bytes, bytes + sizeof(bytes));
    WriteMapFile(name, &view);
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
        out->blend = state->blendLength != 0 ? static_cast<float>(platform::Milliseconds() - state->blendStart) /
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
    ObjectListIterator walk{nullptr, {}};
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
    if (walk.manager) walk.manager->ForgetReference();
    return found;
}

} // namespace ei::verify::image
