#pragma once

// The harness's image side (tests/verify/image): what it reads and calls of our code, behind functions that name no
// class of ours. Only the files in this folder include our game headers (ticket H30b).

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Object3D;

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

// ---- the orientation watch (system/watch_win32.cpp; image/harness_watch_image.cpp)

// The client world's unit `id` (null without one), with the addresses of its flags and its localRotation.
void* ClientUnit(unsigned id, std::uint32_t** flags, std::uint32_t** rotation);

// ---- the "netstate" step (harness_netstate.cpp; image/harness_netstate_image.cpp)

// The sessions' state as console lines (harness_netstate_read.h).
std::vector<std::string> NetState();
// "netmod" (MF16): the mods our lobby compares (modidentity, net::ReadModMark) as console lines: this game's, the
// client's join state, refusal and the host's mod, the server's refusals for a mod and the last one's.
std::vector<std::string> NetMod();
// "netlist" (MF16a): the client's servers as console lines: each one's name, the mod its info answer named
// (ServerEntry::mod) and whether this game may join it for the mods (ServerEntry::ModAgrees).
std::vector<std::string> NetList();
// "serverscript <command>" (MF15e): a world script command run on the server world (WorldScript(<command>)), also in a
// network game, where the console's "@" lines do nothing. "" or the script's error.
std::string ServerScript(const std::string& line);
// "originalnet" (MF16): this game's lobby acts as the original's from now on (net::g_originalPeer).
void OriginalNet();
// The awards the game server keeps queued (each for 60 s: ServerSession::QueueAward), -1 without a server.
int ServerAwards();
// The client session's player limit (ClientSession::PlayerLimit), -1 without a client.
int SessionPlayerLimit();
// "graphthreads <zone> ..." (MF13d1): each zone loaded (NewZone) and its region graph built on the processors
// (AIMap::BuildRegionGraph) and with one local search measuring every block in order; a console line per zone
// (whether SaveBlocks' bytes are the same; the log has both times), each difference also into `failures`. The main
// menu loads after the last.
std::vector<std::string> GraphThreads(const std::vector<std::string>& zones, std::vector<std::string>* failures);
// Companions: the server world player's party records (-1 without the player); the unit id of server player 0's
// companion `index` (from 0, -1 none); the nearest living unit hostile to unit `id` (-1 none); the saved hero of the
// heroes' list named `name` (its file), read from the disk (false without one or when it does not read).
int ServerPartyRecords(int player);
int CompanionUnit(int index);
int NearestEnemy(int id);
// The companions picked in the base (SavedParties::companions); the add of the saved hero `name` sent from the
// local client as a mid-game add (restore 0; false when there is no such hero or client).
int CompanionList();
bool AskCompanion(const char* name);
bool SavedHero(const char* name, std::string* file, float* experience);

// ---- the game's global objects (harness_objects.h; image/harness_objects_image.cpp)

void* MainWindow();                    // g_mainWindow (the native window)
std::uint8_t& InterfaceHasScreen();    // app::g_interfaceHasScreen
std::uint8_t& RecordingFrames();       // app::g_recordingFrames
bool AppActive();                      // app::g_active
int& FlowState();                      // GameFlow::state
void SetLoadTarget(const char* slot);  // GameFlow::requestTarget
void ClearShowTutorial();
void SetMasterHost(const char* host);
void SetShadows(bool on);

struct CursorState {
    int imageShown = 0;
    bool image = false;    // it has an image: a frame's size, its frames, the hot spot
    int width = 0, height = 0, frames = 0;
    int hotX = 0, hotY = 0;
    bool held = false;     // the cursor held at its position (game points)
    int x = 0, y = 0;
};
CursorState Cursor();

bool CloseAllPending();
bool ScreenFade(bool* running, float* alpha);   // false without one
int CampaignMode();                             // the server world's session kind
void* ClientWorldObject();                      // the client world
bool ClientPaused();                            // the client world's pause
bool ServerPaused();                            // the server world's pause
void* SceneDevice();                            // the scene renderer's device, null before it is made

// ---- the steps' calls of the game (harness.cpp; image/harness_objects_image.cpp)

void ConsoleExecute(const char* text);
void RequestGameState(int id);                       // GameRequestGameState: ask the server for interface screen `id`
void SetConstructionVariable(float value);           // GameSetConstructionVariable
void ObserveConsoleLines(void (*observer)(std::string_view line));
unsigned& UpdatePeriod();                       // app::g_updatePeriod
const char* DataFolder();                       // the data folder's text, may be empty
// IsaacRandom's seed hook: rand() seeded with 12345 and the generator's results filled by `fill`.
void SetFixedSeed(void (*fill)(std::uint32_t* results));
// The generators seeded again as a first world seeds them (World::Create): a recording's part begins so (the "origin"
// step), since a save holds no game generator, and the client's (kept beside the save, client_visuals.h) has drawn in
// the frames between the load and the origin, which a replay that begins at a load draws otherwise than the recording.
void ReseedRandom();
int TextKey(const char* name);                  // TextKeyFromName
void* LocalWorldPlayer(int player);             // WorldServer::GetPlayer
// The "hit" step: the server world's unit `target` takes `amount` damage from unit `attacker` (0: none) as a blow lands
// (Unit::ReceiveHit with the attacker as its source; the hit as InflictDamage's). "" or the error.
std::string HitUnit(int target, int attacker, float amount);
// The "giveitem" step: the server world's player `player` is given a new item named `name` (WorldServer::CreateItemNamed,
// Player::GiveItems), as the script's GiveItem. "" or the error.
std::string GiveItem(int player, const char* name);
// The "groundprints" step: the client world's ground prints now.
struct GroundPrintCounts {
    int blood = 0, footprints = 0, shownFootprints = 0, scorch = 0;
    int lifeLeft = 0;   // the life left of all of them together, in ticks
};
GroundPrintCounts CountGroundPrints();
void* WorldHandle();                          // the server world's worldHandle
const char* ServerZoneName();                   // the server world's zone, may be null
const char* LoadingMovieName();                 // app::g_loadingMovieName: the loading screen's movie file
void* PickAt2(void* world, int x, int y);       // WorldClient::Pick by the parts, as its EngineObject
void* UnitAt(void* world, int x, int y);        // WorldClient::UnitAt, as its EngineObject
// The client world's rectangle of object `id` in this frame's screen list (screenEntries; the last entry for it):
// 1, 0 when it is not drawn, -1 without a list.
int ScreenRect(void* world, int id, Rect* rect);
int ObjectId(const void* object);               // a EngineObject's id (what PickAt2 and UnitAt give)
std::uint32_t ObjectFlags(const void* object);  // its flags
void* PlayerConnection(void* player);           // a Player's connection (PlayerServer / PlayerClient)
int ConnectionState(void* connection);          // its state
// The camp `camp` (the window on top, CampPages): its page and its item strip's source items (null ones too) with
// their types (EngineObject::type); false without a source.
bool CampItems(void* camp, int* page, std::vector<void*>* items, std::vector<unsigned>* types);
void CampTakeItem(void* camp, void* item, int mode);   // CampPages::TakeItem (slot 42, MoveItem)
// The magic stock of trader class `type` (its class template's spells party): each item as "scroll <spell code>",
// "prototype <spell code>" or "other"; false without such a template.
bool TraderMagic(int type, std::vector<std::string>* items);
void PickSurface(void* world, float* out, int x, int y);
void LoadUnitFaces(void* world);
void BlinkOpen(void* half);
void BlinkClose(void* half);
// The client halves of the client world's units with a face texture.
bool UnitFaceHalves(std::vector<void*>* halves);   // false without a client world
// The "skin" step: the client world's object `id` takes the textures `first` and `second` (null: its second name
// kept) through Object3DClientSpecific::SetSkins, as a map placement's primary and secondary textures name them.
// False when the client world has no such object (yet).
bool SetObjectSkins(int id, const char* first, const char* second);
// The "unitskins" step: the texture names the client world's object `id` was given (the client half's skinName and
// secondSkinName). False when the client world has no such object.
bool ObjectSkins(int id, std::string* first, std::string* second);
// The client world's object `id` (the MapObject its meshes are drawn with, render::BeginCaster's), else null.
const Object3D* ClientObject(int id);
// The window's regions (null ones too) and region `index`'s rectangle, hit offset and enabled flag; false for none.
struct RegionInfo {
    int left = 0, top = 0, right = 0, bottom = 0;
    int offsetX = 0, offsetY = 0;
    bool enabled = false;
};
int RegionCount(void* window);
bool Region(void* window, int index, RegionInfo* region);
void ScaleToScreen(int* x, int* y);             // screen_layout::ToScreen
void InterfaceSize(int* width, int* height);
// The "droplist" steps: the first drop-down list (UiDropList) among `window`'s children, at any depth: open, the
// chosen entry and its text, the entry count, the first shown, how many are shown, the active entry and the row
// (layout); false without one. DropListEntry scrolls the open list to show entry `index` (UiDropList::ScrollTo) and
// gives the entry's centre (layout); false when the list is closed or has no such entry.
struct DropListInfo {
    bool open = false;
    int value = 0, count = 0, first = 0, shown = 0, active = 0;
    std::string text;
    Rect row{};
};
bool DropList(void* window, DropListInfo* info);
bool DropListEntry(void* window, int index, Point* centre);
bool SaveLoadSliding(void* window);
bool OptionsSliding(void* window);
int DialogueState(void* window);                // the base screen's dialogue state (BriefingScreen::state), else -1
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
// "hudstate": the battle screen's state logged; a failure, or "" (harness_objects_image.cpp).
std::string HudState(void* screen, const std::string& argument);

// ---- the main loop's calls the harness steps into (harness.cpp, harness_capture.cpp;
// image/harness_objects_image.cpp): each hook ends with the function it stands in for, called by name here.

void ServerUpdate(void* server);                // WorldServer::Update
bool ClientSegment(void* client);               // WorldClient::RunTick
void CameraUpdate(void* camera, float seconds); // Camera::Update
void FrameStep(bool draw);        // app::FrameStep

// ---- the image's renderer (harness.cpp, harness_capture.cpp; image/harness_render_image.cpp)

// render::CursorInfo and render::WindowPresentInfo.
struct CursorPresents {
    std::uint32_t presentsShown = 0, presentsHeld = 0, presentsHidden = 0;
    std::uint32_t made = 0, failures = 0, shapeChanges = 0, frameChanges = 0;
    bool shown = false;
    int width = 0, height = 0, hotX = 0, hotY = 0;
};
struct WindowPresents {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    bool rampCheckPending = false;
    bool rampIdentity = true;
};
CursorPresents CursorPresentCounts();
// The gamma ramp the present applies: 256 RGBA8 entries (render::GammaRampTable).
const std::uint8_t* GammaRampEntries();
WindowPresents WindowPresentCounts();
// The frame's size (render::FrameInWindow); none when the renderer has no frame or window.
std::optional<Size> FrameInWindow();
// The frame's size in the window (render::FrameInWindow's placed rectangle).
std::optional<Size> FramePlaced();
Point FrameToWindow(Point p);
Point WindowToFrame(Point p);
void ConfigureRenderer(const char* spec);
void SetWaitForVsync(bool on);
void SetPlayedMoviesPaced(bool on);   // SetPlayedMoviesPaced (game/app/movies.h)
void EndFrame();
// The last frame as 24-bit BGR pixels, top-down rows of width * 3 bytes (render::ReadFrame); none without a frame.
struct FrameImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgr;
};
std::optional<FrameImage> ReadFrame();
// The renderer's steps (terrainlevels .. renderstats; harness.cpp, IsRenderStep): false for another verb.
bool RenderStep(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
                std::vector<std::string>* frames);

// ---- the "modfile" step (harness_starter.cpp; image/harness_objects_image.cpp)

const char* TextFolder();   // the text resources' folder
// The bytes of `name` in the archive at `path` through a MappedResource: -2 when the archive does not open, 0
// without the file.
long ModFile(const char* path, const char* name);
// How many names the archive at `path` lists for `pattern` through ListFiles (-2: it does not open).
long ModList(const char* path, const char* pattern);
// The client world's text "string <name>" as the interface reads it (TextManager::FindString), UTF-8: 1 found, 0 not,
// -3 without the client world's texts.
long GameText(const char* name, std::string* text);
// The sound of unit record `unit`'s named speech `speech` in the acks database the units speak from (SayNamedAck):
// 1 found, 0 no such speech, -3 no such record.
long AckSound(const char* unit, const char* speech, std::string* sound);
// A record of a database as text, for the checks of a merge by field: kind "ack" (`name` "<unit>/<ack kind number>":
// the speech count and the first sound of that list), "anim" ("<figure>/<animation>": the animation's chance, frames,
// key, flags and moment), "track" ("<figure>/<animation>/<part>": the part's track as the figure manager loads it, its
// key counts), "blood", "footprint" or "scorch" (a print by name: its dry and wet looks). 1 found, 0 not, -3 without
// the client world's map object manager (for "track" its figure manager).
long RecordText(const char* kind, const char* name, std::string* text);
// The first weapon of the prototype named `weapon` among the local player's given items, then its party's: its
// damage as the game reckons it (WeaponMinDamage, WeaponMaxDamage of its packed id). 1 found, 0 none, -3 without the
// server world's players.
long LocalWeaponDamage(const char* weapon, float* minimum, float* maximum);
// The harness's `effect <name> set <input> <value> | fire <event> | check <particles> [<stage>]` on every client source
// of the data effect `name` (docs/notes/effects.md, "Data effects"): an input set (a number, three for a vector, an
// enum's value by name) or an event fired, or the sources' particles counted and the first one's stage named. The
// problem, or empty.
std::string DataEffectStep(const std::string& argument);
// The size of `name` in a manager's archives ("figures": the figure manager's, "links": the link figure loader's,
// "textures": the texture manager's), in their order: 0 without it, -3 when the client world has no such manager.
long ModManagerFile(const char* which, const char* name);
// The music stream asked for `name` (an .mp3 file of the stream folder), as the main menu and the zone's music ask.
void PlayStream(const char* name);
// The "mod" step's switch of mods (app::RequestModSwitch; `folder` in full, empty for none): false when refused.
bool SwitchMod(const char* folder);
// Whether the switch asked for is still to be made (at the main loop's next pass).
bool ModSwitchPending();
// The maps folder's MapFileExists.
bool MapsFileExists(const char* name);
// The ground's height at (x, y) in the client world's landscape (Terrain::HeightAt, the drawn ground) and in the server
// world's AI map (GridHeight: what the scripts' GetZValue answers). False when either world has no map.
bool GroundHeights(float x, float y, float* client, float* server);

// ---- the save trace (harness_savetrace.cpp; image/harness_objects_image.cpp)

// g_memoryStreamWriteObserver: each memory stream write, before its bytes.
void ObserveStreamWrites(void (*observer)(void* stream, std::uint32_t size, void* const* frame));

// ---- the run's settings file (harness_settings.cpp; image/harness_settings_image.cpp)

// One line of a settings file, in order: a value of `section` (Document::Set), or with `name` empty a comment
// (Document::Comment: `value` is its text).
struct SettingsLine {
    std::string section;
    std::string name;
    std::string value;
};
using SettingsMissing = void (*)(const char* section, const char* name);
const char* SettingsFileName();                                      // settings::kFileName
std::string SettingsText(const std::vector<SettingsLine>& lines);    // the file's text
// The file pinned (settings::Pin); and pinned writable without a file, which the first use writes with the defaults
// for a `width` x `height` desktop (settings::SetDesktopSize), every value absent from them read as absent.
using SettingsPrepare = void (*)(std::string_view file);
void PinSettings(const char* file, SettingsMissing missing, bool readOnly, SettingsPrepare prepare);
void DefaultSettings(const char* file, int width, int height);

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

// "camera" (harness.cpp): the view camera's place. Placing it stops its following of a unit and its pan, and looks at
// map point (x, y) (Camera::SetFocus, ApplyView); a heading (degrees about z, 0 the camera's north), a distance and a
// pitch (degrees) are set only where `set*` says. Returns the camera's pose after it.
struct CameraPlacement {
    float x = 0, y = 0;
    bool setHeading = false, setDistance = false, setPitch = false;
    float heading = 0, distance = 0, pitch = 0;
};
// `x`, `y`, `z`: the drawn view's eye (camera::FrameView); `nearGround`: the lowest height of its near plane above the
// ground and the water at rest (Terrain::CalmSurfaceHeight) over 9 x 9 points of the plane, the eye's own height
// above them included (0 without a map). The plane's size is the projection's (renderer_init.cpp): the near
// distance, the field of view across the window's 4:3 part (aspect 4:3) and the axis swap's scales
// (screen_layout.h), worked out here apart from the camera's own code.
struct CameraView {
    float focusX = 0, focusY = 0, focusZ = 0, heading = 0, distance = 0, pitch = 0;
    float x = 0, y = 0, z = 0, nearGround = 0;
};
CameraView PlaceCamera(const CameraPlacement& placement);
CameraView ViewCamera();
// "climate" (harness.cpp): the server world's climate (WorldServer::SetClimate: 0 none, 1 rain, 2 snow, 3 storms; a
// change of climate clears the weather): with 0 no rain starts by chance (UpdateRain's rolls) during a scene's captures.
void SetClimate(int climate);
// "weatherstate" (harness.cpp): the server world's weather (0 clear, 1 light rain, 2 heavy rain), its fade and sight
// factor, and the client's rain / snow layer (mode, the mode it heads for, density 0..64).
struct WeatherState {
    int weather = 0;
    float strength = 0, factor = 0;
    int layerMode = 0, layerTarget = 0, layerDensity = 0;
};
WeatherState Weather();

// What the object trace samples of a client world object (harness_camera.cpp): its id, its own place and the
// place the draw shows (drawn::PoseOf), the yaw in radians (about z); with a part named, both the part's place in
// the figure (what the pose and its blends move).
struct ObjectPlace {
    float x = 0, y = 0, z = 0, yaw = 0;
};
struct ObjectState {
    int id = 0;
    ObjectPlace own, drawn;
    std::string animation;   // the animation playing ("" for none), its frame and the blend's weight now
    float frame = 0, blend = 1;
};
// The client world's object `id`, or with `id` 0 its first bow arrow (the figure "efar1"); `part` "" for the
// object's origin, "light" for its own light (own: the light's localPosition, drawn: drawn::LightPosition),
// "lastdraw" for where its last draw put it (both: its world matrix's place). False when there is none (or no such
// part).
bool ClientObject(int id, const char* part, ObjectState* out);
// The client world's point light effect `id` (EffectPointLight), or with `id` 0 the first moving one (a spell's or a
// missile's light): its light's own place and drawn one (drawn::LightPosition). False when there is none.
bool ClientLight(int id, ObjectState* out);
// The client world's lightning bolts (EffectLightning): each one's ends as "x y z - x y z".
std::vector<std::string> ClientBolts();
// The client world's living units whose packed hit points (as the server last sent them) are below their maximum:
// each one's "id <id> hp <hit points> of <maximum>".
std::vector<std::string> ClientHurt();
// The client world's units whose figure has the part `part` carried by its skeleton's bone of that name (a worn
// weapon's or armour's part attached; docs/notes/mods.md, "Worn models"): each one's "id <id> <figure>: <part> under
// <parent bone> at <x y z>", " hidden" after a hidden part.
std::vector<std::string> UnitsWearing(const std::string& part);
// A figure alone (FigureInstance, MF15k3; "figurealone"): `model` built by the client world's figure manager without a
// game object (a hero's model with the heroes' parts and hair 1, another whole), dressed with the skin of its name,
// its animation database's `animation` played and moved on `frames` frames; `part`'s point at rest and then each
// logged into `lines`. The figure is let go after. The failure's text, or "".
std::string FigureAlone(const std::string& model, const std::string& animation, const std::string& part, float frames,
                        std::vector<std::string>* lines);
// Animation switches blend (FigureAnimationState::SetBlends; "animblend on|off").
void SetAnimationBlends(bool on);
// The blends begun so far in world figures (FigureAnimationState::WorldBlendsBegun; "animblend count").
std::uint32_t WorldBlendsBegun();

// ---- the UTF-8 checks (harness_utf8.cpp; image/harness_utf8_image.cpp)

namespace utf8files {
std::string Check(const char* savesDir);
} // namespace utf8files
namespace utf8typing {
std::string Type(void* window, const std::string& typed);
std::string CheckConsoleLine(const std::string& argument);
} // namespace utf8typing
namespace utf8text {
std::string Check();
std::string CheckChatWrap(const std::string& text);
} // namespace utf8text

} // namespace ei::verify::image
