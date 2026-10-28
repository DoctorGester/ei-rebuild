#pragma once

// The harness's image side (tests/verify/image): what it reads and calls of our code, behind functions that name no
// class of ours. The shared harness calls them only in the image's runs (ei::Standalone()); in game.exe's runs it
// reads game.exe's objects through their views (exe_objects.h) and calls game.exe's code (harness_original.h). Only
// the files in this folder include our game headers, so run_all's reference key (tools/run_all.py, harness_files)
// leaves them out: our classes do not decide what the original's runs do (ticket H30b).

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ei::verify::image {

// ---- the random trace (harness_clock.cpp; image/harness_clock_image.cpp)

// One draw: the caller, the value, what drew it (" rand()", " (another generator)" or ""), and the generator's place
// in its stream after the draw.
using DrawWriter = void (*)(std::uintptr_t caller, std::uint32_t value, const char* what, std::uint32_t where);
// Every draw of our ISAAC generators and rand() to `write`.
void ObserveDraws(DrawWriter write);

// ---- the interface windows (harness_windows.cpp; image/harness_windows_image.cpp)

// The window on top (CInterface3D::GetActiveWindow), null without one.
void* TopWindow();
// game.exe's table of `window`'s class (by its C++ type), else its +0.
std::uintptr_t WindowKind(const void* window);

// ---- the base screen's player list (harness_address_order.cpp; image/harness_address_order_image.cpp)

struct PlayerList {
    int players = 0;      // the network client's players
    bool bound = false;   // its binding is live
    int listed = 0;       // the players the base screen's chat window lists
};
// The base screen `briefing`'s list against its network client's players; false without a client or a chat window.
bool BriefingPlayerList(void* briefing, PlayerList* list);

// ---- the orientation watch (harness_watch.cpp; image/harness_watch_image.cpp)

// The client world's unit `id` (null without one), with the addresses of its flags_ and its localRotation_.
void* ClientUnit(unsigned id, std::uint32_t** flags, std::uint32_t** rotation);

// ---- the "netstate" step (harness_netstate.cpp; image/harness_netstate_image.cpp)

// The sessions' state as console lines (harness_netstate_read.h).
std::vector<std::string> NetState();

// ---- the game's global objects (harness_objects.h; image/harness_objects_image.cpp)

const char* WindowTitle();             // app::g_windowTitle
void* MainWindow();                    // app::g_mainWindow (an HWND)
std::uint8_t& InterfaceHasScreen();    // app::g_interfaceHasScreen
std::uint8_t& RecordingFrames();       // app::g_recordingFrames
bool AppActive();                      // app::g_active
int& FlowState();                      // GameFlow::state_
void SetLoadTarget(const char* slot);  // GameFlow::requestTarget_
void ClearShowTutorial();
void SetMasterHost(const char* host);
void SetWaterWaves(bool on);

struct CursorState {
    int mode = 0;
    bool thread = false;   // the cursor's thread runs
    int imageShown = 0;
    bool image = false;    // it has an image, with this hot spot
    int hotX = 0, hotY = 0;
};
CursorState Cursor();

void* QueuedWindow();
bool CloseAllPending();
bool ScreenFade(bool* running, float* alpha);   // false without one
int CampaignMode();                             // the server world's session kind
void* ClientWorldObject();                      // the client world
void* SceneDevice();                            // the scene renderer's device, null before it is made
void* DeviceLock();                             // the device's lock (a CRITICAL_SECTION)
bool RenderActive();                            // render::Active

// ---- the steps' calls of the game (harness.cpp; image/harness_objects_image.cpp)

void ConsoleExecute(const char* text);
void SendLeave04(int id);                       // GameSendLeave04: ask the server for interface screen `id`
void SetConstructionVariable(std::uint32_t value);   // GameSetConstructionVariable
void ObserveConsoleLines(void (*observer)(const char* line));
unsigned& UpdatePeriod();                       // worlddata::g_updatePeriod
const char* DataFolder();                       // the data folder's text, may be empty
// IsaacRandom's seed hook: rand() seeded with 12345 and the generator's results filled by `fill`.
void SetFixedSeed(void (*fill)(std::uint32_t* results));
int TextKey(const char* name);                  // TextKeyFromName
void* LocalWorldPlayer(int player);             // CWorldServer::GetPlayer
void* WorldShared5C();                          // the server world's shared5C_
const char* ServerZoneName();                   // the server world's zone, may be null
void* PickAt2(void* world, int x, int y);       // CWorldClient::PickAt2, as its CEngineObject
void* UnitAt(void* world, int x, int y);        // CWorldClient::UnitAt, as its CEngineObject
// The client world's rectangle of object `id` in this frame's screen list (screenEntries_; the last entry for it):
// 1, 0 when it is not drawn, -1 without a list.
int ScreenRect(void* world, int id, RECT* rect);
int ObjectId(const void* object);               // a CEngineObject's id_ (what PickAt2 and UnitAt give)
std::uint32_t ObjectFlags(const void* object);  // its flags_
void* PlayerConnection(void* player);           // a CPlayer's connection_ (CPlayerServer / CPlayerClient)
int ConnectionState(void* connection);          // its state_
// The camp `camp` (the window on top, CCampPages): its page and its item strip's source items (null ones too) with
// their types (CEngineObject::type_); false without a source.
bool CampItems(void* camp, int* page, std::vector<void*>* items, std::vector<unsigned>* types);
void CampTakeItem(void* camp, void* item, int mode);   // CCampPages::TakeItem (slot 42, MoveItem)
void PickSurface(void* world, float* out, int x, int y);
void LoadUnitFaces(void* world);
void BlinkOpen(void* half);
void BlinkClose(void* half);
// The client halves of the client world's units with a face texture.
bool UnitFaceHalves(std::vector<void*>* halves);   // false without a client world
// The window's regions (null ones too) and region `index`'s rectangle, hit offset and enabled flag; false for none.
struct RegionInfo {
    int left = 0, top = 0, right = 0, bottom = 0;
    int offsetX = 0, offsetY = 0;
    bool enabled = false;
};
int RegionCount(void* window);
bool Region(void* window, int index, RegionInfo* region);
void ScaleToScreen(long* x, long* y);           // CInterface3D::ScalePointToScreenEntry
void InterfaceSize(int* width, int* height);
bool SaveLoadSliding(void* window);
bool OptionsSliding(void* window);
bool HasChatOverlay();
void ChatKeyEnter();
void CloseChat();
bool HasClassTemplate(int id);
const void* FindMap(const char* name);          // CMapDatabase::FindMap: the map.txt record, null for none
int MapKind(const void* record);                // a map.txt record's kind (0 game, 1 edge, 2 brief)
void LeaveToZone(void* connection, const char* name, int exit);
// The device's surfaces restored after a minimise (the image's device holds no DirectDraw object: nothing to
// restore); false without a device.
bool RestoreSurfaces(long* result);
// "hudstate": the battle screen's state logged; a failure, or "" (harness_hud_read.inc).
std::string HudState(void* screen, const std::string& argument);

// ---- the main loop's calls the harness steps into (harness.cpp, harness_capture.cpp, harness_hooks.h;
// image/harness_objects_image.cpp): each hook ends with the function it stands in for, called by name here.

int ServerUpdate(void* server);                 // CWorldServer::Update
int ClientSegment(void* client);                // CWorldClient::Segment
void CameraUpdate(void* camera, float seconds); // CCamera::Update
void FrameStep(bool draw, bool cursor);         // app::FrameStep
void CursorLock(void* cursor);                  // CCursor::TakeLock

// ---- the image's renderer (harness.cpp, harness_capture.cpp; image/harness_render_image.cpp)

// render::CursorPresentInfo and render::WindowPresentInfo.
struct CursorPresents {
    std::uint32_t presents = 0;
    std::uint32_t calls = 0;
    std::uint32_t refused[4] = {};
    int lastLeft = 0, lastTop = 0, lastRight = 0, lastBottom = 0;
    int checks = 0, mismatches = 0;
};
struct WindowPresents {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    int cursorChecks = 0, cursorMismatches = 0;
    bool rampCheckPending = false;
    bool rampIdentity = true;
    bool failed = false;
    int deviceLosses = 0;
    int deviceRecoveries = 0;
    std::uint32_t presentsAtRecovery = 0;
    long lastLossResult = 0;
    long lastLossReason = 0;
    bool lastLossSimulated = false;
    bool removable = false;
    int removals = 0;
};
CursorPresents CursorPresentCounts();
WindowPresents WindowPresentCounts();
bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr);
bool FrameInWindow(int* width, int* height);
void FrameToWindow(int* x, int* y);
void WindowToFrame(int* x, int* y);
bool PresentsItself();
int MissedSurfaceWrites(const char** first);
void ConfigureRenderer(const char* spec);
void SetWaitForVsync(bool on);
void LoseDevice();
bool RemoveDevice();
void SetSmoothCamera(bool on);
void EndFrame();
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);
// The renderer's steps (terrainlevels .. renderstats; harness.cpp, IsRenderStep): false for another verb.
bool RenderStep(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
                std::vector<std::string>* frames);

// ---- the "modfile" step (harness_starter.cpp; image/harness_objects_image.cpp)

const char* TextFolder();   // the text resources' folder
// The bytes of `name` in the archive at `path` through a CMappedResource: -2 when the archive does not open, 0
// without the file.
long ModFile(const char* path, const char* name);

// ---- the save trace (harness_savetrace.cpp; image/harness_objects_image.cpp)

// g_memoryStreamWriteObserver: each memory stream write, before its bytes.
void ObserveStreamWrites(void (*observer)(void* stream, std::uint32_t size, void* const* frame));

// ---- the run's settings file (harness_settings.cpp, harness_startup.cpp; image/harness_settings_image.cpp)

// One entry of a settings document, in order: a key (AddKey: `value` false), or a value of a key (Set).
struct SettingsValue {
    std::string key;
    bool value = false;
    std::string name;
    unsigned long type = 0;
    std::string data;
};
using SettingsMissing = void (*)(const char* path, const char* name);
using SettingsReadFilter = void (*)(const char* name, unsigned long type, std::string& data);
const char* SettingsFileName();                                        // settings_file::kFileName
std::string SettingsText(const std::vector<SettingsValue>& document);  // the document's file text
// The file pinned (settings_file::Pin); and pinned writable with `document` as the registry the first use imports.
void PinSettings(const char* file, SettingsMissing missing, bool readOnly);
void ImportSettings(const std::vector<SettingsValue>& document, const char* file, SettingsMissing missing);
void AddSettingsReadFilter(SettingsReadFilter filter);

// ---- the game's UDP port (harness_net.cpp; image/harness_objects_image.cpp)

int& GamePort();   // net::g_gamePort

// ---- the camera trace (harness_camera.cpp; image/harness_objects_image.cpp)

// What the trace samples of a CCamera: its focus, its view rotation's z and w, its distance and its flags.
struct CameraState {
    float focusX = 0, focusY = 0;
    float turnZ = 0, turnW = 1;
    float distance = 0;
    unsigned flags = 0;
};
CameraState Camera(const void* camera);

// ---- the interface drawing census (ui_census.cpp; image/harness_census_image.cpp)

void* ClientBitmap(const void* holder);          // a holder's DIB section (an HBITMAP)
bool IsSurfaceMemoryDc(void* dc);                // a DC 005FDC00 gave out
bool IsSurfaceType(std::uintptr_t typeDescriptor);   // CUiSurface's or CUiScrollingSurface's type descriptor
std::size_t SurfaceSize();                       // sizeof(CUiSurface)
struct SurfaceFields {
    void* dc = nullptr;
    void* holder = nullptr;
    int width = 0, height = 0;
};
void ReadSurfaceFields(const void* copy, SurfaceFields* fields);   // of a CUiSurface's copy
const void* HolderSurfaceField(void* holder);    // &CDXSurface::surface
void WindowSurfaceFields(const void* window, const void** data, const void** size);   // &surfaces_.data, .size
// The interface's own surfaces, and where its console keeps its surface (null without a console).
struct InterfaceSurfaces {
    const void* backdrop = nullptr;
    const void* secondBackdrop = nullptr;
    const void* fps = nullptr;
    const void* status = nullptr;
    const void* consoleSurfaceField = nullptr;
};
void ReadInterfaceSurfaces(InterfaceSurfaces* surfaces);
// g_uiSurfaceOwnerObserver and g_uiSurfacePixelObserver.
void ObserveSurfaces(void (*owner)(const void* window, const void* surface, bool added),
                     void (*pixels)(const void* surface, const char* what, bool writes, void* const* frame));

// ---- the interface's two paths compared (harness_ui.cpp; image/harness_ui_image.cpp)

namespace uicompare {
void Install();
void Frame();
bool Step(const std::string& verb, const std::string& argument, std::string* error);
void Report(std::FILE* f);
std::string Failure();
} // namespace uicompare

// ---- the UTF-8 checks (harness_utf8.cpp; image/harness_utf8_image.cpp)

namespace utf8files {
std::string Check(const char* savesDir);
} // namespace utf8files
namespace utf8typing {
std::string Type(HWND window, const std::string& typed);
std::string CheckConsoleLine(const std::string& argument);
} // namespace utf8typing
namespace utf8text {
std::string Check();
std::string CheckChatWrap(const std::string& text);
} // namespace utf8text

} // namespace ei::verify::image
