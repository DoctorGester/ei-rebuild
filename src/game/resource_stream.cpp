// The rest of the resource code: ResourceBase, the sound system's file reader, the archive name
// listing, and the MMIOResource constructors and destructors (ResourceManager's are in
// resource_manager.cpp). See resource_stream.h and docs/notes/models.md.

#include "resource_stream.h"

#include "mod_folders.h"
#include "platform/platform.h"
#include "resource.h"

#include "lib/crt.h"
#include "lib/path.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

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





void ResourceStream::Close() { file.Close(); }

bool ResourceStream::OpenFile(const std::string& name, std::uint32_t* size) {
    Close();
    if (!platform::FileExists(name)) return false;
    if (!file.Open(name, platform::FileMode::Read)) return false;
    if (size != nullptr) *size = file.Size();
    return true;
}

long ResourceStream::Read(char* buffer, long size) {
    if (!file.IsOpen()) return -1;
    try {
        return static_cast<long>(file.Read(buffer, static_cast<unsigned>(size)));
    } catch (const platform::FileError&) {
        return -1;
    }
}

long ResourceStream::Seek(long offset, int origin) {
    if (!file.IsOpen()) return -1;
    try {
        return file.Seek(offset, origin == 2 ? platform::SeekFrom::End : platform::SeekFrom::Begin);
    } catch (const platform::FileError&) {
        return -1;
    }
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
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) {
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
            lib::SplitPath(name, nameDrive, nameDir, nameBase, nameExt);
            lib::SplitPath(pattern, patternDrive, patternDir, patternBase, patternExt);
            if (!PartMatches(nameDrive, patternDrive) || !PartMatches(nameDir, patternDir)) continue;
            // An empty name pattern only matches an empty name; an empty extension pattern anything.
            if (WildcardMatch(nameBase, patternBase, 0) == 0 || WildcardMatch(nameExt, patternExt, 1) == 0) continue;
        }
        fileNames.push_back(name);
    }
    return fileNames;
}


MMIOResource::MMIOResource() { mappedImage = nullptr; }

MMIOResource::~MMIOResource() {
    // CloseResource, inlined.
    file.Close();
    entries = nullptr;
    names = nullptr;
    mappedImage = nullptr;
    header = ResHeader{};
}
