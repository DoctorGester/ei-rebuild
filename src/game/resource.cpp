#include "bink_video.h"
#include "core_streams.h"
#include "resource.h"
#include "resource_stream.h"
#include "mod_folders.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include "lib/text_case.h"

#include <cstdio>
#include <windows.h>

int ResourceBase::FindEntry(std::string_view name) const {
    std::uint32_t count = header.count;
    if (count == 0) return -1;

    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    for (; length < name.size(); ++length)
        hash += text::ToLower(static_cast<unsigned char>(name[length]));

    std::uint32_t index = hash % count;
    for (;;) {
        const ResEntry& e = entries[index];
        if (length == e.nameLength && text::CompareIgnoringCase(name.data(), names + e.nameOffset, length) == 0)
            return static_cast<int>(index);
        if (e.next == ResEntry::kChainEnd) return -1;
        index = static_cast<std::uint32_t>(e.next);
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

MappedResource::MappedResource() {
    image = nullptr;
    handle = INVALID_HANDLE_VALUE;
    writable = 0;
}

MappedResource::~MappedResource() {
    // Rebirth's ResOverride: addon forgets the archive first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnDestroyResource(this);
    MappedResource::FreeAllFiles();   // called directly, not through the vtable
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    nameTable = {};
    image = nullptr;
    header = ResHeader{};
    // ~ResourceBase follows.
}

void MappedResource::Destroy() { this->MappedResource::~MappedResource(); }

void MappedResource::Flush() {}   // 4

bool MappedResource::OpenResource(const std::string& path, bool forWriting) {
    // Rebirth's ResOverride: addon loads the mod's archives on the first open and records
    // which archive this is.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::OnOpenResource(this, path);
    CloseResource();
    writable = forWriting;
    handle = gameapi::CreateFileA(path.c_str(), forWriting ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return game::FatalError("CMappedResource::OpenResource()\nUnable to open file \"" + path + "\"");

    if (ReadAt(handle, 0, &header, sizeof(header))) {
        if (header.magic != kResMagic) {
            game::FatalError("Wrong format resource file \"" + path + "\"");
            CloseResource();
            return false;
        }
        unsigned tableSize = header.count * sizeof(ResEntry);
        entryTable.resize(header.count);
        nameTable.resize(header.namesSize);
        entries = entryTable.data();
        names = nameTable.data();
        if (ReadAt(handle, header.tableOffset, entries, tableSize) &&
            ReadAt(handle, header.tableOffset + tableSize, names, header.namesSize))
            return true;
    }
    CloseResource();
    return game::FatalError("CMappedResource::OpenResource()\nUnable to read file \"" + path + "\"");
}

bool MappedResource::OpenFromMemory(const void* memoryImage) {
    CloseResource();
    // GetFile hands out pointers into the image (writable through them).
    char* base = const_cast<char*>(static_cast<const char*>(memoryImage));
    image = base;
    lib::LoadBytes(header, base);
    if (header.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
    entries = reinterpret_cast<ResEntry*>(base + header.tableOffset);
    names = base + header.tableOffset + header.count * sizeof(ResEntry);
    return true;
}

void MappedResource::CloseResource() {
    FreeAllFiles();
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    nameTable = {};
    image = nullptr;
    header = ResHeader{};
}

// Rebirth's ResOverride sends every call to addon.
void* MappedResource::GetFile(std::string_view name, std::uint32_t* size) {
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) return modfolders::GetFile(this, name, size);
    return GetFilePlain(name, size);
}

void* MappedResource::GetFilePlain(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;

    // Already loaded: share it.
    {
        std::string key(name);
        if (auto node = filesByName.find(key); node != filesByName.end()) {
            LoadedFile* file = node->second;
            ++file->refCount;
            if (size) *size = file->size;
            return file->data;
        }
    }

    int index = FindEntry(name);
    if (index == -1) return nullptr;
    const ResEntry& entry = entries[index];

    // An archive image in memory hands out pointers into the image (size is not reported).
    if (image) return image + entry.offset;

    auto* file = new LoadedFile;
    {
        std::string requested(name);
        file->name.assign(requested.data(), requested.data() + requested.size());
    }
    file->refCount = 1;
    file->size = entry.size;
    file->data = ::operator new(file->size);
    file->offset = entry.offset;

    if (ReadAt(handle, file->offset, file->data, file->size)) {
        if (size) *size = file->size;
        {
            std::string key(file->name);
            filesByName[key] = file;
        }
        filesByData[file->data] = file;
        return file->data;
    }

    // Read failed. file->data leaks here (kept).
    delete file;
    return nullptr;
}

bool MappedResource::CloseFile(void* data) {
    if (image) return true;
    if (!data) return false;

    auto byData = filesByData.find(data);
    if (byData == filesByData.end()) {
        game::DebugLog("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    LoadedFile* file = byData->second;
    if (--file->refCount != 0) return true;

    if (writable) {
        if (!WriteAt(handle, file->offset, file->data, file->size)) {
            // The file name is the formatted data pointer.
            std::string name;
            name = static_cast<const char*>(data);
            game::FatalError("CMappedResource::CloseFile()\nUnable to write file \"" + name + "\"\n");
        }
    }

    filesByData.erase(byData);
    filesByName.erase(file->name);
    ::operator delete(file->data);
    delete file;
    return true;
}

void MappedResource::FreeAllFiles() {
    if (image) return;
    for (auto& [data, file] : filesByData) {
        ::operator delete(file->data);
        delete file;
    }
    filesByData.clear();
    filesByName.clear();
}

MappedFile::MappedFile() : file(INVALID_HANDLE_VALUE) {}

void MappedFile::Close() {
    if (view) {
        gameapi::UnmapViewOfFile(view);
        view = nullptr;
    }
    if (mapping) {
        gameapi::CloseHandle(mapping);
        mapping = nullptr;
    }
    if (file != INVALID_HANDLE_VALUE) {
        gameapi::CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
    size = 0;
}

bool MappedFile::Open(const std::string& path, bool writable) {
    if (writable) game::DebugLog("CMappedFile::Open().\nMapping file \"%s\" with writting access.\n", path);

    // Unbuffered I/O only when the volume's sector size divides the page size.
    char root[4];
    DWORD bytesPerSector = 0xFFFFFFFF, sectorsPerCluster, freeClusters, totalClusters;
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    std::strncpy(root, path.c_str(), 3);
    root[3] = '\0';
    const char* rootPath = root;
    if (!text::IsAlpha(root[0]) || root[1] != ':' || root[2] != '\\') rootPath = nullptr;
    gameapi::GetDiskFreeSpaceA(rootPath, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    DWORD flags = (info.dwPageSize < bytesPerSector || info.dwPageSize % bytesPerSector != 0)
                      ? FILE_FLAG_RANDOM_ACCESS | FILE_ATTRIBUTE_NORMAL
                      : FILE_FLAG_NO_BUFFERING | FILE_ATTRIBUTE_NORMAL;

    file = gameapi::CreateFileA(path.c_str(), writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, flags, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        gameapi::GetLastError();
        char message[256];
        std::snprintf(message, sizeof(message), "CMappedFile::Open().\nUnable to open file \"%s\".\n", path.c_str());
        game::DebugLog(message);
        return false;
    }

    size = gameapi::GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE) {
        game::FatalError("CMappedFile::Open().\nUnable to determine file size.\n");
    } else if (size > 0x80000000) {
        game::FatalError("CMappedFile::Open().\nToo large file to map.\n");
    } else {
        mapping = gameapi::CreateFileMappingA(file, nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) {
            game::FatalError("CMappedFile::Open().\nUnable to create file mapping.\n");
        } else {
            view = gameapi::MapViewOfFile(mapping, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
            if (view) return true;
            game::FatalError("CMappedFile::Open( \"%s\" ).\nUnable to create view of file (error=0x%X).\n", path,
                             gameapi::GetLastError());
        }
    }
    Close();
    return false;
}

void MappedFile::Flush() {
    if (!view) return;
    gameapi::FlushViewOfFile(view, 0);
    SYSTEMTIME now;
    gameapi::GetSystemTime(&now);
    BY_HANDLE_FILE_INFORMATION info;
    gameapi::GetFileInformationByHandle(file, &info);
    FILETIME time;
    gameapi::SystemTimeToFileTime(&now, &time);
    gameapi::SetFileTime(file, nullptr, nullptr, &time);
}


bool MMIOResource::OpenResource(const std::string& path, bool forWriting) {
    if (!file.Open(path, forWriting)) return false;
    if (OpenFromMemory(file.view)) return true;
    CloseResource();
    return false;
}

bool MMIOResource::OpenFromMemory(const void* memoryImage) {
    char* base = const_cast<char*>(static_cast<const char*>(memoryImage));
    mappedImage = base;
    lib::LoadBytes(header, base);
    if (header.magic != kResMagic) {
        game::FatalError("Wrong format resource");
        CloseResource();
        return false;
    }
    entries = reinterpret_cast<ResEntry*>(base + header.tableOffset);
    names = base + header.tableOffset + header.count * sizeof(ResEntry);
    return true;
}


void MMIOResource::CloseResource() {
    file.Close();
    entries = nullptr;
    names = nullptr;
    mappedImage = nullptr;
    header = ResHeader{};
}

void MMIOResource::Flush() { file.Flush(); }

void* MMIOResource::GetFile(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;
    int index = FindEntry(name);
    if (index == -1) return nullptr;
    if (size) *size = entries[index].size;
    return mappedImage + entries[index].offset;
}

bool MMIOResource::CloseFile(void*) { return true; }

void MMIOResource::FreeAllFiles() {}
