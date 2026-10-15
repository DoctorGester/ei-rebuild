// The key bindings file (the base path 007AF460 + the name at 00787C04): loaded at start
// (00462C10) and saved (00462DF0). The tables are the ones FindCommandByKey (object_tables.cpp)
// reads: the bound key of each command slot (0x200 signed bytes at 0079BF70), the commands
// (0x48-byte entries at 007857B0: slot, name, the key's display text) and the key names (0x10-byte
// entries at 00787070: code, name). See docs/notes/app.md.
#include "console.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "world_glue.h"

#include <cstdint>
#include <cstring>

namespace keybind {

namespace {

struct Command {
    int slot;              // also the scan code whose key name is shown
    const char* name;
    char keyText[0x40];    // GetKeyNameTextA's text (up to 31 bytes)
};
static_assert(sizeof(Command) == 0x48);
struct KeyName {
    int code;
    const char* name;
    std::uint8_t unused_08[8];
};
static_assert(sizeof(KeyName) == 0x10);

Command* Commands() { return reinterpret_cast<Command*>(0x007857B0); }
constexpr int kCommandCount = (0x00787070 - 0x007857B0) / 0x48;   // 88
KeyName* KeyNames() { return reinterpret_cast<KeyName*>(0x00787070); }
constexpr int kKeyNameCount = 60;
signed char* Bindings() { return reinterpret_cast<signed char*>(0x0079BF70); }


// 00700C05: operator+(CString, LPCTSTR).
mfc::CString* __stdcall ConcatText(mfc::CString* a0, const mfc::CString* a1, const char* a2) { return CStringConcatTextOriginal(a0, a1, a2); }

// Opens the bindings file with `flags`; the path is a temporary CString, as in the original.
bool OpenFile(console::CStdioFile& file, unsigned flags) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* path = ConcatText(reinterpret_cast<mfc::CString*>(raw), GameDataPathString(), game::Text(0x00787C04));
    bool opened = file.Open(*path, flags, nullptr) != 0;
    path->~CString();
    return opened;
}

} // namespace

// 00462C10: each command's key text from its scan code (empty when Windows has no name), the
// bindings cleared, then "command key" lines read: both names looked up case-insensitively, the
// key's code stored for the command's slot.
void __cdecl LoadKeyBindings() {
    for (int i = 0; i < kCommandCount; ++i) {
        Command& c = Commands()[i];
        if (!gameapi::GetKeyNameTextA(c.slot << 16, c.keyText, 0x1F)) c.keyText[0] = 0;
    }
    mfc::CString line;
    console::CStringArray words;
    console::CStdioFile file;
    std::memset(Bindings(), 0, 0x200);
    if (!OpenFile(file, console::CStdioFile::typeText)) return;
    while (file.ReadString(line)) {
        SplitWords(line, &words, ' ');
        if (words.GetSize() <= 1) continue;
        bool known = false;
        int command = 0;
        for (; command < kCommandCount; ++command) {
            if (gamecrt::stricmp(Commands()[command].name, words[0]) == 0) {
                known = true;
                break;
            }
        }
        for (int key = 0; key < kKeyNameCount; ++key) {
            if (gamecrt::stricmp(KeyNames()[key].name, words[1]) != 0) continue;
            if (known) Bindings()[Commands()[command].slot] = static_cast<signed char>(KeyNames()[key].code);
            break;
        }
    }
    file.Close();
}
EI_REPLACE(0x00462C10, LoadKeyBindings);

// 00462DF0: one line per bound slot whose command and key are both known (format 00787C18:
// command name, key name).
void __cdecl SaveKeyBindings() {
    mfc::CString line;
    console::CStdioFile file;
    if (!OpenFile(file, console::CStdioFile::modeCreate | console::CStdioFile::modeWrite)) return;
    for (int slot = 0; slot < 0x200; ++slot) {
        signed char code = Bindings()[slot];
        if (!code) continue;
        bool known = false;
        int command = 0;
        for (; command < kCommandCount; ++command) {
            if (Commands()[command].slot == slot) {
                known = true;
                break;
            }
        }
        for (int key = 0; key < kKeyNameCount; ++key) {
            if (KeyNames()[key].code != code) continue;
            if (known) {
                line.Format(game::Text(0x00787C18), Commands()[command].name, KeyNames()[key].name);
                file.WriteString(line);
            }
            break;
        }
    }
    file.Close();
}
EI_REPLACE(0x00462DF0, SaveKeyBindings);

} // namespace keybind
