#include "resource.h"
#include "originals_engine.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>

int CMappedResource::FindEntry(const char* name) const {
    std::uint32_t count = header_.count;
    if (count == 0) return -1;

    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    for (; name[length] != '\0'; ++length)
        hash += gamecrt::tolower(static_cast<unsigned char>(name[length]));

    std::uint32_t index = hash % count;
    for (;;) {
        const ResEntry& e = entries_[index];
        if (length == e.nameLength && gamecrt::strnicmp(name, names_ + e.nameOffset, length) == 0)
            return static_cast<int>(index);
        index = static_cast<std::uint32_t>(entries_[index].next);
        if (index == 0xFFFFFFFF) return -1;
    }
}

CMappedResource* CMappedResource::Construct() {
    // CResourceBase part.
    *reinterpret_cast<std::uintptr_t*>(this) = 0x00743324;
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
    accelTable_ = nullptr;
    accelCount_ = 0;
    // The loaded-file maps, 193 buckets each (STLport's first prime >= 100).
    std::memset(&filesByData_, 0, sizeof(filesByData_));
    filesByData_.InitBuckets(100);
    std::memset(&filesByName_, 0, sizeof(filesByName_));
    filesByName_.InitBuckets(100);
    *reinterpret_cast<std::uintptr_t*>(this) = 0x007432E0;
    image_ = nullptr;
    fd_ = -1;
    writable_ = 0;
    return this;
}

void CMappedResource::Destroy() {
    *reinterpret_cast<std::uintptr_t*>(this) = 0x007432E0;
    CMappedResource::FreeAllFiles();   // called directly, not through the vtable
    if (fd_ >= 0) {
        gamecrt::close(fd_);
        fd_ = -1;
    }
    if (!image_) {
        if (entries_) gamecrt::operator_delete(entries_);
        if (!image_ && names_) gamecrt::operator_delete(names_);
    }
    entries_ = nullptr;
    names_ = nullptr;
    image_ = nullptr;
    header_ = ResHeader{};
    filesByName_.Destroy();
    filesByData_.Destroy();

    // ~CResourceBase.
    *reinterpret_cast<std::uintptr_t*>(this) = 0x00743324;
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
    if (accelTable_) {
        gamecrt::operator_delete(accelTable_);
        accelTable_ = nullptr;
        accelCount_ = 0;
    }
}

CMappedResource* CMappedResource::DeletingDestroy(unsigned flags) {
    MappedResourceDestroyOriginal(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CMappedResource::OpenResource(const char* path, BOOL writable) {
    CloseResource();
    writable_ = writable;
    fd_ = gamecrt::open(path, (writable ? 2 /* _O_RDWR */ : 0) | 0x8000 /* _O_BINARY */ | 0x10 /* _O_RANDOM */);
    if (fd_ == -1)
        return game::FatalError("CMappedResource::OpenResource()\nUnable to open file \"" + mfc::CString(path) + "\"");

    if (gamecrt::lseek(fd_, 0, 0) != -1 && gamecrt::read(fd_, &header_, sizeof(header_)) == sizeof(header_)) {
        if (header_.magic != kResMagic) {
            game::FatalError("Wrong format resource file \"" + mfc::CString(path) + "\"");
            CloseResource();
            return 0;
        }
        unsigned tableSize = header_.count * sizeof(ResEntry);
        entries_ = static_cast<ResEntry*>(gamecrt::operator_new(tableSize));
        names_ = static_cast<char*>(gamecrt::operator_new(header_.namesSize));
        if (gamecrt::lseek(fd_, static_cast<long>(header_.tableOffset), 0) != -1 &&
            static_cast<unsigned>(gamecrt::read(fd_, entries_, tableSize)) == tableSize &&
            static_cast<std::uint32_t>(gamecrt::read(fd_, names_, header_.namesSize)) == header_.namesSize)
            return 1;
    }
    CloseResource();
    return game::FatalError("CMappedResource::OpenResource()\nUnable to read file \"" + mfc::CString(path) + "\"");
}

BOOL CMappedResource::OpenFromMemory(const void* image) {
    CloseResource();
    image_ = image;
    header_ = *static_cast<const ResHeader*>(image);
    if (header_.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
    const char* base = static_cast<const char*>(image);
    entries_ = reinterpret_cast<ResEntry*>(const_cast<char*>(base) + header_.tableOffset);
    names_ = const_cast<char*>(base) + header_.tableOffset + header_.count * sizeof(ResEntry);
    return true;
}

void CMappedResource::CloseResource() {
    FreeAllFiles();
    if (fd_ >= 0) {
        gamecrt::close(fd_);
        fd_ = -1;
    }
    // The table and names were read into memory by OpenResource unless the archive is an image.
    if (!image_) {
        if (entries_) gamecrt::operator_delete(entries_);
        if (!image_ && names_) gamecrt::operator_delete(names_);
    }
    entries_ = nullptr;
    names_ = nullptr;
    image_ = nullptr;
    header_ = ResHeader{};
}

void* CMappedResource::GetFile(const char* name, std::uint32_t* size) {
    if (!entries_ || !names_) return nullptr;

    // Already loaded: share it.
    {
        stlp::string key(name);
        if (auto* node = filesByName_.Find(key)) {
            LoadedFile* file = node->value;
            ++file->refCount;
            if (size) *size = file->size;
            return file->data;
        }
    }

    int index = FindEntry(name);
    if (index == -1) return nullptr;
    const ResEntry& entry = entries_[index];

    // An archive image in memory hands out pointers into the image (size is not reported).
    if (image_) return const_cast<char*>(static_cast<const char*>(image_)) + entry.offset;

    auto* file = static_cast<LoadedFile*>(gamecrt::operator_new(sizeof(LoadedFile)));
    new (&file->name) stlp::string();
    stlp::string::Reserve(file->name, 8);
    {
        stlp::string requested(name);
        file->name.Assign(requested.data(), requested.data() + requested.size());
    }
    file->refCount = 1;
    file->size = entry.size;
    file->data = gamecrt::operator_new(file->size);
    file->offset = entry.offset;

    if (gamecrt::lseek(fd_, static_cast<long>(file->offset), 0 /* SEEK_SET */) != -1) {
        std::uint32_t bytes = file->size;
        if (static_cast<std::uint32_t>(gamecrt::read(fd_, file->data, bytes)) == bytes) {
            if (size) *size = bytes;
            {
                stlp::string key(file->name);
                filesByName_[key] = file;
            }
            filesByData_[file->data] = file;
            return file->data;
        }
    }

    // Read failed. The original leaks file->data here.
    file->name.~string();
    gamecrt::operator_delete(file);
    return nullptr;
}

bool CMappedResource::CloseFile(void* data) {
    if (image_) return true;
    if (!data) return false;

    auto* byData = filesByData_.Find(data);
    if (!byData) {
        game::DebugLog("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    LoadedFile* file = byData->value;
    if (--file->refCount != 0) return true;

    if (writable_) {
        if (gamecrt::lseek(fd_, static_cast<long>(file->offset), 0 /* SEEK_SET */) == -1 ||
            static_cast<std::uint32_t>(gamecrt::write(fd_, file->data, file->size)) != file->size) {
            // The original formats the data pointer as the file name.
            mfc::CString name;
            name = static_cast<const char*>(data);
            game::FatalError("CMappedResource::CloseFile()\nUnable to write file \"" + name + "\"\n");
        }
    }

    filesByData_.Erase(byData);
    filesByName_.Erase(filesByName_.Find(file->name));
    gamecrt::operator_delete(file->data);
    file->name.~string();
    gamecrt::operator_delete(file);
    return true;
}

void CMappedResource::FreeAllFiles() {
    if (image_) return;
    filesByData_.ForEach([](auto* node) {
        LoadedFile* file = node->value;
        gamecrt::operator_delete(file->data);
        if (file) {
            file->name.~string();
            gamecrt::operator_delete(file);
        }
    });
    filesByData_.Clear();
    filesByName_.Clear();
}

// Hooks. Virtual methods are installed through shims that call the body non-virtually; taking a
// pointer to a virtual member would give a vtable-dispatch thunk instead.
namespace {
BOOL __fastcall OpenResourceHook(CMappedResource* self, void*, const char* path, BOOL writable) {
    return self->CMappedResource::OpenResource(path, writable);
}
BOOL __fastcall OpenFromMemoryHook(CMappedResource* self, void*, const void* image) {
    return self->CMappedResource::OpenFromMemory(image);
}
void __fastcall CloseResourceHook(CMappedResource* self) { self->CMappedResource::CloseResource(); }
void* __fastcall GetFileHook(CMappedResource* self, void*, const char* name, std::uint32_t* size) {
    return self->CMappedResource::GetFile(name, size);
}
bool __fastcall CloseFileHook(CMappedResource* self, void*, void* data) {
    return self->CMappedResource::CloseFile(data);
}
void __fastcall FreeAllFilesHook(CMappedResource* self) { self->CMappedResource::FreeAllFiles(); }
} // namespace

EI_REPLACE_METHOD(0x00674850, CMappedResource::FindEntry);
EI_REPLACE_METHOD(0x00671280, CMappedResource::Construct);
EI_REPLACE_METHOD(0x006714A0, CMappedResource::Destroy);           // patched by Rebirth: vanilla only
EI_REPLACE_METHOD(0x006713A0, CMappedResource::DeletingDestroy);
EI_REPLACE(0x00671730, OpenResourceHook);   // patched by Rebirth: installs in the vanilla launch only
EI_REPLACE(0x00671A40, OpenFromMemoryHook);
EI_REPLACE(0x00671AC0, CloseResourceHook);
EI_REPLACE(0x00671B40, GetFileHook);
EI_REPLACE(0x00671E00, CloseFileHook);
EI_REPLACE(0x00672040, FreeAllFilesHook);

// --- CMappedFile -------------------------------------------------------------------------------

CMappedFile::CMappedFile() { Construct(); }

CMappedFile* CMappedFile::Construct() {
    file_ = INVALID_HANDLE_VALUE;
    mapping_ = nullptr;
    view_ = nullptr;
    size_ = 0;
    return this;
}

void CMappedFile::Close() {
    if (view_) {
        gameapi::UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        gameapi::CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        gameapi::CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    size_ = 0;
}

BOOL CMappedFile::Open(const char* path, BOOL writable) {
    if (writable) game::DebugLog("CMappedFile::Open().\nMapping file \"%s\" with writting access.\n", path);

    // Unbuffered I/O only when the volume's sector size divides the page size.
    char root[4];
    DWORD bytesPerSector = 0xFFFFFFFF, sectorsPerCluster, freeClusters, totalClusters;
    gamecrt::strncpy(root, path, 3);
    root[3] = '\0';
    const char* rootPath = root;
    if (!gamecrt::isalpha(root[0]) || root[1] != ':' || root[2] != '\\') rootPath = nullptr;
    gameapi::GetDiskFreeSpaceA(rootPath, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    DWORD flags = (info.dwPageSize < bytesPerSector || info.dwPageSize % bytesPerSector != 0)
                      ? FILE_FLAG_RANDOM_ACCESS | FILE_ATTRIBUTE_NORMAL
                      : FILE_FLAG_NO_BUFFERING | FILE_ATTRIBUTE_NORMAL;

    file_ = gameapi::CreateFileA(path, writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, flags, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        gameapi::GetLastError();
        char message[256];
        gamecrt::sprintf(message, "CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        game::DebugLog(message);
        return false;
    }

    size_ = gameapi::GetFileSize(file_, nullptr);
    if (size_ == 0xFFFFFFFF) {
        game::FatalError("CMappedFile::Open().\nUnable to determine file size.\n");
    } else if (size_ > 0x80000000) {
        game::FatalError("CMappedFile::Open().\nToo large file to map.\n");
    } else {
        mapping_ = gameapi::CreateFileMappingA(file_, nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) {
            game::FatalError("CMappedFile::Open().\nUnable to create file mapping.\n");
        } else {
            view_ = gameapi::MapViewOfFile(mapping_, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
            if (view_) return true;
            game::FatalError("CMappedFile::Open( \"%s\" ).\nUnable to create view of file (error=0x%X).\n", path,
                             gameapi::GetLastError());
        }
    }
    Close();
    return false;
}

void CMappedFile::Flush() {
    if (!view_) return;
    gameapi::FlushViewOfFile(view_, 0);
    SYSTEMTIME now;
    gameapi::GetSystemTime(&now);
    BY_HANDLE_FILE_INFORMATION info;
    gameapi::GetFileInformationByHandle(file_, &info);
    FILETIME time;
    gameapi::SystemTimeToFileTime(&now, &time);
    gameapi::SetFileTime(file_, nullptr, nullptr, &time);
}

// --- CMMIOResource -----------------------------------------------------------------------------

BOOL CMMIOResource::OpenResource(const char* path, BOOL writable) {
    if (!file_.Open(path, writable)) return false;
    if (OpenFromMemory(file_.view_)) return true;
    CloseResource();
    return false;
}

BOOL CMMIOResource::OpenFromMemory(const void* image) {
    mappedImage_ = image;
    header_ = *static_cast<const ResHeader*>(image);
    if (header_.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
    char* base = const_cast<char*>(static_cast<const char*>(image));
    entries_ = reinterpret_cast<ResEntry*>(base + header_.tableOffset);
    names_ = base + header_.tableOffset + header_.count * sizeof(ResEntry);
    return true;
}

void CMMIOResource::CloseResource() {
    file_.Close();
    entries_ = nullptr;
    names_ = nullptr;
    mappedImage_ = nullptr;
    header_ = ResHeader{};
}

void CMMIOResource::Flush() { file_.Flush(); }

void* CMMIOResource::GetFile(const char* name, std::uint32_t* size) {
    if (!entries_ || !names_) return nullptr;
    int index = FindEntry(name);
    if (index == -1) return nullptr;
    if (size) *size = entries_[index].size;
    return const_cast<char*>(static_cast<const char*>(mappedImage_)) + entries_[index].offset;
}

bool CMMIOResource::CloseFile(void*) { return true; }

void CMMIOResource::FreeAllFiles() {}

namespace {
BOOL __fastcall MmioOpenResourceHook(CMMIOResource* self, void*, const char* path, BOOL writable) {
    return self->CMMIOResource::OpenResource(path, writable);
}
BOOL __fastcall MmioOpenFromMemoryHook(CMMIOResource* self, void*, const void* image) {
    return self->CMMIOResource::OpenFromMemory(image);
}
void __fastcall MmioCloseResourceHook(CMMIOResource* self) { self->CMMIOResource::CloseResource(); }
void __fastcall MmioFlushHook(CMMIOResource* self) { self->CMMIOResource::Flush(); }
void* __fastcall MmioGetFileHook(CMMIOResource* self, void*, const char* name, std::uint32_t* size) {
    return self->CMMIOResource::GetFile(name, size);
}
bool __fastcall MmioCloseFileHook(CMMIOResource* self, void*, void* data) {
    return self->CMMIOResource::CloseFile(data);
}
} // namespace

EI_REPLACE_METHOD(0x006756B0, CMappedFile::Construct);
EI_REPLACE_METHOD(0x006756D0, CMappedFile::Close);
EI_REPLACE_METHOD(0x00675720, CMappedFile::Close);
EI_REPLACE_METHOD(0x00675770, CMappedFile::Open);
EI_REPLACE_METHOD(0x00675A50, CMappedFile::Flush);
EI_REPLACE(0x006724E0, MmioOpenResourceHook);
EI_REPLACE(0x00672520, MmioOpenFromMemoryHook);
EI_REPLACE(0x006725A0, MmioCloseResourceHook);
EI_REPLACE(0x006725D0, MmioFlushHook);
EI_REPLACE(0x006725E0, MmioGetFileHook);
EI_REPLACE(0x00672640, MmioCloseFileHook);
