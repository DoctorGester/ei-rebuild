// The rest of the resource code: ResourceBase, the sound system's file reader, the archive name
// listing, and the MMIOResource constructors and destructors (ResourceManager's are in
// resource_manager.cpp). See resource_stream.h and docs/notes/models.md.

#include "lib/text_case.h"
#include "resource.h"
#include "resource_stream.h"
#include "mod_folders.h"
#include <cstdint>
#include <cstring>
#include <string>

#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/gameapi.h"

#include <cstdlib>
#include <new>
#include <windows.h>
#include <mmsystem.h>

namespace {

// CRT and library routines of the game.
void SplitPath(const char* path, char* drive, char* dir, char* name, char* ext) {
#pragma warning(suppress : 4996)
    _splitpath(path, drive, dir, name, ext);
}

// The listing's drive and directory tests: an empty pattern matches anything.
bool PartMatches(const char* name, const char* pattern) {
    return pattern[0] == '\0' || WildcardMatch(name, pattern, 0) != 0;
}

} // namespace


ResourceBase::ResourceBase() {
    header = ResHeader{};
    entries = nullptr;
    names = nullptr;
}

ResourceBase::~ResourceBase() {
    header = ResHeader{};
    entries = nullptr;
    names = nullptr;
}





void ResourceStream::Close() {
    if (file != nullptr) gameapi::mmioClose(file, 0);
    file = nullptr;
}

bool ResourceStream::OpenFile(const std::string& name, std::uint32_t* size) {
    Close();
    if (!lib::FileExists(name)) return false;
    WIN32_FIND_DATAA found{};   // the size (the original's _findfirst never closed its search)
    const HANDLE search = gameapi::FindFirstFileA(name.c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    file = gameapi::mmioOpenA(const_cast<char*>(name.c_str()), nullptr, MMIO_DENYWRITE);
    if (file == nullptr) return false;
    if (size != nullptr) *size = found.nFileSizeLow;
    return true;
}

long ResourceStream::Read(char* buffer, long size) {
    if (file == nullptr) return -1;
    return gameapi::mmioRead(file, buffer, size);
}

long ResourceStream::Seek(long offset, int origin) {
    if (file == nullptr) return -1;
    return gameapi::mmioSeek(file, offset, origin == 2 ? SEEK_END : SEEK_SET);
}

// --- Archive listing -----------------------------------------------------------------------------

int WildcardMatch(const char* name, const char* pattern, int emptyPatternResult) {
    if (*pattern == '\0') return *name == '\0' ? 1 : emptyPatternResult;
    while (*name != '\0' && *pattern != '\0') {
        if (*pattern == '*') return 1;
        if (*pattern != '?' && text::ToUpper(*pattern) != text::ToUpper(*name)) return 0;
        ++name;
        ++pattern;
    }
    for (; *pattern != '\0'; ++pattern)
        if (*pattern != '?' && *pattern != '*') return 0;
    return *name == '\0' ? 1 : 0;
}

// Rebirth's ResOverride sends every call to addon.
void MappedResource::ListFiles(std::vector<std::string>* fileNames, const char* pattern) const {
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        modfolders::ListFiles(this, fileNames, pattern);
        return;
    }
    *fileNames = ListFilesPlain(pattern);
}

std::vector<std::string> MappedResource::ListFilesPlain(const char* pattern) const {
    std::vector<std::string> fileNames;
    for (std::uint32_t i = 0; i < header.count; ++i) {
        const ResEntry& entry = entries[i];
        char name[0x400];
        std::memcpy(name, names + entry.nameOffset, entry.nameLength);
        name[entry.nameLength] = '\0';
        if (pattern != nullptr) {
            char nameDrive[4], patternDrive[4];
            char nameDir[0x100], patternDir[0x100];
            char nameBase[0x100], patternBase[0x100];
            char nameExt[0x100], patternExt[0x100];
            SplitPath(name, nameDrive, nameDir, nameBase, nameExt);
            SplitPath(pattern, patternDrive, patternDir, patternBase, patternExt);
            if (!PartMatches(nameDrive, patternDrive) || !PartMatches(nameDir, patternDir)) continue;
            // An empty name pattern only matches an empty name; an empty extension pattern anything.
            if (WildcardMatch(nameBase, patternBase, 0) == 0 || WildcardMatch(nameExt, patternExt, 1) == 0) continue;
        }
        fileNames.push_back(name);
    }
    return fileNames;
}


// The mapped file is constructed as a member (MappedFile's constructor).
MMIOResource::MMIOResource() { mappedImage = nullptr; }

MMIOResource::~MMIOResource() {
    // CloseResource, inlined (is MappedFile::Close's copy).
    file.Close();
    entries = nullptr;
    names = nullptr;
    mappedImage = nullptr;
    header = ResHeader{};
}
