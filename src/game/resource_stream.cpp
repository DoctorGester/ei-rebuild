// The rest of the resource code: CResourceBase, the sound system's file reader, the archive name
// listing, and the CMMIOResource constructors and destructors (CResourceManager's are in
// resource_manager.cpp). See resource_stream.h and docs/notes/range_660.md.

#include "resource_stream.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_library.h"

#include <cstring>

namespace {

// CRT and MFC routines of the game.
__declspec(naked) int __cdecl Access(const char* /*path*/, int /*mode*/) EI_ORIGINAL(0x006EAF20)
// _findfirst: the game never closes the handle it returns.
__declspec(naked) long __cdecl FindFirst(const char* /*path*/, void* /*findData*/) EI_ORIGINAL(0x006EB832)
__declspec(naked) void __cdecl SplitPath(const char* /*path*/, char* /*drive*/, char* /*dir*/, char* /*name*/,
                                         char* /*ext*/) EI_ORIGINAL(0x006E8BEE)
void __fastcall StringArraySetAtGrow(void* a0, void*, int a2, const char* a3) { StringArraySetAtGrowTextOriginal(a0, nullptr, a2, a3); }

// CMappedResource::FindEntry (resource.cpp), through its game address.
__declspec(naked) int __fastcall FindEntryAt(const void* /*resource*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00674850)

// _finddata_t (0x118 bytes): attrib, three times, size at +0x10, name.
struct FindData {
    std::uint32_t attrib;
    std::uint32_t times[3];
    std::uint32_t size;
    char name[260];
};
static_assert(sizeof(FindData) == 0x118);

constexpr std::uintptr_t kResourceBaseVtable = 0x00743324;
constexpr std::uintptr_t kResourceStreamVtable = 0x00743320;
constexpr std::uintptr_t kMappedResourceVtable = 0x007432E0;
constexpr std::uintptr_t kMmioResourceVtable = 0x00743300;

void SetVtable(void* object, std::uintptr_t vtable) { *static_cast<std::uintptr_t*>(object) = vtable; }

// The listing's drive and directory tests: an empty pattern matches anything.
bool PartMatches(const char* name, const char* pattern) {
    return pattern[0] == '\0' || WildcardMatch(name, pattern, 0) != 0;
}

} // namespace

// --- CResourceBase -------------------------------------------------------------------------------

CResourceBase* CResourceBase::Construct() {
    SetVtable(this, kResourceBaseVtable);
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
    accelTable_ = nullptr;
    accelCount_ = 0;
    return this;
}

void CResourceBase::Destruct() {
    SetVtable(this, kResourceBaseVtable);
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
    if (accelTable_ != nullptr) {
        gamecrt::operator_delete(accelTable_);
        accelTable_ = nullptr;
        accelCount_ = 0;
    }
}

CResourceBase* CResourceBase::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CResourceBase::FreeAccelTable() {
    if (accelTable_ != nullptr) {
        gamecrt::operator_delete(accelTable_);
        accelTable_ = nullptr;
        accelCount_ = 0;
    }
}

// --- CResourceStream -----------------------------------------------------------------------------

CResourceStream* CResourceStream::Construct() {
    CResourceBase::Construct();
    SetVtable(this, kResourceStreamVtable);
    archive_ = nullptr;
    file_ = nullptr;
    offset_ = 0;
    size_ = 0;
    return this;
}

void CResourceStream::Destruct() {
    SetVtable(this, kResourceStreamVtable);
    Close();
    CResourceBase::Destruct();
}

CResourceStream* CResourceStream::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CResourceStream::Close() {
    if (offset_ != 0) {
        offset_ = 0;
        size_ = 0;
        if (file_ != nullptr) {
            gameapi::mmioClose(static_cast<HMMIO>(file_), 0);
            file_ = nullptr;
        }
    }
    if (entries_ != nullptr) {
        gamecrt::operator_delete(entries_);
        entries_ = nullptr;
    }
    if (names_ != nullptr) {
        gamecrt::operator_delete(names_);
        names_ = nullptr;
    }
    header_ = ResHeader{};
    if (archive_ != nullptr) {
        gameapi::mmioClose(static_cast<HMMIO>(archive_), 0);
        archive_ = nullptr;
    }
    FreeAccelTable();
    if (file_ != nullptr) {
        gameapi::mmioClose(static_cast<HMMIO>(file_), 0);
        file_ = nullptr;
    }
    offset_ = 0;
    size_ = 0;
}

BOOL CResourceStream::OpenFile(const char* name, std::uint32_t* size) {
    // Close the file opened before (the archive stays open).
    if (offset_ != 0) {
        offset_ = 0;
        size_ = 0;
        if (file_ != nullptr) {
            gameapi::mmioClose(static_cast<HMMIO>(file_), 0);
            file_ = nullptr;
        }
    }
    // A readable loose file comes first.
    if (Access(name, 4) == 0) {
        FindData data;
        FindFirst(name, &data);
        file_ = gameapi::mmioOpenA(const_cast<char*>(name), nullptr, MMIO_DENYWRITE);
        if (file_ != nullptr) {
            if (size != nullptr) *size = data.size;
            offset_ = 1;
            return 1;
        }
    }
    if (entries_ == nullptr || names_ == nullptr) return 0;
    int index = FindEntryAt(this, nullptr, name);
    if (index == -1) return 0;
    offset_ = static_cast<std::int32_t>(entries_[index].offset);
    if (gameapi::mmioSeek(static_cast<HMMIO>(archive_), offset_, SEEK_SET) == -1) {
        offset_ = 0;
        return 0;
    }
    if (size != nullptr) *size = entries_[index].size;
    return 1;
}

long CResourceStream::Read(char* buffer, long size) {
    if (offset_ == 0) return -1;
    if (file_ != nullptr) return gameapi::mmioRead(static_cast<HMMIO>(file_), buffer, size);
    return gameapi::mmioRead(static_cast<HMMIO>(archive_), buffer, size);
}

long CResourceStream::Seek(long offset, int origin) {
    if (offset_ == 0) return -1;
    if (file_ != nullptr) {
        // A loose file: only "from the end" is passed on; anything else seeks from the start.
        return gameapi::mmioSeek(static_cast<HMMIO>(file_), offset, origin == 2 ? SEEK_END : SEEK_SET);
    }
    int mode;
    if (origin == 1) {
        mode = SEEK_CUR;
    } else if (origin == 2) {
        mode = SEEK_SET;
        offset = offset + size_ + offset_;
    } else {
        mode = SEEK_SET;
        offset = offset + offset_;
    }
    long result = gameapi::mmioSeek(static_cast<HMMIO>(archive_), offset, mode);
    if (result == -1) return -1;
    return result - offset_;
}

// --- Archive listing -----------------------------------------------------------------------------

int __fastcall WildcardMatch(const char* name, const char* pattern, int emptyPatternResult) {
    if (*pattern == '\0') return *name == '\0' ? 1 : emptyPatternResult;
    while (*name != '\0' && *pattern != '\0') {
        if (*pattern == '*') return 1;
        if (*pattern != '?' && gamecrt::toupper(*pattern) != gamecrt::toupper(*name)) return 0;
        ++name;
        ++pattern;
    }
    for (; *pattern != '\0'; ++pattern)
        if (*pattern != '?' && *pattern != '*') return 0;
    return *name == '\0' ? 1 : 0;
}

void CMappedResource::ListFiles(void* stringArray, const char* pattern) const {
    StringArraySetSize(stringArray, nullptr, 0, -1);
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
        int count = static_cast<const int*>(stringArray)[2];
        StringArraySetAtGrow(stringArray, nullptr, count, name);
    }
}

// --- CMMIOResource -------------------------------------------------------------------------------

CMMIOResource* CMMIOResource::Construct() {
    CMappedResource::Construct();
    file_.Construct();
    SetVtable(this, kMmioResourceVtable);
    mappedImage_ = nullptr;
    return this;
}

void CMMIOResource::Destroy() {
    SetVtable(this, kMmioResourceVtable);
    // CloseResource, inlined (00675720 is CMappedFile::Close's copy).
    file_.Close();
    entries_ = nullptr;
    names_ = nullptr;
    mappedImage_ = nullptr;
    header_ = ResHeader{};
    // The CMappedFile destructor, then the CMappedResource destructor (an inline copy; Rebirth
    // patches the out-of-line one, 006714A0, so this is not a call to it).
    file_.Close();
    CMappedResource::Destroy();
}

CMMIOResource* CMMIOResource::DeletingDestroy(unsigned flags) {
    Destroy();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// --- Hooks ---------------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x00674790, CResourceBase::Construct);
EI_REPLACE_METHOD(0x00674810, CResourceBase::Destruct);
EI_REPLACE_METHOD(0x006747C0, CResourceBase::DeletingDestruct);
EI_REPLACE_METHOD(0x00674A90, CResourceBase::FreeAccelTable);

EI_REPLACE_METHOD(0x00674280, CResourceStream::Construct);
EI_REPLACE_METHOD(0x006742C0, CResourceStream::Destruct);
EI_REPLACE_METHOD(0x006742A0, CResourceStream::DeletingDestruct);
EI_REPLACE_METHOD(0x00674430, CResourceStream::Close);
EI_REPLACE_METHOD(0x006744C0, CResourceStream::OpenFile);
EI_REPLACE_METHOD(0x006746B0, CResourceStream::Read);
EI_REPLACE_METHOD(0x006746F0, CResourceStream::Seek);

EI_REPLACE(0x00674CE0, WildcardMatch);
EI_REPLACE_METHOD(0x00674AC0, CMappedResource::ListFiles);

EI_REPLACE_METHOD(0x006721D0, CMMIOResource::Construct);
EI_REPLACE_METHOD(0x006722F0, CMMIOResource::Destroy);
EI_REPLACE_METHOD(0x006722D0, CMMIOResource::DeletingDestroy);
