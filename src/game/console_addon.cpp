// Rebirth's console additions (addon.dll v0.12.4.2), reproduced behind their flags
// (include/ei/rebirth.h, docs/notes/rebirth.md):
// - AddNewConsoleCommands: the console executor (005DC620) also registers "help" (wrapped),
//   "addon", "execs", "listgvar", "drawall" (after the cheat word) and "nogui" on every call, and
//   the battle screen's drawing (00636020) applies nogui's GUI visibility to its panels.
// - AutoExec: the "Console AutoExec" lines (HKCU\Software\Gipat.Ru\EI_Starter\Settings, a
//   REG_MULTI_SZ, then the lines of 2utoexec.cfg) run before each of three console executions
//   (0057ABE1, 0057BD30, 0057C9A3; the host functions are not rewritten yet).
// The texts are addon's. Its own state (the two log levels, the GUI flags) lives here.
#include "console.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_ui.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

namespace {

// 004D36F0 (the script loader 007C27D4: load a file) and 004BF000 (run what was loaded).
__declspec(naked) void __fastcall ScriptsRun(void*) EI_ORIGINAL(0x004BF000)
// 006002A0. CUiWindow::SetEnabled(enabled, notify).
// 006FDC12. CString::FormatV(format, arguments).
struct FormatVAt : mfc::CString {
    void FormatV(const char* format, va_list arguments);
};
__declspec(naked) void FormatVAt::FormatV(const char*, va_list) EI_ORIGINAL(0x006FDC12)
// 005DC620 through its address.

void* const kScripts = reinterpret_cast<void*>(0x007C27D4);
void* const kRenderer = reinterpret_cast<void*>(0x007C7BD8);
int* const kSpectator = reinterpret_cast<int*>(0x0079BE6C);
void* const kFlushManipulator = reinterpret_cast<void*>(0x00435000);

// addon's state.
CommandHandler g_gameHelp = nullptr;   // the game's "help", saved at each registration
int g_consoleLogLevel = 1;             // addon's messages to the console (100BA6F4)
int g_addonLogLevel = 0;               // addon.log (100BD4A4)
bool g_guiShown = true;                // nogui (100BA664)
bool g_cursorShown = true;             // (100BA665)
bool g_applyGui = false;               // the battle screen applies g_guiShown (100BD128)

// addon's console output: the text, "\n" (as a string), flush.
void Say(const char* text) { (Out() << text << "\n").Apply(kFlushManipulator); }
void __cdecl SayFormat(const char* format, ...) {
    mfc::CString text;
    va_list args;
    va_start(args, format);
    static_cast<FormatVAt&>(text).FormatV(format, args);
    va_end(args);
    Say(text);
}

// listgvar's object text: "null", or "<" class name (slot 11) ":" id ">".
std::string ObjectText(void* const* slot) {
    void* object = *slot;
    if (!object) return "null";
    using NameFn = const char*(__thiscall*)(void*);
    const char* name = reinterpret_cast<NameFn>((*static_cast<void***>(object))[11])(object);
    char id[16];
    std::snprintf(id, sizeof(id), "%d", *reinterpret_cast<int*>(static_cast<std::uint8_t*>(object) + 0xC));
    return std::string("<") + name + ":" + id + ">";
}
// A group (0x14 bytes: objects at +4, count at +8): "empty" or the objects joined by ", ".
std::string GroupText(const std::uint8_t* group) {
    int count = *reinterpret_cast<const int*>(group + 8);
    if (count == 0) return "empty";
    void* const* objects = *reinterpret_cast<void* const* const*>(group + 4);
    std::string text;
    for (int i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        text += ObjectText(&objects[i]);
    }
    return text;
}

// ---- The commands (ECX: the arguments, lower case) ----------------------------------------------

void __fastcall Help(CStringArray* args) {
    g_gameHelp(args);
    Say("ADDON [command]");
    Say("addon-specific commands");
}

void AddonDebug(CStringArray* args) {
    std::string type = args->GetSize() >= 1 ? static_cast<const char*>((*args)[0]) : "";
    int level = args->GetSize() >= 2 ? GameAtoi((*args)[1]) : -1;
    if (type.empty() || type == "help") {
        SayFormat("Usage:\n\taddon debug {type} [level]\n\nSet debug level for type of addon's debugging settings\n"
                  "or show current value if level is not specified\n\nTypes:\n* log      - addon log file (current: %d)\n"
                  "* console  - addon's messages to console (current: %d)\n\nLevels:\n* %d        - disable\n"
                  "* %d        - error\n* %d        - warning\n* %d        - information\n* %d        - debug",
                  g_addonLogLevel, g_consoleLogLevel, 4, 3, 2, 1, 0);
    } else if (type == "console") {
        if (level >= 0)
            g_consoleLogLevel = level < 5 ? level : 4;
        else
            SayFormat("Current console log level: %d", g_consoleLogLevel);
    } else if (type == "log") {
        if (level >= 0)
            g_addonLogLevel = level < 5 ? level : 4;
        else
            SayFormat("Current addon log level: %d", g_addonLogLevel);
    } else {
        SayFormat("Unknown debug type: %s", type.c_str());
    }
}

void __fastcall Addon(CStringArray* args) {
    if (args->GetSize() == 0 || std::strcmp((*args)[0], "help") == 0) {
        Say("Welcome to Evil Islands Addon Engine v0.12.4.2\n\nAvailable commands:\n"
            "* addon info          - show detailed information about addon\n"
            "* addon debug [args]  - set addon's debug settings");
    } else if (std::strcmp((*args)[0], "info") == 0) {
        SayFormat("Addon core version: 0.12.4.2\nCurrent mod: %s", "No mod is used");
    } else if (std::strcmp((*args)[0], "debug") == 0) {
        args->RemoveAt(0, 1);
        AddonDebug(args);
    } else {
        SayFormat("Unknown command: %s", static_cast<const char*>((*args)[0]));
    }
}

// execs file: loads a script file into the script loader and runs it.
void __fastcall Execs(CStringArray* args) {
    if (LoadScriptFile(kScripts, nullptr, (*args)[0])) {
        ScriptsRun(kScripts);
        return;
    }
    SayFormat("Script error: line %d: %s", *reinterpret_cast<int*>(0x007C29C8), *reinterpret_cast<const char**>(0x007C29C0));
}

// listgvar: the global script variables (floats, strings, objects, groups).
void __fastcall ListGlobals(CStringArray*) {
    auto count = [](std::uintptr_t a) { return *reinterpret_cast<int*>(a); };
    for (int i = 0; i < count(0x007C287C); ++i)
        SayFormat("%s : float = %f", (*reinterpret_cast<const char***>(0x007C288C))[i],
                  static_cast<double>((*reinterpret_cast<float**>(0x007C2878))[i]));
    for (int i = 0; i < count(0x007C28A4); ++i)
        SayFormat("%s : string = \"%s\"", (*reinterpret_cast<const char***>(0x007C28B4))[i],
                  (*reinterpret_cast<const char***>(0x007C28A0))[i]);
    for (int i = 0; i < count(0x007C28CC); ++i) {
        std::string text = ObjectText(&(*reinterpret_cast<void***>(0x007C28C8))[i]);
        SayFormat("%s : object = %s", (*reinterpret_cast<const char***>(0x007C28DC))[i], text.c_str());
    }
    for (int i = 0; i < count(0x007C28F4); ++i) {
        std::string text = GroupText(*reinterpret_cast<std::uint8_t**>(0x007C28F0) + i * 0x14);
        SayFormat("%s : group = %s", (*reinterpret_cast<const char***>(0x007C2904))[i], text.c_str());
    }
}

// drawall n: 0079BE6C = (n == 0).
void __fastcall DrawAll(CStringArray* args) {
    if (args->GetSize() < 1) return;
    *kSpectator = GameAtoi((*args)[0]) == 0 ? 1 : 0;
}

// nogui 0|1|2: GUI and cursor shown / GUI hidden / both hidden.
void __fastcall NoGui(CStringArray* args) {
    int mode = args->GetSize() == 1 ? GameAtoi((*args)[0]) : -1;
    if (static_cast<unsigned>(mode) > 2) {
        Say("Hide or show GUI in game missions\n\nnogui 0 - show all (default)\nnogui 1 - hide GUI but show cursor\n"
            "nogui 2 - hide GUI and cursor");
        return;
    }
    g_cursorShown = mode < 2;
    g_guiShown = mode == 0;
    g_applyGui = true;
    SayFormat("GUI: %s, cursor: %s", g_guiShown ? "show" : "hide", g_cursorShown ? "show" : "hide");
    CursorShowOriginal(kRenderer, nullptr, g_cursorShown ? 1 : 0);
}

void Register(CommandMap* map, const char* name, CommandHandler handler) {
    mfc::CString key(name);
    *map->Index(key) = handler;
}

// The AutoExec lines, read once (addon reads them when it loads).
const std::vector<std::string>& AutoExecLines() {
    static std::vector<std::string> lines = [] {
        std::vector<std::string> result;
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_READ, &key) == 0) {
            DWORD type = 0, size = 0;
            if (RegQueryValueExW(key, L"Console AutoExec", nullptr, &type, nullptr, &size) == 0 && size != 0) {
                std::vector<wchar_t> data(size / 2 + 1, L'\0');
                if (RegQueryValueExW(key, L"Console AutoExec", nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size) == 0 &&
                    type == REG_MULTI_SZ) {
                    std::size_t count = size / 2;
                    for (std::size_t at = 0; at < count;) {
                        std::size_t end = at;
                        while (end < count && data[end] != 0) ++end;
                        if (end > at) {
                            int wide = static_cast<int>(end - at);
                            int n = WideCharToMultiByte(CP_ACP, 0, &data[at], wide, nullptr, 0, nullptr, nullptr);
                            std::string line(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
                            if (n > 0) WideCharToMultiByte(CP_ACP, 0, &data[at], wide, &line[0], n, nullptr, nullptr);
                            result.push_back(line);
                        }
                        at = end + 1;
                    }
                }
            }
            RegCloseKey(key);
        }
        DWORD attributes = GetFileAttributesA("2utoexec.cfg");
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::ifstream file("2utoexec.cfg");
            std::string line;
            while (std::getline(file, line)) result.push_back(line);
        }
        return result;
    }();
    return lines;
}

} // namespace

const AddonCommandRegistration kAddonCommandRegistrations[6] = {
    {true, "help", Help},
    {true, "addon", Addon},
    {true, "execs", Execs},
    {true, "listgvar", ListGlobals},
    {true, "drawall", DrawAll},
    {false, "nogui", NoGui},
};

void RegisterAddonCommands() {
    g_gameHelp = *CheatCommands()->Index(mfc::CString("help"));
    for (const AddonCommandRegistration& r : kAddonCommandRegistrations)
        Register(r.cheat ? CheatCommands() : PublicCommands(), r.name, r.handler);
}

void ApplyAddonGuiVisibility(void* screen) {
    if (!g_applyGui) return;
    auto* bytes = static_cast<std::uint8_t*>(screen);
    for (int i = 0; i < *reinterpret_cast<int*>(bytes + 0x14C); ++i)
        WindowSetEnabled((*reinterpret_cast<void***>(bytes + 0x148))[i], nullptr, g_guiShown ? 1 : 0, 0);
    g_applyGui = !g_guiShown;
}

void RunAddonAutoExec() {
    for (const std::string& line : AutoExecLines()) ExecuteAt(line.c_str());
}

} // namespace console
