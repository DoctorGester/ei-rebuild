// WinMain (00456440; A-E2): the application's start-up, its main loop and its shutdown.
//
// Start-up: the system messages (res\sysmsg.txt, three lines, with built-in ones as a fallback),
// the DirectX version, the command line, the window class and a second instance's hand-over, the
// folders and the options from the registry, the two worlds and the camera, the free disk space and
// the second disc, the mouse object, the save folders, the window, the renderer and what the video
// memory allows, the sound, the intro movies, the databases, the cursors, the interface, the maps
// and the first map (or the main menu).
//
// Main loop: while no message waits, the time since the last pass runs the server world in whole
// ticks and the client world in ticks stretched by the "client speed" (1 in a local game; a network
// client speeds up or slows down by its backlog of server updates), then the camera, the interface
// and a frame. A waiting message goes to the interface's input (after a frame) or to the window
// procedure (while the renderer is inactive).
//
// Every game function is called through its address, so hooks and verify wrappers on the callees
// keep running. Rebirth patches the original (NivalRegistry), so in game.exe this body runs in the
// vanilla launch only; the standalone image runs it on Rebirth too, with the flag. See
// docs/notes/app.md ("WinMain").
#include "bink_video.h"
#include "device.h"   // the old device's calls
#include "game_strings.h"
#include "app.h"
#include "app_helpers.h"
#include "camera.h"
#include "console.h"
#include "logic_settings.h"
#include "model.h"
#include "net_session.h"
#include "object_tables.h"
#include "options.h"
#include "sound.h"
#include "texture.h"
#include "ui.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/main_loop.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/static_object.h"
#include "lib/vecmath.h"
#include "originals_core.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <ddraw.h>

#include <bit>
#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

using options::GameOptions;

namespace app {
std::uint32_t __cdecl GetDxVersion();

// ---- The application's globals that the static initialisers set (app.h) ----------------------------------------

// The command line's CStrings, built by the static initialisers 00455CE0-00455DA0 in the list's order and destroyed
// by the exit functions they register with the game's atexit; the mouse object's pointer, null at the start, as
// 00455DD0 leaves it, and released by its exit function (docs/notes/standalone.md, "The static initialisers'
// objects"). The initialisers and exit functions are ported on the names and registered at their addresses.
constinit lib::StaticObject<mfc::CString> g_movieFolder = /*0079BA0C*/ {};
constinit lib::StaticObject<mfc::CString> g_commandLineMap = /*0079BA10*/ {};
constinit lib::StaticObject<mfc::CString> g_commandLineMob = /*0079BA14*/ {};
constinit lib::StaticObject<mfc::CString> g_commandLineScript = /*0079BA18*/ {};
constinit lib::StaticObject<mfc::CString> g_cameraFile = /*0079BA1C*/ {};
constinit CDirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};

mfc::CString& MovieFolder() { return *g_movieFolder; }
mfc::CString& CommandLineMap() { return *g_commandLineMap; }
mfc::CString& CommandLineMob() { return *g_commandLineMob; }
mfc::CString& CommandLineScript() { return *g_commandLineScript; }
mfc::CString& CameraFile() { return *g_cameraFile; }
CDirectInputMouse*& Mouse() { return g_mouse; }

namespace {

using AtExitFunction = void(__cdecl*)();

// The initialisers: each CString empty (".\capture\" for the movie folder), its exit function registered.
void __cdecl ConstructCameraFile() {   // 00455CE0
    g_cameraFile.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x00455D00));
}
void __cdecl ConstructCommandLineScript() {   // 00455D10
    g_commandLineScript.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x00455D30));
}
void __cdecl ConstructCommandLineMob() {   // 00455D40
    g_commandLineMob.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x00455D60));
}
void __cdecl ConstructCommandLineMap() {   // 00455D70
    g_commandLineMap.Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x00455D90));
}
void __cdecl ConstructMovieFolder() {   // 00455DA0
    g_movieFolder.Construct(".\\capture\\");
    GameAtExit(ei::Code<AtExitFunction>(0x00455DC0));
}

// The exit functions.
void __cdecl DestroyCameraFile() { g_cameraFile.Destroy(); }                 // 00455D00
void __cdecl DestroyCommandLineScript() { g_commandLineScript.Destroy(); }   // 00455D30
void __cdecl DestroyCommandLineMob() { g_commandLineMob.Destroy(); }         // 00455D60
void __cdecl DestroyCommandLineMap() { g_commandLineMap.Destroy(); }         // 00455D90
void __cdecl DestroyMovieFolder() { g_movieFolder.Destroy(); }               // 00455DC0
// 00455DF0: the mouse's reference dropped (ShutDown has already taken it: null at exit).
void __cdecl ReleaseMouseAtExit() {
    if (CDirectInputMouse* mouse = g_mouse)
        if (--mouse->references_ == 0) mouse->DeletingDestruct(1);
}

} // namespace

EI_REPLACE(0x00455CE0, ConstructCameraFile);
EI_REPLACE(0x00455D10, ConstructCommandLineScript);
EI_REPLACE(0x00455D40, ConstructCommandLineMob);
EI_REPLACE(0x00455D70, ConstructCommandLineMap);
EI_REPLACE(0x00455DA0, ConstructMovieFolder);
EI_REPLACE(0x00455D00, DestroyCameraFile);
EI_REPLACE(0x00455D30, DestroyCommandLineScript);
EI_REPLACE(0x00455D60, DestroyCommandLineMob);
EI_REPLACE(0x00455D90, DestroyCommandLineMap);
EI_REPLACE(0x00455DC0, DestroyMovieFolder);
EI_REPLACE(0x00455DF0, ReleaseMouseAtExit);

namespace {

// Constants of the exe, read when they are used (a literal could be folded at another precision).
// A step of a float expression kept apart from the next: the value passes through a double, which
// holds any result at the precisions the game runs at (53 bits before Direct3D starts, 24 after).
double Step(double value) {
    volatile double kept = value;
    return kept;
}

// ---- The application's globals ---------------------------------------------------------------------

// The window: its class atom, class name and title (0x100 bytes each, from the string table), the
// instance and the window.
std::uint16_t& WindowClassAtom() { return ei::GameData<std::uint16_t>(0x0079BA20); }
char* WindowClassName() { return &ei::GameData<char>(0x0079BA24); }
char* WindowTitle() { return &ei::GameData<char>(0x0079BB24); }
HINSTANCE& Instance() { return ei::GameData<HINSTANCE>(0x0079BC24); }
HWND& MainWindow() { return ei::GameData<HWND>(0x0079BC28); }
constexpr int kTextSize = 0x100;
constexpr UINT kTitleString = 0x65;
constexpr UINT kClassNameString = 0x66;
constexpr int kIcon = 0x69;

// The registry key of the options and the folders: where the game was started, the disc's, the
// text resources' and the data folder (the last three are set from the options).
mfc::CString& RegistryRoot() { return TheRegistryRoot(); }
mfc::CString& StartFolder() { return TheStartFolder(); }
mfc::CString& DiscFolder() { return TheDiscFolder(); }
mfc::CString& TextFolder() { return TheTextFolder(); }

// The window's activation as the window procedure left it (app.cpp).
std::uint8_t& Active() { return ei::GameData<std::uint8_t>(0x0079BC5C); }
std::uint8_t& ActivationChanged() { return ei::GameData<std::uint8_t>(0x0079BC5D); }

// The loop's clock: the time of the last pass (a function-local static of the original: guard bit
// 0 of 0079B9FC), the time not yet run by the server and by the client (milliseconds), and the
// client's speed.
std::uint8_t& LastTimeGuard() { return ei::GameData<std::uint8_t>(0x0079B9FC); }
DWORD& LastTime() { return ei::GameData<DWORD>(0x0079B9F8); }
std::uint32_t& ServerBacklog() { return ei::GameData<std::uint32_t>(0x0079BC64); }
std::uint32_t& ClientBacklog() { return ei::GameData<std::uint32_t>(0x0079BC68); }
float& ClientSpeed() { return ei::GameData<float>(0x00784758); }
// The console's switch that prints the client speed every frame.
int& ShowClientSpeed() { return ei::GameData<int>(0x0079BC60); }
// A movie of the frames is being written (app_frame.cpp): the loop then runs a fixed time a frame.
std::uint8_t& RecordingFrames() { return ei::GameData<std::uint8_t>(0x0079BC54); }

// What the video memory allows (bytes read elsewhere): little texture memory, little memory besides
// the textures, little non-local memory; and whether a device was named on the command line.
std::uint8_t& LittleOtherMemory() { return ei::GameData<std::uint8_t>(0x00784753); }
std::uint8_t& LittleTextureMemory() { return ei::GameData<std::uint8_t>(0x00784754); }
std::uint8_t& LittleNonLocalMemory() { return ei::GameData<std::uint8_t>(0x00784755); }
std::uint8_t& DeviceNamed() { return ei::GameData<std::uint8_t>(0x00784756); }
constexpr DWORD kLittleMemory = 0x800000;

// The loading screen's movie file.
const char*& LoadingMovieName() { return ei::GameData<const char*>(0x00791EE0); }

// The objects.
void* ClassRegistry() { return &ei::GameData<char>(0x0079B270); }
void* LogicSettingsObject() { return &ei::GameData<char>(0x007C53F8); }
void* QuestDatabaseObject() { return &ei::GameData<char>(0x007C87A8); }
void* SpellTablesObject() { return &ei::GameData<char>(0x007C8510); }
void* ItemDatabaseObject() { return &ei::GameData<char>(0x007C86D8); }
void* LeverTableObject() { return &ei::GameData<char>(0x0079C1A0); }
void* AckDatabaseObject() { return &ei::GameData<char>(0x007C8A60); }
void* PrintsDatabaseObject() { return &ei::GameData<char>(0x007C89C8); }
UnitDatabase* UnitDatabaseObject() { return &ei::GameData<UnitDatabase>(0x007AF358); }
SkillDatabase* SkillDatabaseObject() { return &ei::GameData<SkillDatabase>(0x007AF268); }

// The game's log stream (the old iostream).
console::GameOStream& SpeedLog() { return ei::GameData<console::GameOStream>(0x007C56B0); }

// ---- The callees -----------------------------------------------------------------------------------

// app::TypeRegistryRegister (00458570): a class the chunk archives can hold, by id and factory. The
// two factories: 00457850 (class 1) and 00457740 (class 2, the party record).
using ClassFactory = void* (__cdecl*)();
ClassFactory ClassTemplateFactory() { return ei::Code<ClassFactory>(0x00457850); }
ClassFactory PartyRecordFactory() { return ei::Code<ClassFactory>(0x00457740); }
// 0045B210 / 0045C560 (RET 4): the options read from and written to the registry key.
__declspec(naked) void __fastcall OptionsLoadAt(GameOptions*, void*, const char* /*key*/) EI_ORIGINAL(0x0045B210)
__declspec(naked) void __fastcall OptionsSaveAt(GameOptions*, void*, const char* /*key*/) EI_ORIGINAL(0x0045C560)
// The camera: 0040D0A0 (RET 0x10) Init, 0040EB60 / 0040EFB0 (RET 4) a frame written to / read from the
// camera file at a time in ticks. 0040FB30 (RET 4, CCamera::Update) as the frame calls it through a pointer.
void __fastcall CameraUpdateOf(CCamera* camera, void*, float seconds) { camera->Update(seconds); }
// The renderer (007AF060): 00500B70 its creation (slot 2 of its vtable, RET 8), 00504A60 (RET 0xC)
// the display's brightness, contrast and gamma.
// The sound manager: 006B6A30 (RET 0x14) opened, 006BAE20 (RET 8) the music folder and its
// registry file, 006B7060 / 006B7AB0 (RET 4) the wave archives, the three volumes (RET 4).
__declspec(naked) int __fastcall SoundSetPathAt(CSoundManager*, void*, const char* /*folder*/, const char* /*registry*/) EI_ORIGINAL(0x006BAE20)
// The databases read from the resources folder (RET 4; the others are in originals_world.h).
// 00463390 is patched by Rebirth.
__declspec(naked) int __fastcall QuestDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006AE3A0)
__declspec(naked) int __fastcall LeverTableLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x00463390)
__declspec(naked) int __fastcall AckDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006D7620)
__declspec(naked) int __fastcall PrintsDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006D0630)
// The interface (007C7D50): 005F4990 (RET 4) created on the device, 005F57C0 its update, 005F4F90
// shut down.
__declspec(naked) void __fastcall InterfaceCreateAt(CInterface3D*, void*, CTextureDevice*) EI_ORIGINAL(0x005F4990)
__declspec(naked) void __fastcall InterfaceShutdownAt(CInterface3D*) EI_ORIGINAL(0x005F4F90)
// The game flow (007AFE20): 00581660 both sessions closed.

// ---- Start-up --------------------------------------------------------------------------------------

constexpr int kExitDeclined = 0xDEAD;   // WinMain's result when the player gives up at a warning
constexpr UINT kWarningBox = 0x31;      // MB_OKCANCEL | MB_ICONEXCLAMATION
constexpr UINT kCriticalBox = 0x30;     // MB_ICONEXCLAMATION
// The system messages by line.
enum MessageLine { kNeedsDirectX7, kLittleDiskSpace, kInsertDisc };
constexpr int kMessageLines = 3;

void AppendDefaultMessages(console::CStringArray& messages) {
    messages.SetAtGrow(messages.size, "This Game requires DX7 in order to run!");
    messages.SetAtGrow(messages.size, "Not enough free disk space - play on your own risk!");
    messages.SetAtGrow(messages.size, "Please, insert EI disc 2 in the CD-ROM and press OK");
}

// The lines of .\res\sysmsg.txt, or the built-in three when the file is missing or shorter.
void LoadSystemMessages(console::CStringArray& messages) {
    gamecrt::CrtFile* file = libcrt::Fopen(".\\res\\sysmsg.txt", "rt");
    if (!file) {
        AppendDefaultMessages(messages);
        return;
    }
    const std::size_t length = static_cast<std::size_t>(libcrt::Filelength(gamecrt::fileno(file)));
    char* text = static_cast<char*>(gamecrt::operator_new(length));
    std::memset(text, 0, length);
    libcrt::Fread(text, 1, length, file);
    libcrt::Fclose(file);
    text::Split(text, &messages, '\n');
    if (messages.size < kMessageLines) {
        messages.SetSize(0, -1);
        AppendDefaultMessages(messages);
    }
    gamecrt::operator_delete(text);
}

// An argument's text after its first `prefixLength` characters.
mfc::CString Tail(const mfc::CString& argument, int prefixLength) { return argument.Right(argument.GetLength() - prefixLength); }

// The command line: "-moviepath_<folder>" (the client's flag 0x20 off), "-nocam" (flag 8 off; only
// while flag 0x10 is off), a ".cam", ".scr", ".mob" or ".mpr" file, "-benchmark_<frames>" and
// "-devicetarget_<driver>".
void ReadCommandLine(const char* commandLine) {
    console::CStringArray arguments;
    text::Split(commandLine, &arguments, ' ');
    CameraFile() = ".\\camera.cam";
    CommandLineScript().Empty();
    CommandLineMob().Empty();
    CommandLineMap().Empty();
    CWorldClient* client = TheClientWorld();
    GameOptions* options = options::TheOptions();
    for (int i = 0; i < arguments.size; ++i) {
        if (arguments[i].Find("-moviepath_") == 0) {
            client->flags_ &= ~0x20u;
            {
                mfc::CString folder = Tail(arguments[i], 11);
                MovieFolder() = folder;
            }
            const char* folder = MovieFolder();
            if (folder[MovieFolder().GetLength() - 1] != '\\') MovieFolder() += '\\';
            continue;
        }
        if (!(client->flags_ & 0x10)) {
            if (gamecrt::mbsicmp(static_cast<const char*>(arguments[i]),
                                "-nocam") == 0) {
                client->flags_ &= ~8u;
                continue;
            }
            if (!(client->flags_ & 0x10) && arguments[i].Find(".cam") != -1) {
                CameraFile() = arguments[i];
                continue;
            }
        }
        if (arguments[i].Find(".scr") != -1) {
            CommandLineScript() = arguments[i];
        } else if (arguments[i].Find(".mob") != -1) {
            CommandLineMob() = arguments[i];
        } else if (arguments[i].Find(".mpr") != -1) {
            CommandLineMap() = arguments[i];
        } else if (arguments[i].Find("-benchmark_") == 0) {
            mfc::CString frames = Tail(arguments[i], 11);
            options->numberOfFrames_ = libcrt::Atoi(frames);
            options->createOutputInfo_ = 1;
        } else if (arguments[i].Find("-devicetarget_") == 0) {
            mfc::CString driver = Tail(arguments[i], 14);
            options->driverIndex_ = libcrt::Atoi(driver);
        }
    }
}

// The window class registered; a game that runs already is brought to the front instead and this
// one exits.
void RegisterWindowClass(HINSTANCE instance) {
    gameapi::LoadStringA(instance, kTitleString, WindowTitle(), kTextSize);
    gameapi::LoadStringA(instance, kClassNameString, WindowClassName(), kTextSize);
    WNDCLASSEXA windowClass;
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = ei::Code<WNDPROC>(0x00457D40);
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance;
    windowClass.hIcon = gameapi::LoadIconA(instance, MAKEINTRESOURCEA(kIcon));
    windowClass.hIconSm = gameapi::LoadIconA(instance, MAKEINTRESOURCEA(kIcon));
    windowClass.hCursor = nullptr;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = WindowClassName();
    WindowClassAtom() = gameapi::RegisterClassExA(&windowClass);

    HWND other = gameapi::FindWindowA(MAKEINTRESOURCEA(WindowClassAtom()), WindowTitle());
    if (!other) return;
    const DWORD foregroundThread = gameapi::GetWindowThreadProcessId(gameapi::GetForegroundWindow(), nullptr);
    const DWORD otherThread = gameapi::GetWindowThreadProcessId(other, nullptr);
    if (otherThread != foregroundThread || gameapi::IsIconic(other)) {
        HWND popup = gameapi::GetLastActivePopup(other);
        if (gameapi::IsIconic(popup)) gameapi::ShowWindow(popup, SW_RESTORE);
        gameapi::BringWindowToTop(popup);
        gameapi::SetForegroundWindow(popup);
    }
    libcrt::Exit(-1);
}

// The buffers _splitpath fills, as the original sizes them.
struct PathParts {
    char drive[0x80];
    char directory[0x400];
    char name[0x80];
    char extension[0x80];
};

// "<drive>\." of `path`: the root the volume calls take.
void DriveRoot(const char* path, PathParts& parts) {
    libcrt::Splitpath(path, parts.drive, parts.directory, parts.name, parts.extension);
    std::memcpy(parts.drive + std::strlen(parts.drive), "\\.", 3);
}

// The free bytes on the drive of `folder` through GetDiskFreeSpaceExA where kernel32 has it, else
// from the cluster counts. False when the call fails.
bool FreeDiskSpace(const char* folder, double* freeBytes) {
    PathParts parts;
    DriveRoot(folder, parts);
    using FreeSpaceEx = BOOL(__stdcall*)(const char*, ULARGE_INTEGER*, ULARGE_INTEGER*, ULARGE_INTEGER*);
    auto* extended = reinterpret_cast<FreeSpaceEx>(
        gameapi::GetProcAddress(gameapi::GetModuleHandleA("kernel32.dll"), "GetDiskFreeSpaceExA"));   // kernel32.dll
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
bool CheckDrives(const console::CStringArray& messages) {
    mfc::CString discFolder(DiscFolder());
    mfc::CString dataFolder(TheDataFolder());
    double freeBytes;
    if (FreeDiskSpace(dataFolder, &freeBytes) && freeBytes != 0.0 /*0073C340*/ && freeBytes < 314572800.0 /*0073C358*/) {
        if (gameapi::MessageBoxA(nullptr, messages[kLittleDiskSpace], "Warning", kWarningBox) == IDCANCEL) return false;
    }
    if (GameMbscmp(dataFolder, discFolder) != 0) {
        char volume[0x400] = {};
        PathParts parts;
        DriveRoot(discFolder, parts);
        gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        while (GameMbscmp(options::TheOptions()->cdLabels_[1], volume) != 0) {
            if (gameapi::MessageBoxA(nullptr, messages[kInsertDisc], "Warning", kWarningBox) == IDCANCEL) return false;
            gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        }
    }
    return true;
}

// One reference of the mouse object dropped: deleted at the last (slot 0 of its vtable).
void ReleaseMouse(CDirectInputMouse* mouse) {
    if (--mouse->references_ == 0) mouse->DeletingDestruct(1);
}

// The mouse object made (not set up yet) and kept with one reference.
void CreateMouse() {
    auto* mouse = static_cast<CDirectInputMouse*>(gamecrt::operator_new(sizeof(CDirectInputMouse)));
    if (mouse) {
        mouse->references_ = 0;
        mouse->input_ = nullptr;
        mouse->device_ = nullptr;
        mouse->vtable_ = &ei::GameData<char>(0x0073C350);
    }
    CDirectInputMouse* old = Mouse();
    Mouse() = mouse;
    if (mouse) ++mouse->references_;
    if (old) ReleaseCountedOriginal(old);
}

// The folders the game writes to.
void CreateFolders(mfc::CString& path) {
    path.Format("%ssaves\\", static_cast<const char*>(TheDataFolder()));
    gameapi::CreateDirectoryA(path, nullptr);
    path.Format("%smp\\", static_cast<const char*>(TheDataFolder()));
    gameapi::CreateDirectoryA(path, nullptr);
    path.Format("%stextures\\", static_cast<const char*>(TheDataFolder()));
    gameapi::CreateDirectoryA(path, nullptr);
}

// The renderer created on the window, then what the video memory allows. Returns the free
// non-local video memory.
DWORD CreateRenderer() {
    GameOptions* options = options::TheOptions();
    CCamera* camera = TheCamera();
    camera->localPosition_.x = options->cameraDefaultXDeploy_;
    camera->localPosition_.y = options->cameraDefaultYDeploy_;
    camera->localPosition_.z = options->cameraDefaultHeight_;
    SceneRenderer()->CreateRendererImpl(MainWindow(), options);

    DDSCAPS2 caps = {};
    DWORD total = 0, free = 0;
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    device::GetAvailableVidMem(static_cast<IDirectDraw7*>(TheRenderDevice()->ddraw_), &caps, &total, &free);
    DWORD other = free;
    caps.dwCaps = DDSCAPS_TEXTURE;
    device::GetAvailableVidMem(static_cast<IDirectDraw7*>(TheRenderDevice()->ddraw_), &caps, &total, &free);
    LittleTextureMemory() = free <= kLittleMemory;
    if (free < other) other -= free;
    LittleOtherMemory() = other <= kLittleMemory;
    caps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_NONLOCALVIDMEM;
    device::GetAvailableVidMem(static_cast<IDirectDraw7*>(TheRenderDevice()->ddraw_), &caps, &total, &free);
    LittleNonLocalMemory() = free <= kLittleMemory;
    DeviceNamed() = options->driverIndex_ != 0;
    TheSceneRenderer()->SetGamma(options->brightness_, options->contrast_, options->gamma_);
    return free;
}

// The sound opened (44.1 kHz, 16 bits) with its music folder, wave archives and volumes. Returns
// the disc folder's text pointer (see the caller).
std::uintptr_t OpenSound() {
    GameOptions* options = options::TheOptions();
    mfc::CString textFolder(TextFolder());
    mfc::CString dataFolder(TheDataFolder());
    mfc::CString discFolder(DiscFolder());
    TheSoundManager()->Open(MainWindow(), 1, -1, 44100, 16);
    {
        mfc::CString registry = textFolder + "streamsn.reg";
        mfc::CString folder = dataFolder + "stream\\";
        SoundSetPathAt(TheSoundManager(), nullptr, folder, registry);
    }
    {
        mfc::CString effects = textFolder + "sfx.res";
        CSoundManager* sound = TheSoundManager();
        sound->ClearWaveSources();
        sound->AddWaveSource(effects);
    }
    {
        mfc::CString unused = discFolder + "res\\speech.res";   // "res\speech.res" (built twice)
        mfc::CString speech = discFolder + "res\\speech.res";
        TheSoundManager()->AddWaveSource(speech);
    }
    TheSoundManager()->SetEffectsVolume(options->audioSfx_);
    TheSoundManager()->SetSpeechVolume(options->audioSpeech_);
    TheSoundManager()->SetMusicVolume(options->audioStreaming_);
    TheSoundManager()->panDirect = options->reverseStereo_ != 0;
    return reinterpret_cast<std::uintptr_t>(static_cast<const char*>(discFolder));
}

void LoadDatabases() {
    const char* archive = "database.res";
    GameOptions* options = options::TheOptions();
    SkillDatabaseLoadOriginal(SkillDatabaseObject(), nullptr, options->resourcesPath_, archive);
    QuestDatabaseLoadAt(QuestDatabaseObject(), nullptr, options->resourcesPath_);
    UnitDatabaseLoadOriginal(UnitDatabaseObject(), nullptr, options->resourcesPath_, archive);
    SpellTablesLoadOriginal(SpellTablesObject(), nullptr, options->resourcesPath_, archive);
    ItemDatabaseLoadOriginal(ItemDatabaseObject(), nullptr, options->resourcesPath_, archive);
    LeverTableLoadAt(LeverTableObject(), nullptr, options->resourcesPath_);
    AckDatabaseLoadAt(AckDatabaseObject(), nullptr, options->resourcesPath_);
    PrintsDatabaseLoadAt(PrintsDatabaseObject(), nullptr, options->resourcesPath_);
}

// ---- The main loop ---------------------------------------------------------------------------------

constexpr float kOneTenth = std::bit_cast<float>(0x3DCCCCCDu);
constexpr float kSlowest = kOneTenth;   // the client speed's limits
constexpr float kFastest = 10.0f;
constexpr std::uint32_t kBacklogLimit = 0x4000000;
constexpr int kLongestPassTicks = 5;

// The world tick's length in milliseconds (0078FF34) and the factor from milliseconds to the
// camera's time (0073C348: 1 / 55).
std::uint32_t TickLength() { return ei::GameData<std::uint32_t>(0x0078FF34); }
float CameraSeconds(std::uint32_t milliseconds) {
    return static_cast<float>(x87::FromUnsigned(milliseconds) * 0.018181818f /*0073C348*/);
}

// Three calls of the loop take a harness hook (ei/main_loop.h, tests/verify/harness_hooks.h) when
// one is set: the frame's last camera update (00457595 -> 0040FB30) is held still while a scenario
// is frozen; the server's update (00457314 -> 0058DFF0) and the client's tick (00457368 -> 005641F0)
// are followed by the harness's "speed" hold. Otherwise the function itself.
using CameraUpdate = void(__fastcall*)(CCamera*, void*, float);
CameraUpdate FrameCameraUpdate() {
    if (auto hook = ei::mainloop::g_harnessHooks.frameCameraUpdate) return reinterpret_cast<CameraUpdate>(hook);
    return &CameraUpdateOf;
}
using WorldUpdate = ei::mainloop::HarnessHooks::WorldUpdate;
WorldUpdate ServerUpdate() {
    if (auto hook = ei::mainloop::g_harnessHooks.serverUpdate) return hook;
    return reinterpret_cast<WorldUpdate>(&ServerUpdateOriginal);
}
WorldUpdate ClientSegment() {
    if (auto hook = ei::mainloop::g_harnessHooks.clientSegment) return hook;
    return reinterpret_cast<WorldUpdate>(&ClientSegmentOriginal);
}

// A change of the window's activation taken up: active again, every surface is restored (when the
// renderer was created) and the renderer runs; inactive, the renderer stops and the desktop is
// repainted.
void TakeActivationChange() {
    if (!ActivationChanged()) return;
    if (Active()) {
        if (TheSceneRenderer()->flags_ & 2) {
            CTextureDevice* device = TheRenderDevice();
            gameapi::EnterCriticalSection(TheDeviceLock());
            device->lastLoadResult_ = device::RestoreAllSurfaces(static_cast<IDirectDraw7*>(device->ddraw_));
            gameapi::LeaveCriticalSection(TheDeviceLock());
        }
        SceneRenderer()->flags_ |= 1;
        gameapi::SetCursor(nullptr);
    } else {
        SceneRenderer()->flags_ &= ~1u;
        gameapi::InvalidateRect(nullptr, nullptr, TRUE);
    }
    ActivationChanged() = 0;
}

// The milliseconds since the last pass.
std::uint32_t TimeSinceLastPass() {
    if (!(LastTimeGuard() & 1)) {
        LastTimeGuard() |= 1;
        LastTime() = gameapi::timeGetTime();
    }
    DWORD now;
    {
        ei::mainloop::InSite site(ei::mainloop::Site::FrameTime);
        now = gameapi::timeGetTime();
    }
    const std::uint32_t elapsed = now - LastTime();
    LastTime() += elapsed;
    return elapsed;
}

// The worlds run for the time that has passed: the server a tick at a time while it has a whole
// tick, the client likewise while it has one and either server updates wait or its view is not in
// mode 2 (each client tick also moves the camera by a tick). False when a world stops the frame.
bool RunWorlds() {
    for (;;) {
        bool settled = true;
        if (ServerBacklog() >= TickLength()) {
            settled = false;
            if (!ServerUpdate()(TheServerWorld(), nullptr)) return false;
            ServerBacklog() -= TickLength();
            net::Pump();
        }
        if (ClientBacklog() >= TickLength() && (TheClientWorld()->PumpHasIncoming() || !TheClientWorld()->IsLocalViewMode2())) {
            if (!ClientSegment()(TheClientWorld(), nullptr)) return false;
            const std::uint32_t tick = TickLength();
            ClientBacklog() -= tick;
            TheCamera()->Update(CameraSeconds(tick));
            net::Pump();
            continue;
        }
        if (settled) return true;
    }
}

// The client's speed for the next pass from how far it is behind the server: `balance` is the
// number of waiting server updates less the part of a tick the client has left. Behind (negative),
// it is e^(balance / 10); up to one update ahead, 1; further ahead, 1 + (sqrt(balance) - 1) / 10
// in the original's order of operations. Kept within 0.1 .. 10.
float SpeedForBalance(double balance, float rounded) {
    double speed;
    if (balance < 0.0f /*0073B858*/) {
        speed = x87::fexp(Step(rounded * 0.1f /*0073B850*/));
    } else if (rounded > 1.0f /*0073B84C*/) {
        speed = Step(Step(std::sqrt(rounded) * 0.1f /*0073B850*/) + 1.0f /*0073B84C*/) - 0.1f /*0073B850*/;
    } else {
        speed = 1.0f /*0073B84C*/;   // also NaN
    }
    float result = static_cast<float>(speed);
    result = result >= kSlowest ? result : kSlowest;
    if (kFastest < result) result = kFastest;
    return result;
}

void PrintClientSpeed(float speed, float balance) {
    console::GameOStream& log = SpeedLog();
    log << "Client speed = ";
    log.PutFloat(speed) << " balance = ";
    log.PutFloat(balance) << '\n';
    log.Flush();
}

// One pass of the loop while no message waits. Returns whether the interface takes the input
// messages from now on: not while the renderer is inactive.
bool RunPass() {
    TakeActivationChange();
    std::uint32_t elapsed = TimeSinceLastPass();
    void* glue = GameGlueObject();
    if (!(SceneRenderer()->flags_ & 1)) {
        GuardPlayerData(glue, nullptr, 0);
        return false;
    }
    GuardPlayerData(glue, nullptr, 1);
    CWorldClient* client = TheClientWorld();
    if (client->IsLocalPlayerIdle()) {
        if (IsMultiplayerClient()) TheGameFlow()->RecreateClientSession();
        else TheGameFlow()->CloseSessions();
    }
    if (elapsed > 0x80000000u) elapsed = 0;
    net::Pump();
    if (elapsed > TickLength() * kLongestPassTicks) elapsed = TickLength() * kLongestPassTicks;
    if (RecordingFrames()) {
        // A recorded frame lasts 1000 / the movie's frames a second, rounded up.
        const double frame = Step(1000.0f /*0073C34C*/ / static_cast<double>(options::TheOptions()->movieCapturingFps_));
        elapsed = gamecrt::Ftol(libcrt::Ceil(frame));
    }
    ServerBacklog() += elapsed;
    if (!client->IsLocalViewMode2()) ClientSpeed() = 1.0f;
    ClientBacklog() =
        gamecrt::Ftol(Step(x87::FromUnsigned(elapsed) * ClientSpeed()) + static_cast<double>(static_cast<std::int32_t>(ClientBacklog())));
    if (ClientBacklog() > kBacklogLimit) ClientBacklog() = 0;

    if (RunWorlds()) {
        FlowProcessRequestOriginal(TheGameFlow());
        const float fraction = static_cast<float>(x87::FromUnsigned(ClientBacklog()) / static_cast<double>(static_cast<std::int32_t>(TickLength())));
        if (!client->paused_) client->tickFraction_ = fraction;
        const double balance = Step(static_cast<double>(client->PumpIncomingCount()) - fraction);
        const float roundedBalance = static_cast<float>(balance);
        ClientSpeed() = SpeedForBalance(balance, roundedBalance);
        if (ShowClientSpeed()) PrintClientSpeed(ClientSpeed(), roundedBalance);

        // The camera runs on the real time of the pass, a tick at a time.
        CCamera* camera = TheCamera();
        std::uint32_t tick = TickLength();
        while (elapsed > tick) {
            camera->Update(CameraSeconds(tick));
            tick = TickLength();
            elapsed -= tick;
        }
        FrameCameraUpdate()(camera, nullptr, CameraSeconds(elapsed));
        if (!client->paused_) {
            if (camera->flags_ & CCamera::kRecording)
                camera->RecordFrame(std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<double>(client->tick_) + client->tickFraction_)));
            if (camera->flags_ & CCamera::kPlaying)
                camera->PlaybackStep(static_cast<float>(static_cast<double>(client->tick_) + client->tickFraction_));
        }
    }
    TheInterface()->Update();
    const bool draw = static_cast<char>(MovieModeFlag()) != 0;
    if (auto hook = ei::mainloop::g_harnessHooks.frameStep) hook(draw, true);
    else app::FrameStep(draw, true);
    return true;
}

bool MessageWaits(MSG* message, ei::mainloop::Site where) {
    ei::mainloop::InSite site(where);
    return gameapi::PeekMessageA(message, nullptr, 0, 0, PM_NOREMOVE) != 0;
}

// The loop: passes while no message waits, then the message handled. Returns the quit message's
// wParam. `interfaceInput` is the original's BL (see GameMain).
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
        else WindowProc(MainWindow(), message.message, message.wParam, message.lParam);
    }
}

// ---- Shutdown --------------------------------------------------------------------------------------

void ShutDown(gamecrt::NewHandler previousHandler) {
    DeleteComposedTextures();
    InterfaceShutdownAt(TheInterface());
    TheCursor()->Shutdown();
    CDirectInputMouse* mouse = Mouse();
    Mouse() = nullptr;
    if (mouse) ReleaseMouse(mouse);
    TheSceneRenderer()->SetGamma(0.0f, 0.0f, 0.0f);
    SceneRenderer()->DestroyRenderer();
    OptionsSaveAt(options::TheOptions(), nullptr, RegistryRoot());
    TheClientWorld()->SetCamera(nullptr);
    TheServerWorld()->Teardown();
    TheClientWorld()->Teardown();
    libcrt::SetNewHandler(previousHandler);
}

} // namespace

int __stdcall GameMain(HINSTANCE instance, HINSTANCE, char* commandLine, int show) {
    console::CStringArray messages;
    LoadSystemMessages(messages);
    if (GetDxVersion() < 0x700) {
        gameapi::MessageBoxA(nullptr, messages[kNeedsDirectX7], "CRITICAL ERROR", kCriticalBox);
        return kExitDeclined;
    }
    app::TypeRegistryRegister(ClassRegistry(), nullptr, 1, ClassTemplateFactory());
    app::TypeRegistryRegister(ClassRegistry(), nullptr, 2, PartyRecordFactory());
    ReadCommandLine(commandLine);
    RegisterWindowClass(instance);

    GameOptions* options = options::TheOptions();
    // NivalRegistry: addon's PUSH at 004568E9 names its own key instead.
    RegistryRoot() = ei::rebirth::Enabled(ei::rebirth::Patch::NivalRegistry)
                         ? "Software\\Gipat.ru\\EI_Starter\\EvilIslands"
                         : "Software\\Nival Interactive\\EvilIslands";
    StoreStartFolder();
    {
        mfc::CString settings = StartFolder() + "config\\ai.reg";
        static_cast<LogicSettings*>(LogicSettingsObject())->Load(settings);
    }
    OptionsLoadAt(options, nullptr, RegistryRoot());
    const gamecrt::NewHandler previousHandler =
        libcrt::SetNewHandler(ei::Code<gamecrt::NewHandler>(0x00455A30));
    TheClientWorld()->Setup();
    TheServerWorld()->Setup();
    // The field of view: pi * 2 / 7 (51.4 degrees).
    TheCamera()->Init(reinterpret_cast<CameraSettings*>(options), std::bit_cast<std::uint32_t>(options->nearClipDistance_),
                   std::bit_cast<std::uint32_t>(options->farClipDistance_),
                   std::bit_cast<std::uint32_t>(static_cast<float>(3.141592653589793 /*0073C308*/ * 0.2857142857142857 /*0073B9D8*/)));
    TheClientWorld()->SetCamera(TheCamera());
    if (!CheckDrives(messages)) return kExitDeclined;
    CreateMouse();

    mfc::CString folder;
    CreateFolders(folder);
    Instance() = instance;
    MainWindow() = gameapi::CreateWindowExA(0, MAKEINTRESOURCEA(WindowClassAtom()), WindowTitle(), WS_POPUP | WS_CLIPSIBLINGS, 0, 0,
                                            options->resolutionX_, options->resolutionY_, nullptr, nullptr, instance, nullptr);
    if (!MainWindow()) return 0;
    gameapi::ShowWindow(MainWindow(), show);
    gameapi::UpdateWindow(MainWindow());
    gameapi::SetCursor(nullptr);

    // The original keeps "the interface takes the input" in BL and sets it after every pass. Before
    // the first pass it holds the top byte of a stack slot that start-up used last for the disc
    // folder's text (with sound) or for the free non-local video memory (without): a message that
    // waits before the first pass goes to the interface when that byte is not zero.
    std::uintptr_t reusedSlot = CreateRenderer();
    if (options->audio_) reusedSlot = OpenSound();

    PlayMoviesOriginal("Start", 1, 1, 1, 1);
    LoadingMovieName() = "Progres.bik";
    ShowLoadingMovie(0);
    CloseLoadingMovie();
    LoadDatabases();
    Mouse()->Setup(MainWindow(), instance);
    TheClientWorld()->SetUpGraphics(TheRenderDevice());
    // The last argument's upper three bytes are whatever the call before left in ECX in the
    // original; only the low byte is a value (the shapes' flag: no device named).
    SetUpCursors(options->fpsIndependentCursor_ != 0, static_cast<int>(reinterpret_cast<std::intptr_t>(Mouse())), 0,
                 DeviceNamed() == 0);
    InterfaceCreateAt(TheInterface(), nullptr, TheRenderDevice());
    TheMapDatabase()->LoadAll();
    StartFirstMap();
    TheSceneRenderer()->SetGamma(options->brightness_, options->contrast_, options->gamma_);

    const int result = RunMainLoop((reusedSlot >> 24) != 0);
    ShutDown(previousHandler);
    return result;
}
EI_REPLACE(0x00456440, GameMain);

} // namespace app
