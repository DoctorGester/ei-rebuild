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
// keep running. Rebirth patches the original (NivalRegistry), so this body runs in the vanilla
// launch only. See docs/notes/app.md ("WinMain").
#include "app.h"
#include "app_helpers.h"
#include "camera.h"
#include "console.h"
#include "model.h"
#include "options.h"
#include "sound.h"
#include "texture.h"
#include "ui.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/main_loop.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <ddraw.h>

#include <bit>
#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

using console::GameText;
using options::GameOptions;

namespace app {
namespace {

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}
// Constants of the exe, read when they are used (a literal could be folded at another precision).
float ExeFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double ExeDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
// A step of a float expression kept apart from the next: the value passes through a double, which
// holds any result at the precisions the game runs at (53 bits before Direct3D starts, 24 after).
double Step(double value) {
    volatile double kept = value;
    return kept;
}

// ---- The application's globals ---------------------------------------------------------------------

// What the command line gave: the folder of the movie frames ("-moviepath_"), a map (".mpr"), a mob
// file (".mob"), a script (".scr") and the camera file (".cam"; ".\camera.cam" without one).
mfc::CString& MovieFolder() { return Global<mfc::CString>(0x0079BA0C); }
mfc::CString& CommandLineMap() { return Global<mfc::CString>(0x0079BA10); }
mfc::CString& CommandLineMob() { return Global<mfc::CString>(0x0079BA14); }
mfc::CString& CommandLineScript() { return Global<mfc::CString>(0x0079BA18); }
mfc::CString& CameraFile() { return Global<mfc::CString>(0x0079BA1C); }

// The window: its class atom, class name and title (0x100 bytes each, from the string table), the
// instance and the window.
std::uint16_t& WindowClassAtom() { return Global<std::uint16_t>(0x0079BA20); }
char* WindowClassName() { return &Global<char>(0x0079BA24); }
char* WindowTitle() { return &Global<char>(0x0079BB24); }
HINSTANCE& Instance() { return Global<HINSTANCE>(0x0079BC24); }
HWND& MainWindow() { return Global<HWND>(0x0079BC28); }
constexpr int kTextSize = 0x100;
constexpr UINT kTitleString = 0x65;
constexpr UINT kClassNameString = 0x66;
constexpr int kIcon = 0x69;

// The registry key of the options and the folders: where the game was started, the disc's, the
// text resources' and the data folder (the last three are set from the options).
mfc::CString& RegistryRoot() { return Global<mfc::CString>(0x007AF450); }
mfc::CString& StartFolder() { return Global<mfc::CString>(0x007AF454); }
mfc::CString& DiscFolder() { return Global<mfc::CString>(0x007AF458); }
mfc::CString& TextFolder() { return Global<mfc::CString>(0x007AF45C); }
mfc::CString& DataFolder() { return Global<mfc::CString>(0x007AF460); }

// The window's activation as the window procedure left it (app.cpp).
std::uint8_t& Active() { return Global<std::uint8_t>(0x0079BC5C); }
std::uint8_t& ActivationChanged() { return Global<std::uint8_t>(0x0079BC5D); }

// The loop's clock: the time of the last pass (a function-local static of the original: guard bit
// 0 of 0079B9FC), the time not yet run by the server and by the client (milliseconds), and the
// client's speed.
std::uint8_t& LastTimeGuard() { return Global<std::uint8_t>(0x0079B9FC); }
DWORD& LastTime() { return Global<DWORD>(0x0079B9F8); }
std::uint32_t& ServerBacklog() { return Global<std::uint32_t>(0x0079BC64); }
std::uint32_t& ClientBacklog() { return Global<std::uint32_t>(0x0079BC68); }
float& ClientSpeed() { return Global<float>(0x00784758); }
// The console's switch that prints the client speed every frame.
int& ShowClientSpeed() { return Global<int>(0x0079BC60); }
// A movie of the frames is being written (app_frame.cpp): the loop then runs a fixed time a frame.
std::uint8_t& RecordingFrames() { return Global<std::uint8_t>(0x0079BC54); }

// What the video memory allows (bytes read elsewhere): little texture memory, little memory besides
// the textures, little non-local memory; and whether a device was named on the command line.
std::uint8_t& LittleOtherMemory() { return Global<std::uint8_t>(0x00784753); }
std::uint8_t& LittleTextureMemory() { return Global<std::uint8_t>(0x00784754); }
std::uint8_t& LittleNonLocalMemory() { return Global<std::uint8_t>(0x00784755); }
std::uint8_t& DeviceNamed() { return Global<std::uint8_t>(0x00784756); }
constexpr DWORD kLittleMemory = 0x800000;

// The loading screen's movie file.
const char*& LoadingMovieName() { return Global<const char*>(0x00791EE0); }

// The objects.
CWorldClient* Client() { return &Global<CWorldClient>(0x007AF5B8); }
CWorldServer* Server() { return &Global<CWorldServer>(0x007AFF60); }
CCamera* Camera() { return &Global<CCamera>(0x0079B2A8); }
CSoundManager* Sound() { return Global<CSoundManager*>(0x007AF784); }
CDirectInputMouse*& Mouse() { return Global<CDirectInputMouse*>(0x0079BA08); }
CInterface3D* Interface() { return &Global<CInterface3D>(0x007C7D50); }
void* ClassRegistry() { return &Global<char>(0x0079B270); }
void* LogicSettingsObject() { return &Global<char>(0x007C53F8); }
void* QuestDatabaseObject() { return &Global<char>(0x007C87A8); }
void* SpellTablesObject() { return &Global<char>(0x007C8510); }
void* ItemDatabaseObject() { return &Global<char>(0x007C86D8); }
void* LeverTableObject() { return &Global<char>(0x0079C1A0); }
void* AckDatabaseObject() { return &Global<char>(0x007C8A60); }
void* PrintsDatabaseObject() { return &Global<char>(0x007C89C8); }
void* MapDatabaseObject() { return &Global<char>(0x0079BF00); }
void* TextureManager() { return Global<void*>(0x007AF744); }
UnitDatabase* UnitDatabaseObject() { return &Global<UnitDatabase>(0x007AF358); }
SkillDatabase* SkillDatabaseObject() { return &Global<SkillDatabase>(0x007AF268); }
CRITICAL_SECTION* DeviceLock() { return &Global<CRITICAL_SECTION>(0x007AEE78); }

// The game's log stream (the old iostream).
console::GameOStream& SpeedLog() { return Global<console::GameOStream>(0x007C56B0); }

// ---- The callees -----------------------------------------------------------------------------------

// 0045E310: the DirectX version (0x700 for DirectX 7).
__declspec(naked) std::uint32_t __cdecl DirectXVersionAt() EI_ORIGINAL(0x0045E310)
// 00458570 (RET 8): a class the chunk archives can hold, by id and factory. The two factories:
// 00457850 (class 1) and 00457740 (class 2, the party record).
__declspec(naked) void __fastcall RegisterClassAt(void* /*registry*/, void*, std::uint32_t /*id*/, void* /*factory*/) EI_ORIGINAL(0x00458570)
void* ClassTemplateFactory() { return &Global<char>(0x00457850); }
void* PartyRecordFactory() { return &Global<char>(0x00457740); }
// 0055EA10: the start folder kept. 005CE0F0 (RET 4): the logic settings read from a .reg file.
__declspec(naked) void __cdecl StoreStartFolderAt() EI_ORIGINAL(0x0055EA10)
__declspec(naked) void __fastcall LogicSettingsLoadAt(void* /*settings*/, void*, const char* /*path*/) EI_ORIGINAL(0x005CE0F0)
// 0045B210 / 0045C560 (RET 4): the options read from and written to the registry key.
__declspec(naked) void __fastcall OptionsLoadAt(GameOptions*, void*, const char* /*key*/) EI_ORIGINAL(0x0045B210)
__declspec(naked) void __fastcall OptionsSaveAt(GameOptions*, void*, const char* /*key*/) EI_ORIGINAL(0x0045C560)
// The worlds: set up (00561E50 client, 0058AE10 server) and torn down (00563290, 0058B020); the
// client's camera (00565670, RET 4) and graphics (00563710, RET 4).
__declspec(naked) int __fastcall ClientSetupAt(CWorldClient*) EI_ORIGINAL(0x00561E50)
__declspec(naked) void __fastcall ClientTeardownAt(CWorldClient*) EI_ORIGINAL(0x00563290)
__declspec(naked) int __fastcall ServerSetupAt(CWorldServer*) EI_ORIGINAL(0x0058AE10)
__declspec(naked) void __fastcall ServerTeardownAt(CWorldServer*) EI_ORIGINAL(0x0058B020)
__declspec(naked) void __fastcall ClientSetCameraAt(CWorldClient*, void*, CCamera*) EI_ORIGINAL(0x00565670)
__declspec(naked) void __fastcall ClientSetUpGraphicsAt(CWorldClient*, void*, CTextureDevice*) EI_ORIGINAL(0x00563710)
// The client's state for the loop (all AL / EAX): 00566EF0 the local player is idle, 00566EC0 the
// local view is in mode 2, 00566E20 / 00566E70 server updates are waiting / how many.
__declspec(naked) bool __fastcall ClientLocalPlayerIdleAt(CWorldClient*) EI_ORIGINAL(0x00566EF0)
__declspec(naked) bool __fastcall ClientViewModeTwoAt(CWorldClient*) EI_ORIGINAL(0x00566EC0)
__declspec(naked) bool __fastcall ClientHasIncomingAt(CWorldClient*) EI_ORIGINAL(0x00566E20)
__declspec(naked) int __fastcall ClientIncomingCountAt(CWorldClient*) EI_ORIGINAL(0x00566E70)
// The camera: 0040D0A0 (RET 0x10) Init, 0040FB30 (RET 4) the update, 0040EB60 / 0040EFB0 (RET 4) a
// frame written to / read from the camera file at a time in ticks.
__declspec(naked) int __fastcall CameraInitAt(CCamera*, void*, GameOptions*, float /*nearClip*/, float /*farClip*/, float /*fieldOfView*/)
    EI_ORIGINAL(0x0040D0A0)
__declspec(naked) void __fastcall CameraUpdateAt(CCamera*, void*, float /*seconds*/) EI_ORIGINAL(0x0040FB30)
__declspec(naked) int __fastcall CameraRecordFrameAt(CCamera*, void*, float /*time*/) EI_ORIGINAL(0x0040EB60)
__declspec(naked) int __fastcall CameraPlaybackStepAt(CCamera*, void*, float /*time*/) EI_ORIGINAL(0x0040EFB0)
// The renderer (007AF060): 00500B70 its creation (slot 2 of its vtable, RET 8), 00504A60 (RET 0xC)
// the display's brightness, contrast and gamma.
__declspec(naked) void __fastcall RendererCreateAt(CSceneRenderer*, void*, HWND, GameOptions*) EI_ORIGINAL(0x00500B70)
__declspec(naked) bool __fastcall RendererSetGammaAt(CSceneRenderer*, void*, float, float, float) EI_ORIGINAL(0x00504A60)
// The sound manager: 006B6A30 (RET 0x14) opened, 006BAE20 (RET 8) the music folder and its
// registry file, 006B7060 / 006B7AB0 (RET 4) the wave archives, the three volumes (RET 4).
__declspec(naked) int __fastcall SoundOpenAt(CSoundManager*, void*, HWND, int /*reverb*/, int /*device*/, unsigned /*rate*/, int /*bits*/)
    EI_ORIGINAL(0x006B6A30)
__declspec(naked) int __fastcall SoundSetPathAt(CSoundManager*, void*, const char* /*folder*/, const char* /*registry*/) EI_ORIGINAL(0x006BAE20)
__declspec(naked) void __fastcall SoundClearWaveSourcesAt(CSoundManager*) EI_ORIGINAL(0x006B7060)
__declspec(naked) int __fastcall SoundAddWaveSourceAt(CSoundManager*, void*, const char* /*path*/) EI_ORIGINAL(0x006B7AB0)
__declspec(naked) void __fastcall SoundSetEffectsVolumeAt(CSoundManager*, void*, int) EI_ORIGINAL(0x006B7740)
__declspec(naked) void __fastcall SoundSetSpeechVolumeAt(CSoundManager*, void*, int) EI_ORIGINAL(0x006B78A0)
__declspec(naked) void __fastcall SoundSetMusicVolumeAt(CSoundManager*, void*, int) EI_ORIGINAL(0x006B7A00)
// 005DA3C0: the loading screen's movie closed.
__declspec(naked) void __cdecl CloseLoadingMovieAt() EI_ORIGINAL(0x005DA3C0)
// The databases read from the resources folder (RET 4; the others are in originals_world.h).
// 00463390 is patched by Rebirth.
__declspec(naked) int __fastcall QuestDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006AE3A0)
__declspec(naked) int __fastcall LeverTableLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x00463390)
__declspec(naked) int __fastcall AckDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006D7620)
__declspec(naked) int __fastcall PrintsDatabaseLoadAt(void*, void*, const char* /*folder*/) EI_ORIGINAL(0x006D0630)
// The mouse: 0065EAD0 (RET 8) DirectInput set up for the window; 004578B0 (RET 4) slot 0 of its
// vtable, the deleting destructor.
__declspec(naked) int __fastcall MouseSetupAt(CDirectInputMouse*, void*, HWND, HINSTANCE) EI_ORIGINAL(0x0065EAD0)
__declspec(naked) CDirectInputMouse* __fastcall MouseDeleteAt(CDirectInputMouse*, void*, unsigned /*flags*/) EI_ORIGINAL(0x004578B0)
// The interface (007C7D50): 005F4990 (RET 4) created on the device, 005F57C0 its update, 005F4F90
// shut down.
__declspec(naked) void __fastcall InterfaceCreateAt(CInterface3D*, void*, CTextureDevice*) EI_ORIGINAL(0x005F4990)
__declspec(naked) void __fastcall InterfaceUpdateAt(CInterface3D*) EI_ORIGINAL(0x005F57C0)
__declspec(naked) void __fastcall InterfaceShutdownAt(CInterface3D*) EI_ORIGINAL(0x005F4F90)
// 0045EA00: the map list read. 00456140: the first map loaded, or the main menu opened.
__declspec(naked) bool __fastcall MapDatabaseLoadAllAt(void*) EI_ORIGINAL(0x0045EA00)
__declspec(naked) int __cdecl StartFirstMapAt() EI_ORIGINAL(0x00456140)
// 005790E0 (RET 4; the glue object is not read): the players' data scrambled (0) or plain (1).
__declspec(naked) void __fastcall GuardPlayerDataAt(void* /*glue*/, void*, char /*plain*/) EI_ORIGINAL(0x005790E0)
// The game flow (007AFE20): 0057F390 a new client session, 00581660 both sessions closed.
__declspec(naked) void __fastcall FlowRecreateClientSessionAt(GameFlow*) EI_ORIGINAL(0x0057F390)
__declspec(naked) void __fastcall FlowCloseSessionsAt(GameFlow*) EI_ORIGINAL(0x00581660)
// The messages: 00457970 (ECX message, EDX wParam, RET 4) to the interface's input, 00457D40 the
// window procedure.
__declspec(naked) LRESULT __fastcall DispatchInputAt(UINT, WPARAM, LPARAM) EI_ORIGINAL(0x00457970)
__declspec(naked) LRESULT __stdcall WindowProcAt(HWND, UINT, WPARAM, LPARAM) EI_ORIGINAL(0x00457D40)
// 006916F0 (ECX the texture manager; not read): the composed textures' files deleted.
__declspec(naked) void __fastcall DeleteComposedTexturesAt(void* /*textureManager*/) EI_ORIGINAL(0x006916F0)

// ---- Start-up --------------------------------------------------------------------------------------

constexpr int kExitDeclined = 0xDEAD;   // WinMain's result when the player gives up at a warning
constexpr UINT kWarningBox = 0x31;      // MB_OKCANCEL | MB_ICONEXCLAMATION
constexpr UINT kCriticalBox = 0x30;     // MB_ICONEXCLAMATION
// The system messages by line.
enum MessageLine { kNeedsDirectX7, kLittleDiskSpace, kInsertDisc };
constexpr int kMessageLines = 3;

void AppendDefaultMessages(console::CStringArray& messages) {
    messages.SetAtGrow(messages.size, GameText(0x007849A8));   // "This Game requires DX7 in order to run!"
    messages.SetAtGrow(messages.size, GameText(0x00784974));   // "Not enough free disk space - play on your own risk!"
    messages.SetAtGrow(messages.size, GameText(0x00784940));   // "Please, insert EI disc 2 in the CD-ROM and press OK"
}

// The lines of .\res\sysmsg.txt, or the built-in three when the file is missing or shorter.
void LoadSystemMessages(console::CStringArray& messages) {
    gamecrt::CrtFile* file = gamecrt::fopen(GameText(0x007849D0), GameText(0x007849E4));   // "rt"
    if (!file) {
        AppendDefaultMessages(messages);
        return;
    }
    const std::size_t length = static_cast<std::size_t>(gamecrt::filelength(gamecrt::fileno(file)));
    char* text = static_cast<char*>(gamecrt::operator_new(length));
    std::memset(text, 0, length);
    gamecrt::fread(text, 1, length, file);
    gamecrt::fclose(file);
    StringSplit(text, &messages, '\n');
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
    StringSplit(commandLine, &arguments, ' ');
    CameraFile() = GameText(0x00784920);   // ".\camera.cam"
    CommandLineScript().Empty();
    CommandLineMob().Empty();
    CommandLineMap().Empty();
    CWorldClient* client = Client();
    GameOptions* options = options::TheOptions();
    for (int i = 0; i < arguments.size; ++i) {
        if (arguments[i].Find(GameText(0x00784914)) == 0) {   // "-moviepath_"
            client->flags_ &= ~0x20u;
            {
                mfc::CString folder = Tail(arguments[i], 11);
                MovieFolder() = folder;
            }
            const char* folder = MovieFolder();
            if (folder[MovieFolder().GetLength() - 1] != '\\') CStringAppendCharOriginal(&MovieFolder(), nullptr, '\\');
            continue;
        }
        if (!(client->flags_ & 0x10)) {
            if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(arguments[i])),
                                 reinterpret_cast<const unsigned char*>(GameText(0x0078490C))) == 0) {   // "-nocam"
                client->flags_ &= ~8u;
                continue;
            }
            if (!(client->flags_ & 0x10) && arguments[i].Find(GameText(0x00784904)) != -1) {   // ".cam"
                CameraFile() = arguments[i];
                continue;
            }
        }
        if (arguments[i].Find(GameText(0x007848FC)) != -1) {   // ".scr"
            CommandLineScript() = arguments[i];
        } else if (arguments[i].Find(GameText(0x007848F4)) != -1) {   // ".mob"
            CommandLineMob() = arguments[i];
        } else if (arguments[i].Find(GameText(0x007848EC)) != -1) {   // ".mpr"
            CommandLineMap() = arguments[i];
        } else if (arguments[i].Find(GameText(0x007848E0)) == 0) {   // "-benchmark_"
            mfc::CString frames = Tail(arguments[i], 11);
            options->numberOfFrames_ = GameAtoi(frames);
            options->createOutputInfo_ = 1;
        } else if (arguments[i].Find(GameText(0x007848D0)) == 0) {   // "-devicetarget_"
            mfc::CString driver = Tail(arguments[i], 14);
            options->driverIndex_ = GameAtoi(driver);
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
    windowClass.lpfnWndProc = reinterpret_cast<WNDPROC>(&Global<char>(0x00457D40));
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
    ExitGame(-1);
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
    gamecrt::splitpath(path, parts.drive, parts.directory, parts.name, parts.extension);
    std::memcpy(parts.drive + std::strlen(parts.drive), GameText(0x00784894), 3);   // "\." appended
}

// The free bytes on the drive of `folder` through GetDiskFreeSpaceExA where kernel32 has it, else
// from the cluster counts. False when the call fails.
bool FreeDiskSpace(const char* folder, double* freeBytes) {
    PathParts parts;
    DriveRoot(folder, parts);
    using FreeSpaceEx = BOOL(__stdcall*)(const char*, ULARGE_INTEGER*, ULARGE_INTEGER*, ULARGE_INTEGER*);
    auto* extended = reinterpret_cast<FreeSpaceEx>(
        gameapi::GetProcAddress(gameapi::GetModuleHandleA(GameText(0x00784788)), GameText(0x00784798)));   // kernel32.dll
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
    mfc::CString dataFolder(DataFolder());
    double freeBytes;
    if (FreeDiskSpace(dataFolder, &freeBytes) && !(freeBytes == ExeDouble(0x0073C340)) && freeBytes < ExeDouble(0x0073C358)) {
        if (gameapi::MessageBoxA(nullptr, messages[kLittleDiskSpace], GameText(0x0078488C), kWarningBox) == IDCANCEL) return false;
    }
    if (GameMbscmp(dataFolder, discFolder) != 0) {
        char volume[0x400] = {};
        PathParts parts;
        DriveRoot(discFolder, parts);
        gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        while (GameMbscmp(options::TheOptions()->cdLabels_[1], volume) != 0) {
            if (gameapi::MessageBoxA(nullptr, messages[kInsertDisc], GameText(0x0078488C), kWarningBox) == IDCANCEL) return false;
            gameapi::GetVolumeInformationA(parts.drive, volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0);
        }
    }
    return true;
}

// One reference of the mouse object dropped: deleted at the last (slot 0 of its vtable).
void ReleaseMouse(CDirectInputMouse* mouse) {
    if (--mouse->references_ == 0) MouseDeleteAt(mouse, nullptr, 1);
}

// The mouse object made (not set up yet) and kept with one reference.
void CreateMouse() {
    auto* mouse = static_cast<CDirectInputMouse*>(gamecrt::operator_new(sizeof(CDirectInputMouse)));
    if (mouse) {
        mouse->references_ = 0;
        mouse->input_ = nullptr;
        mouse->device_ = nullptr;
        mouse->vtable_ = &Global<char>(0x0073C350);
    }
    CDirectInputMouse* old = Mouse();
    Mouse() = mouse;
    if (mouse) ++mouse->references_;
    if (old) ReleaseCountedOriginal(old);
}

// The folders the game writes to.
void CreateFolders(mfc::CString& path) {
    path.Format(GameText(0x00784880), static_cast<const char*>(DataFolder()));   // "%ssaves\"
    gameapi::CreateDirectoryA(path, nullptr);
    path.Format(GameText(0x00784878), static_cast<const char*>(DataFolder()));   // "%smp\"
    gameapi::CreateDirectoryA(path, nullptr);
    path.Format(GameText(0x0078486C), static_cast<const char*>(DataFolder()));   // "%stextures\"
    gameapi::CreateDirectoryA(path, nullptr);
}

// The renderer created on the window, then what the video memory allows. Returns the free
// non-local video memory.
DWORD CreateRenderer() {
    GameOptions* options = options::TheOptions();
    CCamera* camera = Camera();
    camera->localPosition_.x = options->cameraDefaultXDeploy_;
    camera->localPosition_.y = options->cameraDefaultYDeploy_;
    camera->localPosition_.z = options->cameraDefaultHeight_;
    RendererCreateAt(static_cast<CSceneRenderer*>(DisplayObject()), nullptr, MainWindow(), options);

    DDSCAPS2 caps = {};
    DWORD total = 0, free = 0;
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    static_cast<IDirectDraw7*>(RenderDeviceObject()->ddraw_)->GetAvailableVidMem(&caps, &total, &free);
    DWORD other = free;
    caps.dwCaps = DDSCAPS_TEXTURE;
    static_cast<IDirectDraw7*>(RenderDeviceObject()->ddraw_)->GetAvailableVidMem(&caps, &total, &free);
    LittleTextureMemory() = free <= kLittleMemory;
    if (free < other) other -= free;
    LittleOtherMemory() = other <= kLittleMemory;
    caps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_NONLOCALVIDMEM;
    static_cast<IDirectDraw7*>(RenderDeviceObject()->ddraw_)->GetAvailableVidMem(&caps, &total, &free);
    LittleNonLocalMemory() = free <= kLittleMemory;
    DeviceNamed() = options->driverIndex_ != 0;
    RendererSetGammaAt(static_cast<CSceneRenderer*>(SceneContext()), nullptr, options->brightness_, options->contrast_, options->gamma_);
    return free;
}

// The sound opened (44.1 kHz, 16 bits) with its music folder, wave archives and volumes. Returns
// the disc folder's text pointer (see the caller).
std::uintptr_t OpenSound() {
    GameOptions* options = options::TheOptions();
    mfc::CString textFolder(TextFolder());
    mfc::CString dataFolder(DataFolder());
    mfc::CString discFolder(DiscFolder());
    SoundOpenAt(Sound(), nullptr, MainWindow(), 1, -1, 44100, 16);
    {
        mfc::CString registry = textFolder + GameText(0x0078485C);   // "streamsn.reg"
        mfc::CString folder = dataFolder + GameText(0x00784854);     // "stream\"
        SoundSetPathAt(Sound(), nullptr, folder, registry);
    }
    {
        mfc::CString effects = textFolder + GameText(0x0078484C);   // "sfx.res"
        CSoundManager* sound = Sound();
        SoundClearWaveSourcesAt(sound);
        SoundAddWaveSourceAt(sound, nullptr, effects);
    }
    {
        mfc::CString unused = discFolder + GameText(0x0078483C);   // "res\speech.res" (built twice)
        mfc::CString speech = discFolder + GameText(0x0078483C);
        SoundAddWaveSourceAt(Sound(), nullptr, speech);
    }
    SoundSetEffectsVolumeAt(Sound(), nullptr, options->audioSfx_);
    SoundSetSpeechVolumeAt(Sound(), nullptr, options->audioSpeech_);
    SoundSetMusicVolumeAt(Sound(), nullptr, options->audioStreaming_);
    Sound()->panDirect = options->reverseStereo_ != 0;
    return reinterpret_cast<std::uintptr_t>(static_cast<const char*>(discFolder));
}

void LoadDatabases() {
    const char* archive = GameText(0x00784818);   // "database.res"
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
std::uint32_t TickLength() { return Global<std::uint32_t>(0x0078FF34); }
float CameraSeconds(std::uint32_t milliseconds) {
    return RoundToFloat(x87::FromUnsigned(milliseconds) * ExeFloat(0x0073C348));
}

// The frame's last camera update goes through the target of the original CALL at 00457595 (the
// update 0040FB30 unless something redirected it): the verify harness redirects that call to hold
// the camera still while a scenario is frozen (tests/verify/harness.cpp), and keeps working with
// this rewrite. The original body stays in place behind the hook's jump.
using CameraUpdate = void(__fastcall*)(CCamera*, void*, float);
CameraUpdate FrameCameraUpdate() {
    const auto* call = &Global<const unsigned char>(0x00457595);
    if (call[0] != 0xE8) return &CameraUpdateAt;
    std::int32_t relative;
    std::memcpy(&relative, call + 1, sizeof(relative));
    return reinterpret_cast<CameraUpdate>(reinterpret_cast<std::uintptr_t>(call) + 5 + relative);
}

// A change of the window's activation taken up: active again, every surface is restored (when the
// renderer was created) and the renderer runs; inactive, the renderer stops and the desktop is
// repainted.
void TakeActivationChange() {
    if (!ActivationChanged()) return;
    if (Active()) {
        if (static_cast<CSceneRenderer*>(SceneContext())->flags_ & 2) {
            CTextureDevice* device = RenderDeviceObject();
            gameapi::EnterCriticalSection(DeviceLock());
            device->lastLoadResult_ = static_cast<IDirectDraw7*>(device->ddraw_)->RestoreAllSurfaces();
            gameapi::LeaveCriticalSection(DeviceLock());
        }
        static_cast<CSceneRenderer*>(DisplayObject())->flags_ |= 1;
        gameapi::SetCursor(nullptr);
    } else {
        static_cast<CSceneRenderer*>(DisplayObject())->flags_ &= ~1u;
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
            if (!ServerUpdateOriginal(Server())) return false;
            ServerBacklog() -= TickLength();
            NetworkTimers();
        }
        if (ClientBacklog() >= TickLength() && (ClientHasIncomingAt(Client()) || !ClientViewModeTwoAt(Client()))) {
            if (!ClientSegmentOriginal(Client())) return false;
            const std::uint32_t tick = TickLength();
            ClientBacklog() -= tick;
            CameraUpdateAt(Camera(), nullptr, CameraSeconds(tick));
            NetworkTimers();
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
    if (balance < ExeFloat(0x0073B858)) {
        speed = x87::fexp(Step(rounded * ExeFloat(0x0073B850)));
    } else if (!(rounded > ExeFloat(0x0073B84C))) {
        speed = ExeFloat(0x0073B84C);
    } else {
        speed = Step(Step(x87::fsqrt(rounded) * ExeFloat(0x0073B850)) + ExeFloat(0x0073B84C)) - ExeFloat(0x0073B850);
    }
    float result = RoundToFloat(speed);
    if (!(result >= kSlowest)) result = kSlowest;
    if (kFastest < result) result = kFastest;
    return result;
}

void PrintClientSpeed(float speed, float balance) {
    console::GameOStream& log = SpeedLog();
    log << GameText(0x00784808);            // "Client speed = "
    log.PutFloat(speed) << GameText(0x007847FC);   // " balance = "
    log.PutFloat(balance) << '\n';
    log.Flush();
}

// One pass of the loop while no message waits. Returns whether the interface takes the input
// messages from now on: not while the renderer is inactive.
bool RunPass() {
    TakeActivationChange();
    std::uint32_t elapsed = TimeSinceLastPass();
    void* glue = GameGlueObject();
    if (!(static_cast<CSceneRenderer*>(DisplayObject())->flags_ & 1)) {
        GuardPlayerDataAt(glue, nullptr, 0);
        return false;
    }
    GuardPlayerDataAt(glue, nullptr, 1);
    CWorldClient* client = Client();
    if (ClientLocalPlayerIdleAt(client)) {
        if (IsNetworkClientOriginal()) FlowRecreateClientSessionAt(TheGameFlow());
        else FlowCloseSessionsAt(TheGameFlow());
    }
    if (elapsed > 0x80000000u) elapsed = 0;
    NetworkTimers();
    if (elapsed > TickLength() * kLongestPassTicks) elapsed = TickLength() * kLongestPassTicks;
    if (RecordingFrames()) {
        // A recorded frame lasts 1000 / the movie's frames a second, rounded up.
        const double frame = Step(ExeFloat(0x0073C34C) / static_cast<double>(options::TheOptions()->movieCapturingFps_));
        elapsed = gamecrt::Ftol(gamecrt::ceil(frame));
    }
    ServerBacklog() += elapsed;
    if (!ClientViewModeTwoAt(client)) ClientSpeed() = 1.0f;
    ClientBacklog() =
        gamecrt::Ftol(Step(x87::FromUnsigned(elapsed) * ClientSpeed()) + static_cast<double>(static_cast<std::int32_t>(ClientBacklog())));
    if (ClientBacklog() > kBacklogLimit) ClientBacklog() = 0;

    if (RunWorlds()) {
        FlowProcessRequestOriginal(TheGameFlow());
        const float fraction = RoundToFloat(x87::FromUnsigned(ClientBacklog()) / static_cast<double>(static_cast<std::int32_t>(TickLength())));
        if (!client->paused_) client->tickFraction_ = fraction;
        const double balance = Step(static_cast<double>(ClientIncomingCountAt(client)) - fraction);
        const float roundedBalance = RoundToFloat(balance);
        ClientSpeed() = SpeedForBalance(balance, roundedBalance);
        if (ShowClientSpeed()) PrintClientSpeed(ClientSpeed(), roundedBalance);

        // The camera runs on the real time of the pass, a tick at a time.
        CCamera* camera = Camera();
        std::uint32_t tick = TickLength();
        while (elapsed > tick) {
            CameraUpdateAt(camera, nullptr, CameraSeconds(tick));
            tick = TickLength();
            elapsed -= tick;
        }
        FrameCameraUpdate()(camera, nullptr, CameraSeconds(elapsed));
        if (!client->paused_) {
            if (camera->flags_ & CCamera::kRecording)
                CameraRecordFrameAt(camera, nullptr, RoundToFloat(static_cast<double>(client->tick_) + client->tickFraction_));
            if (camera->flags_ & CCamera::kPlaying)
                CameraPlaybackStepAt(camera, nullptr, RoundToFloat(static_cast<double>(client->tick_) + client->tickFraction_));
        }
    }
    InterfaceUpdateAt(Interface());
    FrameStepOriginal(static_cast<char>(MovieModeFlag()), 1);
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
        if (interfaceInput) DispatchInputAt(message.message, message.wParam, message.lParam);
        else WindowProcAt(MainWindow(), message.message, message.wParam, message.lParam);
    }
}

// ---- Shutdown --------------------------------------------------------------------------------------

void ShutDown(gamecrt::NewHandler previousHandler) {
    DeleteComposedTexturesAt(TextureManager());
    InterfaceShutdownAt(Interface());
    CursorShutdownOriginal(GameCursor());
    CDirectInputMouse* mouse = Mouse();
    Mouse() = nullptr;
    if (mouse) ReleaseMouse(mouse);
    RendererSetGammaAt(static_cast<CSceneRenderer*>(SceneContext()), nullptr, 0.0f, 0.0f, 0.0f);
    static_cast<CSceneRenderer*>(DisplayObject())->DestroyRenderer();
    OptionsSaveAt(options::TheOptions(), nullptr, RegistryRoot());
    ClientSetCameraAt(Client(), nullptr, nullptr);
    ServerTeardownAt(Server());
    ClientTeardownAt(Client());
    gamecrt::set_new_handler(previousHandler);
}

} // namespace

int __stdcall GameMain(HINSTANCE instance, HINSTANCE, char* commandLine, int show) {
    console::CStringArray messages;
    LoadSystemMessages(messages);
    if (DirectXVersionAt() < 0x700) {
        gameapi::MessageBoxA(nullptr, messages[kNeedsDirectX7], GameText(0x00784930), kCriticalBox);   // "CRITICAL ERROR"
        return kExitDeclined;
    }
    RegisterClassAt(ClassRegistry(), nullptr, 1, ClassTemplateFactory());
    RegisterClassAt(ClassRegistry(), nullptr, 2, PartyRecordFactory());
    ReadCommandLine(commandLine);
    RegisterWindowClass(instance);

    GameOptions* options = options::TheOptions();
    RegistryRoot() = GameText(0x007848A8);   // "Software\Nival Interactive\EvilIslands"
    StoreStartFolderAt();
    {
        mfc::CString settings = StartFolder() + GameText(0x00784898);   // "config\ai.reg"
        LogicSettingsLoadAt(LogicSettingsObject(), nullptr, settings);
    }
    OptionsLoadAt(options, nullptr, RegistryRoot());
    const gamecrt::NewHandler previousHandler =
        gamecrt::set_new_handler(reinterpret_cast<gamecrt::NewHandler>(&Global<char>(0x00455A30)));
    ClientSetupAt(Client());
    ServerSetupAt(Server());
    // The field of view: pi * 2 / 7 (51.4 degrees).
    CameraInitAt(Camera(), nullptr, options, options->nearClipDistance_, options->farClipDistance_,
                 RoundToFloat(ExeDouble(0x0073C308) * ExeDouble(0x0073B9D8)));
    ClientSetCameraAt(Client(), nullptr, Camera());
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

    PlayMoviesOriginal(GameText(0x00784834), 1, 1, 1, 1);   // "Start"
    LoadingMovieName() = GameText(0x00784828);              // "Progres.bik"
    ShowLoadingMovieOriginal(0);
    CloseLoadingMovieAt();
    LoadDatabases();
    MouseSetupAt(Mouse(), nullptr, MainWindow(), instance);
    ClientSetUpGraphicsAt(Client(), nullptr, RenderDeviceObject());
    // The last argument's upper three bytes are whatever the call before left in ECX in the
    // original; only the low byte is a value (the shapes' flag: no device named).
    SetUpCursorsOriginal(options->fpsIndependentCursor_ != 0, static_cast<int>(reinterpret_cast<std::intptr_t>(Mouse())), 0,
                         DeviceNamed() == 0);
    InterfaceCreateAt(Interface(), nullptr, RenderDeviceObject());
    MapDatabaseLoadAllAt(MapDatabaseObject());
    StartFirstMapAt();
    RendererSetGammaAt(static_cast<CSceneRenderer*>(SceneContext()), nullptr, options->brightness_, options->contrast_, options->gamma_);

    const int result = RunMainLoop((reusedSlot >> 24) != 0);
    ShutDown(previousHandler);
    return result;
}
EI_REPLACE(0x00456440, GameMain);

} // namespace app
