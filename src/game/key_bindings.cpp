// The key bindings file (the base path + the name): loaded at start and saved on the interface's shutdown.
// Each line is "<key> <action>". The tables are the ones FindCommandByKey and the options window use: the
// action of each scan code (0x200 signed bytes, g_keyActions), the keys (0x48-byte entries at Scan code, the
// file's name, Windows' display text) and the actions (0x10-byte entries at Id, name, options group and row;
// the first is a blank id 0). See docs/notes/ui.md section 8.
#include "console.h"
#include "game_strings.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "text_split.h"
#include "ui.h"
#include "world_glue.h"
#include "game_data.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

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
signed char* ActionOfScan() { return g_keyActions; }


mfc::CString* __stdcall ConcatText(mfc::CString* a0, const mfc::CString* a1, const char* a2) { return std::construct_at(a0, *a1 + a2); }

// Opens the bindings file with `flags`; the path is a temporary CString.
bool OpenFile(console::CStdioFile& file, unsigned flags) {
    mfc::CString path(mfc::CString::kUnset);
    return file.Open(*ConcatText(&path, GameDataPathString(), "config\\keyboard.ini"), flags, nullptr) != 0;
}

} // namespace

// Each key's text from its scan code (empty when Windows has no name), the table
// cleared, then "key action" lines read: both names looked up case-insensitively, the action's id
// stored for the key's scan code (a line whose key is unknown is skipped after the action lookup).
void __cdecl LoadKeyBindings() {
    for (int i = 0; i < kKeyCount; ++i) {
        UiKeyName& k = g_keyNames[i];
        if (!gameapi::GetKeyNameTextA(k.scan_ << 16, k.name_, 0x1F)) k.name_[0] = 0;
    }
    mfc::CString line;
    std::vector<std::string> words;
    console::CStdioFile file;
    std::memset(ActionOfScan(), 0, 0x200);
    if (!OpenFile(file, console::CStdioFile::typeText)) return;
    while (file.ReadString(line)) {
        text::SplitWords(line, &words, ' ');
        if (words.size() <= 1) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (libcrt::Stricmp(g_keyNames[key].file_, words[0].c_str()) == 0) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (libcrt::Stricmp(kActionDefinitions[action].name_, words[1].c_str()) != 0) continue;
            if (known) ActionOfScan()[g_keyNames[key].scan_] = static_cast<signed char>(kActionDefinitions[action].id_);
            break;
        }
    }
    file.Close();
}

// One line per scan code with an action, when both the key and the action are known:
// key name, action name.
void __cdecl SaveKeyBindings() {
    std::string line;
    console::CStdioFile file;
    if (!OpenFile(file, console::CStdioFile::modeCreate | console::CStdioFile::modeWrite)) return;
    for (int scan = 0; scan < 0x200; ++scan) {
        signed char id = ActionOfScan()[scan];
        if (!id) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (g_keyNames[key].scan_ == scan) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (kActionDefinitions[action].id_ != id) continue;
            if (known) {
                line = str::Format("%s %s\n", g_keyNames[key].file_, kActionDefinitions[action].name_);
                file.WriteString(line.c_str());
            }
            break;
        }
    }
    file.Close();
}

} // namespace keybind
