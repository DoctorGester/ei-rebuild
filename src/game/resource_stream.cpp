// The rest of the resource code: CResourceBase, the sound system's file reader, the archive name
// listing, and the CMMIOResource constructors and destructors (CResourceManager's are in
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


CResourceBase::CResourceBase() {
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
}

CResourceBase::~CResourceBase() {
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
}





void CResourceStream::Close() {
    if (file != nullptr) gameapi::mmioClose(file, 0);
    file = nullptr;
}

bool CResourceStream::OpenFile(const std::string& name, std::uint32_t* size) {
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

long CResourceStream::Read(char* buffer, long size) {
    if (file == nullptr) return -1;
    return gameapi::mmioRead(file, buffer, size);
}

long CResourceStream::Seek(long offset, int origin) {
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
void CMappedResource::ListFiles(std::vector<std::string>* names, const char* pattern) const {
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        modfolders::ListFiles(this, names, pattern);
        return;
    }
    *names = ListFilesPlain(pattern);
}

std::vector<std::string> CMappedResource::ListFilesPlain(const char* pattern) const {
    std::vector<std::string> names;
    for (std::uint32_t i = 0; i < header_.count; ++i) {
        const ResEntry& entry = entries_[i];
        char name[0x400];
        std::memcpy(name, names_ + entry.nameOffset, entry.nameLength);
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
        names.push_back(name);
    }
    return names;
}


// The mapped file is constructed as a member (CMappedFile's constructor).
CMMIOResource::CMMIOResource() { mappedImage_ = nullptr; }

CMMIOResource::~CMMIOResource() {
    // CloseResource, inlined (is CMappedFile::Close's copy).
    file_.Close();
    entries_ = nullptr;
    names_ = nullptr;
    mappedImage_ = nullptr;
    header_ = ResHeader{};
}
