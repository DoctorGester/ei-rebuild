// Rebirth's console additions (addon.dll v0.12.4.2) behind their flags (docs/notes/rebirth.md, "Scripting and
// console"): AddNewConsoleCommands ("help", "addon", "execs", "listgvar", "drawall", "nogui") and AutoExec (the
// "Console AutoExec" registry lines, then 2utoexec.cfg, before each of three console executions). The texts are
// addon's; its own state (the two log levels, the GUI flags) lives here.
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
#include "lib/str.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>
#undef GetClassName

namespace console {

namespace {

int* const kSpectator = &options::TheOptions()->drawVisibleOnly_;

// addon's state.
CommandHandler g_gameHelp = nullptr;   // the game's "help", saved at each registration
int g_consoleLogLevel = 1;             // addon's messages to the console
int g_addonLogLevel = 0;               // addon.log
bool g_guiShown = true;                // nogui
bool g_cursorShown = true;
bool g_applyGui = false;               // the battle screen applies g_guiShown

// addon's console output: the text, "\n" (as a string), flush.
void Say(const char* text) { (Out() << text << "\n").Apply(libcrt::FlushManipulator); }
void __cdecl SayFormat(const char* format, ...) {
    std::string text;
    va_list args;
    va_start(args, format);
    text = str::FormatV(format, args);
    va_end(args);
    Say(text.c_str());
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

// ---- The commands (the arguments, lower case) ----------------------------------------------

void __fastcall Help(Arguments* args) {
    g_gameHelp(args);
    Say("ADDON [command]");
    Say("addon-specific commands");
}

void AddonDebug(Arguments* args) {
    std::string type = !args->empty() ? (*args)[0] : "";
    int level = args->size() >= 2 ? std::atoi((*args)[1].c_str()) : -1;
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

void __fastcall Addon(Arguments* args) {
    if (args->empty() || std::strcmp((*args)[0].c_str(), "help") == 0) {
        Say("Welcome to Evil Islands Addon Engine v0.12.4.2\n\nAvailable commands:\n"
            "* addon info          - show detailed information about addon\n"
            "* addon debug [args]  - set addon's debug settings");
    } else if (std::strcmp((*args)[0].c_str(), "info") == 0) {
        SayFormat("Addon core version: 0.12.4.2\nCurrent mod: %s", "No mod is used");
    } else if (std::strcmp((*args)[0].c_str(), "debug") == 0) {
        args->erase(args->begin());
        AddonDebug(args);
    } else {
        SayFormat("Unknown command: %s", (*args)[0].c_str());
    }
}

// execs file: loads a script file into the script loader and runs it.
void __fastcall Execs(Arguments* args) {
    if (TheMissionScript()->LoadFile((*args)[0].c_str())) {
        ScriptTick(TheMissionScript());
        return;
    }
    SayFormat("Script error: line %d: %s", TheMissionScript()->errorLine_, TheMissionScript()->errorText_.c_str());
}

// listgvar: the global script variables (floats, strings, objects, groups).
void __fastcall ListGlobals(Arguments*) {
    const ScriptVars& globals = TheMissionScript()->globals_;
    auto name = [](const mfc::CArray<std::string>& names, int i) { return names.data[i].c_str(); };
    for (int i = 0; i < globals.floats.values.size; ++i)
        SayFormat("%s : float = %f", name(globals.floats.names, i), static_cast<double>(globals.floats.values.data[i]));
    for (int i = 0; i < globals.strings.values.size; ++i)
        SayFormat("%s : string = \"%s\"", name(globals.strings.names, i), name(globals.strings.values, i));
    for (int i = 0; i < globals.objects.values.size; ++i) {
        std::string text = ObjectText(globals.objects.values.data[i]);
        SayFormat("%s : object = %s", name(globals.objects.names, i), text.c_str());
    }
    for (int i = 0; i < globals.groups.values.size; ++i) {
        std::string text = GroupText(globals.groups.values.data[i]);
        SayFormat("%s : group = %s", name(globals.groups.names, i), text.c_str());
    }
}

// drawall n: the draw-all flag becomes (n == 0).
void __fastcall DrawAll(Arguments* args) {
    if (args->size() < 1) return;
    *kSpectator = std::atoi((*args)[0].c_str()) == 0 ? 1 : 0;
}

// nogui 0|1|2: GUI and cursor shown / GUI hidden / both hidden.
void __fastcall NoGui(Arguments* args) {
    int mode = args->size() == 1 ? std::atoi((*args)[0].c_str()) : -1;
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
    std::string key = name;
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
    g_gameHelp = Command(CheatCommands(), mfc::CString("help").GetString());
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
