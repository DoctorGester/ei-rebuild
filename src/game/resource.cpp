#include "game/bink_video.h"
#include "game/core_streams.h"
#include "game/resource.h"
#include "game/resource_stream.h"
#include "game/mod_folders.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
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

MappedResource::~MappedResource() {
    // Rebirth's ResOverride: addon forgets the archive first.
    if (modfolders::Active()) modfolders::OnDestroyResource(this);
    CloseResource();
}

// The archive's reads are File's ReadAt (SetFilePointer, then ReadFile; VC6's _lseek and _read on a binary descriptor
// did the same calls).
bool MappedResource::OpenResource(const std::string& path) {
    // Rebirth's ResOverride: addon loads the mod's archives on the first open and records
    // which archive this is.
    if (modfolders::Active()) modfolders::OnOpenResource(this, path);
    CloseResource();
    if (!archive.Open(path, platform::FileMode::Read))
        game::FatalExit("CMappedResource::OpenResource()\nUnable to open file \"" + path + "\"");
    const std::string unreadable = "CMappedResource::OpenResource()\nUnable to read file \"" + path + "\"";
    if (!archive.ReadAt(0, &header, sizeof(header))) game::FatalExit(unreadable);
    if (header.magic != kResMagic) game::FatalExit("Wrong format resource file \"" + path + "\"");
    unsigned tableSize = header.count * sizeof(ResEntry);
    entryTable.resize(header.count);
    nameTable.resize(header.namesSize);
    entries = entryTable.data();
    names = nameTable.data();
    if (!archive.ReadAt(static_cast<long>(header.tableOffset), entries, tableSize) ||
        !archive.ReadAt(static_cast<long>(header.tableOffset + tableSize), names, header.namesSize))
        game::FatalExit(unreadable);
    return true;
}

void MappedResource::ReadImage(char* base) {
    header = lib::Load<ResHeader>(base);
    if (header.magic != kResMagic) game::FatalExit("Wrong format resource");
    entryTable.resize(header.count);
    if (header.count != 0)
        std::memcpy(entryTable.data(), base + header.tableOffset, header.count * sizeof(ResEntry));
    entries = entryTable.data();
    names = base + header.tableOffset + header.count * sizeof(ResEntry);
}

void MappedResource::OpenFromMemory(const void* memoryImage) {
    CloseResource();
    // GetFile hands out pointers into the image (writable through them).
    image = const_cast<char*>(static_cast<const char*>(memoryImage));
    ReadImage(image);
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

// With a mod, Rebirth's ResOverride sends every call to addon.
void* MappedResource::GetFile(std::string_view name, std::uint32_t* size) {
    if (modfolders::Active()) return modfolders::GetFile(this, name, size);
    return GetFilePlain(name, size);
}

void* MappedResource::GetFilePlain(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;

    // Already loaded: share it.
    if (auto node = filesByName.find(name); node != filesByName.end()) {
        LoadedFile* file = node->second;
        ++file->refCount;
        if (size) *size = file->size;
        return file->bytes.data();
    }

    int index = FindEntry(name);
    if (index == -1) return nullptr;
    const ResEntry& entry = entries[index];

    // An archive image in memory hands out pointers into the image (size is not reported).
    if (image) return image + entry.offset;

    auto file = std::make_unique<LoadedFile>();
    file->name = name;
    file->refCount = 1;
    file->size = entry.size;
    file->bytes.resize(std::max(entry.size, 1u));
    void* data = file->bytes.data();
    if (!archive.ReadAt(static_cast<long>(entry.offset), data, entry.size)) return nullptr;
    if (size) *size = entry.size;
    filesByName[file->name] = file.get();
    filesByData[data] = std::move(file);
    return data;
}

bool MappedResource::CloseFile(void* data) {
    if (image) return true;
    if (!data) return false;

    auto byData = filesByData.find(data);
    if (byData == filesByData.end()) {
        // Rebirth's ResOverride: a file one of the mod's archives gave in this one's place goes back to it.
        if (modfolders::Active())
            if (MappedResource* mod = modfolders::ArchiveHolding(this, data)) return mod->CloseFile(data);
        game::DebugLog("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    LoadedFile* file = byData->second.get();
    if (--file->refCount != 0) return true;

    filesByName.erase(file->name);
    filesByData.erase(byData);
    return true;
}

void MappedResource::FreeAllFiles() {
    if (image) return;
    filesByName.clear();
    filesByData.clear();
}

bool MMIOResource::OpenResource(const std::string& path) {
    if (!file.Open(path)) {
        game::DebugLog("CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        return false;
    }
    OpenFromMemory(file.Data());
    return true;
}

void MMIOResource::OpenFromMemory(const void* memoryImage) {
    mappedImage = const_cast<char*>(static_cast<const char*>(memoryImage));
    ReadImage(mappedImage);
}

void MMIOResource::CloseResource() {
    file.Close();
    entries = nullptr;
    names = nullptr;
    entryTable = {};
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
