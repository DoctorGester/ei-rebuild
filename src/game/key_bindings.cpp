// The key bindings file (the base path + the name): loaded at start and saved on the interface's shutdown.
// Each line is "<key> <action>". The tables are the ones FindCommandByKey and the options window use: the
// action of each scan code (0x200 signed bytes, g_keyActions), the keys (0x48-byte entries at Scan code, the
// file's name, Windows' display text) and the actions (0x10-byte entries at Id, name, options group and row;
// the first is a blank id 0). See docs/notes/ui.md section 8.
#include "console.h"

#include "platform/platform.h"
#include <cstring>
#include <string>
#include "lib/str.h"
#include "lib/text_case.h"
#include "text_split.h"
#include "ui.h"
#include "world_glue.h"
#include "game_singletons.h"

#include <cstdint>
#include <memory>
#include <format>

// The keys (object_tables.h's UiKeyName): scan code, the bindings file's name and Windows' text
// (LoadKeyBindings fills it). The actions' table (kActionDefinitions) follows it in memory.
UiKeyName g_keyNames[88] = /*007857B0*/ {
    {2, "1", ""}, {3, "2", ""}, {4, "3", ""}, {5, "4", ""}, {6, "5", ""}, {7, "6", ""}, {8, "7", ""}, {9, "8", ""},
    {10, "9", ""}, {11, "0", ""}, {12, "-", ""}, {13, "+", ""}, {15, "TAB", ""}, {16, "Q", ""}, {17, "W", ""},
    {18, "E", ""}, {19, "R", ""}, {20, "T", ""}, {21, "Y", ""}, {22, "U", ""}, {23, "I", ""}, {24, "O", ""},
    {25, "P", ""}, {26, "[", ""}, {27, "]", ""}, {30, "A", ""}, {31, "S", ""}, {32, "D", ""}, {33, "F", ""},
    {34, "G", ""}, {35, "H", ""}, {36, "J", ""}, {37, "K", ""}, {38, "L", ""}, {39, ";", ""}, {40, "'", ""},
    {43, "\\", ""}, {44, "Z", ""}, {45, "X", ""}, {46, "C", ""}, {47, "V", ""}, {48, "B", ""}, {49, "N", ""},
    {50, "M", ""}, {51, ",", ""}, {52, ".", ""}, {53, "/", ""}, {55, "KP_STAR", ""}, {57, "SPACE", ""},
    {58, "CAPSLOCK", ""}, {59, "F1", ""}, {60, "F2", ""}, {61, "F3", ""}, {62, "F4", ""}, {63, "F5", ""},
    {64, "F6", ""}, {65, "F7", ""}, {66, "F8", ""}, {67, "F9", ""}, {68, "F10", ""}, {69, "PAUSE", ""},
    {71, "KP_HOME", ""}, {72, "KP_UP", ""}, {73, "KP_PGUP", ""}, {74, "KP_MINUS", ""}, {75, "KP_LEFT", ""},
    {76, "KP_CENTER", ""}, {77, "KP_RIGHT", ""}, {78, "KP_PLUS", ""}, {79, "KP_END", ""}, {80, "KP_DOWN", ""},
    {81, "KP_PGDN", ""}, {82, "KP_INS", ""}, {83, "KP_DEL", ""}, {87, "F11", ""}, {88, "F12", ""},
    {309, "KP_SLASH", ""}, {325, "KP_NUMLOCK", ""}, {327, "HOME", ""}, {328, "UP", ""}, {329, "PGUP", ""},
    {331, "LEFT", ""}, {333, "RIGHT", ""}, {335, "END", ""}, {336, "DOWN", ""}, {337, "PGDN", ""}, {338, "INS", ""},
    {339, "DEL", ""}
};

namespace keybind {

namespace {

constexpr int kKeyCount = kUiKeyNameCount;
constexpr int kActionCount = 60;



} // namespace

// Each key's text from its scan code (empty when Windows has no name), the table
// cleared, then "key action" lines read: both names looked up case-insensitively, the action's id
// stored for the key's scan code (a line whose key is unknown is skipped after the action lookup).
void LoadKeyBindings() {
    for (int i = 0; i < kKeyCount; ++i) {
        UiKeyName& k = g_keyNames[i];
        if (!platform::KeyName(k.scan, k.name, 0x1F)) k.name[0] = 0;
    }
    std::vector<std::string> words;
    std::string bytes;
    std::memset(g_keyActions, 0, 0x200);
    if (!platform::ReadFile(g_dataFolder + "config\\keyboard.ini", &bytes)) return;
    for (const std::string& line : str::Lines(bytes)) {
        words = text::SplitWords(line, ' ');
        if (words.size() <= 1) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (text::CompareIgnoringCase(g_keyNames[key].file, words[0]) == 0) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (text::CompareIgnoringCase(kActionDefinitions[action].name, words[1]) != 0) continue;
            if (known) g_keyActions[g_keyNames[key].scan] = static_cast<signed char>(kActionDefinitions[action].id);
            break;
        }
    }
}

// One line per scan code with an action, when both the key and the action are known:
// key name, action name.
void SaveKeyBindings() {
    platform::File file;
    if (!file.Open(g_dataFolder + "config\\keyboard.ini", platform::FileMode::Create)) return;
    for (int scan = 0; scan < 0x200; ++scan) {
        signed char id = g_keyActions[scan];
        if (!id) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (g_keyNames[key].scan == scan) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (kActionDefinitions[action].id != id) continue;
            if (known) file.WriteLine(std::format("{} {}", g_keyNames[key].file, kActionDefinitions[action].name));
            break;
        }
    }
    file.Close();
}

} // namespace keybind
