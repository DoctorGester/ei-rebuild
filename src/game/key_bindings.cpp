// The key bindings file (the base path 007AF460 + the name at 00787C04): loaded at start
// (00462C10) and saved (00462DF0) on the interface's shutdown. Each line is "<key> <action>". The
// tables are the ones FindCommandByKey (object_tables.cpp) and the options window (ui_options.cpp)
// use: the action of each scan code (0x200 signed bytes at 0079BF70), the keys (0x48-byte entries at
// 007857B0: scan code, the file's name, Windows' display text) and the actions (0x10-byte entries at
// 00787070: id, name, options group and row; the first is a blank id 0). See docs/notes/ui.md
// section 8.
#include "console.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "text_split.h"
#include "world_glue.h"

#include <cstdint>
#include <cstring>

namespace keybind {

namespace {

struct Key {
    int scan;              // the scan code, the index into the action table
    const char* name;      // the file's name ("TAB", "Q", ...)
    char text[0x40];       // GetKeyNameTextA's text (up to 31 bytes), shown by the options window
};
static_assert(sizeof(Key) == 0x48);
struct Action {
    int id;
    const char* name;      // the file's name ("obj", "weapon1", ...)
    std::uint8_t unused_08[8];
};
static_assert(sizeof(Action) == 0x10);

constexpr std::uintptr_t kKeysAddress = std::uintptr_t(0x007857B0);      // the key table, then the actions' table
constexpr std::uintptr_t kActionsAddress = std::uintptr_t(0x00787070);
Key* Keys() { return reinterpret_cast<Key*>(kKeysAddress); }
constexpr int kKeyCount = (kActionsAddress - kKeysAddress) / 0x48;   // 88
Action* Actions() { return reinterpret_cast<Action*>(kActionsAddress); }
constexpr int kActionCount = 60;
signed char* ActionOfScan() { return reinterpret_cast<signed char*>(0x0079BF70); }


// 00700C05: operator+(CString, LPCTSTR).
mfc::CString* __stdcall ConcatText(mfc::CString* a0, const mfc::CString* a1, const char* a2) { return CStringConcatTextOriginal(a0, a1, a2); }

// Opens the bindings file with `flags`; the path is a temporary CString, as in the original.
bool OpenFile(console::CStdioFile& file, unsigned flags) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* path = ConcatText(reinterpret_cast<mfc::CString*>(raw), GameDataPathString(), "config\\keyboard.ini");
    bool opened = file.Open(*path, flags, nullptr) != 0;
    path->~CString();
    return opened;
}

} // namespace

// 00462C10: each key's text from its scan code (empty when Windows has no name), the table
// cleared, then "key action" lines read: both names looked up case-insensitively, the action's id
// stored for the key's scan code (a line whose key is unknown is skipped after the action lookup).
void __cdecl LoadKeyBindings() {
    for (int i = 0; i < kKeyCount; ++i) {
        Key& k = Keys()[i];
        if (!gameapi::GetKeyNameTextA(k.scan << 16, k.text, 0x1F)) k.text[0] = 0;
    }
    mfc::CString line;
    console::CStringArray words;
    console::CStdioFile file;
    std::memset(ActionOfScan(), 0, 0x200);
    if (!OpenFile(file, console::CStdioFile::typeText)) return;
    while (file.ReadString(line)) {
        text::SplitWords(line, &words, ' ');
        if (words.GetSize() <= 1) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (libcrt::Stricmp(Keys()[key].name, words[0]) == 0) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (libcrt::Stricmp(Actions()[action].name, words[1]) != 0) continue;
            if (known) ActionOfScan()[Keys()[key].scan] = static_cast<signed char>(Actions()[action].id);
            break;
        }
    }
    file.Close();
}
EI_REPLACE(0x00462C10, LoadKeyBindings);

// 00462DF0: one line per scan code with an action, when both the key and the action are known
// (format 00787C18: key name, action name).
void __cdecl SaveKeyBindings() {
    mfc::CString line;
    console::CStdioFile file;
    if (!OpenFile(file, console::CStdioFile::modeCreate | console::CStdioFile::modeWrite)) return;
    for (int scan = 0; scan < 0x200; ++scan) {
        signed char id = ActionOfScan()[scan];
        if (!id) continue;
        bool known = false;
        int key = 0;
        for (; key < kKeyCount; ++key) {
            if (Keys()[key].scan == scan) {
                known = true;
                break;
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            if (Actions()[action].id != id) continue;
            if (known) {
                line.Format("%s %s\n", Keys()[key].name, Actions()[action].name);
                file.WriteString(line);
            }
            break;
        }
    }
    file.Close();
}
EI_REPLACE(0x00462DF0, SaveKeyBindings);

} // namespace keybind
