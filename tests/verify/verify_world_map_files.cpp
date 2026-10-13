// Checks for the maps folder functions (src/game/world_map_files.cpp, A-W14) and the figure
// manager's Create. Rebirth patches all of them, so they are hooked (and checked) in the vanilla
// launch only.
// - MapsFolder (0055E990), MapPath (00595BA0): PUSH -1; PUSH handler (7 bytes). Differential: the
//   original builds its CString in scratch storage, ours in the caller's; the texts are compared.
//   Reached by every zone load (the server's map, objects and AI graph paths; the client's map):
//   newgame and every scenario after it.
// - MapFileExists (005812F0; 7 bytes the same): differential, both ask the file system. Reached by
//   the network client's file message (ClientOnFile, 0041EAC0): a multiplayer client only, no
//   scenario.
// - WriteMapFile (005813A0), ours only (it writes a file): the buffer's high mark is not below its
//   cursor afterwards. The client's file chunk handler (0041ECC0): a multiplayer client only, no
//   scenario.
// - CFigureManager::CreateWithArchives (005AA510), ours only (it opens archives): returns 1 and
//   the manager then has the menus' "menus.reg". Reached when a world's managers are created
//   (WorldCreateManagers): the start of the game and every new game.
#include "verify.h"

#include "../../src/game/figure_manager.h"
#include "../../src/game/net_coder.h"
#include "../../src/game/world_map_files.h"

#include <cstring>

namespace {
void* s_unused;

// A CString a game function constructs into raw storage.
struct BuiltText {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* text = nullptr;
    ~BuiltText() {
        if (text) text->~CString();
    }
    mfc::CString* Storage() { return reinterpret_cast<mfc::CString*>(storage); }
    const char* Chars() const { return text ? static_cast<const char*>(*text) : "(none)"; }
};

mfc::CString*(__stdcall* s_mapsFolder)(mfc::CString*);
mfc::CString* __stdcall MapsFolderVerify(mfc::CString* out) {
    BuiltText expected;
    expected.text = s_mapsFolder(expected.Storage());
    mfc::CString* result = MapsFolder(out);
    ei::verify::Check("MapsFolder", true,
                      expected.text == expected.Storage() && result == out && std::strcmp(expected.Chars(), *result) == 0,
                      "'%s', expected '%s'", static_cast<const char*>(*result), expected.Chars());
    return result;
}

mfc::CString*(__stdcall* s_mapPath)(mfc::CString*, const char*);
mfc::CString* __stdcall MapPathVerify(mfc::CString* out, const char* name) {
    BuiltText expected;
    expected.text = s_mapPath(expected.Storage(), name);
    mfc::CString* result = MapPath(out, name);
    ei::verify::Check("MapPath", true,
                      expected.text == expected.Storage() && result == out && std::strcmp(expected.Chars(), *result) == 0,
                      "%s: '%s', expected '%s'", name, static_cast<const char*>(*result), expected.Chars());
    return result;
}

bool(__stdcall* s_mapFileExists)(const char*);
bool __stdcall MapFileExistsVerify(const char* name) {
    bool expected = s_mapFileExists(name);
    bool result = MapFileExists(name);
    ei::verify::Check("MapFileExists", expected, result, "%s", name);
    return result;
}

void __stdcall WriteMapFileVerify(const char* name, GameBuffer* buffer) {
    WriteMapFile(name, buffer);
    ei::verify::Check("WriteMapFile", true, buffer->high >= buffer->cur, "%s: %d bytes", name,
                      static_cast<int>(buffer->high - buffer->begin));
}

int __fastcall CreateWithArchivesVerify(CFigureManager* self) {
    int result = self->CreateWithArchives();
    ei::verify::Check("CFigureManager::CreateWithArchives", true, result == 0 || (result == 1 && self->HasFile("menus.reg")),
                      "result %d", result);
    return result;
}

} // namespace

EI_VERIFY(0x0055E990, MapsFolderVerify, 7, s_mapsFolder);
EI_VERIFY(0x00595BA0, MapPathVerify, 7, s_mapPath);
EI_VERIFY(0x005812F0, MapFileExistsVerify, 7, s_mapFileExists);
EI_VERIFY(0x005813A0, WriteMapFileVerify, 0, s_unused);
EI_VERIFY(0x005AA510, CreateWithArchivesVerify, 0, s_unused);
