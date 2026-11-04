// WinMain: the application's start-up, its main loop and its shutdown. See docs/notes/app.md, "WinMain".
#include "bink_video.h"
#include "engine_object.h"
#include "app.h"
#include "acks_db.h"
#include "asset_text.h"
#include "app_helpers.h"
#include "camera.h"
#include "class_templates.h"
#include "console.h"
#include "drawn_motion.h"
#include "item_db.h"
#include "lever.h"
#include "logic_settings.h"
#include "model.h"
#include "net_coder.h"
#include "net_session.h"
#include "object_tables.h"
#include "options.h"
#include "party_records.h"
#include "quest_records.h"
#include "sound.h"
#include "texture.h"
#include "ui.h"
#include "ui_cursor.h"
#include "screen_layout.h"
#include "settings.h"
#include "unit_db.h"
#include "weather_noise.h"
#include "world.h"

#include "main_loop.h"
#include "lib/maths.h"
#include "lib/rebirth.h"
#include "render/render.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include "lib/text_case.h"
#include "text_split.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "../platform/log.h"
#include "game_singletons.h"
#include "world_data.h"

#include <cmath>
#include <cstdlib>

#include <cstdio>

using options::GameOptions;

namespace app {

// ---- The application's globals that the static initialisers set ------------------------------------

// The command line's strings.
std::string g_movieFolder = ".\\capture\\";
std::string g_commandLineMap;
std::string g_commandLineMob;
std::string g_commandLineScript;
std::string g_cameraFile;


namespace {


// ---- The application's globals ---------------------------------------------------------------------

// The window's title and class name: strings 101 and 102 of the game's string table.
constexpr const char* kWindowTitle = "Evil Islands";
constexpr const char* kWindowClassName = "EIGAME";

// ---- Start-up --------------------------------------------------------------------------------------

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
            {
                std::string folder = Tail(arguments[i], 11);
                g_movieFolder = folder;
            }
            if (g_movieFolder.back() != '\\') g_movieFolder += '\\';
            continue;
        }
        if (!(client->flags & WorldClientFlag::RecordingCamera) && str::Find(arguments[i], ".cam") != -1) {
            g_cameraFile = arguments[i].c_str();
            continue;
        }
        if (str::Find(arguments[i], ".scr") != -1) {
            g_commandLineScript = arguments[i].c_str();
        } else if (str::Find(arguments[i], ".mob") != -1) {
            g_commandLineMob = arguments[i].c_str();
        } else if (str::Find(arguments[i], ".mpr") != -1) {
            g_commandLineMap = arguments[i].c_str();
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
    return platform::ShowMessageBox(nullptr, text.c_str(), "Warning", platform::MessageButtons::OkCancel,
                                    platform::MessageIcon::Warning) != platform::MessageAnswer::Cancel;
}

// Warns when the data folder's drive has under 300 MB free. False when the player cancels.
bool CheckDiskSpace() {
    const double freeBytes = platform::FreeDiskSpace(g_dataFolder.c_str());
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
    g_clientWorld.sounds->Open(g_mainWindow, 1, 44100);
    {
        std::string registry = textFolder + "streamsn.reg";
        std::string folder = dataFolder + "stream\\";
        g_clientWorld.sounds->SetPath(folder, registry);
    }
    {
        std::string effects = textFolder + "sfx.res";
        SoundManager* sound = g_clientWorld.sounds.get();
        sound->ClearWaveSources();
        sound->AddWaveSource(effects);
    }
    {
        std::string speech = dataFolder + "res\\speech.res";
        g_clientWorld.sounds->AddWaveSource(speech);
    }
    g_clientWorld.sounds->SetEffectsVolume(options->audioSfx);
    g_clientWorld.sounds->SetSpeechVolume(options->audioSpeech);
    g_clientWorld.sounds->SetMusicVolume(options->audioStreaming);
    g_clientWorld.sounds->panDirect = options->reverseStereo != 0;
}

void LoadDatabases() {
    const std::string archive = "database.res";
    GameOptions* options = &options::g_options;
    g_skillDatabase.Load(options->resourcesPath, archive);
    g_questDatabase.Load(options->resourcesPath);
    g_unitDatabase.Load(options->resourcesPath, archive);
    g_spellDatabase.Load(options->resourcesPath, archive);
    g_itemDatabase.Load(options->resourcesPath, archive);
    g_leverTable.Load(options->resourcesPath);
    g_ackDatabase.Load(options->resourcesPath);
    g_printsDatabase.Load(options->resourcesPath);
}

// ---- The main loop ---------------------------------------------------------------------------------

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
std::uint32_t TickLength() { return static_cast<std::uint32_t>(worlddata::g_updatePeriod); }
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
    platform::Event event;
    bool interfaceInput = true;
    for (;;) {
        bool waits = EventWaits(ei::mainloop::Site::PeekBeforeFrame);
        while (!waits) {
            interfaceInput = RunPass();
            waits = EventWaits(ei::mainloop::Site::PeekAfterFrame);
        }
        if (!platform::NextEvent(&event)) return event.quitCode;
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

void ShutDown() {
    ForgetComposedTextures();
    g_interface.Shutdown();
    g_cursor.Shutdown();
    g_sceneRenderer.SetGamma(0.0f, 0.0f);
    g_sceneRenderer.DestroyRenderer();
    options::g_options.Save();
    g_clientWorld.camera = nullptr;
    EmptyWorlds();
}

} // namespace

char g_windowTitle[256] = /*0079BB24*/ {""};
platform::NativeWindow g_mainWindow = /*0079BC28*/ {};
std::uint8_t g_active = /*0079BC5C*/ {};
std::uint8_t g_interfaceHasScreen = /*00784750*/ 1;
std::uint8_t g_quitAllowed = /*00784751*/ 1;
std::uint8_t g_recordingFrames = /*0079BC54*/ {};
int g_showClientSpeed = /*0079BC60*/ {};
const char* g_loadingMovieName = /*00791EE0*/ "progres.bik";

void StartVideo() {
    // The one-instance check first: a second game ends here, before it has a device thread to stop.
    ExitIfGameRuns();
    platform::StartWindowSystem(kWindowClassName);
    // SDL's video is up now (the device needs it): the renderer's device and pipelines are made while the rest starts.
    // The harness build's settings file lacks the render quality, as in TextureDevice's constructor.
    render::PrepareDevice(render::QualitySettingsSupported() ? settings::GetInt(settings::kRenderer, "Msaa", render::kDefaultMsaa) : 1);
}

int GameMain(char* commandLine) {
    // What game.exe's static initialisers did beyond setting values, in their order: the exit functions go in the
    // game's order, not at our CRT's.
    InitGameServerPointer();
    InitGameClientPointer();
    InitCodeTables();
    BuildWeatherNoise();

    // The classes the chunk archives can hold: 1 the class template, 2 the party record.
    TypeRegistryRegister(&g_classRegistry, 1, CreateClassTemplate);
    TypeRegistryRegister(&g_classRegistry, 2, CreatePartyRecord);
    ReadCommandLine(commandLine);

    GameOptions* options = &options::g_options;
    g_startFolder = platform::CurrentFolder() + '\\';
    g_logicSettings.Load(g_startFolder + "config\\ai.reg");
    options->Load();
    // The layout comes from the configured resolution, which is the frame's size: the window made later may get a
    // smaller client area (a work area too small for it), and the renderer scales the frame into it.
    screen_layout::Configure(options->resolutionX, options->resolutionY);
    std::atexit(EmptyWorlds);
    g_clientWorld.Create();
    g_serverWorld.Create();
    // The field of view: pi * 2 / 7 (51.4 degrees).
    g_camera.Init(options, options->nearClipDistance, options->farClipDistance, kPi * 2.0f / 7.0f);
    g_clientWorld.camera = &g_camera;
    if (!CheckDiskSpace()) return kExitDeclined;
    CreateFolders();

    g_mainWindow = platform::CreateGameWindow(g_windowTitle, options->resolutionX, options->resolutionY,
                                              options->fullScreen != 0);
    if (!g_mainWindow) return 0;
    platform::ShowGameWindow(g_mainWindow);
    platform::SetCursor(nullptr);

    // What needs no device is done first: the renderer's start (CreateRenderer) takes the device from its thread, which
    // has most of its ~260 ms still to go.
    if (options->audio) OpenSound();
    LoadDatabases();
    g_mapDatabase.LoadAll();
    g_sceneRenderer.CreateRenderer(g_mainWindow, options);
    g_sceneRenderer.SetGamma(options->brightness, options->gamma);

    PlayMovies("Start", true);
    // The loading screen's first frame stays up while the world's graphics, the cursors and the interface are made.
    ShowLoadingMovie(0);
    CloseLoadingMovie();
    g_clientWorld.SetUpGraphics(g_sceneRenderer.device);
    SetUpCursors(0);
    g_interface.Create(g_sceneRenderer.device);
    StartFirstMap();

    const int result = RunMainLoop();
    ShutDown();
    return result;
}

} // namespace app
