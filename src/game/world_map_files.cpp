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
#include "platform/file.h"

namespace {

const char* MapsFolderName() { return "maps\\"; }

// Rebirth's Maps (modfolders::SelectMaps): addon names the file before each maps-folder use.
bool ModMaps() { return modfolders::Active(ei::rebirth::Patch::Maps); }

} // namespace

std::string MapsFolder() {
    // Rebirth's Maps: the folder the last site selected.
    std::string folder(ModMaps() ? modfolders::MapsRoot(g_dataFolder) : g_dataFolder);
    return folder + MapsFolderName();
}

std::string MapPath(std::string_view name) {
    if (name.size() > 1 && name[1] == ':') return std::string(name);
    modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
    std::string path = MapsFolder();
    path += name;
    return path;
}

bool MapFileExists(const char* name) {
    platform::FileFind find;
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    std::string path = MapsFolder() + name;
    return find.FindFile(path) != 0;
}

void WriteMapFile(const char* name, GameBuffer* buffer) {
    netlink::FileStream file;
    {
        modfolders::SelectMaps(name, modfolders::MapsSite::WriteFile);
        std::string path = MapsFolder() + name;
        file.Open(path, netlink::FileStream::kOpenWrite, 0);
    }
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    file.Write(buffer->begin, static_cast<std::uint32_t>(buffer->base + (buffer->high - buffer->begin)));
    file.Close();
}
