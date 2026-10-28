#include "bink_video.h"
#include "core_streams.h"
#include "resource.h"
#include "resource_stream.h"
#include "mod_folders.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/text_case.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

int CResourceBase::FindEntry(const char* name) const {
    std::uint32_t count = header_.count;
    if (count == 0) return -1;

    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    for (; name[length] != '\0'; ++length)
        hash += text::ToLower(static_cast<unsigned char>(name[length]));

    std::uint32_t index = hash % count;
    for (;;) {
        const ResEntry& e = entries_[index];
        if (length == e.nameLength && text::CompareIgnoringCase(name, names_ + e.nameOffset, length) == 0)
            return static_cast<int>(index);
        index = static_cast<std::uint32_t>(entries_[index].next);
        if (index == 0xFFFFFFFF) return -1;
    }
}

namespace {

// `size` bytes at `offset` of the archive, read or written through the game's slots (SetFilePointer, ReadFile,
// WriteFile; VC6's _lseek, _read and _write on a binary descriptor did the same calls).
bool ReadAt(void* file, std::uint32_t offset, void* to, std::uint32_t size) {
    DWORD read = 0;
    return gameapi::SetFilePointer(file, static_cast<LONG>(offset), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           gameapi::ReadFile(file, to, size, &read, nullptr) && read == size;
}

bool WriteAt(void* file, std::uint32_t offset, const void* from, std::uint32_t size) {
    DWORD written = 0;
    return gameapi::SetFilePointer(file, static_cast<LONG>(offset), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           gameapi::WriteFile(file, from, size, &written, nullptr) && written == size;
}

} // namespace

CMappedResource::CMappedResource() {
    image_ = nullptr;
    handle_ = INVALID_HANDLE_VALUE;
    writable_ = 0;
}

CMappedResource::~CMappedResource() {
    // Rebirth's ResOverride: addon forgets the archive first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnDestroyResource(this);
    CMappedResource::FreeAllFiles();   // called directly, not through the vtable
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (!image_) {
        if (entries_) delete[] entries_;
        if (!image_ && names_) delete[] names_;
    }
    entries_ = nullptr;
    names_ = nullptr;
    image_ = nullptr;
    header_ = ResHeader{};
    // ~CResourceBase follows.
}

void CMappedResource::Destroy() { this->CMappedResource::~CMappedResource(); }

void CMappedResource::Flush() {}   // 4

BOOL CMappedResource::OpenResource(const char* path, BOOL writable) {
    // Rebirth's ResOverride: addon loads the mod's archives on the first open and records
    // which archive this is.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnOpenResource(this, path);
    CloseResource();
    writable_ = writable;
    handle_ = gameapi::CreateFileA(path, writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
        return game::FatalError(("CMappedResource::OpenResource()\nUnable to open file \"" + std::string(path) + "\"").c_str());

    if (ReadAt(handle_, 0, &header_, sizeof(header_))) {
        if (header_.magic != kResMagic) {
            game::FatalError(("Wrong format resource file \"" + std::string(path) + "\"").c_str());
            CloseResource();
            return 0;
        }
        unsigned tableSize = header_.count * sizeof(ResEntry);
        entries_ = new ResEntry[header_.count];
        names_ = new char[header_.namesSize];
        if (ReadAt(handle_, header_.tableOffset, entries_, tableSize) &&
            ReadAt(handle_, header_.tableOffset + tableSize, names_, header_.namesSize))
            return 1;
    }
    CloseResource();
    return game::FatalError(("CMappedResource::OpenResource()\nUnable to read file \"" + std::string(path) + "\"").c_str());
}

BOOL CMappedResource::OpenFromMemory(const void* image) {
    CloseResource();
    // GetFile hands out pointers into the image (writable through them).
    char* base = const_cast<char*>(static_cast<const char*>(image));
    image_ = base;
    std::memcpy(&header_, base, sizeof(header_));
    if (header_.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
    entries_ = reinterpret_cast<ResEntry*>(base + header_.tableOffset);
    names_ = base + header_.tableOffset + header_.count * sizeof(ResEntry);
    return true;
}

void CMappedResource::CloseResource() {
    FreeAllFiles();
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    // The table and names were read into memory by OpenResource unless the archive is an image.
    if (!image_) {
        if (entries_) delete[] entries_;
        if (!image_ && names_) delete[] names_;
    }
    entries_ = nullptr;
    names_ = nullptr;
    image_ = nullptr;
    header_ = ResHeader{};
}

// Rebirth's ResOverride sends every call to addon.
void* CMappedResource::GetFile(const char* name, std::uint32_t* size) {
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) return modfolders::GetFile(this, name, size);
    return GetFilePlain(name, size);
}

void* CMappedResource::GetFilePlain(const char* name, std::uint32_t* size) {
    if (!entries_ || !names_) return nullptr;

    // Already loaded: share it.
    {
        std::string key(name);
        if (auto node = filesByName_.find(key); node != filesByName_.end()) {
            LoadedFile* file = node->second;
            ++file->refCount;
            if (size) *size = file->size;
            return file->data;
        }
    }

    int index = FindEntry(name);
    if (index == -1) return nullptr;
    const ResEntry& entry = entries_[index];

    // An archive image in memory hands out pointers into the image (size is not reported).
    if (image_) return image_ + entry.offset;

    auto* file = new LoadedFile;
    {
        std::string requested(name);
        file->name.assign(requested.data(), requested.data() + requested.size());
    }
    file->refCount = 1;
    file->size = entry.size;
    file->data = ::operator new(file->size);
    file->offset = entry.offset;

    if (ReadAt(handle_, file->offset, file->data, file->size)) {
        if (size) *size = file->size;
        {
            std::string key(file->name);
            filesByName_[key] = file;
        }
        filesByData_[file->data] = file;
        return file->data;
    }

    // Read failed. file->data leaks here (kept).
    delete file;
    return nullptr;
}

bool CMappedResource::CloseFile(void* data) {
    if (image_) return true;
    if (!data) return false;

    auto byData = filesByData_.find(data);
    if (byData == filesByData_.end()) {
        game::DebugLog("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    LoadedFile* file = byData->second;
    if (--file->refCount != 0) return true;

    if (writable_) {
        if (!WriteAt(handle_, file->offset, file->data, file->size)) {
            // The file name is the formatted data pointer.
            std::string name;
            name = static_cast<const char*>(data);
            game::FatalError(("CMappedResource::CloseFile()\nUnable to write file \"" + name + "\"\n").c_str());
        }
    }

    filesByData_.erase(byData);
    filesByName_.erase(file->name);
    ::operator delete(file->data);
    delete file;
    return true;
}

void CMappedResource::FreeAllFiles() {
    if (image_) return;
    for (auto& [data, file] : filesByData_) {
        ::operator delete(file->data);
        delete file;
    }
    filesByData_.clear();
    filesByName_.clear();
}

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
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    std::strncpy(root, path, 3);
    root[3] = '\0';
    const char* rootPath = root;
    if (!text::IsAlpha(root[0]) || root[1] != ':' || root[2] != '\\') rootPath = nullptr;
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
        std::snprintf(message, sizeof(message), "CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
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


BOOL CMMIOResource::OpenResource(const char* path, BOOL writable) {
    if (!file_.Open(path, writable)) return false;
    if (OpenFromMemory(file_.view_)) return true;
    CloseResource();
    return false;
}

BOOL CMMIOResource::OpenFromMemory(const void* image) {
    char* base = const_cast<char*>(static_cast<const char*>(image));
    mappedImage_ = base;
    std::memcpy(&header_, base, sizeof(header_));
    if (header_.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
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
    return mappedImage_ + entries_[index].offset;
}

bool CMMIOResource::CloseFile(void*) { return true; }

void CMMIOResource::FreeAllFiles() {}
