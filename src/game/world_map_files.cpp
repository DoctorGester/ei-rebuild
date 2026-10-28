// The maps folder and its files (MapsFolder, MapPath, MapFileExists, WriteMapFile), with Rebirth's Maps in
// the standalone image (mod_folders.h). See world_map_files.h and docs/notes/world.md.
#include "world_map_files.h"

#include "mod_folders.h"
#include "net_coder.h"
#include "net_link.h"
#include "world_data.h"

#include "game_singletons.h"

#include <memory>
#include <new>
#include <string>

namespace {

const char* MapsFolderName() { return "maps\\"; }

// Rebirth's Maps (modfolders::SelectMaps): addon names the file before each maps-folder use.
bool ModMaps() { return modfolders::Active(ei::rebirth::Patch::Maps); }

} // namespace

mfc::CString* __stdcall MapsFolder(mfc::CString* out) {
    // Rebirth's Maps: the folder the last site selected.
    mfc::CString folder(ModMaps() ? modfolders::MapsRoot(TheDataFolder()) : TheDataFolder());
    return std::construct_at(out, folder + MapsFolderName());
}

mfc::CString* __stdcall MapPath(mfc::CString* out, const char* name) {
    std::string path;
    if (name[1] == ':') {
        path = name;
    } else {
        mfc::CString folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
        MapsFolder(&folder);
        path = folder + name;
    }
    return std::construct_at(out, path);
}

bool __stdcall MapFileExists(const char* name) {
    mfc::CFileFind find;
    mfc::CString folder;
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    MapsFolder(&folder);
    std::string path = (folder + name).GetString();
    return find.FindFile(path, 0) != 0;
}

void __stdcall WriteMapFile(const char* name, GameBuffer* buffer) {
    netlink::FileStream file;
    {
        mfc::CString folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::WriteFile);
        MapsFolder(&folder);
        std::string path = (folder + name).GetString();
        file.Open(path, netlink::FileStream::kOpenWrite, 0);
    }
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    file.Write(buffer->begin, static_cast<std::uint32_t>(buffer->base + (buffer->high - buffer->begin)));
    file.Close();
}
