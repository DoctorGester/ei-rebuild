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

std::string* MapsFolder(std::string* out) {
    // Rebirth's Maps: the folder the last site selected.
    std::string folder(ModMaps() ? modfolders::MapsRoot(TheDataFolder()) : TheDataFolder());
    *out = folder + MapsFolderName(); return out;
}

std::string* MapPath(std::string* out, const char* name) {
    std::string path;
    if (name[1] == ':') {
        path = name;
    } else {
        std::string folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
        MapsFolder(&folder);
        path = folder + name;
    }
    *out = path; return out;
}

bool MapFileExists(const char* name) {
    mfc::CFileFind find;
    std::string folder;
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    MapsFolder(&folder);
    std::string path = folder + name;
    return find.FindFile(path, 0) != 0;
}

void WriteMapFile(const char* name, GameBuffer* buffer) {
    netlink::FileStream file;
    {
        std::string folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::WriteFile);
        MapsFolder(&folder);
        std::string path = folder + name;
        file.Open(path, netlink::FileStream::kOpenWrite, 0);
    }
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    file.Write(buffer->begin, static_cast<std::uint32_t>(buffer->base + (buffer->high - buffer->begin)));
    file.Close();
}
