// The sound manager's engine object slots, its wave sources and sound folder, and the music
// stream's file reading (A-H14). The constructor / destructor bodies (006B6040 / 006B65D0) and the
// Miles setup (Reset 006B73E0, Close 006B6D80) stay original.
#include "sound.h"
#include "game_strings.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"

#include "ei/hook.h"

#include <new>

namespace {
constexpr int kSoundManagerClassId = 0xE;
constexpr std::uintptr_t kSoundManagerClassNameAddress = std::uintptr_t(0x007968C0);   // "CSoundManager"
constexpr std::size_t kArchiveBytes = sizeof(CMappedResource);
const char* const kBackslash = "\\";

int CompareText(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
CRITICAL_SECTION* Lock(std::uint8_t* storage) { return reinterpret_cast<CRITICAL_SECTION*>(storage); }
} // namespace

__declspec(naked) void CSoundManager::DestructBody() EI_ORIGINAL(0x006B65D0)
__declspec(naked) void CSoundManager::Reset() EI_ORIGINAL(0x006B73E0)

const char* __fastcall SoundManagerClassName(CSoundManager*) {
    return reinterpret_cast<const char*>(kSoundManagerClassNameAddress);
}
int __fastcall SoundManagerClassId(CSoundManager*) { return kSoundManagerClassId; }
EI_REPLACE(0x006B6290, SoundManagerClassName);
EI_REPLACE(0x006B62A0, SoundManagerClassId);

int CSoundManager::CreateBody() {
    Reset();
    return 1;
}
EI_REPLACE_METHOD(0x006B6D70, CSoundManager::CreateBody);

CSoundManager* CSoundManager::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006B62B0, CSoundManager::DeletingDestruct);

// 006B7060. The list's RemoveHead (inlined), then the archive's deleting destructor.
void CSoundManager::ClearWaveSources() {
    while (waveSources.count != 0) {
        CMappedResource* source = waveSources.RemoveHead();
        delete source;
    }
}
EI_REPLACE_METHOD(0x006B7060, CSoundManager::ClearWaveSources);

// 006B7AB0. Called at startup (00456440) for the two sound archives. A failed allocation is not
// tested (the open call goes through a null object, as in the original).
int CSoundManager::AddWaveSource(const char* path) {
    if (enabled == 0) return 1;
    void* memory = gamecrt::operator_new(kArchiveBytes);
    CMappedResource* source = memory != nullptr ? static_cast<CMappedResource*>(memory)->Construct() : nullptr;
    if (source->OpenResource(path, 0) == 0) {
        delete source;
        return 0;
    }
    waveSources.AddTail(source);
    return 1;
}
EI_REPLACE_METHOD(0x006B7AB0, CSoundManager::AddWaveSource);

// 006B8960. From the map's ambient sounds (006C68B0, 006CB390).
void* CSoundManager::LoadWaveFile(const char* name, std::uint32_t* size, CMappedResource** source) {
    for (auto* node = waveSources.head; node != nullptr;) {
        CMappedResource* current = node->data;
        node = node->next;
        void* data = current->GetFile(name, size);
        if (data != nullptr) {
            if (source != nullptr) *source = current;
            return data;
        }
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x006B8960, CSoundManager::LoadWaveFile);

// 006BAE20. Called at startup (00456440).
int CSoundManager::SetPath(const char* folder, const char* registryPath) {
    if (enabled == 0) return 1;
    soundPath = folder;
    bool missing;
    {
        mfc::CString last = soundPath.Right(1);
        missing = CompareText(last, kBackslash) != 0;
    }
    if (missing) soundPath += kBackslash;
    return soundRegistry.Open(registryPath) != 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x006BAE20, CSoundManager::SetPath);

mfc::CString CSoundManager::CurrentStreamName() {
    gameapi::EnterCriticalSection(Lock(nameLock));
    mfc::CString name;
    if (streamState == 1 || streamState == 2) name = streamName;
    else name = game::EmptyText();
    gameapi::LeaveCriticalSection(Lock(nameLock));
    return name;
}
EI_REPLACE_METHOD(0x006BAEA0, CSoundManager::CurrentStreamName);

int __stdcall SoundStreamRead(CSoundManager* manager, void* buffer, int size, int offset) {
    if (offset >= 0) {
        manager->streamReader.Seek(offset, 0);
        manager->streamReadPosition = offset;
    }
    int left = manager->streamFileSize - manager->streamReadPosition;
    if (size > left) size = left;
    if (manager->streamReader.Read(static_cast<char*>(buffer), size) < 0) return 0;
    manager->streamReadPosition += size;
    return size;
}
EI_REPLACE(0x006BA950, SoundStreamRead);
