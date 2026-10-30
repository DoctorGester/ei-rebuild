#pragma once

// The harness's image side (tests/verify/image): what it reads and calls of our code, behind functions that name no
// class of ours. Only the files in this folder include our game headers (ticket H30b).

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct IDirectInput7A;

namespace ei::verify::image {

// ---- the random trace (harness_clock.cpp; image/harness_clock_image.cpp)

// One draw: the caller, the value, what drew it (" rand()", " (another generator)" or ""), and the generator's place
// in its stream after the draw.
using DrawWriter = void (*)(std::uintptr_t caller, std::uint32_t value, const char* what, std::uint32_t where);
// Every draw of our ISAAC generators and rand() to `write`.
void ObserveDraws(DrawWriter write);

// ---- the interface windows (harness_windows.cpp; image/harness_windows_image.cpp)

// The window on top (Interface3D::GetActiveWindow), null without one.
void* TopWindow();
// `window`'s name as a scenario names it (waitwindow: menu, battle, ...), else its class's name.
std::string WindowKind(const void* window);
// Whether `name` is a window's name a scenario can wait for.
bool IsWindowName(const std::string& name);

// ---- the base screen's player list (harness_address_order.cpp; image/harness_address_order_image.cpp)

struct PlayerList {
    int players = 0;      // the network client's players
    bool bound = false;   // its binding is live
    int listed = 0;       // the players the base screen's chat window lists
};
// The base screen `briefing`'s list against its network client's players; false without a client or a chat window.
bool BriefingPlayerList(void* briefing, PlayerList* list);

// ---- the orientation watch (harness_watch.cpp; image/harness_watch_image.cpp)

// The client world's unit `id` (null without one), with the addresses of its flags and its localRotation.
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
int& FlowState();                      // GameFlow::state
void SetLoadTarget(const char* slot);  // GameFlow::requestTarget
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
void* DeviceLock();                             // null: our renderer has no device lock

// ---- the steps' calls of the game (harness.cpp; image/harness_objects_image.cpp)

void ConsoleExecute(const char* text);
void SendLeave04(int id);                       // GameSendLeave04: ask the server for interface screen `id`
void SetConstructionVariable(std::uint32_t value);   // GameSetConstructionVariable
void ObserveConsoleLines(void (*observer)(const char* line));
void ObserveDirectInput(void (*observer)(IDirectInput7A* input));   // g_directInputObserver (platform_win32.h)
unsigned& UpdatePeriod();                       // worlddata::g_updatePeriod
const char* DataFolder();                       // the data folder's text, may be empty
// IsaacRandom's seed hook: rand() seeded with 12345 and the generator's results filled by `fill`.
void SetFixedSeed(void (*fill)(std::uint32_t* results));
int TextKey(const char* name);                  // TextKeyFromName
void* LocalWorldPlayer(int player);             // WorldServer::GetPlayer
void* WorldHandle();                          // the server world's worldHandle
const char* ServerZoneName();                   // the server world's zone, may be null
void* PickAt2(void* world, int x, int y);       // WorldClient::PickAt2, as its EngineObject
void* UnitAt(void* world, int x, int y);        // WorldClient::UnitAt, as its EngineObject
// The client world's rectangle of object `id` in this frame's screen list (screenEntries; the last entry for it):
// 1, 0 when it is not drawn, -1 without a list.
int ScreenRect(void* world, int id, RECT* rect);
int ObjectId(const void* object);               // a EngineObject's id (what PickAt2 and UnitAt give)
std::uint32_t ObjectFlags(const void* object);  // its flags
void* PlayerConnection(void* player);           // a Player's connection (PlayerServer / PlayerClient)
int ConnectionState(void* connection);          // its state
// The camp `camp` (the window on top, CampPages): its page and its item strip's source items (null ones too) with
// their types (EngineObject::type); false without a source.
bool CampItems(void* camp, int* page, std::vector<void*>* items, std::vector<unsigned>* types);
void CampTakeItem(void* camp, void* item, int mode);   // CampPages::TakeItem (slot 42, MoveItem)
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
void ScaleToScreen(long* x, long* y);           // Interface3D::ScalePointToScreen
void InterfaceSize(int* width, int* height);
bool SaveLoadSliding(void* window);
bool OptionsSliding(void* window);
bool HasChatOverlay();
void ChatKeyEnter();
void CloseChat();
bool HasClassTemplate(int id);
const void* FindMap(const char* name);          // MapDatabase::FindMap: the map.txt record, null for none
int MapKind(const void* record);                // a map.txt record's kind (0 game, 1 edge, 2 brief)
void LeaveToZone(void* connection, const char* name, int exit);
// The device's surfaces restored after a minimise (the image's device holds no DirectDraw object: nothing to
// restore); false without a device.
bool RestoreSurfaces(long* result);
// "hudstate": the battle screen's state logged; a failure, or "" (harness_hud_read.inc).
std::string HudState(void* screen, const std::string& argument);

// ---- the main loop's calls the harness steps into (harness.cpp, harness_capture.cpp;
// image/harness_objects_image.cpp): each hook ends with the function it stands in for, called by name here.

int ServerUpdate(void* server);                 // WorldServer::Update
int ClientSegment(void* client);                // WorldClient::Segment
void CameraUpdate(void* camera, float seconds); // Camera::Update
void FrameStep(bool draw, bool cursor);         // app::FrameStep
void CursorLock(void* cursor);                  // Cursor::TakeLock

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
void ConfigureRenderer(const char* spec);
void SetWaitForVsync(bool on);
void LoseDevice();
bool RemoveDevice();
void EndFrame();
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);
// The renderer's steps (terrainlevels .. renderstats; harness.cpp, IsRenderStep): false for another verb.
bool RenderStep(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
                std::vector<std::string>* frames);

// ---- the "modfile" step (harness_starter.cpp; image/harness_objects_image.cpp)

const char* TextFolder();   // the text resources' folder
// The bytes of `name` in the archive at `path` through a MappedResource: -2 when the archive does not open, 0
// without the file.
long ModFile(const char* path, const char* name);

// ---- the save trace (harness_savetrace.cpp; image/harness_objects_image.cpp)

// g_memoryStreamWriteObserver: each memory stream write, before its bytes.
void ObserveStreamWrites(void (*observer)(void* stream, std::uint32_t size, void* const* frame));

// ---- the run's settings file (harness_settings.cpp, harness_startup.cpp; image/harness_settings_image.cpp)

// One line of a settings file, in order: a value of `section` (Document::Set), or with `name` empty a comment
// (Document::Comment: `value` is its text).
struct SettingsLine {
    std::string section;
    std::string name;
    std::string value;
};
using SettingsMissing = void (*)(const char* section, const char* name);
using SettingsReadFilter = void (*)(const char* name, std::string& value);
const char* SettingsFileName();                                      // settings::kFileName
std::string SettingsText(const std::vector<SettingsLine>& lines);    // the file's text
// The file pinned (settings::Pin); and pinned writable without a file, which the first use writes with the defaults
// for a `width` x `height` desktop (settings::SetDesktopSize), every value absent from them read as absent.
using SettingsPrepare = void (*)(const char* file);
void PinSettings(const char* file, SettingsMissing missing, bool readOnly, SettingsPrepare prepare);
void DefaultSettings(const char* file, int width, int height);
void AddSettingsReadFilter(SettingsReadFilter filter);

// ---- the game's UDP port (harness_net.cpp; image/harness_objects_image.cpp)

int& GamePort();   // net::g_gamePort

// ---- the camera trace (harness_camera.cpp; image/harness_objects_image.cpp)

// A pose of the camera as the trace samples it: the focus, the heading (the view rotation's z and w), the distance,
// the position and the rotation (w, x, y, z).
struct CameraPose {
    float focusX = 0, focusY = 0, focusZ = 0;
    float turnZ = 0, turnW = 1;
    float distance = 0;
    float position[3] = {};
    float rotation[4] = {1, 0, 0, 0};
};
// What the trace samples of a Camera: its own pose, the pose the world's draw shows (camera::DrawnView) and its flags.
struct CameraState {
    CameraPose own, drawn;
    unsigned flags = 0;
};
CameraState Camera(const void* camera);

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
