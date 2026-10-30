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
#include "item_db.h"
#include "lever.h"
#include "logic_settings.h"
#include "model.h"
#include "net_session.h"
#include "object_tables.h"
#include "options.h"
#include "party_records.h"
#include "quest_records.h"
#include "sound.h"
#include "texture.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ui_widescreen.h"
#include "unit_db.h"
#include "world.h"

#include "ei/main_loop.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/pc24.h"
#include "platform/platform.h"
#include "lib/text_case.h"
#include "text_split.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "world_glue.h"
#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"
#include "world_data.h"

#include <cmath>
#include <cstdlib>

#include <bit>
#include <cstdio>
#include <format>

using options::GameOptions;

namespace app {

// ---- The application's globals that the static initialisers set ------------------------------------

// The command line's strings; the mouse object's pointer, null at the start, released by its exit function.
std::string g_movieFolder = ".\\capture\\";
std::string g_commandLineMap;
std::string g_commandLineMob;
std::string g_commandLineScript;
std::string g_cameraFile;
constinit DirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};


namespace {


// ---- The application's globals ---------------------------------------------------------------------

// The window's title and class name: strings 101 and 102 of the game's string table.
constexpr const char* kWindowTitle = "Evil Islands";
constexpr const char* kWindowClassName = "EIGAME";


// The loop's clock: the time of the last pass (a function-local static), the time not yet run by the server and by
// the client (milliseconds), and the client's speed.
std::uint8_t g_lastTimeGuard = /*0079B9FC*/ {};
std::uint32_t g_lastTime = /*0079B9F8*/ {};
std::uint32_t g_serverBacklog = /*0079BC64*/ {};
std::uint32_t g_clientBacklog = /*0079BC68*/ {};
float g_clientSpeed = /*00784758*/ 1.0f;

// ---- Start-up --------------------------------------------------------------------------------------

constexpr int kExitDeclined = 0xDEAD;   // WinMain's result when the player gives up at a warning
// The lines of sysmsg.txt: line 0 was the "requires DX7" text of the DirectX version check (gone: the game needs
// no DirectDraw), kept so the other lines keep their places.
enum MessageLine { kUnusedDirectX7, kLittleDiskSpace, kInsertDisc };
constexpr int kMessageLines = 3;

void AppendDefaultMessages(std::vector<std::string>& messages) {
    messages.push_back("This Game requires DX7 in order to run!");
    messages.push_back("Not enough free disk space - play on your own risk!");
    messages.push_back("Please, insert EI disc 2 in the CD-ROM and press OK");
}

// The lines of .\res\sysmsg.txt (in the asset code page), or the built-in three when the file is missing or shorter.
void LoadSystemMessages(std::vector<std::string>& messages) {
    messages.clear();
    std::string bytes;
    if (platform::ReadFile(".\\res\\sysmsg.txt", &bytes))
        for (const std::string& line : str::Lines(bytes)) messages.push_back(text::FromAssetText(line));
    if (messages.size() < kMessageLines) {
        messages.clear();
        AppendDefaultMessages(messages);
    }
}

std::string Tail(const std::string& argument, int prefixLength) { return str::Right(argument, static_cast<int>(argument.size()) - prefixLength); }

// The command line: "-moviepath_<folder>" (the client's WorldClientFlag::MoviePathOption off), "-nocam" (NoCamOption
// off; only while not RecordingCamera), a ".cam", ".scr", ".mob" or ".mpr" file, "-benchmark_<frames>" and "-devicetarget_<driver>".
void ReadCommandLine(const char* commandLine) {
    std::vector<std::string> arguments = text::Split(commandLine, ' ');
    g_cameraFile = ".\\camera.cam";
    g_commandLineScript.clear();
    g_commandLineMob.clear();
    g_commandLineMap.clear();
    WorldClient* client = &g_clientWorld;
    GameOptions* options = &options::g_options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (str::Find(arguments[i], "-moviepath_") == 0) {
            client->flags &= ~WorldClientFlag::MoviePathOption;
            {
                std::string folder = Tail(arguments[i], 11);
                g_movieFolder = folder;
            }
            if (g_movieFolder.back() != '\\') g_movieFolder += '\\';
            continue;
        }
        if (!(client->flags & WorldClientFlag::RecordingCamera)) {
            if (text::CompareIgnoringCase(arguments[i],
                                "-nocam") == 0) {
                client->flags &= ~WorldClientFlag::NoCamOption;
                continue;
            }
            if (!(client->flags & WorldClientFlag::RecordingCamera) && str::Find(arguments[i], ".cam") != -1) {
                g_cameraFile = arguments[i].c_str();
                continue;
            }
        }
        if (str::Find(arguments[i], ".scr") != -1) {
            g_commandLineScript = arguments[i].c_str();
        } else if (str::Find(arguments[i], ".mob") != -1) {
            g_commandLineMob = arguments[i].c_str();
        } else if (str::Find(arguments[i], ".mpr") != -1) {
            g_commandLineMap = arguments[i].c_str();
        } else if (str::Find(arguments[i], "-benchmark_") == 0) {
            std::string frames = Tail(arguments[i], 11);
            options->numberOfFrames = str::ToInt(frames);
            options->createOutputInfo = 1;
        } else if (str::Find(arguments[i], "-devicetarget_") == 0) {
            std::string driver = Tail(arguments[i], 14);
            options->driverIndex = str::ToInt(driver);
        }
    }
}

// The window class registered (no icon: its resource is not shipped); a game that runs already is brought to the
// front instead and this one exits.
void RegisterWindowClass(platform::NativeInstance instance) {
    std::snprintf(g_windowTitle, sizeof(g_windowTitle), "%s", kWindowTitle);
    platform::RegisterWindowClass(instance, kWindowClassName, HandleWindowEvent);

    platform::NativeWindow other = platform::FindClassWindow(kWindowClassName, g_windowTitle);
    if (!other) return;
    ei::Log("app: another game window exists (window %p of process %lu, class and title \"%s\" the same): the one-instance "
            "check ends this game with exit code -1", other, static_cast<unsigned long>(platform::WindowProcess(other)),
            g_windowTitle);
    platform::BringToFront(other);
    std::exit(-1);
}

struct PathParts {
    char drive[0x80];
    char directory[0x400];
    char name[0x80];
    char extension[0x80];
};

// "<drive>\." of `path`: the root the volume calls take.
void DriveRoot(const char* path, PathParts& parts) {
#pragma warning(suppress : 4996)
    _splitpath(path, parts.drive, parts.directory, parts.name, parts.extension);
    std::memcpy(parts.drive + std::strlen(parts.drive), "\\.", 3);
}

// The free bytes on the drive of `folder`; 0 when unknown.
double FreeDiskSpace(const char* folder) {
    PathParts parts;
    DriveRoot(folder, parts);
    return platform::FreeDiskSpace(parts.drive);
}

// The player's answer to a warning: false when cancelled.
bool WarningAccepted(const std::string& text) {
    return platform::ShowMessageBox(nullptr, text.c_str(), "Warning", platform::MessageButtons::OkCancel,
                                    platform::MessageIcon::Warning) != platform::MessageAnswer::Cancel;
}

// Warns when the data folder's drive has under 300 MB free, and asks for the second disc while the
// disc folder (when it is not the data folder) is not on a volume with the options' label. False
// when the player cancels.
bool CheckDrives(const std::vector<std::string>& messages) {
    std::string discFolder = g_discFolder;
    std::string dataFolder = g_dataFolder;
    const double freeBytes = FreeDiskSpace(dataFolder.c_str());
    if (freeBytes != 0.0 && freeBytes < 314572800.0 && !WarningAccepted(messages[kLittleDiskSpace])) return false;
    if (str::Compare(dataFolder, discFolder) != 0) {
        PathParts parts;
        DriveRoot(discFolder.c_str(), parts);
        while (str::Compare(options::g_options.cdLabels[1], platform::VolumeLabel(parts.drive)) != 0) {
            if (!WarningAccepted(messages[kInsertDisc])) return false;
        }
    }
    return true;
}

void ReleaseMouse(DirectInputMouse* mouse) {
    if (--mouse->references == 0) delete mouse;
}

void CreateMouse() {
    auto* mouse = new DirectInputMouse();
    DirectInputMouse* old = g_mouse;
    g_mouse = mouse;
    if (mouse) ++mouse->references;
    if (old) ReleaseMouse(old);
}

void CreateFolders(std::string& path) {
    path = std::format("{}saves\\", g_dataFolder);
    platform::CreateFolder(path.c_str());
    path = std::format("{}mp\\", g_dataFolder);
    platform::CreateFolder(path.c_str());
    path = std::format("{}textures\\", g_dataFolder);
    platform::CreateFolder(path.c_str());
}

// The renderer created on the window, its gamma set from the options.
void CreateRenderer() {
    GameOptions* options = &options::g_options;
    Camera* camera = &g_camera;
    camera->localPosition.x = options->cameraDefaultXDeploy;
    camera->localPosition.y = options->cameraDefaultYDeploy;
    camera->localPosition.z = options->cameraDefaultHeight;
    g_sceneRenderer.CreateRenderer(g_mainWindow, options);
    g_sceneRenderer.SetGamma(options->brightness, options->contrast, options->gamma);
}

// The sound opened (44.1 kHz, 16 bits) with its music folder, wave archives and volumes. Returns
// the disc folder's text pointer (see the caller).
std::uintptr_t OpenSound() {
    GameOptions* options = &options::g_options;
    std::string textFolder = g_textFolder;
    std::string dataFolder = g_dataFolder;
    std::string discFolder = g_discFolder;
    g_clientWorld.sounds->Open(g_mainWindow, 1, 44100);
    {
        std::string registry = textFolder + "streamsn.reg";
        std::string folder = dataFolder + "stream\\";
        g_clientWorld.sounds->SetPath(folder, registry);
    }
    {
        std::string effects = textFolder + "sfx.res";
        SoundManager* sound = g_clientWorld.sounds;
        sound->ClearWaveSources();
        sound->AddWaveSource(effects);
    }
    {
        std::string unused = discFolder + "res\\speech.res";   // "res\speech.res" (built twice)
        std::string speech = discFolder + "res\\speech.res";
        g_clientWorld.sounds->AddWaveSource(speech);
    }
    g_clientWorld.sounds->SetEffectsVolume(options->audioSfx);
    g_clientWorld.sounds->SetSpeechVolume(options->audioSpeech);
    g_clientWorld.sounds->SetMusicVolume(options->audioStreaming);
    g_clientWorld.sounds->panDirect = options->reverseStereo != 0;
    return reinterpret_cast<std::uintptr_t>(discFolder.c_str());
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

constexpr float kOneTenth = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kOneTenth) == 0x3DCCCCCDu);
constexpr float kSlowest = kOneTenth;   // the client speed's limits
constexpr float kFastest = 10.0f;
constexpr std::uint32_t kBacklogLimit = 0x4000000;
constexpr int kLongestPassTicks = 5;

// The world tick's length in milliseconds, and milliseconds in the camera's time: times 1 / 55, one 24-bit rounding.
std::uint32_t TickLength() { return static_cast<std::uint32_t>(worlddata::g_updatePeriod); }
float CameraSeconds(std::uint32_t milliseconds) { return pc24::Mul(static_cast<double>(milliseconds), 0.018181818f); }

// Three calls of the loop take a harness hook (ei/main_loop.h, set by tests/verify/harness.cpp) when
// one is set: the frame's last camera update is held still while a scenario is frozen; the server's
// update and the client's tick are followed by the harness's "speed" hold. Otherwise the function itself.
void FrameCameraUpdate(Camera* camera, float seconds) {
    if (auto hook = ei::mainloop::g_harnessHooks.frameCameraUpdate) hook(camera, seconds);
    else camera->Update(seconds);
}
// False when the update stops the frame.
bool UpdateServer() {
    if (auto hook = ei::mainloop::g_harnessHooks.serverUpdate) return hook(&g_serverWorld) != 0;
    g_serverWorld.Update();
    return true;
}
bool RunClientSegment() {
    if (auto hook = ei::mainloop::g_harnessHooks.clientSegment) return hook(&g_clientWorld) != 0;
    return g_clientWorld.RunTick();
}

// A change of the window's activation taken up: active again, every surface is restored (when the renderer
// was created) and the renderer runs; inactive, the renderer stops and the desktop is repainted.
void TakeActivationChange() {
    if (!g_activationChanged) return;
    if (g_active) {
        g_sceneRenderer.flags |= 1;
        platform::SetCursor(nullptr);
    } else {
        g_sceneRenderer.flags &= ~1u;
        platform::RepaintDesktop();
    }
    g_activationChanged = 0;
}

std::uint32_t TimeSinceLastPass() {
    if (!(g_lastTimeGuard & 1)) {
        g_lastTimeGuard |= 1;
        g_lastTime = platform::Milliseconds();
    }
    std::uint32_t now;
    {
        ei::mainloop::InSite site(ei::mainloop::Site::FrameTime);
        now = platform::Milliseconds();
    }
    // The voices whose wave has played out by the frame's time end now (the sound manager's Tick frees them).
    platform::audio::DeliverEnds(now);
    const std::uint32_t elapsed = now - g_lastTime;
    g_lastTime += elapsed;
    return elapsed;
}

// The worlds run for the time that has passed: the server a tick at a time while it has a whole
// tick, the client likewise while it has one and either server updates wait or its view is not in
// mode 2 (each client tick also moves the camera by a tick, and notes that step for the drawn view:
// camera::NoteViewTick). False when a world stops the frame.
bool RunWorlds() {
    for (;;) {
        bool settled = true;
        if (g_serverBacklog >= TickLength()) {
            settled = false;
            if (!UpdateServer()) return false;
            g_serverBacklog -= TickLength();
            net::Pump();
        }
        if (g_clientBacklog >= TickLength() && (g_clientWorld.PumpHasIncoming() || !g_clientWorld.IsLocalViewMode2())) {
            if (!RunClientSegment()) return false;
            const std::uint32_t tick = TickLength();
            g_clientBacklog -= tick;
            camera::NoteViewBeforeTick(&g_camera);
            g_camera.Update(CameraSeconds(tick));
            camera::NoteViewTick(&g_camera);
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
        result = pc24::Exp(balance * kOneTenth);
    } else if (balance > 1.0f) {
        result = (std::sqrt(balance) * kOneTenth + 1.0f) - kOneTenth;
    } else {
        result = 1.0f;   // also NaN
    }
    result = result >= kSlowest ? result : kSlowest;
    if (kFastest < result) result = kFastest;
    return result;
}

void PrintClientSpeed(float speed, float balance) {
    console::EndLine(console::g_netLogOut << "Client speed = " << speed << " balance = " << balance);
}

// One pass of the loop while no message waits. Returns whether the interface takes the input
// messages from now on: not while the renderer is inactive.
bool RunPass() {
    TakeActivationChange();
    std::uint32_t elapsed = TimeSinceLastPass();
    if (!(g_sceneRenderer.flags & 1)) {
        GuardPlayerData(0);
        return false;
    }
    GuardPlayerData(1);
    WorldClient* client = &g_clientWorld;
    if (client->IsLocalPlayerIdle()) {
        if (IsMultiplayerClient()) g_gameFlow.RecreateClientSession();
        else g_gameFlow.CloseSessions();
    }
    if (elapsed > 0x80000000u) elapsed = 0;
    net::Pump();
    if (elapsed > TickLength() * kLongestPassTicks) elapsed = TickLength() * kLongestPassTicks;
    if (g_recordingFrames) {
        // A recorded frame lasts 1000 / the movie's frames a second, rounded up.
        const float frame = pc24::Div(1000.0, static_cast<double>(options::g_options.movieCapturingFps));
        elapsed = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::ceil(frame)));
    }
    g_serverBacklog += elapsed;
    if (!client->IsLocalViewMode2()) g_clientSpeed = 1.0f;
    // The elapsed time at the client's speed plus the backlog (an int), each a 24-bit rounding, truncated as _ftol does.
    const float backlog = pc24::Add(pc24::Mul(static_cast<double>(elapsed), g_clientSpeed),
                                    static_cast<double>(static_cast<std::int32_t>(g_clientBacklog)));
    g_clientBacklog = static_cast<std::uint32_t>(static_cast<std::int64_t>(backlog));
    if (g_clientBacklog > kBacklogLimit) g_clientBacklog = 0;

    if (RunWorlds()) {
        g_gameFlow.ProcessRequest();
        const float fraction =
            pc24::Div(static_cast<double>(g_clientBacklog), static_cast<double>(static_cast<std::int32_t>(TickLength())));
        if (!client->paused) client->tickFraction = fraction;
        const float balance = pc24::Sub(static_cast<double>(client->PumpIncomingCount()), fraction);
        g_clientSpeed = SpeedForBalance(balance);
        if (g_showClientSpeed) PrintClientSpeed(g_clientSpeed, balance);

        // The camera runs on the real time of the pass, a tick at a time; the draw spreads the last tick's
        // step over the part of the next tick the client has had (camera::DrawnView).
        camera::SetViewProgress(fraction, static_cast<float>(elapsed) / static_cast<float>(TickLength()));
        Camera* camera = &g_camera;
        std::uint32_t tick = TickLength();
        while (elapsed > tick) {
            camera->Update(CameraSeconds(tick));
            tick = TickLength();
            elapsed -= tick;
        }
        FrameCameraUpdate(camera, CameraSeconds(elapsed));
        if (!client->paused) {
            if (camera->flags & Camera::kRecording)
                camera->RecordFrame(std::bit_cast<std::uint32_t>(pc24::Add(static_cast<double>(client->tick), client->tickFraction)));
            if (camera->flags & Camera::kPlaying)
                camera->PlaybackStep(pc24::Add(static_cast<double>(client->tick), client->tickFraction));
        }
    }
    g_interface.Update();
    const bool draw = static_cast<char>(g_movieMode) != 0;
    if (auto hook = ei::mainloop::g_harnessHooks.frameStep) hook(draw, true);
    else app::FrameStep(draw, true);
    return true;
}

bool EventWaits(ei::mainloop::Site where) {
    ei::mainloop::InSite site(where);
    return platform::EventWaits();
}

// The loop: passes while no event waits, then the event handled. Returns the quit request's code.
// `interfaceInput` says whether the interface takes the input (see GameMain).
int RunMainLoop(bool interfaceInput) {
    platform::Event event;
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
    DirectInputMouse* mouse = g_mouse;
    g_mouse = nullptr;
    if (mouse) ReleaseMouse(mouse);
    g_sceneRenderer.SetGamma(0.0f, 0.0f, 0.0f);
    g_sceneRenderer.DestroyRenderer();
    options::g_options.Save();
    g_clientWorld.SetCamera(nullptr);
    EmptyWorlds();
}

} // namespace

// The mouse's reference dropped (ShutDown has already taken it: null at exit).
void ReleaseMouseAtExit() {
    if (DirectInputMouse* mouse = g_mouse)
        if (--mouse->references == 0) delete mouse;
}

char g_windowTitle[256] = /*0079BB24*/ {""};
platform::NativeInstance g_instance = /*0079BC24*/ {};
platform::NativeWindow g_mainWindow = /*0079BC28*/ {};
std::uint8_t g_active = /*0079BC5C*/ {};
std::uint8_t g_activationChanged = /*0079BC5D*/ {};
std::uint8_t g_interfaceHasScreen = /*00784750*/ 1;
std::uint8_t g_quitAllowed = /*00784751*/ 1;
std::uint8_t g_recordingFrames = /*0079BC54*/ {};
int g_showClientSpeed = /*0079BC60*/ {};
const char* g_loadingMovieName = /*00791EE0*/ "progres.bik";

int GameMain(platform::NativeInstance instance, platform::NativeInstance, char* commandLine, int show) {
    std::vector<std::string> messages;
    LoadSystemMessages(messages);
    // The classes the chunk archives can hold: 1 the class template, 2 the party record.
    app::TypeRegistryRegister(&g_classRegistry, 1, CreateClassTemplate);
    app::TypeRegistryRegister(&g_classRegistry, 2, CreatePartyRecord);
    ReadCommandLine(commandLine);
    RegisterWindowClass(instance);

    GameOptions* options = &options::g_options;
    StoreStartFolder();
    {
        std::string settings = g_startFolder + "config\\ai.reg";
        g_logicSettings.Load(settings);
    }
    options->Load();
    widescreen::Configure(options->resolutionX, options->resolutionY);
    std::atexit(EmptyWorlds);
    g_clientWorld.Create();
    g_serverWorld.Create();
    // The field of view: pi * 2 / 7 (51.4 degrees).
    g_camera.Init(options, std::bit_cast<std::uint32_t>(options->nearClipDistance),
                   std::bit_cast<std::uint32_t>(options->farClipDistance),
                   std::bit_cast<std::uint32_t>(static_cast<float>(3.141592653589793 * 0.2857142857142857)));
    g_clientWorld.SetCamera(&g_camera);
    if (!CheckDrives(messages)) return kExitDeclined;
    CreateMouse();

    std::string folder;
    CreateFolders(folder);
    g_instance = instance;
    g_mainWindow = platform::CreateGameWindow(instance, g_windowTitle, options->resolutionX, options->resolutionY);
    if (!g_mainWindow) return 0;
    platform::ShowGameWindow(g_mainWindow, show);
    platform::SetCursor(nullptr);

    // "The interface takes the input" is carried in one byte and set after every pass. Before
    // the first pass it holds the top byte of stack memory that start-up used last for the disc
    // folder's text (with sound) or for the free non-local video memory (without; dgVoodoo's 256 MB,
    // whose top byte is not zero): a message that waits before the first pass goes to the interface
    // when that byte is not zero.
    CreateRenderer();
    const bool interfaceTakesInput = options->audio ? (OpenSound() >> 24) != 0 : true;

    PlayMovies("Start", true);
    g_loadingMovieName = "Progres.bik";
    ShowLoadingMovie(0);
    CloseLoadingMovie();
    LoadDatabases();
    g_mouse->Setup(g_mainWindow, instance);
    g_clientWorld.SetUpGraphics(g_sceneRenderer.device);
    SetUpCursors(options->fpsIndependentCursor != 0, g_mouse, 0);
    g_interface.Create(g_sceneRenderer.device);
    g_mapDatabase.LoadAll();
    StartFirstMap();
    g_sceneRenderer.SetGamma(options->brightness, options->contrast, options->gamma);

    const int result = RunMainLoop(interfaceTakesInput);
    ShutDown();
    return result;
}

} // namespace app
