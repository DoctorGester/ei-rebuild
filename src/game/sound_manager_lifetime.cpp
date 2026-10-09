// The sound manager's engine object slots, its wave sources and sound folder, and the music
// stream's file reading (A-H14). The constructor / destructor bodies (006B6040 / 006B65D0) and the
// Miles setup (Reset 006B73E0, Close 006B6D80) stay original.
#include "sound.h"
#include "game_strings.h"
#include "originals_engine.h"
#include "originals_library.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"

#include "ei/hook.h"
#include "ei/vcall.h"

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

__declspec(naked) void CSoundManager::Close() EI_ORIGINAL(0x006B6D80)
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

// --- A-H15: constructor, destructor, stream reset ---------------------------------------------------

namespace {

constexpr std::uintptr_t kWaveSourceListVtable = std::uintptr_t(0x00744130);
constexpr std::uintptr_t kCacheListVtable = std::uintptr_t(0x00744124);
constexpr int kListBlockSize = 10;
constexpr unsigned kMapBuckets = 100;
constexpr int kFullVolume = 100;
constexpr int kNoSwitch = 4;   // RequestStream's "switch at the current position"

// The element constructors / destructors the CRT's eh vector iterators run on the emitters and the
// sequences (compiler-generated; config/library.csv).
constexpr std::uintptr_t kEmitterConstruct = std::uintptr_t(0x006B63D0);
constexpr std::uintptr_t kEmitterDestruct = std::uintptr_t(0x006B63E0);
constexpr std::uintptr_t kSequenceConstruct = std::uintptr_t(0x006B63F0);
constexpr std::uintptr_t kSequenceDestruct = std::uintptr_t(0x006B6400);

// The two STLport hash maps' members (library instantiations, RET 0x10 / plain RET).
__declspec(naked) void __fastcall CacheMapConstruct(void* /*map*/, void*, unsigned /*buckets*/, const void* /*hash*/,
                                                    const void* /*equal*/, const void* /*alloc*/) EI_ORIGINAL(0x006BBF90)
__declspec(naked) void __fastcall DeferredMapConstruct(void* /*map*/, void*, unsigned /*buckets*/, const void* /*hash*/,
                                                       const void* /*equal*/, const void* /*alloc*/) EI_ORIGINAL(0x006BC2B0)
__declspec(naked) void __fastcall CacheMapClear(void* /*map*/) EI_ORIGINAL(0x006BCE00)
__declspec(naked) void __fastcall DeferredMapClear(void* /*map*/) EI_ORIGINAL(0x006BD100)
__declspec(naked) void __fastcall CacheBucketsDestruct(void* /*buckets*/) EI_ORIGINAL(0x006BC4D0)
__declspec(naked) void __fastcall DeferredBucketsDestruct(void* /*buckets*/) EI_ORIGINAL(0x006BC510)
// DestructElements<WaveRecord> (the record's name string).
__declspec(naked) void __stdcall WaveRecordsDestruct(WaveRecord* /*first*/, int /*count*/) EI_ORIGINAL(0x006BE210)
// Miles (game.exe's imports 0073B7D4 / 0073B7D0).
using AilVoidFn = void(__stdcall*)();
AilVoidFn AilStartup() { return *reinterpret_cast<AilVoidFn*>(std::uintptr_t(0x0073B7D4)); }
AilVoidFn AilShutdown() { return *reinterpret_cast<AilVoidFn*>(std::uintptr_t(0x0073B7D0)); }

template <typename T, typename E> void ConstructList(mfc::CList<T, E>& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = kListBlockSize;
}

void ConstructFader(SoundFader& fader) {
    fader.volume = kFullVolume;
    fader.Recalc();
    fader.SetHold(0);
}

} // namespace

CSoundManager* CSoundManager::Construct() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    ClearFields();
    streamReader.Construct();
    new (&soundPath) mfc::CString();
    soundRegistry.Construct();
    archive074.Construct();
    registry0CC.Construct();
    ConstructList(waveSources, kWaveSourceListVtable);
    musicFader.volume = kFullVolume;
    musicFader.Recalc();
    musicFader.SetHold(0);
    new (&streamName) mfc::CString();
    new (&nextName) mfc::CString();
    markers.Construct();
    markers2.Construct();
    gameapi::InitializeCriticalSection(Lock(nameLock));
    gameapi::InitializeCriticalSection(Lock(streamLock));
    new (&midiName) mfc::CString();
    new (&midiNextName) mfc::CString();
    midiArray1E4.Construct();
    midiArray1F4.Construct();
    ConstructFader(midiFader);
    gameapi::InitializeCriticalSection(Lock(midiLock));
    EhVectorConstructorIterator(emitters, sizeof(SoundEmitter), 16, reinterpret_cast<void*>(kEmitterConstruct),
                                reinterpret_cast<void*>(kEmitterDestruct));
    EhVectorConstructorIterator(sequences, sizeof(SoundSequence), 8, reinterpret_cast<void*>(kSequenceConstruct),
                                reinterpret_cast<void*>(kSequenceDestruct));
    ConstructList(cacheList, kCacheListVtable);
    // The functor and allocator tags are empty objects (the original passes stack bytes).
    std::uint8_t tags[3] = {};
    CacheMapConstruct(cache, nullptr, kMapBuckets, &tags[0], &tags[1], &tags[2]);
    DeferredMapConstruct(&deferred, nullptr, kMapBuckets, &tags[0], &tags[1], &tags[2]);
    ei::SetVtable(this, kVtableAddress);
    AilStartup()();
    Reset();
    return this;
}

void CSoundManager::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    Close();
    AilShutdown()();
    DeferredMapClear(&deferred);
    DeferredBucketsDestruct(&deferred.buckets);
    CacheMapClear(cache);
    CacheBucketsDestruct(cacheBuckets);
    cacheList.vtable = reinterpret_cast<void*>(kCacheListVtable);
    for (WaveNode* node = cacheList.head; node; node = node->next) WaveRecordsDestruct(&node->data, 1);
    cacheList.RemoveAll();
    EhVectorDestructorIterator(sequences, sizeof(SoundSequence), 8, reinterpret_cast<void*>(kSequenceDestruct));
    EhVectorDestructorIterator(emitters, sizeof(SoundEmitter), 16, reinterpret_cast<void*>(kEmitterDestruct));
    gameapi::DeleteCriticalSection(Lock(midiLock));
    midiArray1F4.FreeData();
    midiArray1E4.FreeData();
    midiNextName.~CString();
    midiName.~CString();
    gameapi::DeleteCriticalSection(Lock(streamLock));
    gameapi::DeleteCriticalSection(Lock(nameLock));
    markers2.FreeData();
    markers.FreeData();
    nextName.~CString();
    streamName.~CString();
    waveSources.vtable = reinterpret_cast<void*>(kWaveSourceListVtable);
    waveSources.RemoveAll();
    registry0CC.Destroy();
    MappedResourceDestroyOriginal(&archive074);
    soundRegistry.Destroy();
    soundPath.~CString();
    streamReader.Destruct();
    DestructBase();
}

void CSoundManager::ResetStream() {
    streamState = 0;
    streamSample = nullptr;
    unknown1AC = 0;
    streamFile = nullptr;
    streamCodec = nullptr;
    unknown118 = 0;
    unknown114 = 0;
    unknown110 = 0;
    unknown11C = -1;
    unknown12C = -1;
    unknown128 = -1;
    unknown124 = -1;
    streamPosition = -1;
    streamSwitchAt = -1;
    unknown138 = -1;
    unknown13C = -1;
    unknown140 = 0;
    streamName = game::EmptyText();
    nextName = game::EmptyText();
    streamReadPosition = 0;
    streamFileSize = 0;
    switchMode = kNoSwitch;
    ConstructFader(musicFader);
}

// 006BA9C0 runs on the manager's +0x100 (the MP3 stream part).
void __fastcall SoundResetStream(void* stream) {
    auto* manager = reinterpret_cast<CSoundManager*>(static_cast<char*>(stream) - offsetof(CSoundManager, streamState));
    manager->ResetStream();
}

EI_REPLACE_METHOD(0x006B6040, CSoundManager::Construct);
EI_REPLACE_METHOD(0x006B65D0, CSoundManager::DestructBody);
EI_REPLACE(0x006BA9C0, SoundResetStream);
