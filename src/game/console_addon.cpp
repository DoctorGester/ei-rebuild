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
#include "options.h"
#include "ui.h"
#include "script.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ui.h"
#include "game_data.h"
#include "game_singletons.h"
#include "settings_file.h"
#include "system_text.h"
#include "ui_cursor.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>
#undef GetClassName   // CEngineObject::GetClassName

namespace console {

namespace {

// 004D36F0 (the script loader 007C27D4: load a file) and 004BF000 (run what was loaded).
// 006002A0. CUiWindow::SetEnabled(enabled, notify).

// The saved / console script (the world's command script).
int* const kSpectator = &options::TheOptions()->drawVisibleOnly_;

// addon's state.
CommandHandler g_gameHelp = nullptr;   // the game's "help", saved at each registration
int g_consoleLogLevel = 1;             // addon's messages to the console (100BA6F4)
int g_addonLogLevel = 0;               // addon.log (100BD4A4)
bool g_guiShown = true;                // nogui (100BA664)
bool g_cursorShown = true;             // (100BA665)
bool g_applyGui = false;               // the battle screen applies g_guiShown (100BD128)

// addon's console output: the text, "\n" (as a string), flush.
void Say(const char* text) { (Out() << text << "\n").Apply(libcrt::FlushManipulator); }
void __cdecl SayFormat(const char* format, ...) {
    mfc::CString text;
    va_list args;
    va_start(args, format);
    text.FormatV(format, args);
    va_end(args);
    Say(text);
}

// listgvar's object text: "null", or "<" class name ":" id ">".
std::string ObjectText(const ScriptObjectRef& ref) {
    CEngineObject* object = ref.object;
    if (!object) return "null";
    const char* name = object->GetClassName();
    char id[16];
    std::snprintf(id, sizeof(id), "%d", object->id_);
    return std::string("<") + name + ":" + id + ">";
}
// A group: "empty" or the objects joined by ", ".
std::string GroupText(const ScriptGroup& group) {
    int count = group.size;
    if (count == 0) return "empty";
    std::string text;
    for (int i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        text += ObjectText(group.data[i]);
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
    std::string type = args->GetSize() >= 1 ? ((*args)[0]).GetString() : "";
    int level = args->GetSize() >= 2 ? libcrt::Atoi((*args)[1]) : -1;
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
        SayFormat("Unknown command: %s", ((*args)[0]).GetString());
    }
}

// execs file: loads a script file into the script loader and runs it.
void __fastcall Execs(CStringArray* args) {
    if (TheMissionScript()->LoadFile((*args)[0])) {
        ScriptTick(TheMissionScript());
        return;
    }
    SayFormat("Script error: line %d: %s", TheMissionScript()->errorLine_, TheMissionScript()->errorText_.GetString());
}

// listgvar: the global script variables (floats, strings, objects, groups).
void __fastcall ListGlobals(CStringArray*) {
    const ScriptVars& globals = TheMissionScript()->globals_;
    auto name = [](const mfc::CArray<mfc::CString>& names, int i) { return names.data[i].GetString(); };
    for (int i = 0; i < globals.floats.size; ++i)
        SayFormat("%s : float = %f", name(globals.floatNames, i), static_cast<double>(globals.floats.data[i]));
    for (int i = 0; i < globals.strings.size; ++i)
        SayFormat("%s : string = \"%s\"", name(globals.stringNames, i), name(globals.strings, i));
    for (int i = 0; i < globals.objects.size; ++i) {
        std::string text = ObjectText(globals.objects.data[i]);
        SayFormat("%s : object = %s", name(globals.objectNames, i), text.c_str());
    }
    for (int i = 0; i < globals.groups.size; ++i) {
        std::string text = GroupText(globals.groups.data[i]);
        SayFormat("%s : group = %s", name(globals.groupNames, i), text.c_str());
    }
}

// drawall n: 0079BE6C = (n == 0).
void __fastcall DrawAll(CStringArray* args) {
    if (args->GetSize() < 1) return;
    *kSpectator = libcrt::Atoi((*args)[0]) == 0 ? 1 : 0;
}

// nogui 0|1|2: GUI and cursor shown / GUI hidden / both hidden.
void __fastcall NoGui(CStringArray* args) {
    int mode = args->GetSize() == 1 ? libcrt::Atoi((*args)[0]) : -1;
    if (static_cast<unsigned>(mode) > 2) {
        Say("Hide or show GUI in game missions\n\nnogui 0 - show all (default)\nnogui 1 - hide GUI but show cursor\n"
            "nogui 2 - hide GUI and cursor");
        return;
    }
    g_cursorShown = mode < 2;
    g_guiShown = mode == 0;
    g_applyGui = true;
    SayFormat("GUI: %s, cursor: %s", g_guiShown ? "show" : "hide", g_cursorShown ? "show" : "hide");
    TheCursor()->ShowImage(g_cursorShown ? 1 : 0);
}

void Register(CommandMap* map, const char* name, CommandHandler handler) {
    mfc::CString key(name);
    Command(map, key) = handler;
}

// The AutoExec lines, read once (addon reads them when it loads).
const std::vector<std::string>& AutoExecLines() {
    static std::vector<std::string> lines = [] {
        std::vector<std::string> result;
        // The lines as the game holds its text (the ANSI code page, EI_UTF8: UTF-8; settings_file.h's bytes).
        constexpr const char* kStarterSettings = "Software\\Gipat.Ru\\EI_Starter\\Settings";
        DWORD type = 0, size = 0;
        if (settings_file::ReadValue(kStarterSettings, "Console AutoExec", &type, nullptr, &size) == 0 && size != 0) {
            std::string data(size, '\0');
            if (settings_file::ReadValue(kStarterSettings, "Console AutoExec", &type, data.data(), &size) == 0 &&
                type == REG_MULTI_SZ) {
                data.resize(size);
                for (std::size_t at = 0; at < data.size();) {
                    std::size_t end = data.find('\0', at);
                    if (end == std::string::npos) end = data.size();
                    if (end > at) result.push_back(data.substr(at, end - at));
                    at = end + 1;
                }
            }
        }
        DWORD attributes = text::sys::GetFileAttributesA("2utoexec.cfg");
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

const CommandRegistration kAddonCommandRegistrations[6] = {
    {true, "help", Help},
    {true, "addon", Addon},
    {true, "execs", Execs},
    {true, "listgvar", ListGlobals},
    {true, "drawall", DrawAll},
    {false, "nogui", NoGui},
};

void RegisterAddonCommands() {
    g_gameHelp = Command(CheatCommands(), mfc::CString("help"));
    for (const CommandRegistration& r : kAddonCommandRegistrations)
        Register(r.cheat ? CheatCommands() : PublicCommands(), r.name, r.handler);
}

void ApplyAddonGuiVisibility(CUiContainer* screen) {
    if (!g_applyGui) return;
    for (int i = 0; i < screen->children_.size; ++i) screen->children_.data[i]->SetEnabled(g_guiShown ? 1 : 0, 0);
    g_applyGui = !g_guiShown;
}

void RunAddonAutoExec() {
    for (const std::string& line : AutoExecLines()) Execute(line.c_str());
}

} // namespace console
