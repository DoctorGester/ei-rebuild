#include "bink_video.h"
#include "core_streams.h"
#include "resource.h"
#include "resource_stream.h"
#include "mod_folders.h"
#include "originals_engine.h"

#include "ei/hook.h"
#include "ei/vcall.h"
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

CMappedResource::CMappedResource() {
    // CResourceBase part.
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
    image_ = nullptr;
    fd_ = -1;
    writable_ = 0;
}

CMappedResource* CMappedResource::Construct() { return new (this) CMappedResource(); }

CMappedResource::~CMappedResource() {
    // Rebirth's ResOverride (006714A2): addon forgets the archive first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnDestroyResource(this);
    CMappedResource::FreeAllFiles();   // called directly, not through the vtable
    if (fd_ >= 0) {
        libcrt::Close(fd_);
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
    header_ = ResHeader{};
    entries_ = nullptr;
    names_ = nullptr;
    if (accelTable_) {
        gamecrt::operator_delete(accelTable_);
        accelTable_ = nullptr;
        accelCount_ = 0;
    }
}

void CMappedResource::Destroy() { this->CMappedResource::~CMappedResource(); }

CMappedResource* CMappedResource::DeletingDestroy(unsigned flags) {
    this->CMappedResource::~CMappedResource();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CMappedResource::operator delete(void* block) { gamecrt::operator_delete(block); }

// The slots whose bodies are registered by member pointer (the Impl members), and slot 4.
BOOL CMappedResource::OpenResource(const char* path, BOOL writable) { return OpenResourceImpl(path, writable); }
BOOL CMappedResource::OpenFromMemory(const void* image) { return OpenFromMemoryImpl(image); }
void CMappedResource::Flush() {}   // 00671390 (short_functions.cpp)
void* CMappedResource::GetFile(const char* name, std::uint32_t* size) { return GetFileImpl(name, size); }
bool CMappedResource::CloseFile(void* data) { return CloseFileImpl(data); }

BOOL CMappedResource::OpenResourceImpl(const char* path, BOOL writable) {
    // Rebirth's ResOverride (00671738): addon loads the mod's archives on the first open and records
    // which archive this is.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnOpenResource(this, path);
    CloseResource();
    writable_ = writable;
    fd_ = libcrt::Open(path, (writable ? 2 /* _O_RDWR */ : 0) | 0x8000 /* _O_BINARY */ | 0x10 /* _O_RANDOM */);
    if (fd_ == -1)
        return game::FatalError("CMappedResource::OpenResourceImpl()\nUnable to open file \"" + mfc::CString(path) + "\"");

    if (libcrt::Lseek(fd_, 0, 0) != -1 && libcrt::Read(fd_, &header_, sizeof(header_)) == sizeof(header_)) {
        if (header_.magic != kResMagic) {
            game::FatalError("Wrong format resource file \"" + mfc::CString(path) + "\"");
            CloseResource();
            return 0;
        }
        unsigned tableSize = header_.count * sizeof(ResEntry);
        entries_ = static_cast<ResEntry*>(gamecrt::operator_new(tableSize));
        names_ = static_cast<char*>(gamecrt::operator_new(header_.namesSize));
        if (libcrt::Lseek(fd_, static_cast<long>(header_.tableOffset), 0) != -1 &&
            static_cast<unsigned>(libcrt::Read(fd_, entries_, tableSize)) == tableSize &&
            static_cast<std::uint32_t>(libcrt::Read(fd_, names_, header_.namesSize)) == header_.namesSize)
            return 1;
    }
    CloseResource();
    return game::FatalError("CMappedResource::OpenResourceImpl()\nUnable to read file \"" + mfc::CString(path) + "\"");
}

BOOL CMappedResource::OpenFromMemoryImpl(const void* image) {
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
        libcrt::Close(fd_);
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

// Rebirth's ResOverride sends every call of 00671B40 and the vtable slot 007432F4 to addon.
void* CMappedResource::GetFileImpl(const char* name, std::uint32_t* size) {
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) return modfolders::GetFile(this, name, size);
    return GetFilePlain(name, size);
}

void* CMappedResource::GetFilePlain(const char* name, std::uint32_t* size) {
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

    if (libcrt::Lseek(fd_, static_cast<long>(file->offset), 0 /* SEEK_SET */) != -1) {
        std::uint32_t bytes = file->size;
        if (static_cast<std::uint32_t>(libcrt::Read(fd_, file->data, bytes)) == bytes) {
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

bool CMappedResource::CloseFileImpl(void* data) {
    if (image_) return true;
    if (!data) return false;

    auto* byData = filesByData_.Find(data);
    if (!byData) {
        game::DebugLog("CMappedResource::CloseFileImpl()\nFile already closed\n");
        return false;
    }
    LoadedFile* file = byData->value;
    if (--file->refCount != 0) return true;

    if (writable_) {
        if (libcrt::Lseek(fd_, static_cast<long>(file->offset), 0 /* SEEK_SET */) == -1 ||
            static_cast<std::uint32_t>(gamecrt::write(fd_, file->data, file->size)) != file->size) {
            // The original formats the data pointer as the file name.
            mfc::CString name;
            name = static_cast<const char*>(data);
            game::FatalError("CMappedResource::CloseFileImpl()\nUnable to write file \"" + name + "\"\n");
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
void __fastcall CloseResourceHook(CMappedResource* self) { self->CMappedResource::CloseResource(); }
void __fastcall FreeAllFilesHook(CMappedResource* self) { self->CMappedResource::FreeAllFiles(); }
} // namespace

EI_REWRITTEN(0x00674850, CMappedResource::FindEntry);
EI_REPLACE_METHOD(0x00671280, CMappedResource::Construct);
EI_REPLACE_METHOD(0x006714A0, CMappedResource::Destroy);           // patched by Rebirth: vanilla only
EI_REWRITTEN(0x006713A0, CMappedResource::DeletingDestroy);
EI_REPLACE_METHOD(0x00671730, CMappedResource::OpenResourceImpl);   // patched by Rebirth: installs in the vanilla launch only
EI_REWRITTEN(0x00671A40, CMappedResource::OpenFromMemoryImpl);
EI_REWRITTEN(0x00671AC0, CloseResourceHook);
EI_REWRITTEN(0x00671B40, CMappedResource::GetFileImpl);
EI_REWRITTEN(0x00671E00, CMappedResource::CloseFileImpl);
EI_REWRITTEN(0x00672040, FreeAllFilesHook);

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
    if (!libcrt::Isalpha(root[0]) || root[1] != ':' || root[2] != '\\') rootPath = nullptr;
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

BOOL CMMIOResource::OpenResourceImpl(const char* path, BOOL writable) {
    if (!file_.Open(path, writable)) return false;
    if (OpenFromMemory(file_.view_)) return true;
    CloseResource();
    return false;
}

BOOL CMMIOResource::OpenFromMemoryImpl(const void* image) {
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

BOOL CMMIOResource::OpenResource(const char* path, BOOL writable) { return OpenResourceImpl(path, writable); }
BOOL CMMIOResource::OpenFromMemory(const void* image) { return OpenFromMemoryImpl(image); }
void* CMMIOResource::GetFile(const char* name, std::uint32_t* size) { return GetFileImpl(name, size); }
bool CMMIOResource::CloseFile(void* data) { return CloseFileImpl(data); }

void CMMIOResource::CloseResource() {
    file_.Close();
    entries_ = nullptr;
    names_ = nullptr;
    mappedImage_ = nullptr;
    header_ = ResHeader{};
}

void CMMIOResource::Flush() { file_.Flush(); }

void* CMMIOResource::GetFileImpl(const char* name, std::uint32_t* size) {
    if (!entries_ || !names_) return nullptr;
    int index = FindEntry(name);
    if (index == -1) return nullptr;
    if (size) *size = entries_[index].size;
    return const_cast<char*>(static_cast<const char*>(mappedImage_)) + entries_[index].offset;
}

bool CMMIOResource::CloseFileImpl(void*) { return true; }

void CMMIOResource::FreeAllFiles() {}

EI_REWRITTEN(0x006756B0, CMappedFile::Construct);
EI_REWRITTEN(0x006756D0, CMappedFile::Close);
EI_REWRITTEN(0x00675720, CMappedFile::Close);
EI_REWRITTEN(0x00675770, CMappedFile::Open);
EI_REWRITTEN(0x00675A50, CMappedFile::Flush);
EI_REWRITTEN(0x006724E0, CMMIOResource::OpenResourceImpl);
EI_REWRITTEN(0x00672520, CMMIOResource::OpenFromMemoryImpl);
EI_REWRITTEN(0x006725E0, CMMIOResource::GetFileImpl);
EI_REWRITTEN(0x00672640, CMMIOResource::CloseFileImpl);
