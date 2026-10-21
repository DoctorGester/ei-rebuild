// The maps folder and its files (A-W14): 0055E990 MapsFolder, 00595BA0 MapPath, 005812F0
// MapFileExists, 005813A0 WriteMapFile, with Rebirth's Maps in the standalone image (mod_folders.h). See
// world_map_files.h and docs/notes/world.md.
#include "world_map_files.h"

#include "mod_folders.h"
#include "net_coder.h"
#include "net_link.h"
#include "world_data.h"

#include "ei/hook.h"

#include <new>

namespace {

const mfc::CString& DataFolder() { return *static_cast<const mfc::CString*>(worlddata::DataPathString()); }
const char* MapsFolderName() { return "maps\\"; }

// Rebirth's Maps (modfolders::SelectMaps): addon names the file before each maps-folder use.
bool ModMaps() { return modfolders::Active(ei::rebirth::Patch::Maps); }

// A file stream on the stack, as the original inlines its constructor and destructor.
struct LocalFileStream {
    LocalFileStream() { stream.Construct(); }
    ~LocalFileStream() { stream.Destruct(); }
    LocalFileStream(const LocalFileStream&) = delete;
    LocalFileStream& operator=(const LocalFileStream&) = delete;
    netlink::FileStream stream;
};

} // namespace

mfc::CString* __stdcall MapsFolder(mfc::CString* out) {
    // Rebirth's Maps (0055E9A9): the folder the last site selected.
    mfc::CString folder(ModMaps() ? modfolders::MapsRoot(DataFolder()) : DataFolder());
    return new (out) mfc::CString(folder + MapsFolderName());
}
EI_REPLACE(0x0055E990, MapsFolder);

mfc::CString* __stdcall MapPath(mfc::CString* out, const char* name) {
    mfc::CString path;
    if (name[1] == ':') {
        path = name;
    } else {
        mfc::CString folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
        MapsFolderAt(&folder);
        path = folder + name;
    }
    return new (out) mfc::CString(path);
}
EI_REPLACE(0x00595BA0, MapPath);

bool __stdcall MapFileExists(const char* name) {
    mfc::CFileFind find;
    mfc::CString folder;
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    MapsFolderAt(&folder);
    mfc::CString path = folder + name;
    return find.FindFile(path, 0) != 0;
}
EI_REPLACE(0x005812F0, MapFileExists);

void __stdcall WriteMapFile(const char* name, GameBuffer* buffer) {
    LocalFileStream file;
    {
        mfc::CString folder;
        modfolders::SelectMaps(name, modfolders::MapsSite::WriteFile);
        MapsFolderAt(&folder);
        mfc::CString path = folder + name;
        file.stream.Open(path, netlink::FileStream::kOpenWrite, 0);
    }
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    file.stream.WriteImpl(buffer->begin, static_cast<std::uint32_t>(buffer->base + (buffer->high - buffer->begin)));
    file.stream.Close();
}
EI_REPLACE(0x005813A0, WriteMapFile);
