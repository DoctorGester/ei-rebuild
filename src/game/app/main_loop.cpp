// WinMain: the application's start-up, its main loop and its shutdown. See docs/notes/app.md, "WinMain".
#include "game/units/acks_db.h"
#include "game/app/app.h"
#include "engine/core/program_window.h"
#include "game/app/game_flow.h"
#include "game/app/harness_hooks.h"
#include "game/app/logic_settings.h"
#include "game/app/movies.h"
#include "game/app/options.h"
#include "game/app/settings.h"
#include "game/app/singletons.h"
#include "game/world/camera.h"
#include "game/console/console.h"
#include "engine/core/asset_text.h"
#include "engine/core/coder.h"
#include "engine/core/engine_object.h"
#include "engine/core/random.h"
#include "engine/core/text_split.h"
#include "engine/data/ini_file.h"
#include "engine/scene/device.h"
#include "game/app/present_window.h"
#include "engine/models/dib.h"
#include "engine/scene/drawn_motion.h"
#include "engine/particles/particle_data.h"
#include "game/terrain/ground_print.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/objects/lever.h"
#include "game/map/map_database.h"
#include "engine/scene/scene_renderer.h"
#include "engine/mods/mod_folders.h"
#include "game/net/net_session.h"
#include "game/players/class_templates.h"
#include "game/players/party_records.h"
#include "game/ui/screen_layout.h"
#include "game/sound/sound.h"
#include "engine/models/texture.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit_db.h"
#include "game/world/quest_records.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using options::GameOptions;

namespace app {

// ---- The application's globals that the static initialisers set ------------------------------------

// The command line's strings.
std::string g_movieFolder = ".\\capture\\";
std::string g_commandLineMap;
std::string g_commandLineMob;
std::string g_commandLineScript;
std::string g_cameraFile;

int g_updatePeriod = /*0078FF34*/ 55;


namespace {


// ---- The application's globals ---------------------------------------------------------------------

// The window's title and class name: strings 101 and 102 of the game's string table.
constexpr const char* kWindowTitle = "Evil Islands";
constexpr const char* kWindowClassName = "EIGAME";

// ---- Start-up --------------------------------------------------------------------------------------

// The render quality (docs/notes/renderer.md section 11, "Render quality"): [renderer] Msaa (the world view's samples:
// 1 off, 2, 4 or 8) and Anisotropy (its models' anisotropic sampling: 1 off .. 16). A value missing (or not a number)
// is written with its catalog default, the look the user's dgVoodoo setup gave, so the file shows both.
int QualitySetting(const char* name) {
    const int value = settings::GetInt(settings::kRenderer, name);
    settings::SetInt(settings::kRenderer, name, value);
    return value;
}

// The engine's config (engine_globals.h) from the options and [renderer] of ei_settings.ini: the performance overlay
// (docs/notes/renderer.md section 15), the vertical blank's switch and the render quality, each read only where the
// renderer has it (the product: the harness build's settings file lacks them).
void FillEngineConfig(const GameOptions& options) {
    EngineConfig& config = g_engineConfig;
    config.workPath = options.workPath;
    config.resourcesPath = options.resourcesPath;
    config.resolutionX = options.resolutionX;
    config.resolutionY = options.resolutionY;
    config.fullScreen = options.fullScreen != 0;
    config.perfOverlay = render::PerfOverlaySupported() && settings::GetBool(settings::kRenderer, "PerfOverlay");
    if (render::VsyncSwitchSupported()) config.vsync = settings::GetBool(settings::kRenderer, "VSync");
    if (render::QualitySettingsSupported()) {
        config.msaa = QualitySetting("Msaa");
        config.anisotropy = QualitySetting("Anisotropy");
        settings::Save();
    }
}

constexpr int kExitDeclined = 0xDEAD;   // WinMain's result when the player gives up at a warning
constexpr double kLittleDiskSpace = 314572800.0;   // 300 MB

// The low disk space warning: line 1 of .\res\sysmsg.txt (in the asset code page) when the file has two lines or more
// (line 0 was the DirectX version check's), else the built-in text.
std::string LittleDiskSpaceWarning() {
    std::string bytes;
    if (platform::ReadWholeFile(".\\res\\sysmsg.txt", &bytes)) {
        const std::vector<std::string> lines = str::Lines(bytes);
        if (lines.size() >= 2) return text::FromAssetText(lines[1]);
    }
    return "Not enough free disk space - play on your own risk!";
}

std::string Tail(const std::string& argument, int prefixLength) { return str::Right(argument, static_cast<int>(argument.size()) - prefixLength); }

// The command line: "-moviepath_<folder>", a ".cam" file (only while not RecordingCamera), a ".scr", ".mob" or ".mpr"
// file. ("-nocam" and the original's "-benchmark_<frames>" set nothing anything reads; they are ignored.)
void ReadCommandLine(const char* commandLine) {
    std::vector<std::string> arguments = text::Split(commandLine, ' ');
    g_cameraFile = ".\\camera.cam";
    g_commandLineScript.clear();
    g_commandLineMob.clear();
    g_commandLineMap.clear();
    WorldClient* client = &g_clientWorld;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (str::Find(arguments[i], "-moviepath_") == 0) {
            g_movieFolder = Tail(arguments[i], 11);
            if (g_movieFolder.back() != '\\') g_movieFolder += '\\';
            continue;
        }
        if (!(client->flags & WorldClientFlag::RecordingCamera) && str::Find(arguments[i], ".cam") != -1) {
            g_cameraFile = arguments[i];
            continue;
        }
        if (str::Find(arguments[i], ".scr") != -1) {
            g_commandLineScript = arguments[i];
        } else if (str::Find(arguments[i], ".mob") != -1) {
            g_commandLineMob = arguments[i];
        } else if (str::Find(arguments[i], ".mpr") != -1) {
            g_commandLineMap = arguments[i];
        }
    }
}

// A game that runs already (its window found by class and title) is brought to the front instead and this one exits.
void ExitIfGameRuns() {
    std::snprintf(g_windowTitle, sizeof(g_windowTitle), "%s", kWindowTitle);
    if (!platform::BringOtherInstanceForward(kWindowClassName, g_windowTitle)) return;
    ei::Log("app: another game runs (its window's class and title \"%s\" the same): the one-instance check ends this "
            "game with exit code -1", g_windowTitle);
    std::exit(-1);
}

// The player's answer to a warning: false when cancelled.
bool WarningAccepted(const std::string& text) {
    return platform::ShowMessageBox(nullptr, text, "Warning", platform::MessageButtons::OkCancel,
                                    platform::MessageIcon::Warning) != platform::MessageAnswer::Cancel;
}

// Warns when the data folder's drive has under 300 MB free. False when the player cancels.
bool CheckDiskSpace() {
    const double freeBytes = platform::FreeDiskSpace(g_dataFolder);
    if (freeBytes != 0.0 && freeBytes < kLittleDiskSpace) return WarningAccepted(LittleDiskSpaceWarning());
    return true;
}

void CreateFolders() {
    for (const char* folder : {"saves\\", "mp\\", "textures\\"}) platform::CreateFolder(g_dataFolder + folder);
}

// The sound opened (44.1 kHz, 16 bits) with its music folder, wave archives and volumes.
void OpenSound() {
    GameOptions* options = &options::g_options;
    std::string textFolder = g_textFolder;
    std::string dataFolder = g_dataFolder;
    g_clientWorld.sounds->Open(true, 44100);
    SoundManager* sound = g_clientWorld.sounds.get();
    sound->SetPath(dataFolder + "stream\\", textFolder + "streamsn.reg");
    sound->ClearWaveSources();
    sound->AddWaveSource(textFolder + "sfx.res");
    sound->AddWaveSource(dataFolder + "res\\speech.res");
    g_clientWorld.sounds->SetEffectsVolume(options->audioSfx);
    g_clientWorld.sounds->SetSpeechVolume(options->audioSpeech);
    g_clientWorld.sounds->SetMusicVolume(options->audioStreaming);
    g_clientWorld.sounds->panDirect = options->reverseStereo != 0;
}

// `database` emptied, then loaded: a start after a switch of mods reads the new mod's.
template <class Database, class... Arguments>
void LoadFresh(Database& database, const Arguments&... arguments) {
    database = Database();
    database.Load(arguments...);
}

void LoadDatabases() {
    const std::string archive = "database.res";
    GameOptions* options = &options::g_options;
    LoadFresh(g_skillDatabase, options->resourcesPath, archive);
    LoadFresh(g_questDatabase, options->resourcesPath);
    LoadFresh(g_unitDatabase, options->resourcesPath, archive);
    LoadFresh(g_spellDatabase, options->resourcesPath, archive);
    LoadFresh(g_itemDatabase, options->resourcesPath, archive);
    LoadFresh(g_leverTable, options->resourcesPath);
    LoadFresh(g_ackDatabase, options->resourcesPath);
    LoadFresh(g_printsDatabase, options->resourcesPath);
    effectdata::g_effects.Reset();
}

// One step of a switch's loading screen (the zone's own steps, 8 and up, follow in the first zone's load): the
// picture of the game's level load at `progress`, with the window's messages taken. The log line says whether a
// picture was drawn (the check of the scenarios, whose harness cannot capture inside a pass). The movie is closed
// again at once: it may hold a stream of the sound device, which the stop and OpenSound close and open again, and the
// last picture presented stays in the window until the next.
void LoadingStep(int progress) {
    const bool drawn = ShowLoadingMovie(progress);
    CloseLoadingMovie();
    ei::Log("mod: loading screen step %d %s", progress, drawn ? "drawn" : "not drawn");
}

// The game's half of the start, which the program's start and a switch of mods both run (docs/notes/mods.md,
// "Switching mods"): the logic settings (config\ai.reg, a mod's file), both worlds, the camera, the folders, the
// sound, the databases and the map list; then the sun (the renderer's default light), the world's graphics, the
// cursors, the interface and the first map. `openDisplay` (the program's start: the window, the renderer and the start
// movies) runs between the two; false from it ends the start. `showLoading` (a switch of mods) steps the loading
// screen after each long part, as a zone's load does (LoadingStep).
bool StartGame(const std::function<bool()>& openDisplay, bool showLoading = false) {
    GameOptions* options = &options::g_options;
    g_gameFlow.Restart();
    g_logicSettings.Load(g_startFolder + "config\\ai.reg");
    // The generators (and the CRT's rand) seeded again by World::Create, as at the program's start.
    g_random.seeded = 0;
    g_clientRandom.seeded = 0;
    g_particleRandom.seeded = 0;
    g_clientWorld.Create();
    g_serverWorld.Create();
    // The field of view: pi * 2 / 7 (51.4 degrees).
    g_camera.Init(options, options->nearClipDistance, kPi * 2.0f / 7.0f);
    g_clientWorld.camera = &g_camera;
    CreateFolders();
    if (options->audio) OpenSound();
    if (showLoading) LoadingStep(1);
    LoadDatabases();
    if (showLoading) LoadingStep(2);
    g_mapDatabase.LoadAll();
    if (openDisplay && !openDisplay()) return false;
    if (showLoading) LoadingStep(3);
    g_sceneRenderer.AddDefaultLight();
    g_clientWorld.SetUpGraphics(g_sceneRenderer.device);
    if (showLoading) LoadingStep(4);
    SetUpCursors(0);
    g_interface.Create(g_sceneRenderer.device);
    if (showLoading) LoadingStep(5);
    StartFirstMap();
    return true;
}

// ---- The main loop ---------------------------------------------------------------------------------

void TakeModSwitch();   // a switch of mods asked for (RequestModSwitch), made between two passes

// The loop's clock (milliseconds): the time of the last pass (read at the first), the time not yet run by the server
// and by the client, and the client's speed.
struct LoopClock {
    bool started = false;
    std::uint32_t last = 0;
    std::uint32_t serverBacklog = 0;
    std::uint32_t clientBacklog = 0;
    float clientSpeed = 1.0f;
};
LoopClock g_clock;

constexpr float kOneTenth = 0.1f;
constexpr float kSlowest = kOneTenth;   // the client speed's limits
constexpr float kFastest = 10.0f;
constexpr std::uint32_t kBacklogLimit = 0x4000000;
constexpr int kLongestPassTicks = 5;

// While frames are recorded (g_recordingFrames; the harness's "record" step) a pass lasts one movie frame: 1000 / 15
// milliseconds rounded up (the original's MovieCapturingFPS setting, 15 by default; nothing in the game turns the
// recorder on).
constexpr std::uint32_t kRecordedFramesPerSecond = 15;
constexpr std::uint32_t kRecordedFrameMs = 67;
static_assert(kRecordedFrameMs * kRecordedFramesPerSecond >= 1000 &&
              (kRecordedFrameMs - 1) * kRecordedFramesPerSecond < 1000);

// The world tick's length in milliseconds, and milliseconds in the camera's time (times 1 / 55).
std::uint32_t TickLength() { return static_cast<std::uint32_t>(g_updatePeriod); }
float CameraSeconds(std::uint32_t milliseconds) { return static_cast<float>(milliseconds) * 0.018181818f; }

std::uint32_t TimeSinceLastPass() {
    if (!g_clock.started) {
        g_clock.started = true;
        g_clock.last = platform::FrameClock();
    }
    std::uint32_t now;
    {
        ei::mainloop::InSite site(ei::mainloop::Site::FrameTime);
        now = platform::FrameClock();
    }
    // The voices whose wave has played out by the frame's time end now (the sound manager's Tick frees them), and
    // without a device the music's stream catches up to it.
    audio::DeliverEnds(now);
    if (g_clientWorld.sounds != nullptr) g_clientWorld.sounds->FollowClock(now);
    const std::uint32_t elapsed = now - g_clock.last;
    g_clock.last += elapsed;
    return elapsed;
}

// The pass's time added to the backlogs and returned: none when the clock went back, at most kLongestPassTicks
// ticks, a movie frame's while frames are recorded. The client's backlog takes it at the client's speed (1 while the
// local view does not send the camera) and is dropped above kBacklogLimit.
std::uint32_t AdvanceClock(std::uint32_t elapsed) {
    if (elapsed > 0x80000000u) elapsed = 0;
    if (elapsed > TickLength() * kLongestPassTicks) elapsed = TickLength() * kLongestPassTicks;
    if (g_recordingFrames) elapsed = kRecordedFrameMs;
    g_clock.serverBacklog += elapsed;
    if (!g_clientWorld.IsLocalViewSendingCamera()) g_clock.clientSpeed = 1.0f;
    // The elapsed time at the client's speed plus the backlog (an int), truncated as _ftol does.
    const float backlog = static_cast<float>(elapsed) * g_clock.clientSpeed +
                          static_cast<float>(static_cast<std::int32_t>(g_clock.clientBacklog));
    g_clock.clientBacklog = static_cast<std::uint32_t>(static_cast<std::int64_t>(backlog));
    if (g_clock.clientBacklog > kBacklogLimit) g_clock.clientBacklog = 0;
    return elapsed;
}

// The worlds run for the time that has passed: the server a tick at a time while it has a whole
// tick, the client likewise while it has one and either server updates wait or its view is not in
// mode 2 (each client tick also moves the camera by a tick, and notes that step for the drawn view:
// camera::NoteViewTick). False when the client's tick stops the frame. The harness may stand in for both ticks.
bool RunWorlds() {
    const ei::mainloop::HarnessHooks& hooks = ei::mainloop::g_harnessHooks;
    for (;;) {
        bool settled = true;
        if (g_clock.serverBacklog >= TickLength()) {
            settled = false;
            ei::mainloop::CallOr(hooks.serverUpdate, &WorldServer::Update, &g_serverWorld);
            g_clock.serverBacklog -= TickLength();
            net::Pump();
        }
        if (g_clock.clientBacklog >= TickLength() &&
            (g_clientWorld.PumpHasIncoming() || !g_clientWorld.IsLocalViewSendingCamera())) {
            if (!ei::mainloop::CallOr(hooks.clientSegment, &WorldClient::RunTick, &g_clientWorld)) return false;
            const std::uint32_t tick = TickLength();
            g_clock.clientBacklog -= tick;
            camera::NoteViewBeforeTick(&g_camera);
            g_camera.Update(CameraSeconds(tick));
            camera::NoteViewTick(&g_camera);
            drawn::TickRan();
            net::Pump();
            continue;
        }
        if (settled) return true;
    }
}

// The client's speed for the next pass from how far it is behind the server: `balance` is the
// number of waiting server updates less the part of a tick the client has left. Behind (negative),
// it is e^(balance / 10); up to one update ahead, 1; further ahead, 1 + (sqrt(balance) - 1) / 10
// (float maths in the original's order: the goldens depend on it; e^x is VC6's inline exp). Kept within 0.1 .. 10.
float SpeedForBalance(float balance) {
    float result;
    if (balance < 0.0f) {
        result = lib::Exp(balance * kOneTenth);
    } else if (balance > 1.0f) {
        result = (std::sqrt(balance) * kOneTenth + 1.0f) - kOneTenth;
    } else {
        result = 1.0f;   // also NaN
    }
    result = result >= kSlowest ? result : kSlowest;
    if (kFastest < result) result = kFastest;
    return result;
}

// The client's speed from the client's part of a tick (`fraction`), printed when the console asks.
void UpdateClientSpeed(float fraction) {
    const float balance = static_cast<float>(g_clientWorld.PumpIncomingCount()) - fraction;
    g_clock.clientSpeed = SpeedForBalance(balance);
    if (g_showClientSpeed)
        console::EndLine(console::g_netLogOut << "Client speed = " << g_clock.clientSpeed << " balance = " << balance);
}

// The camera runs on the real time of the pass (`elapsed`), a tick at a time; the draw spreads the last tick's step
// over the part of the next tick the client has had (camera::DrawnView), and so the objects' steps (drawn::PoseOf).
// The last part of the pass's time may be the harness's (frozen camera, camtrace). Then, unless the client is
// paused, the camera file's record or playback step at the client's time.
void UpdateCamera(float fraction, std::uint32_t elapsed) {
    const float passTicks = static_cast<float>(elapsed) / static_cast<float>(TickLength());
    camera::SetViewProgress(fraction, passTicks);
    drawn::SetProgress(fraction, passTicks);
    Camera* camera = &g_camera;
    std::uint32_t tick = TickLength();
    while (elapsed > tick) {
        camera->Update(CameraSeconds(tick));
        tick = TickLength();
        elapsed -= tick;
    }
    ei::mainloop::CallOr(ei::mainloop::g_harnessHooks.frameCameraUpdate, &Camera::Update, camera,
                         CameraSeconds(elapsed));
    if (g_clientWorld.paused) return;
    const float time = static_cast<float>(g_clientWorld.tick) + g_clientWorld.tickFraction;
    if (camera->flags & Camera::kRecording) camera->RecordFrame(time);
    if (camera->flags & Camera::kPlaying) camera->PlaybackStep(time);
}

// The interface's update and the frame (the harness may stand in for the frame step).
void DrawFrame() {
    g_interface.Update();
    ei::mainloop::CallOr(ei::mainloop::g_harnessHooks.frameStep, &app::FrameStep, true);
}

// One pass of the loop while no message waits. Returns whether the interface takes the input
// messages from now on: not while the renderer is inactive.
bool RunPass() {
    TakeModSwitch();
    g_sceneRenderer.running = g_active != 0;
    TakeResolutionChange();
    UpdateCursorConfinement();
    std::uint32_t elapsed = TimeSinceLastPass();
    if (!g_sceneRenderer.running) {
        GuardPlayerData(0);
        return false;
    }
    GuardPlayerData(1);
    if (g_clientWorld.IsLocalPlayerIdle()) {
        if (IsMultiplayerClient()) g_gameFlow.RecreateClientSession();
        else g_gameFlow.CloseSessions();
    }
    net::Pump();
    elapsed = AdvanceClock(elapsed);
    if (RunWorlds()) {
        g_gameFlow.ProcessRequest();
        const float fraction =
            static_cast<float>(g_clock.clientBacklog) / static_cast<float>(static_cast<std::int32_t>(TickLength()));
        if (!g_clientWorld.paused) g_clientWorld.tickFraction = fraction;
        UpdateClientSpeed(fraction);
        UpdateCamera(fraction, elapsed);
    }
    DrawFrame();
    return true;
}

bool EventWaits(ei::mainloop::Site where) {
    ei::mainloop::InSite site(where);
    return platform::EventWaits();
}

// The loop: passes while no event waits, then the event handled. Returns the quit request's code. Whether the
// interface takes the input is what the last pass said; before the first pass it does.
int RunMainLoop() {
    bool interfaceInput = true;
    for (;;) {
        bool waits = EventWaits(ei::mainloop::Site::PeekBeforeFrame);
        while (!waits) {
            interfaceInput = RunPass();
            waits = EventWaits(ei::mainloop::Site::PeekAfterFrame);
        }
        const platform::Event event = platform::NextEvent();
        if (event.type == platform::EventType::Quit) {
            ei::Log("exit: the quit request");
            return event.quitCode;
        }
        if (interfaceInput) DispatchInput(event);
        else HandleWindowEvent(event);
    }
}

// ---- Shutdown --------------------------------------------------------------------------------------

// Both worlds emptied (their players disconnected, their sessions and managers released): ShutDown's last step,
// and an exit function GameMain registers, so that an exit that skips ShutDown (a fatal error's) still empties
// them before the game's exit functions release the network sessions (the original's world exit functions ran
// first). The worlds' own destructors run after those and do not empty them again: the camera and the scene renderer
// (plain globals of other files) may be gone by then.
void EmptyWorlds() {
    g_serverWorld.Destroy();
    g_clientWorld.Destroy();
}

// The game's half of the stop, which the exit and a switch of mods both run: the interface, the cursors, the composed
// textures and both worlds (every manager and its archives, the sound and the music). The renderer, the window, the
// audio device and the settings stay, holding nothing of the worlds: the sun light, the textures bound to the stages
// and the material let go first.
void StopGame() {
    ForgetComposedTextures();
    g_interface.Shutdown();
    g_cursor.Shutdown();
    g_sceneRenderer.SetSun(nullptr);
    if (TextureDevice* device = g_sceneRenderer.device) {
        for (Surface*& stage : device->stageSurfaces) stage = nullptr;
        device->SetMaterial(nullptr);
    }
    g_clientWorld.camera = nullptr;
    EmptyWorlds();
}

void ShutDown() {
    StopGame();
    ei::Log("exit: the game stopped");
    g_sceneRenderer.SetGamma(0.0f, 0.0f);
    g_sceneRenderer.DestroyRenderer();
    ei::Log("exit: the renderer destroyed");
    options::g_options.Save();
    ei::Log("exit: the settings saved");
}

// ---- A switch of mods ------------------------------------------------------------------------------

std::string g_gameFolder;                  // the game folder (UseMod at the start)
std::optional<std::string> g_modRequest;   // the settings' Mod a switch asked for, taken at the loop's next pass

// In the main menu (its window on top, no request pending) with no network session.
bool ModSwitchAllowed() {
    return g_serverWorld.zoneMode == ZoneMode::MainMenu && g_gameFlow.state == 0 &&
           dynamic_cast<MainMenu*>(g_interface.GetActiveWindow()) != nullptr && !IsLive(net::g_gameClient) &&
           !IsLive(net::g_gameServer);
}

void TakeModSwitch() {
    if (!g_modRequest) return;
    const std::string mod = *std::exchange(g_modRequest, std::nullopt);
    if (!ModSwitchAllowed()) {
        ei::Log("mod: the switch to \"%s\" refused: not in the main menu, or a network session", mod);
        return;
    }
    ei::Log("mod: switching to \"%s\"", mod);
    const auto began = std::chrono::steady_clock::now();
    // The game's loading picture (the one a level's load draws) goes up before the stop (nothing can be drawn while
    // the worlds are gone), and each long part of the start steps it again (StartGame), from the new mod's Movies folder.
    LoadingStep(0);
    StopGame();
    // The mod-folder layer forgets the old mod after the switch: a thread that asks it in between (the music stream)
    // reads the old mod again, which the reset then drops.
    UseMod(g_gameFolder, mod);
    modfolders::Reset();
    settings::SetString(settings::kGame, "Mod", mod);
    settings::Save();
    g_clock = LoopClock{};
    StartGame({}, true);
    ei::Log("mod: the reload took %d ms (the sound and the music are silent from the stop until the menu's zone is made)",
            static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count()));
}

// ---- The frame step's state ------------------------------------------------------------------------

bool Running() { return app::g_active != 0 && app::g_interfaceHasScreen != 0; }

std::uint32_t g_movieFrame = /*0079BC58*/ {};   // the number of the next frame file of the movie being written

} // namespace

void UseMod(const std::string& gameFolder, const std::string& setting) {
    g_gameFolder = gameFolder;
    if (setting.empty()) {
        platform::SwitchMod(gameFolder, "");
        return;
    }
    std::string folder = setting;
    const bool full = setting[0] == '\\' || setting[0] == '/' || (setting.size() >= 2 && setting[1] == ':');
    if (!full) {
        const std::string program = platform::ProgramPath();
        folder = program.substr(0, program.find_last_of("\\/") + 1) + setting;
    }
    if (platform::SwitchMod(gameFolder, folder)) {
        ei::Log("mod: %s (its saves in saves\\%s)", platform::ModFolder(), platform::ModName());
    } else {
        ei::Log("mod: no folder %s; the game without a mod", folder);
    }
}

bool RequestModSwitch(const std::string& setting) {
    if (!ModSwitchAllowed()) {
        ei::Log("mod: a switch to \"%s\" asked for outside the main menu, or in a network session: refused",
                setting);
        return false;
    }
    g_modRequest = setting;
    return true;
}

bool ModSwitchPending() { return g_modRequest.has_value(); }

const std::string& GameFolder() { return g_gameFolder; }

std::uint8_t g_active = /*0079BC5C*/ {};
std::uint8_t g_interfaceHasScreen = /*00784750*/ 1;
std::uint8_t g_recordingFrames = /*0079BC54*/ {};
int g_showClientSpeed = /*0079BC60*/ {};
const char* g_loadingMovieName = /*00791EE0*/ "progres.bik";

void StepMusic() {
    if (g_clientWorld.sounds != nullptr) g_clientWorld.sounds->FollowClock(platform::FrameClock());
}

void StartVideo() {
    // The one-instance check first: a second game ends here, before it has a device thread to stop.
    ExitIfGameRuns();
    platform::StartWindowSystem(kWindowClassName);
    // SDL's video is up now (the device needs it): the renderer's device and pipelines are made while the rest starts.
    // The harness build's settings file lacks the render quality, as in TextureDevice's constructor.
    render::PrepareDevice(render::QualitySettingsSupported() ? settings::GetInt(settings::kRenderer, "Msaa") : 1);
}

int GameMain(char* commandLine) {
    // What game.exe's static initialisers did beyond setting values, in their order: the exit functions go in the
    // game's order, not at our CRT's.
    InitGameServerPointer();
    InitGameClientPointer();
    InitCodeTables();

    ReadCommandLine(commandLine);

    GameOptions* options = &options::g_options;
    g_startFolder = platform::CurrentFolder() + '\\';
    options->Load();
    FillEngineConfig(*options);
    g_sceneRenderer.world = &g_clientWorld;
    // The layout comes from the configured resolution, which is the frame's size: the window made later may get a
    // smaller client area (a work area too small for it), and the renderer scales the frame into it.
    screen_layout::Configure(options->resolutionX, options->resolutionY);
    std::atexit(EmptyWorlds);

    // What needs no device is done first (StartGame's files): the renderer's start (CreateRenderer) takes the device
    // from its thread, which has most of its ~260 ms still to go.
    int declined = 0;
    const bool started = StartGame([&declined, options] {
        if (!CheckDiskSpace()) {
            declined = kExitDeclined;
            return false;
        }
        g_mainWindow = platform::CreateGameWindow(g_windowTitle, options->resolutionX, options->resolutionY,
                                                  options->fullScreen != 0);
        if (!g_mainWindow) return false;
        platform::ShowGameWindow(g_mainWindow);
        platform::SetModalLoopStep(StepMusic);
        platform::SetCursor(nullptr);
        g_sceneRenderer.CreateRenderer(g_mainWindow);
        g_sceneRenderer.SetGamma(options->brightness, options->gamma);
        PlayMovies("Start", true);
        // The loading screen's first frame stays up while the world's graphics, the cursors and the interface are made.
        ShowLoadingMovie(0);
        CloseLoadingMovie();
        return true;
    });
    if (!started) return declined;

    const int result = RunMainLoop();
    ShutDown();
    return result;
}

// --- WinMain's frame step ------------------------------------------------------------------------

// The interface drawn and the frame presented, a screenshot per frame while recording a movie. See docs/notes/app.md.
// Nothing while the application is inactive (a 50 ms sleep when no window of the interface is active).
void FrameStep(bool draw) {
    if (!Running()) return;
    if (!g_interface.GetActiveWindow()) {
        platform::Sleep(50);
        return;
    }
    if (!g_sceneRenderer.BeginScene()) return;
    g_interface.Draw();
    g_sceneRenderer.EndScene();
    g_interface.DrawOverlays();
    if (!draw) {
        render::EndUnshownFrame();
        return;
    }
    if (!Running()) return;
    // The harness's frame capture reads the finished frame here (game/app/harness_hooks.h).
    if (auto hook = ei::mainloop::g_harnessHooks.beforePresent) hook();
    CursorDrawFrame(&g_cursor);
    present_window::Present(g_sceneRenderer.device);
    if (!app::g_recordingFrames) return;
    const std::string path = std::format("{}shot{:04}.bmp", g_movieFolder, g_movieFrame);
    platform::File file;
    if (!file.Open(path, platform::FileMode::Create)) {
        // Recording stops; the game goes on.
        game::Warning("Can't open file \"%s\" to write movie frame: recording stops", path);
        app::g_recordingFrames = 0;
        return;
    }
    Dib* bitmap = CaptureScreen(g_sceneRenderer.device);
    if (bitmap) bitmap->Write(&file);
    delete bitmap;
    file.Close();
    ++g_movieFrame;
}

// Plays the movies listed under `section` in the .ini under the movie folder setting (each
// from the movie directory).
void PlayMovies(const std::string& section, bool loadingStep) {
    IniFile ini;
    ini.Load(g_dataFolder + "config\\movie.ini");
    for (const std::string& movie : ini.GetList(section, "movies", ',')) {
        PlayMovie(g_dataFolder + "Movies\\" + movie, true, false, loadingStep);
    }
}

} // namespace app
