// WinMain: the application's start-up, its main loop and its shutdown. See docs/notes/app.md, "WinMain".
#include "bink_video.h"
#include "engine_object.h"
#include "app.h"
#include "acks_db.h"
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
#include "unit_db.h"
#include "world.h"

#include "ei/main_loop.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/text_case.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/static_object.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "world_glue.h"
#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"
#include "world_data.h"

#include <cmath>
#include <cstdlib>

#include <bit>
#include <cstdio>
#include <io.h>

using options::GameOptions;

namespace app {

// ---- The application's globals that the static initialisers set ------------------------------------

// The command line's strings; the mouse object's pointer, null at the start, released by its exit function.
std::string g_movieFolder = ".\\capture\\";
std::string g_commandLineMap;
std::string g_commandLineMob;
std::string g_commandLineScript;
std::string g_cameraFile;
constinit CDirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};


namespace {


// Constants of the exe, read when they are used (a literal could be folded at another precision).
// A step of a float expression kept apart from the next: the value passes through a double, which
// holds any result at the precisions the game runs at (53 bits before the device object is made, 24 after).
double Step(double value) {
    volatile double kept = value;
    return kept;
}

// ---- The application's globals ---------------------------------------------------------------------

// The window: its class atom, class name and title (0x100 bytes each, set by RegisterWindowClass), the
// instance and the window.
std::uint16_t g_windowClassAtom = /*0079BA20*/ {};
char g_windowClassName[256] = /*0079BA24*/ {""};
constexpr int kTextSize = 0x100;
// The window's title and class name: strings 101 and 102 of the game's string table.
constexpr const char* kWindowTitle = "Evil Islands";
constexpr const char* kWindowClassName = "EIGAME";


// The loop's clock: the time of the last pass (a function-local static), the time not yet run by the server and by
// the client (milliseconds), and the client's speed.
std::uint8_t g_lastTimeGuard = /*0079B9FC*/ {};
DWORD g_lastTime = /*0079B9F8*/ {};
std::uint32_t g_serverBacklog = /*0079BC64*/ {};
std::uint32_t g_clientBacklog = /*0079BC68*/ {};
float g_clientSpeed = /*00784758*/ 1.0f;

// ---- Start-up --------------------------------------------------------------------------------------

constexpr int kExitDeclined = 0xDEAD;   // WinMain's result when the player gives up at a warning
constexpr UINT kWarningBox = 0x31;      // MB_OKCANCEL | MB_ICONEXCLAMATION
// The lines of sysmsg.txt: line 0 was the "requires DX7" text of the DirectX version check (gone: the game needs
// no DirectDraw), kept so the other lines keep their places.
enum MessageLine { kUnusedDirectX7, kLittleDiskSpace, kInsertDisc };
constexpr int kMessageLines = 3;

void AppendDefaultMessages(std::vector<std::string>& messages) {
    messages.push_back("This Game requires DX7 in order to run!");
    messages.push_back("Not enough free disk space - play on your own risk!");
    messages.push_back("Please, insert EI disc 2 in the CD-ROM and press OK");
}

// The lines of .\res\sysmsg.txt, or the built-in three when the file is missing or shorter.
void LoadSystemMessages(std::vector<std::string>& messages) {
    std::FILE* file = lib::OpenFile(".\\res\\sysmsg.txt", "rt");
    if (!file) {
        AppendDefaultMessages(messages);
        return;
    }
    const std::size_t length = static_cast<std::size_t>(_filelength(_fileno(file)));
    char* text = new char[length];
    std::memset(text, 0, length);
    std::fread(text, 1, length, file);
    std::fclose(file);
    text::Split(text, &messages, '\n');
    if (messages.size() < kMessageLines) {
        messages.clear();
        AppendDefaultMessages(messages);
    }
    delete[] text;
}

std::string Tail(const std::string& argument, int prefixLength) { return str::Right(argument, static_cast<int>(argument.size()) - prefixLength); }

// The command line: "-moviepath_<folder>" (the client's WorldClientFlag::MoviePathOption off), "-nocam" (NoCamOption
// off; only while not RecordingCamera), a ".cam", ".scr", ".mob" or ".mpr" file, "-benchmark_<frames>" and "-devicetarget_<driver>".
void ReadCommandLine(const char* commandLine) {
    std::vector<std::string> arguments;
    text::Split(commandLine, &arguments, ' ');
    g_cameraFile = ".\\camera.cam";
    g_commandLineScript.clear();
    g_commandLineMob.clear();
    g_commandLineMap.clear();
    CWorldClient* client = &g_clientWorld;
    GameOptions* options = &options::g_options.Get();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (str::Find(arguments[i], "-moviepath_") == 0) {
            client->flags_ &= ~WorldClientFlag::MoviePathOption;
            {
                std::string folder = Tail(arguments[i], 11);
                g_movieFolder = folder;
            }
            if (g_movieFolder.back() != '\\') g_movieFolder += '\\';
            continue;
        }
        if (!(client->flags_ & WorldClientFlag::RecordingCamera)) {
            if (text::CompareIgnoringCase(arguments[i],
                                "-nocam") == 0) {
                client->flags_ &= ~WorldClientFlag::NoCamOption;
                continue;
            }
            if (!(client->flags_ & WorldClientFlag::RecordingCamera) && str::Find(arguments[i], ".cam") != -1) {
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
            options->numberOfFrames_ = str::ToInt(frames);
            options->createOutputInfo_ = 1;
        } else if (str::Find(arguments[i], "-devicetarget_") == 0) {
            std::string driver = Tail(arguments[i], 14);
            options->driverIndex_ = str::ToInt(driver);
        }
    }
}

// The window class registered; a game that runs already is brought to the front instead and this one exits.
void RegisterWindowClass(HINSTANCE instance) {
    gameapi::lstrcpynA(g_windowTitle, kWindowTitle, kTextSize);
    gameapi::lstrcpynA(g_windowClassName, kWindowClassName, kTextSize);
    WNDCLASSEXA windowClass;
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance;
    windowClass.hIcon = nullptr;     // the icon resource is not shipped
    windowClass.hIconSm = nullptr;
    windowClass.hCursor = nullptr;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = g_windowClassName;
    g_windowClassAtom = gameapi::RegisterClassExA(&windowClass);

    HWND other = gameapi::FindWindowA(MAKEINTRESOURCEA(g_windowClassAtom), g_windowTitle);
    if (!other) return;
    DWORD otherProcess = 0;
    gameapi::GetWindowThreadProcessId(other, &otherProcess);
    ei::Log("app: another game window exists (window %p of process %lu, class and title \"%s\" the same): the one-instance "
            "check ends this game with exit code -1", static_cast<void*>(other), otherProcess, g_windowTitle);
    const DWORD foregroundThread = gameapi::GetWindowThreadProcessId(gameapi::GetForegroundWindow(), nullptr);
    const DWORD otherThread = gameapi::GetWindowThreadProcessId(other, nullptr);
    if (otherThread != foregroundThread || gameapi::IsIconic(other)) {
        HWND popup = gameapi::GetLastActivePopup(other);
        if (gameapi::IsIconic(popup)) gameapi::ShowWindow(popup, SW_RESTORE);
        gameapi::BringWindowToTop(popup);
        gameapi::SetForegroundWindow(popup);
    }
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

// The free bytes on the drive of `folder` through GetDiskFreeSpaceExA where kernel32 has it, else
// from the cluster counts. False when the call fails.
bool FreeDiskSpace(const char* folder, double* freeBytes) {
    PathParts parts;
    DriveRoot(folder, parts);
    using FreeSpaceEx = BOOL(__stdcall*)(const char*, ULARGE_INTEGER*, ULARGE_INTEGER*, ULARGE_INTEGER*);
    auto* extended = reinterpret_cast<FreeSpaceEx>(
        gameapi::GetProcAddress(gameapi::GetModuleHandleA("kernel32.dll"), "GetDiskFreeSpaceExA"));
    if (extended) {
        ULARGE_INTEGER available, total, totalFree;
        const BOOL ok = extended(parts.drive, &available, &total, &totalFree);
        *freeBytes = Step(static_cast<double>(static_cast<std::int64_t>(available.QuadPart)));
        return ok != 0;
    }
    DWORD sectorsPerCluster, bytesPerSector, freeClusters, clusters;
    const BOOL ok = gameapi::GetDiskFreeSpaceA(parts.drive, &sectorsPerCluster, &bytesPerSector, &freeClusters, &clusters);
    *freeBytes = Step(x87::FromUnsigned(sectorsPerCluster) * x87::FromUnsigned(bytesPerSector)) * x87::FromUnsigned(freeClusters);
    return ok != 0;
}

// Warns when the data folder's drive has under 300 MB free, and asks for the second disc while the
// disc folder (when it is not the data folder) is not on a volume with the options' label. False
// when the player cancels.
bool CheckDrives(const std::vector<std::string>& messages) {
    std::string discFolder = g_discFolder;
    std::string dataFolder = g_dataFolder;
    double freeBytes;
    if (FreeDiskSpace(dataFolder.c_str(), &freeBytes) && freeBytes != 0.0 && freeBytes < 314572800.0) {
        if (gameapi::MessageBoxA(nullptr, messages[kLittleDiskSpace].c_str(), "Warning", kWarningBox) == IDCANCEL) return false;
    }
    if (str::Compare(dataFolder, discFolder) != 0) {
        char volume[0x400] = {};
        PathParts parts;
        DriveRoot(discFolder.c_str(), parts);
        gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        while (str::Compare(options::g_options->cdLabels_[1], volume) != 0) {
            if (gameapi::MessageBoxA(nullptr, messages[kInsertDisc].c_str(), "Warning", kWarningBox) == IDCANCEL) return false;
            gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        }
    }
    return true;
}

void ReleaseMouse(CDirectInputMouse* mouse) {
    if (--mouse->references_ == 0) delete mouse;
}

void CreateMouse() {
    auto* mouse = new CDirectInputMouse();
    CDirectInputMouse* old = g_mouse;
    g_mouse = mouse;
    if (mouse) ++mouse->references_;
    if (old) ReleaseMouse(old);
}

void CreateFolders(std::string& path) {
    path = str::Format("%ssaves\\", g_dataFolder);
    gameapi::CreateDirectoryA(path.c_str(), nullptr);
    path = str::Format("%smp\\", g_dataFolder);
    gameapi::CreateDirectoryA(path.c_str(), nullptr);
    path = str::Format("%stextures\\", g_dataFolder);
    gameapi::CreateDirectoryA(path.c_str(), nullptr);
}

// The renderer created on the window, its gamma set from the options.
void CreateRenderer() {
    GameOptions* options = &options::g_options.Get();
    CCamera* camera = &g_camera.Get();
    camera->localPosition_.x = options->cameraDefaultXDeploy_;
    camera->localPosition_.y = options->cameraDefaultYDeploy_;
    camera->localPosition_.z = options->cameraDefaultHeight_;
    g_sceneRenderer->CreateRenderer(g_mainWindow, options);
    g_sceneRenderer->SetGamma(options->brightness_, options->contrast_, options->gamma_);
}

// The sound opened (44.1 kHz, 16 bits) with its music folder, wave archives and volumes. Returns
// the disc folder's text pointer (see the caller).
std::uintptr_t OpenSound() {
    GameOptions* options = &options::g_options.Get();
    std::string textFolder = g_textFolder;
    std::string dataFolder = g_dataFolder;
    std::string discFolder = g_discFolder;
    g_clientWorld.sounds_->Open(g_mainWindow, 1, -1, 44100);
    {
        std::string registry = textFolder + "streamsn.reg";
        std::string folder = dataFolder + "stream\\";
        g_clientWorld.sounds_->SetPath(folder, registry);
    }
    {
        std::string effects = textFolder + "sfx.res";
        CSoundManager* sound = g_clientWorld.sounds_;
        sound->ClearWaveSources();
        sound->AddWaveSource(effects);
    }
    {
        std::string unused = discFolder + "res\\speech.res";   // "res\speech.res" (built twice)
        std::string speech = discFolder + "res\\speech.res";
        g_clientWorld.sounds_->AddWaveSource(speech);
    }
    g_clientWorld.sounds_->SetEffectsVolume(options->audioSfx_);
    g_clientWorld.sounds_->SetSpeechVolume(options->audioSpeech_);
    g_clientWorld.sounds_->SetMusicVolume(options->audioStreaming_);
    g_clientWorld.sounds_->panDirect = options->reverseStereo_ != 0;
    return reinterpret_cast<std::uintptr_t>(discFolder.c_str());
}

void LoadDatabases() {
    const std::string archive = "database.res";
    GameOptions* options = &options::g_options.Get();
    g_skillDatabase.Load(options->resourcesPath_, archive);
    g_questDatabase.Load(options->resourcesPath_);
    g_unitDatabase.Load(options->resourcesPath_, archive);
    g_spellDatabase.Load(options->resourcesPath_, archive);
    g_itemDatabase.Load(options->resourcesPath_, archive);
    g_leverTable.Load(options->resourcesPath_);
    g_ackDatabase.Load(options->resourcesPath_);
    g_printsDatabase.Load(options->resourcesPath_);
}

// ---- The main loop ---------------------------------------------------------------------------------

constexpr float kOneTenth = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kOneTenth) == 0x3DCCCCCDu);
constexpr float kSlowest = kOneTenth;   // the client speed's limits
constexpr float kFastest = 10.0f;
constexpr std::uint32_t kBacklogLimit = 0x4000000;
constexpr int kLongestPassTicks = 5;

// The world tick's length in milliseconds and the factor from milliseconds to the camera's time (1 / 55).
std::uint32_t TickLength() { return static_cast<std::uint32_t>(worlddata::g_updatePeriod); }
float CameraSeconds(std::uint32_t milliseconds) {
    return static_cast<float>(x87::FromUnsigned(milliseconds) * 0.018181818f);
}

// Three calls of the loop take a harness hook (ei/main_loop.h, tests/verify/harness_hooks.h) when
// one is set: the frame's last camera update is held still while a scenario is frozen; the server's
// update and the client's tick are followed by the harness's "speed" hold. Otherwise the function itself.
void FrameCameraUpdate(CCamera* camera, float seconds) {
    if (auto hook = ei::mainloop::g_harnessHooks.frameCameraUpdate) hook(camera, nullptr, seconds);
    else camera->Update(seconds);
}
// Stand-ins in the hook types' convention (__fastcall, include/ei/main_loop.h): in game.exe the harness's hooks are CALL targets.
int __fastcall UpdateServer(void* server, void*) {
    static_cast<CWorldServer*>(server)->Update();
    return 1;
}
int __fastcall RunClientSegment(void* client, void*) { return static_cast<CWorldClient*>(client)->Segment() ? 1 : 0; }
ei::mainloop::HarnessHooks::WorldUpdate ServerUpdate() {
    if (auto hook = ei::mainloop::g_harnessHooks.serverUpdate) return hook;
    return &UpdateServer;
}
ei::mainloop::HarnessHooks::WorldUpdate ClientSegment() {
    if (auto hook = ei::mainloop::g_harnessHooks.clientSegment) return hook;
    return &RunClientSegment;
}

// The smooth camera's clock (render::SmoothCamera; docs/notes/renderer.md section 14): the
// client's time in ms, a tick for each client tick run plus the part of the next tick it has
// (its backlog, up to a tick). Without the option each client tick moves the camera by a tick (in
// RunWorlds) on top of the frame's real time: half of every pan, turn and zoom arrives in jumps of
// a tick 18 times a second. With it the frame moves the camera by its real time plus what this
// clock advanced since the last frame's camera update: the same time in all, spread over the frames.
std::uint32_t g_clientTicksMs = 0;     // our own state, not the game's
std::uint32_t g_cameraClientMs = 0;    // the clock at the last frame's camera update
std::uint32_t ClientClockMs() {
    const std::uint32_t tick = TickLength();
    return g_clientTicksMs + (g_clientBacklog < tick ? g_clientBacklog : tick);
}

// A change of the window's activation taken up: active again, every surface is restored (when the renderer
// was created) and the renderer runs; inactive, the renderer stops and the desktop is repainted.
void TakeActivationChange() {
    if (!g_activationChanged) return;
    if (g_active) {
        g_sceneRenderer->flags_ |= 1;
        gameapi::SetCursor(nullptr);
    } else {
        g_sceneRenderer->flags_ &= ~1u;
        gameapi::InvalidateRect(nullptr, nullptr, TRUE);
    }
    g_activationChanged = 0;
}

std::uint32_t TimeSinceLastPass() {
    if (!(g_lastTimeGuard & 1)) {
        g_lastTimeGuard |= 1;
        g_lastTime = gameapi::timeGetTime();
    }
    DWORD now;
    {
        ei::mainloop::InSite site(ei::mainloop::Site::FrameTime);
        now = gameapi::timeGetTime();
    }
    const std::uint32_t elapsed = now - g_lastTime;
    g_lastTime += elapsed;
    return elapsed;
}

// The worlds run for the time that has passed: the server a tick at a time while it has a whole
// tick, the client likewise while it has one and either server updates wait or its view is not in
// mode 2 (each client tick also moves the camera by a tick; with the smooth camera it notes the
// camera target's place instead). False when a world stops the frame.
bool RunWorlds() {
    for (;;) {
        bool settled = true;
        if (g_serverBacklog >= TickLength()) {
            settled = false;
            if (!ServerUpdate()(&g_serverWorld, nullptr)) return false;
            g_serverBacklog -= TickLength();
            net::Pump();
        }
        if (g_clientBacklog >= TickLength() && (g_clientWorld.PumpHasIncoming() || !g_clientWorld.IsLocalViewMode2())) {
            if (!ClientSegment()(&g_clientWorld, nullptr)) return false;
            const std::uint32_t tick = TickLength();
            g_clientBacklog -= tick;
            g_clientTicksMs += tick;
            if (render::SmoothCamera())
                camera::NoteTargetTick(&g_camera.Get());
            else
                g_camera->Update(CameraSeconds(tick));
            net::Pump();
            continue;
        }
        if (settled) return true;
    }
}

// The client's speed for the next pass from how far it is behind the server: `balance` is the
// number of waiting server updates less the part of a tick the client has left. Behind (negative),
// it is e^(balance / 10); up to one update ahead, 1; further ahead, 1 + (sqrt(balance) - 1) / 10
// (x87 order of operations: the goldens depend on it). Kept within 0.1 .. 10.
float SpeedForBalance(double balance, float rounded) {
    double speed;
    if (balance < 0.0f) {
        speed = x87::fexp(Step(rounded * 0.1f));
    } else if (rounded > 1.0f) {
        speed = Step(Step(std::sqrt(rounded) * 0.1f) + 1.0f) - 0.1f;
    } else {
        speed = 1.0f;   // also NaN
    }
    float result = static_cast<float>(speed);
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
    if (!(g_sceneRenderer->flags_ & 1)) {
        GuardPlayerData(0);
        return false;
    }
    GuardPlayerData(1);
    CWorldClient* client = &g_clientWorld;
    if (client->IsLocalPlayerIdle()) {
        if (IsMultiplayerClient()) g_gameFlow.RecreateClientSession();
        else g_gameFlow.CloseSessions();
    }
    if (elapsed > 0x80000000u) elapsed = 0;
    net::Pump();
    if (elapsed > TickLength() * kLongestPassTicks) elapsed = TickLength() * kLongestPassTicks;
    if (g_recordingFrames) {
        // A recorded frame lasts 1000 / the movie's frames a second, rounded up.
        const double frame = Step(1000.0f / static_cast<double>(options::g_options->movieCapturingFps_));
        elapsed = libcrt::Ftol(std::ceil(frame));
    }
    g_serverBacklog += elapsed;
    if (!client->IsLocalViewMode2()) g_clientSpeed = 1.0f;
    g_clientBacklog =
        libcrt::Ftol(Step(x87::FromUnsigned(elapsed) * g_clientSpeed) + static_cast<double>(static_cast<std::int32_t>(g_clientBacklog)));
    if (g_clientBacklog > kBacklogLimit) g_clientBacklog = 0;

    if (RunWorlds()) {
        g_gameFlow.ProcessRequest();
        const float fraction = static_cast<float>(x87::FromUnsigned(g_clientBacklog) / static_cast<double>(static_cast<std::int32_t>(TickLength())));
        if (!client->paused_) client->tickFraction_ = fraction;
        const double balance = Step(static_cast<double>(client->PumpIncomingCount()) - fraction);
        const float roundedBalance = static_cast<float>(balance);
        g_clientSpeed = SpeedForBalance(balance, roundedBalance);
        if (g_showClientSpeed) PrintClientSpeed(g_clientSpeed, roundedBalance);

        // The camera runs on the real time of the pass, a tick at a time (the smooth camera: also on
        // the client time that passed, which the client's ticks moved it by without the option).
        const std::uint32_t clientClock = ClientClockMs();
        std::uint32_t clientTime = clientClock - g_cameraClientMs;
        g_cameraClientMs = clientClock;
        if (render::SmoothCamera() && clientTime < 0x80000000u) elapsed += clientTime;   // not after a backlog reset
        CCamera* camera = &g_camera.Get();
        std::uint32_t tick = TickLength();
        while (elapsed > tick) {
            camera->Update(CameraSeconds(tick));
            tick = TickLength();
            elapsed -= tick;
        }
        FrameCameraUpdate(camera, CameraSeconds(elapsed));
        if (!client->paused_) {
            if (camera->flags_ & CCamera::kRecording)
                camera->RecordFrame(std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<double>(client->tick_) + client->tickFraction_)));
            if (camera->flags_ & CCamera::kPlaying)
                camera->PlaybackStep(static_cast<float>(static_cast<double>(client->tick_) + client->tickFraction_));
        }
    }
    g_interface.Update();
    const bool draw = static_cast<char>(g_movieMode) != 0;
    if (auto hook = ei::mainloop::g_harnessHooks.frameStep) hook(draw, true);
    else app::FrameStep(draw, true);
    return true;
}

bool MessageWaits(MSG* message, ei::mainloop::Site where) {
    ei::mainloop::InSite site(where);
    return gameapi::PeekMessageA(message, nullptr, 0, 0, PM_NOREMOVE) != 0;
}

// The loop: passes while no message waits, then the message handled. Returns the quit message's
// wParam. `interfaceInput` says whether the interface takes the input (see GameMain).
int RunMainLoop(bool interfaceInput) {
    MSG message;
    for (;;) {
        bool waits = MessageWaits(&message, ei::mainloop::Site::PeekBeforeFrame);
        while (!waits) {
            interfaceInput = RunPass();
            waits = MessageWaits(&message, ei::mainloop::Site::PeekAfterFrame);
        }
        if (!gameapi::GetMessageA(&message, nullptr, 0, 0)) return static_cast<int>(message.wParam);
        gameapi::TranslateMessage(&message);
        if (interfaceInput) DispatchInput(message.message, message.wParam, message.lParam);
        else WindowProc(g_mainWindow, message.message, message.wParam, message.lParam);
    }
}

// ---- Shutdown --------------------------------------------------------------------------------------

// Both worlds emptied (their players disconnected, their sessions and managers released): ShutDown's last step,
// and an exit function GameMain registers, so that an exit that skips ShutDown (a fatal error's) still empties
// them before the game's exit functions release the network sessions, the camera and the renderer (the original's
// world exit functions ran first). The worlds' own destructors run after those.
void EmptyWorlds() {
    g_serverWorld.Destroy();
    g_clientWorld.Destroy();
}

void ShutDown() {
    ForgetComposedTextures();
    g_interface.Shutdown();
    g_cursor.Shutdown();
    CDirectInputMouse* mouse = g_mouse;
    g_mouse = nullptr;
    if (mouse) ReleaseMouse(mouse);
    g_sceneRenderer->SetGamma(0.0f, 0.0f, 0.0f);
    g_sceneRenderer->DestroyRenderer();
    options::g_options->SaveToRegistry(g_registryRoot.c_str());
    g_clientWorld.SetCamera(nullptr);
    EmptyWorlds();
}

} // namespace

// The mouse's reference dropped (ShutDown has already taken it: null at exit).
void ReleaseMouseAtExit() {
    if (CDirectInputMouse* mouse = g_mouse)
        if (--mouse->references_ == 0) delete mouse;
}

char g_windowTitle[256] = /*0079BB24*/ {""};
HINSTANCE g_instance = /*0079BC24*/ {};
HWND g_mainWindow = /*0079BC28*/ {};
std::uint8_t g_active = /*0079BC5C*/ {};
std::uint8_t g_activationChanged = /*0079BC5D*/ {};
std::uint8_t g_interfaceHasScreen = /*00784750*/ 1;
std::uint8_t g_quitAllowed = /*00784751*/ 1;
std::uint8_t g_recordingFrames = /*0079BC54*/ {};
int g_showClientSpeed = /*0079BC60*/ {};
const char* g_loadingMovieName = /*00791EE0*/ "progres.bik";

int GameMain(HINSTANCE instance, HINSTANCE, char* commandLine, int show) {
    std::vector<std::string> messages;
    LoadSystemMessages(messages);
    // The classes the chunk archives can hold: 1 the class template, 2 the party record.
    app::TypeRegistryRegister(&g_classRegistry, 1, CreateClassTemplate);
    app::TypeRegistryRegister(&g_classRegistry, 2, CreatePartyRecord);
    ReadCommandLine(commandLine);
    RegisterWindowClass(instance);

    GameOptions* options = &options::g_options.Get();
    // NivalRegistry: Rebirth's patch names its own key instead.
    g_registryRoot = ei::rebirth::Enabled(ei::rebirth::Patch::NivalRegistry)
                         ? "Software\\Gipat.ru\\EI_Starter\\EvilIslands"
                         : "Software\\Nival Interactive\\EvilIslands";
    StoreStartFolder();
    {
        std::string settings = g_startFolder + "config\\ai.reg";
        g_logicSettings.Load(settings);
    }
    options->LoadFromRegistry(g_registryRoot.c_str());
    std::atexit(EmptyWorlds);
    g_clientWorld.Create();
    g_serverWorld.Create();
    // The field of view: pi * 2 / 7 (51.4 degrees).
    g_camera->Init(options, std::bit_cast<std::uint32_t>(options->nearClipDistance_),
                   std::bit_cast<std::uint32_t>(options->farClipDistance_),
                   std::bit_cast<std::uint32_t>(static_cast<float>(3.141592653589793 * 0.2857142857142857)));
    g_clientWorld.SetCamera(&g_camera.Get());
    if (!CheckDrives(messages)) return kExitDeclined;
    CreateMouse();

    std::string folder;
    CreateFolders(folder);
    g_instance = instance;
    g_mainWindow = gameapi::CreateWindowExA(0, MAKEINTRESOURCEA(g_windowClassAtom), g_windowTitle, WS_POPUP | WS_CLIPSIBLINGS, 0, 0,
                                            options->resolutionX_, options->resolutionY_, nullptr, nullptr, instance, nullptr);
    if (!g_mainWindow) return 0;
    gameapi::ShowWindow(g_mainWindow, show);
    gameapi::UpdateWindow(g_mainWindow);
    gameapi::SetCursor(nullptr);

    // "The interface takes the input" is carried in one byte and set after every pass. Before
    // the first pass it holds the top byte of stack memory that start-up used last for the disc
    // folder's text (with sound) or for the free non-local video memory (without; dgVoodoo's 256 MB,
    // whose top byte is not zero): a message that waits before the first pass goes to the interface
    // when that byte is not zero.
    CreateRenderer();
    const bool interfaceTakesInput = options->audio_ ? (OpenSound() >> 24) != 0 : true;

    PlayMovies("Start", 1);
    g_loadingMovieName = "Progres.bik";
    ShowLoadingMovie(0);
    CloseLoadingMovie();
    LoadDatabases();
    g_mouse->Setup(g_mainWindow, instance);
    g_clientWorld.SetUpGraphics(g_sceneRenderer->device_);
    SetUpCursors(options->fpsIndependentCursor_ != 0, g_mouse, 0);
    g_interface.Create(g_sceneRenderer->device_);
    g_mapDatabase.LoadAll();
    StartFirstMap();
    g_sceneRenderer->SetGamma(options->brightness_, options->contrast_, options->gamma_);

    const int result = RunMainLoop(interfaceTakesInput);
    ShutDown();
    return result;
}

} // namespace app
