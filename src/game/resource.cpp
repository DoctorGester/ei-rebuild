#include "bink_video.h"
#include "core_streams.h"
#include "resource.h"
#include "resource_stream.h"
#include "mod_folders.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include "lib/text_case.h"

#include <cstdio>

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

// The archive's reads are File's ReadAt (SetFilePointer, then ReadFile; VC6's _lseek and _read on a binary descriptor
// did the same calls).
MappedResource::MappedResource() { image = nullptr; }

MappedResource::~MappedResource() {
    // Rebirth's ResOverride: addon forgets the archive first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) modfolders::OnDestroyResource(this);
    MappedResource::FreeAllFiles();   // called directly, not through the vtable
    archive.Close();
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    nameTable = {};
    image = nullptr;
    header = ResHeader{};
    // ~ResourceBase follows.
}

void MappedResource::Destroy() { this->MappedResource::~MappedResource(); }

bool MappedResource::OpenResource(const std::string& path) {
    // Rebirth's ResOverride: addon loads the mod's archives on the first open and records
    // which archive this is.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) modfolders::OnOpenResource(this, path);
    CloseResource();
    if (!archive.Open(path, platform::FileMode::Read))
        return game::FatalError("CMappedResource::OpenResource()\nUnable to open file \"" + path + "\"");

    if (archive.ReadAt(0, &header, sizeof(header))) {
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
        if (archive.ReadAt(static_cast<long>(header.tableOffset), entries, tableSize) &&
            archive.ReadAt(static_cast<long>(header.tableOffset + tableSize), names, header.namesSize))
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
    archive.Close();
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    nameTable = {};
    image = nullptr;
    header = ResHeader{};
}

// Rebirth's ResOverride sends every call to addon.
void* MappedResource::GetFile(std::string_view name, std::uint32_t* size) {
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) return modfolders::GetFile(this, name, size);
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

    if (archive.ReadAt(static_cast<long>(file->offset), file->data, file->size)) {
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

bool MMIOResource::OpenResource(const std::string& path) {
    if (!file.Open(path)) {
        game::DebugLog("CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        return false;
    }
    if (OpenFromMemory(file.Data())) return true;
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

void* MMIOResource::GetFile(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;
    int index = FindEntry(name);
    if (index == -1) return nullptr;
    if (size) *size = entries[index].size;
    return mappedImage + entries[index].offset;
}

bool MMIOResource::CloseFile(void*) { return true; }

void MMIOResource::FreeAllFiles() {}
