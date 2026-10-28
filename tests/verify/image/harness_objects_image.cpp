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
#include "render/render.h"

#include <ddraw.h>

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
    g_fill(random.results_);
}

// The unit's slots (thiscall, RET 4) and a weapon's kind, for harness_hud_read.inc.
CItem* UnitQuickItem(CUnit* unit, std::uint32_t slot) { return unit->GetQuickItem(slot); }
CItem* UnitSpell(CUnit* unit, std::uint32_t slot) { return unit->GetSpell(slot); }
CItemWeapon* UnitWeapon(CUnit* unit, std::uint32_t slot) { return unit->GetWeapon(slot); }
int WeaponType(CItemWeapon* weapon) { return weapon->GetType(); }

#include "../harness_hud_read.inc"

} // namespace

const char* WindowTitle() { return app::g_windowTitle; }
void* MainWindow() { return app::g_mainWindow; }
std::uint8_t& InterfaceHasScreen() { return app::g_interfaceHasScreen; }
std::uint8_t& RecordingFrames() { return app::g_recordingFrames; }
bool AppActive() { return app::g_active != 0; }
int& FlowState() { return g_gameFlow->state_; }
void SetLoadTarget(const char* slot) { g_gameFlow->requestTarget_ = slot; }
void ClearShowTutorial() { options::g_options->showTutorial_ = 0; }
void SetMasterHost(const char* host) { options::g_options->masterServerName_ = host; }
void SetWaterWaves(bool on) { options::g_options->enableWaterWaves_ = on; }

CursorState Cursor() {
    const CCursor* cursor = &g_cursor.Get();
    CursorState state;
    state.mode = cursor->mode_;
    state.thread = cursor->thread_ != nullptr;
    state.imageShown = cursor->imageShown_;
    if (const CursorImage* image = cursor->image_) {
        state.image = true;
        state.hotX = image->hotX;
        state.hotY = image->hotY;
    }
    return state;
}

void* QueuedWindow() { return g_interface->queuedWindow_; }
bool CloseAllPending() { return g_interface->closeAll_; }
bool ScreenFade(bool* running, float* alpha) {
    const CScreenFade* fade = g_interface->fade_;
    if (!fade) return false;
    *running = fade->active_ != 0;
    *alpha = fade->alpha_;
    return true;
}
int CampaignMode() { return g_serverWorld->sessionKind_; }
void* ClientWorldObject() { return &g_clientWorld.Get(); }
void* SceneDevice() { return g_sceneRenderer->device_.device; }
void* DeviceLock() { return &g_deviceLock; }
bool RenderActive() { return render::Active(); }

int ServerUpdate(void* server) { return static_cast<CWorldServer*>(server)->Update(); }
int ClientSegment(void* client) { return static_cast<CWorldClient*>(client)->Segment(); }
void CameraUpdate(void* camera, float seconds) { static_cast<CCamera*>(camera)->Update(seconds); }
void FrameStep(bool draw, bool cursor) { app::FrameStep(draw, cursor); }
void CursorLock(void* cursor) { static_cast<CCursor*>(cursor)->TakeLock(); }

void ConsoleExecute(const char* text) { console::Execute(text); }
void SendLeave04(int id) { GameSendLeave04(id); }
void SetConstructionVariable(std::uint32_t value) { GameSetConstructionVariable(value); }
void ObserveConsoleLines(void (*observer)(const char* line)) { console::g_consoleLineObserver = observer; }
unsigned& UpdatePeriod() { return reinterpret_cast<unsigned&>(worlddata::g_updatePeriod); }
const char* DataFolder() { return DataFolderText(); }
void SetFixedSeed(void (*fill)(std::uint32_t* results)) {
    g_fill = fill;
    IsaacRandom::SetSeedHook(&FixedSeed);
}
int TextKey(const char* name) { return TextKeyFromName(name); }
void* LocalWorldPlayer(int player) { return g_serverWorld->GetPlayer(player); }
void* WorldShared5C() { return g_serverWorld->shared5C_.Get(); }
const char* ServerZoneName() { return g_serverWorld->zoneName_.c_str(); }

void* PickAt2(void* world, int x, int y) {
    const POINT p{x, y};
    return static_cast<CWorldClient*>(world)->PickAt2(&p);
}
void* UnitAt(void* world, int x, int y) {
    const POINT p{x, y};
    return static_cast<CEngineObject*>(static_cast<CWorldClient*>(world)->UnitAt(&p));
}
int ScreenRect(void* world, int id, RECT* rect) {
    int found = 0;
    for (const ScreenEntry& entry : static_cast<CWorldClient*>(world)->screenEntries_) {
        if (static_cast<int>(entry.objectId) != id) continue;
        *rect = entry.rect;
        found = 1;
    }
    return found;
}
int ObjectId(const void* object) { return static_cast<const CEngineObject*>(object)->id_; }
std::uint32_t ObjectFlags(const void* object) { return static_cast<const CEngineObject*>(object)->flags_; }
void* PlayerConnection(void* player) { return static_cast<CPlayer*>(player)->connection_; }
int ConnectionState(void* connection) { return static_cast<CPlayerConnection*>(connection)->state_; }
bool CampItems(void* camp, int* page, std::vector<void*>* items, std::vector<unsigned>* types) {
    auto* pages = static_cast<CCampPages*>(static_cast<CUiWindow*>(camp));
    *page = pages->page_;
    const std::vector<CItem*>* source = pages->itemStrip_ ? pages->itemStrip_->source_ : nullptr;
    if (!source) return false;
    for (int i = 0; i < std::ssize(*source); ++i) {
        CItem* item = (*source)[i];
        items->push_back(item);
        types->push_back(item ? static_cast<unsigned>(item->type_) : 0);
    }
    return true;
}
void CampTakeItem(void* camp, void* item, int mode) {
    static_cast<CCampPages*>(static_cast<CUiWindow*>(camp))->TakeItem(static_cast<CItem*>(item), mode);
}
void PickSurface(void* world, float* out, int x, int y) {
    static_cast<CWorldClient*>(world)->PickSurface(reinterpret_cast<Vector3*>(out), x, y);
}
void LoadUnitFaces(void* world) { static_cast<CWorldClient*>(world)->LoadUnitFaces(); }
void BlinkOpen(void* half) { static_cast<CUnitClientSpecific*>(half)->BlinkOpen(true); }
void BlinkClose(void* half) { static_cast<CUnitClientSpecific*>(half)->BlinkClose(true); }

bool UnitFaceHalves(std::vector<void*>* halves) {
    CWorldClient* world = &g_clientWorld.Get();
    if (!world) return false;
    for (CMapObject* object : world->objects_) {
        if (!object || object->GetClassId() != world::kClassUnit) continue;
        auto* half = reinterpret_cast<CUnitClientSpecific*>(static_cast<CUnit*>(object)->controller);
        if (half && half->faceTexture) halves->push_back(half);
    }
    return true;
}

int RegionCount(void* window) { return std::ssize(static_cast<CUiWindow*>(window)->regions_); }
bool Region(void* window, int index, RegionInfo* region) {
    const CUiRegion* found = static_cast<CUiWindow*>(window)->regions_[index];
    if (!found) return false;
    region->left = found->bounds_.left;
    region->top = found->bounds_.top;
    region->right = found->bounds_.right;
    region->bottom = found->bounds_.bottom;
    region->offsetX = found->offsetX_;
    region->offsetY = found->offsetY_;
    region->enabled = found->enabled_;
    return true;
}
void ScaleToScreen(long* x, long* y) {
    POINT point{*x, *y};
    g_interface->ScalePointToScreenEntry(&point);
    *x = point.x;
    *y = point.y;
}
void InterfaceSize(int* width, int* height) {
    *width = g_interface->width_;
    *height = g_interface->height_;
}
bool SaveLoadSliding(void* window) { return static_cast<CSaveLoadWindow*>(window)->sliding_; }
bool OptionsSliding(void* window) { return static_cast<COptionsWindow*>(window)->sliding_; }
bool HasChatOverlay() { return g_interface->root_ != nullptr; }
void ChatKeyEnter() { g_interface->ChatKeyEnter(); }
void CloseChat() { g_interface->root_->SetOpen(false); }
bool HasClassTemplate(int id) { return g_partyManager->templates.Find(id) != nullptr; }
const void* FindMap(const char* name) { return g_mapDatabase->FindMap(name); }
int MapKind(const void* record) { return static_cast<const MapRecord*>(record)->kind; }
void LeaveToZone(void* connection, const char* name, int exit) {
    static_cast<CPlayerServer*>(connection)->LeaveToZone(name, exit);
}

bool RestoreSurfaces(long* result) {
    CTextureDevice* device = g_sceneRenderer->device_.device;
    if (!device || !device->ddraw_) return false;
    EnterCriticalSection(&g_deviceLock);
    *result = DD_OK;   // the image's device object holds a token: nothing to restore
    device->lastLoadResult_ = DD_OK;
    LeaveCriticalSection(&g_deviceLock);
    return true;
}

int& GamePort() { return net::g_gamePort; }

const char* TextFolder() { return g_textFolder->c_str(); }

long ModFile(const char* path, const char* name) {
    void* memory = ::operator new(sizeof(CMappedResource));
    std::memset(memory, 0, sizeof(CMappedResource));
    auto* res = new (memory) CMappedResource();
    long got = -2;   // the archive did not open
    if (res->CMappedResource::OpenResource(path, 0)) {
        std::uint32_t size = 0;
        void* data = res->GetFile(name, &size);   // slot 5 (00671B40, Rebirth's ResOverride)
        got = data ? static_cast<long>(size) : 0;
        if (data) res->CloseFile(data);
    }
    res->CMappedResource::Destroy();
    ::operator delete(memory);
    return got;
}

namespace {
void (*g_streamWrites)(void* stream, std::uint32_t size, void* const* frame) = nullptr;
void ObservedStreamWrite(CMemoryStream* stream, std::uint32_t size, void* const* frame) { g_streamWrites(stream, size, frame); }
} // namespace

void ObserveStreamWrites(void (*observer)(void* stream, std::uint32_t size, void* const* frame)) {
    g_streamWrites = observer;
    g_memoryStreamWriteObserver = &ObservedStreamWrite;
}

CameraState Camera(const void* camera) {
    const auto* c = static_cast<const CCamera*>(camera);
    CameraState state;
    state.focusX = c->focus_.x;
    state.focusY = c->focus_.y;
    state.turnZ = c->viewRotation_.z;
    state.turnW = c->viewRotation_.w;
    state.distance = c->distance_;
    state.flags = c->flags_;
    return state;
}

std::string HudState(void* screen, const std::string& argument) { return HudStateOf(screen, argument); }

} // namespace ei::verify::image
