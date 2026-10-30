// The game's global objects and the steps' calls of the game in the image (image_side.h; harness_objects.h,
// harness.cpp).
#include "image_side.h"

#include "../../../src/hook/log.h"

#include "game/app.h"
#include "game/camera.h"
#include "game/console.h"
#include "game/core_streams.h"
#include "game/game_singletons.h"
#include "game/item.h"
#include "game/net_classes.h"
#include "game/model.h"
#include "game/object_tables.h"
#include "game/options.h"
#include "game/party_manager.h"
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
#include "game/world_glue.h"
#include "lib/crt.h"
#include "platform/win32/platform_win32.h"

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

// The unit's slots (thiscall, RET 4) and a weapon's kind, for harness_hud_read.inc.
Item* UnitQuickItem(Unit* unit, std::uint32_t slot) { return unit->GetQuickItem(slot); }
Item* UnitSpell(Unit* unit, std::uint32_t slot) { return unit->GetSpell(slot); }
ItemWeapon* UnitWeapon(Unit* unit, std::uint32_t slot) { return unit->GetWeapon(slot); }
int WeaponType(ItemWeapon* weapon) { return weapon->GetType(); }

#include "../harness_hud_read.inc"

} // namespace

const char* WindowTitle() { return app::g_windowTitle; }
void* MainWindow() { return app::g_mainWindow; }
std::uint8_t& InterfaceHasScreen() { return app::g_interfaceHasScreen; }
std::uint8_t& RecordingFrames() { return app::g_recordingFrames; }
bool AppActive() { return app::g_active != 0; }
int& FlowState() { return g_gameFlow.state; }
void SetLoadTarget(const char* slot) { g_gameFlow.requestTarget = slot; }
void ClearShowTutorial() { options::g_options.showTutorial = 0; }
void SetMasterHost(const char* host) { options::g_options.masterServerName = host; }
void SetWaterWaves(bool on) { options::g_options.enableWaterWaves = on; }

CursorState Cursor() {
    const ::Cursor* cursor = &g_cursor;
    CursorState state;
    state.mode = cursor->mode;
    state.thread = cursor->thread.Started();
    state.imageShown = cursor->imageShown;
    if (const CursorImage* image = cursor->image) {
        state.image = true;
        state.hotX = image->hotX;
        state.hotY = image->hotY;
    }
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
int CampaignMode() { return g_serverWorld.sessionKind; }
void* ClientWorldObject() { return &g_clientWorld; }
void* SceneDevice() { return g_sceneRenderer.device.Get(); }
// Our renderer takes no device lock: the harness locks game.exe's only (in its original run).
void* DeviceLock() { return nullptr; }

int ServerUpdate(void* server) {
    static_cast<WorldServer*>(server)->Update();
    return 1;
}
int ClientSegment(void* client) { return static_cast<WorldClient*>(client)->RunTick() ? 1 : 0; }
void CameraUpdate(void* camera, float seconds) { static_cast<::Camera*>(camera)->Update(seconds); }
void FrameStep(bool draw, bool cursor) { app::FrameStep(draw, cursor); }
void CursorLock(void* cursor) { static_cast<::Cursor*>(cursor)->TakeLock(); }

void ConsoleExecute(const char* text) { console::Execute(text); }
void SendLeave04(int id) { GameSendLeave04(id); }
void SetConstructionVariable(std::uint32_t value) { GameSetConstructionVariable(value); }
void ObserveConsoleLines(void (*observer)(const char* line)) { console::g_consoleLineObserver = observer; }
void ObserveDirectInput(void (*observer)(IDirectInput7A* input)) { g_directInputObserver = observer; }
unsigned& UpdatePeriod() { return reinterpret_cast<unsigned&>(worlddata::g_updatePeriod); }
const char* DataFolder() { return DataFolderText(); }
void SetFixedSeed(void (*fill)(std::uint32_t* results)) {
    g_fill = fill;
    IsaacRandom::SetSeedHook(&FixedSeed);
}
int TextKey(const char* name) { return TextKeyFromName(name); }
void* LocalWorldPlayer(int player) { return g_serverWorld.GetPlayer(player); }
void* WorldHandle() { return g_serverWorld.worldHandle.Get(); }
const char* ServerZoneName() { return g_serverWorld.zoneName.c_str(); }

void* PickAt2(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<WorldClient*>(world)->PickAt2(&p);
}
void* UnitAt(void* world, int x, int y) {
    const Point p{x, y};
    return static_cast<EngineObject*>(static_cast<WorldClient*>(world)->UnitAt(&p));
}
int ScreenRect(void* world, int id, RECT* rect) {
    int found = 0;
    for (const ScreenEntry& entry : static_cast<WorldClient*>(world)->screenEntries) {
        if (static_cast<int>(entry.objectId) != id) continue;
        static_assert(sizeof(Rect) == sizeof(RECT) && offsetof(Rect, bottom) == offsetof(RECT, bottom));
        std::memcpy(rect, &entry.rect, sizeof(RECT));
        found = 1;
    }
    return found;
}
int ObjectId(const void* object) { return static_cast<const EngineObject*>(object)->id; }
std::uint32_t ObjectFlags(const void* object) { return static_cast<const EngineObject*>(object)->flags; }
void* PlayerConnection(void* player) { return static_cast<Player*>(player)->connection; }
int ConnectionState(void* connection) { return static_cast<::PlayerConnection*>(connection)->state; }
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
void ScaleToScreen(long* x, long* y) {
    Point point{*x, *y};
    g_interface.ScalePointToScreenEntry(&point);
    *x = point.x;
    *y = point.y;
}
void InterfaceSize(int* width, int* height) {
    *width = g_interface.width;
    *height = g_interface.height;
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

} // namespace ei::verify::image
