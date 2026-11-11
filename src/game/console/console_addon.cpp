// Rebirth's console additions (addon.dll v0.12.4.2) behind their flags (docs/notes/rebirth.md, "Scripting and
// console"): AddNewConsoleCommands ("help", "addon", "execs", "listgvar", "drawall", "nogui") and AutoExec (the
// "Console AutoExec" registry lines, then 2utoexec.cfg, before each of three console executions). The texts are
// addon's; its own state (the two log levels, the GUI flags) lives here.
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "engine/data/options.h"
#include "game/script/script.h"
#include "engine/data/settings.h"
#include "platform/platform.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"


#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace console {

namespace {

int* const kSpectator = &options::g_options.drawVisibleOnly;

// addon's state.
int g_consoleLogLevel = 1;             // addon's messages to the console
int g_addonLogLevel = 0;               // addon.log
bool g_guiShown = true;                // nogui
bool g_cursorShown = true;
bool g_applyGui = false;               // the battle screen applies g_guiShown

// addon's console output: the text, "\n" (as a string), flush.
void Say(std::string_view text) { (g_consoleOut << text << "\n").Flush(); }
EI_PRINTF_FORMAT(1, 2) void SayFormat(const char* format, ...) {
    std::string text;
    va_list args;
    va_start(args, format);
    text = str::FormatV(format, args);
    va_end(args);
    Say(text);
}
template <class... Args>
    requires libprintf::HasClass<Args...>
void SayFormat(const char* format, Args&&... args) {
    SayFormat(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}

// listgvar's object text: "null", or "<" class name ":" id ">".
std::string ObjectText(const ScriptObjectRef& ref) {
    EngineObject* object = ref.object;
    if (!object) return "null";
    const char* name = object->ClassName();
    char id[16];
    std::snprintf(id, sizeof(id), "%d", object->id);
    return std::string("<") + name + ":" + id + ">";
}
// A group: "empty" or the objects joined by ", ".
std::string GroupText(const ScriptGroup& group) {
    if (group.empty()) return "empty";
    std::string text;
    for (const ScriptObjectRef& member : group) {
        if (!text.empty()) text += ", ";
        text += ObjectText(member);
    }
    return text;
}

// ---- The commands (the arguments, lower case) ----------------------------------------------

void Help(const CommandArgs& args) {
    commands::Help(args);
    Say("ADDON [command]");
    Say("addon-specific commands");
}

void AddonDebug(const CommandArgs& args) {
    std::string type = !args.empty() ? args[0] : "";
    str::MakeLower(type);
    int level = args.size() >= 2 ? str::ToInt(args[1]) : -1;
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
        SayFormat("Unknown debug type: %s", type);
    }
}

void Addon(const CommandArgs& args) {
    if (args.empty() || text::CompareIgnoringCase(args[0], "help") == 0) {
        Say("Welcome to Evil Islands Addon Engine v0.12.4.2\n\nAvailable commands:\n"
            "* addon info          - show detailed information about addon\n"
            "* addon debug [args]  - set addon's debug settings");
    } else if (text::CompareIgnoringCase(args[0], "info") == 0) {
        SayFormat("Addon core version: 0.12.4.2\nCurrent mod: %s", "No mod is used");
    } else if (text::CompareIgnoringCase(args[0], "debug") == 0) {
        AddonDebug(CommandArgs(args.begin() + 1, args.end()));
    } else {
        SayFormat("Unknown command: %s", args[0]);
    }
}

// execs file: loads a script file into the script loader and runs it.
void Execs(const CommandArgs& args) {
    if (args.empty()) return;
    if (TheMissionScript()->LoadFile(args[0])) {
        TheMissionScript()->Tick();
        return;
    }
    SayFormat("Script error: line %d: %s", TheMissionScript()->errorLine, TheMissionScript()->errorText);
}

// listgvar: the global script variables (floats, strings, objects, groups).
void ListGlobals(const CommandArgs&) {
    const ScriptVars globals = TheMissionScript()->GlobalValues();
    auto name = [](const std::vector<std::string>& names, int i) -> const std::string& { return names[static_cast<std::size_t>(i)]; };
    for (int i = 0; i < std::ssize(globals.floats.values); ++i)
        SayFormat("%s : float = %f", name(globals.floats.names, i), static_cast<double>(globals.floats.values[i]));
    for (int i = 0; i < std::ssize(globals.strings.values); ++i)
        SayFormat("%s : string = \"%s\"", name(globals.strings.names, i), name(globals.strings.values, i));
    for (int i = 0; i < std::ssize(globals.objects.values); ++i) {
        std::string text = ObjectText(globals.objects.values[i]);
        SayFormat("%s : object = %s", name(globals.objects.names, i), text);
    }
    for (int i = 0; i < std::ssize(globals.groups.values); ++i) {
        std::string text = GroupText(globals.groups.values[i]);
        SayFormat("%s : group = %s", name(globals.groups.names, i), text);
    }
}

// drawall n: the draw-all flag becomes (n == 0).
void DrawAll(const CommandArgs& args) {
    if (args.empty()) return;
    *kSpectator = str::ToInt(args[0]) == 0 ? 1 : 0;
}

// nogui 0|1|2: GUI and cursor shown / GUI hidden / both hidden.
void NoGui(const CommandArgs& args) {
    int mode = args.size() == 1 ? str::ToInt(args[0]) : -1;
    if (static_cast<unsigned>(mode) > 2) {
        Say("Hide or show GUI in game missions\n\nnogui 0 - show all (default)\nnogui 1 - hide GUI but show cursor\n"
            "nogui 2 - hide GUI and cursor");
        return;
    }
    g_cursorShown = mode < 2;
    g_guiShown = mode == 0;
    g_applyGui = true;
    SayFormat("GUI: %s, cursor: %s", g_guiShown ? "show" : "hide", g_cursorShown ? "show" : "hide");
    g_cursor.ShowImage(g_cursorShown);
}

// The AutoExec lines, read once (addon reads them when it loads).
const std::vector<std::string>& AutoExecLines() {
    static std::vector<std::string> lines = [] {
        // The settings file's [autoexec] values (addon's "Console AutoExec" lines of the starter's), empty ones left out.
        std::vector<std::string> result;
        for (std::string& line : settings::GetValues(settings::kAutoExec))
            if (!line.empty()) result.push_back(std::move(line));
        std::string bytes;
        if (platform::ReadWholeFile("2utoexec.cfg", &bytes))
            for (std::string& line : str::Lines(bytes)) result.push_back(std::move(line));
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

void ApplyAddonGuiVisibility(UiContainer* screen) {
    if (!g_applyGui) return;
    for (int i = 0; i < std::ssize(screen->children); ++i) screen->children[i]->SetEnabled(g_guiShown);
    g_applyGui = !g_guiShown;
}

void RunAddonAutoExec() {
    for (const std::string& line : AutoExecLines()) Execute(line);
}

} // namespace console
