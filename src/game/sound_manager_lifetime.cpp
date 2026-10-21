// The sound manager's engine object slots, its wave sources and sound folder, and the music
// stream's file reading (A-H14); the constructor / destructor bodies (006B6040 / 006B65D0, A-H15);
// the Miles setup Open / Close / Reset (006B6A30 / 006B6D80 / 006B73E0, A-H18).
#include "sound.h"
#include "options.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "record_arrays.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/mfc_thread.h"
#include "game_data.h"
#include "lib/mfc_slots.h"

#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
constexpr int kSoundManagerClassId = 0xE;
constexpr const char* kSoundManagerClassNameAddress = "CSoundManager";
constexpr std::size_t kArchiveBytes = sizeof(CMappedResource);
const char* const kBackslash = "\\";

int CompareText(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}
CRITICAL_SECTION* Lock(std::uint8_t* storage) { return reinterpret_cast<CRITICAL_SECTION*>(storage); }
} // namespace

const char* CSoundManager::GetClassName() { return kSoundManagerClassNameAddress; }
int CSoundManager::GetClassId() { return kSoundManagerClassId; }

int CSoundManager::CreateBody() {
    Reset();
    return 1;
}
EI_REWRITTEN(0x006B6D70, CSoundManager::CreateBody);

CSoundManager* CSoundManager::DeletingDestruct(unsigned flags) {
    this->CSoundManager::~CSoundManager();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x006B62B0, CSoundManager::DeletingDestruct);

// 006B7060. The list's RemoveHead (inlined), then the archive's deleting destructor.
void CSoundManager::ClearWaveSources() {
    while (waveSources.count != 0) {
        CMappedResource* source = waveSources.RemoveHead();
        delete source;
    }
}
EI_REWRITTEN(0x006B7060, CSoundManager::ClearWaveSources);

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
EI_REWRITTEN(0x006B7AB0, CSoundManager::AddWaveSource);

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
EI_REWRITTEN(0x006B8960, CSoundManager::LoadWaveFile);

// 006B7110 (A-H26; Rebirth-patched: its ListFiles call is redirected, so this is the vanilla body,
// hooked in the vanilla launch only). From the unit database's sound counts (0053A8E0), on the
// game's main thread.
void CSoundManager::ListWaveFiles(mfc::CStringArray* out, const char* pattern) {
    out->SetSize(0, -1);
    mfc::CStringArray names;
    names.Construct();
    for (auto* node = waveSources.head; node != nullptr;) {
        CMappedResource* source = node->data;
        node = node->next;
        source->ListFiles(&names, pattern);
        for (int i = 0; i < names.size; ++i) out->SetAtGrow(out->size, names[i]);
    }
    names.Destruct();
}
EI_REPLACE_METHOD(0x006B7110, CSoundManager::ListWaveFiles);

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
EI_REWRITTEN(0x006BAEA0, CSoundManager::CurrentStreamName);

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

// Miles (game.exe's imports 0073B7D4 / 0073B7D0).
using AilVoidFn = void(__stdcall*)();
AilVoidFn AilStartup() { return ei::GameData<AilVoidFn>(0x0073B7D4); }
AilVoidFn AilShutdown() { return ei::GameData<AilVoidFn>(0x0073B7D0); }

template <typename T, typename E> void ConstructList(mfc::CList<T, E>& list, void* vtable) {
    list.vtable = vtable;
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

// The emitters' and sequences' element constructors (thiscall, return this) and destructors
// (compiler-generated: the name array at +4; S16, on the standalone boot path).
SoundEmitter* __fastcall ConstructEmitter(SoundEmitter* emitter) {
    emitter->names.Construct();
    return emitter;
}
EI_REPLACE(0x006B63D0, ConstructEmitter);
void __fastcall DestructEmitter(SoundEmitter* emitter) { emitter->names.Destruct(); }
EI_REPLACE(0x006B63E0, DestructEmitter);
SoundSequence* __fastcall ConstructSequence(SoundSequence* sequence) {
    sequence->names.Construct();
    return sequence;
}
EI_REPLACE(0x006B63F0, ConstructSequence);
void __fastcall DestructSequence(SoundSequence* sequence) { sequence->names.Destruct(); }
EI_REPLACE(0x006B6400, DestructSequence);

// The strings and the archive are members the compiler constructs (before the body here; between
// the stream reader and the registries in the original).
CSoundManager::CSoundManager() {
    ClearFields();
    streamReader.Construct();
    soundRegistry.Construct();
    registry0CC.Construct();
    ConstructList(waveSources, mfc::impl::SoundListClass::Vtable());
    musicFader.volume = kFullVolume;
    musicFader.Recalc();
    musicFader.SetHold(0);
    markers.Construct();
    markers2.Construct();
    gameapi::InitializeCriticalSection(Lock(nameLock));
    gameapi::InitializeCriticalSection(Lock(streamLock));
    midiArray1E4.Construct();
    midiArray1F4.Construct();
    ConstructFader(midiFader);
    gameapi::InitializeCriticalSection(Lock(midiLock));
    libcrt::ConstructElements(emitters, sizeof(SoundEmitter), 16, reinterpret_cast<libcrt::ElementFunction>(kEmitterConstruct),
                              reinterpret_cast<libcrt::ElementFunction>(kEmitterDestruct));
    libcrt::ConstructElements(sequences, sizeof(SoundSequence), 8, reinterpret_cast<libcrt::ElementFunction>(kSequenceConstruct),
                              reinterpret_cast<libcrt::ElementFunction>(kSequenceDestruct));
    ConstructList(cacheList, ei::GamePointer<void>(kCacheListVtable));
    // hashtable(n, hash, equal, allocator) (006BBF90 / 006BC2B0); the functor bytes, never read,
    // are 0 here (the original's are stack bytes).
    cache.Construct(kMapBuckets, 0, 0, 0);
    deferred.Construct(kMapBuckets);
    AilStartup()();
    Reset();
}

CSoundManager* CSoundManager::Construct() { return new (this) CSoundManager(); }

// The strings and the archive are destroyed after the body (the archive after the registries; in the
// original between them, through 006714A0's address for Rebirth: the same destructor in the image).
CSoundManager::~CSoundManager() {
    Close();
    AilShutdown()();
    deferred.Destroy();   // clear 006BD100, the buckets 006BC510
    cache.Destroy();      // clear 006BCE00, the buckets 006BC4D0
    cacheList.vtable = ei::GamePointer<void>(kCacheListVtable);
    for (WaveNode* node = cacheList.head; node; node = node->next) recordarrays::Waves::DestructElements(&node->data, 1);
    cacheList.RemoveAll();
    libcrt::DestroyElements(sequences, sizeof(SoundSequence), 8, reinterpret_cast<libcrt::ElementFunction>(kSequenceDestruct));
    libcrt::DestroyElements(emitters, sizeof(SoundEmitter), 16, reinterpret_cast<libcrt::ElementFunction>(kEmitterDestruct));
    gameapi::DeleteCriticalSection(Lock(midiLock));
    midiArray1F4.FreeData();
    midiArray1E4.FreeData();
    gameapi::DeleteCriticalSection(Lock(streamLock));
    gameapi::DeleteCriticalSection(Lock(nameLock));
    markers2.FreeData();
    markers.FreeData();
    waveSources.vtable = mfc::impl::SoundListClass::Vtable();
    waveSources.RemoveAll();
    registry0CC.Destroy();
    soundRegistry.Destroy();
    streamReader.Destruct();
}

void CSoundManager::DestructBody() { this->CSoundManager::~CSoundManager(); }

void CSoundManager::ResetStream() {
    streamState = 0;
    streamSample = nullptr;
    streamDecoder = nullptr;
    streamFile = nullptr;
    streamCodec = nullptr;
    streamBuffers[2] = nullptr;
    streamBuffers[1] = nullptr;
    streamBuffers[0] = nullptr;
    streamBufferSize = -1;
    streamSpareBuffer = -1;
    streamHalfBuffer[1] = -1;
    streamHalfBuffer[0] = -1;
    streamPosition = -1;
    streamSwitchAt = -1;
    streamLoopAt = -1;
    streamFrameBytes = -1;
    streamSilence = 0;
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
EI_REWRITTEN(0x006B65D0, CSoundManager::DestructBody);
EI_REWRITTEN(0x006BA9C0, SoundResetStream);

// --- A-H18: Open, Close, Reset ------------------------------------------------------------------------

int __fastcall SoundCloseStream(void* /*stream*/);
namespace {

constexpr int kVoiceCount = 15;
constexpr int kWaveOutMode = 2;             // the sound option choosing the wave mapper over DirectSound
constexpr int kWaveMapper = -1;
constexpr unsigned kMaxRate = 44100;
constexpr int kStreamThreadPriority = 15;   // THREAD_PRIORITY_TIME_CRITICAL
// Miles preferences (AIL_set_preference).
constexpr unsigned kPrefReverb = 0x28;
constexpr int kReverbBufferBytes = 0x40000;
constexpr unsigned kPrefMixerChannels = 1;
constexpr int kMixerChannels = 0x12;
constexpr unsigned kPrefUseWaveOut = 0xF;

// The sound option (0 off: startup does not open sound; 2 the wave mapper).
int SoundOption() { return options::TheOptions()->audio_; }

// The voice constructor / destructor the CRT's eh vector iterators run (compiler-generated, the
// name string's; config/library.csv), the DirectSoundEnumerateA callback appending each device's
// GUID pointer to an aligned array (006B68A0), the stream thread's entry (006B6A10 -> 006BB910).
constexpr std::uintptr_t kVoiceConstruct = std::uintptr_t(0x006B6CC0);
constexpr std::uintptr_t kVoiceDestruct = std::uintptr_t(0x006B6D30);
constexpr std::uintptr_t kDeviceCallback = std::uintptr_t(0x006B68A0);

// DirectSound and Miles through game.exe's import table (all __stdcall).
using EnumerateFn = long(__stdcall*)(void*, void*);
using PreferenceFn = int(__stdcall*)(unsigned, int);
using WaveOutOpenFn = int(__stdcall*)(void**, void*, int, const void*);
using LastErrorFn = const char*(__stdcall*)();
using DriverWindowFn = void(__stdcall*)(void*, void*);
using AllocateSampleFn = void*(__stdcall*)(void*);
using SampleUserDataFn = void(__stdcall*)(void*, unsigned, std::intptr_t);
using HandleFn = void(__stdcall*)(void*);
EnumerateFn DirectSoundEnumerate() { return ei::GameData<EnumerateFn>(0x0073B04C); }
PreferenceFn AilSetPreference() { return ei::GameData<PreferenceFn>(0x0073B7A0); }
WaveOutOpenFn AilWaveOutOpen() { return ei::GameData<WaveOutOpenFn>(0x0073B7A4); }
LastErrorFn AilLastError() { return ei::GameData<LastErrorFn>(0x0073B7A8); }
DriverWindowFn AilSetDirectSoundWindow() { return ei::GameData<DriverWindowFn>(0x0073B7AC); }
AllocateSampleFn AilAllocateSample() { return ei::GameData<AllocateSampleFn>(0x0073B7B0); }
SampleUserDataFn AilSetSampleUserData() { return ei::GameData<SampleUserDataFn>(0x0073B7C8); }
HandleFn AilReleaseSample() { return ei::GameData<HandleFn>(0x0073B788); }
HandleFn AilEndSequence() { return ei::GameData<HandleFn>(0x0073B800); }
HandleFn AilReleaseSequence() { return ei::GameData<HandleFn>(0x0073B794); }
HandleFn AilMemFree() { return ei::GameData<HandleFn>(0x0073B7FC); }
HandleFn AilWaveOutClose() { return ei::GameData<HandleFn>(0x0073B798); }
HandleFn AilMidiOutClose() { return ei::GameData<HandleFn>(0x0073B79C); }
// The MP3 stream's (OpenStream).
using FindProviderFn = void*(__stdcall*)(const char*, const char*, const char*);
using BufferSizeFn = int(__stdcall*)(void*, int, int);
using MemAllocFn = void*(__stdcall*)(int);
using SampleIntFn = void(__stdcall*)(void*, int);
using SampleTypeFn = void(__stdcall*)(void*, int, int);
using SampleProcessorFn = void*(__stdcall*)(void*, int, void*);
FindProviderFn RibFindFileProvider() { return ei::GameData<FindProviderFn>(0x0073B7E4); }
BufferSizeFn AilMinimumSampleBufferSize() { return ei::GameData<BufferSizeFn>(0x0073B7E0); }
MemAllocFn AilMemAllocLock() { return ei::GameData<MemAllocFn>(0x0073B758); }
HandleFn AilInitSample() { return ei::GameData<HandleFn>(0x0073B808); }
SampleTypeFn AilSetSampleType() { return ei::GameData<SampleTypeFn>(0x0073B7DC); }
SampleIntFn AilSetSamplePlaybackRate() { return ei::GameData<SampleIntFn>(0x0073B7B8); }
SampleIntFn AilSetSampleVolume() { return ei::GameData<SampleIntFn>(0x0073B774); }
SampleIntFn AilSetSamplePan() { return ei::GameData<SampleIntFn>(0x0073B77C); }
SampleProcessorFn AilSetSampleProcessor() { return ei::GameData<SampleProcessorFn>(0x0073B78C); }

// Miles' sample (HSAMPLE), as far as the stream reads it: the functions of its codec stage.
struct MilesSample {
    std::uint8_t unused_000[0x144];
    void* codecFunctions[4];   // +0x144; slot 3 closes a stream
};
static_assert(offsetof(MilesSample, codecFunctions) == 0x144);

constexpr int kStreamRate = 44100;
constexpr int kStereo16 = 3;       // DIG_F_STEREO_16
constexpr int kSignedPcm = 1;      // DIG_PCM_SIGN

void* StreamOf(CSoundManager* manager) { return const_cast<std::int32_t*>(&manager->streamState); }
CSoundManager* ManagerOf(void* stream) {
    return reinterpret_cast<CSoundManager*>(static_cast<char*>(stream) - offsetof(CSoundManager, streamState));
}

// The stream thread's (StreamThread).
using BufferReadyFn = int(__stdcall*)(void*);
using LoadBufferFn = void(__stdcall*)(void*, int, void*, int);
using UserDataFn = std::intptr_t(__stdcall*)(void*, unsigned);
BufferReadyFn AilSampleBufferReady() { return ei::GameData<BufferReadyFn>(0x0073B7E8); }
LoadBufferFn AilLoadSampleBuffer() { return ei::GameData<LoadBufferFn>(0x0073B7EC); }
UserDataFn AilSampleUserData() { return ei::GameData<UserDataFn>(0x0073B7B4); }
constexpr std::uint32_t kStreamPeriodMs = 100;
constexpr int kNoHalfReady = -1;
__declspec(naked) void __fastcall ReadStreamOriginal(CSoundManager*, void*, void* /*stream*/) EI_ORIGINAL(0x006BB3A0)

// PCMWAVEFORMAT, as Open fills it.
#pragma pack(push, 2)
struct PcmFormat {
    std::uint16_t formatTag;
    std::uint16_t channels;
    std::uint32_t samplesPerSecond;
    std::uint32_t bytesPerSecond;
    std::uint16_t blockAlign;
    std::uint16_t bitsPerSample;
};
#pragma pack(pop)
static_assert(sizeof(PcmFormat) == 0x10);

void WaitForThread(const mfc::impl::WinThread* thread) { gameapi::WaitForSingleObject(thread->thread, INFINITE); }

} // namespace

int CSoundManager::CloseStream() {
    using CloseFn = void(__stdcall*)(void*);
    if (streamFile != nullptr && streamCodec != nullptr) ei::VirtualSlot<CloseFn>(streamCodec, 3)(streamFile);
    if (streamSample != nullptr) AilReleaseSample()(streamSample);
    if (streamDecoder != nullptr) AilReleaseSample()(streamDecoder);
    for (void* buffer : streamBuffers)
        if (buffer != nullptr) AilMemFree()(buffer);
    ResetStream();
    return 0;
}

int CSoundManager::OpenStream(void* driver) {
    void* codec = RibFindFileProvider()("ASI codec", "Input file types", ".mp3");
    streamSample = AilAllocateSample()(driver);
    streamDecoder = AilAllocateSample()(driver);
    if (codec == nullptr || streamSample == nullptr || streamDecoder == nullptr) return SoundCloseStream(StreamOf(this));

    streamBufferSize = AilMinimumSampleBufferSize()(driver, kStreamRate, kStereo16) * 2;
    for (void*& buffer : streamBuffers) buffer = AilMemAllocLock()(streamBufferSize);
    AilInitSample()(streamSample);
    AilSetSampleType()(streamSample, kStereo16, kSignedPcm);
    AilSetSamplePlaybackRate()(streamSample, kStreamRate);
    AilSetSampleVolume()(streamSample, 0);
    AilSetSamplePan()(streamSample, 0);
    for (void* buffer : streamBuffers) std::memset(buffer, 0, static_cast<std::uint32_t>(streamBufferSize));
    AilSetSampleProcessor()(streamDecoder, 0, codec);
    AilInitSample()(streamDecoder);
    // (The original also closes the stream when this address is null, which it cannot be.)
    streamCodec = static_cast<MilesSample*>(streamDecoder)->codecFunctions;
    streamHalfBuffer[0] = 0;
    streamHalfBuffer[1] = 1;
    streamSpareBuffer = 2;
    streamPosition = 0;
    streamState = 0;
    // No fade and no hold; the level from the music volume.
    musicFader.SetHold(0);
    return 1;
}

void CSoundManager::StreamThread() {
    std::uint32_t last = gameapi::timeGetTime();
    while (threadsQuit == 0) {
        std::uint32_t now = gameapi::timeGetTime();
        std::uint32_t elapsed = now - last;
        last = now;
        if (elapsed < kStreamPeriodMs) gameapi::Sleep(kStreamPeriodMs - elapsed);

        gameapi::EnterCriticalSection(Lock(streamLock));
        musicFader.Update();
        AilSetSampleVolume()(streamSample, musicFader.level);
        for (;;) {
            int half = AilSampleBufferReady()(streamSample);
            if (half == kNoHalfReady) break;
            // The spare buffer goes into that half; the one it played becomes the spare.
            int buffer = streamSpareBuffer;
            std::int32_t& playing = streamHalfBuffer[half == 0 ? 0 : 1];
            streamSpareBuffer = playing;
            playing = buffer;
            streamPosition += streamBufferFrames;
            AilLoadSampleBuffer()(streamSample, half, streamBuffers[buffer], streamBufferSize);
            ReadStreamOriginal(this, nullptr, StreamOf(this));
        }
        gameapi::LeaveCriticalSection(Lock(streamLock));
    }
    mfc::impl::AfxEndThread(0, 1);
}
EI_REWRITTEN(0x006BB910, CSoundManager::StreamThread);

// 006B6A10: the stream thread's procedure (cdecl): the manager's StreamThread, then 0 (not reached:
// StreamThread ends the thread). S16, on the standalone boot path.
unsigned __cdecl StreamThreadEntry(void* manager) {
    static_cast<CSoundManager*>(manager)->StreamThread();
    return 0;
}
EI_REWRITTEN(0x006B6A10, StreamThreadEntry);

// 006B68A0 (kDeviceCallback; no Ghidra function, S16c): DirectSoundEnumerateA's callback in Open. The
// device's GUID pointer is appended to the aligned array `devices` (its SetSize by one, inlined in the
// original); the enumeration goes on (TRUE).
int __stdcall AddSoundDevice(void* guid, const char*, const char*, void* devices) {
    auto* list = static_cast<lib::AlignedArray<int>*>(devices);
    const int at = list->size;
    list->SetSize(at + 1);
    (*list)[at] = static_cast<int>(reinterpret_cast<std::intptr_t>(guid));
    return 1;
}
EI_REPLACE(0x006B68A0, AddSoundDevice);

// --- A-H26: the stream's reader -----------------------------------------------------------------------

namespace {

// The ASI codec's functions (Miles' ASI_stream_*; all __stdcall) and, after them, its attribute
// names: what it decodes to.
enum CodecSlot {
    kCodecOpen = 0,
    kCodecRead = 1,
    kCodecSeek = 2,
    kCodecClose = 3,
    kCodecAttribute = 4,
    kCodecOutputRate = 11,
    kCodecOutputBits = 12,
    kCodecOutputChannels = 13,
};
using CodecOpenFn = void*(__stdcall*)(void* /*user*/, void* /*read*/, int /*size*/);
using CodecReadFn = int(__stdcall*)(void* /*file*/, void* /*buffer*/, int /*bytes*/);
using CodecSeekFn = void(__stdcall*)(void* /*file*/, int /*position*/);
using CodecCloseFn = void(__stdcall*)(void* /*file*/);
using CodecAttributeFn = int(__stdcall*)(void* /*file*/, void* /*attribute*/);

// The decoder's read callback, by its address (SoundStreamRead, hooked).
constexpr std::uintptr_t kStreamReadCallback = std::uintptr_t(0x006BA950);
constexpr std::uint32_t kSwitchHoldMs = 4000;
constexpr int kNoLoop = 0x7FFFFFFF;
constexpr int kNeverSwitch = -1;
constexpr int kSeekKeep = -1;          // the decoder's "from where the file is"
constexpr int kFullLevel = 0x7F;
constexpr int kCentrePan = 0x40;
constexpr std::uint8_t kSilence8 = 0x7F;
constexpr int kStateSwitching = 2;
constexpr int kSwitchAtEnd = 1;        // RequestStream's modes
constexpr int kSwitchAfterFade = 2;
const char* const kLoopMarkers = "Markers2";
const char* const kSwitchMarkers = "Markers1";

int CodecRead(CSoundManager* m, std::uint8_t* to, int bytes) {
    return ei::VirtualSlot<CodecReadFn>(m->streamCodec, kCodecRead)(m->streamFile, to, bytes);
}
void CodecSeek(CSoundManager* m, int position) {
    ei::VirtualSlot<CodecSeekFn>(m->streamCodec, kCodecSeek)(m->streamFile, position);
}
int CodecAttribute(CSoundManager* m, CodecSlot name) {
    return ei::VirtualSlot<CodecAttributeFn>(m->streamCodec, kCodecAttribute)(m->streamFile, m->streamCodec[name]);
}
void CodecClose(CSoundManager* m) {
    if (m->streamFile == nullptr) return;
    ei::VirtualSlot<CodecCloseFn>(m->streamCodec, kCodecClose)(m->streamFile);
    m->streamFile = nullptr;
}
void Silence(CSoundManager* m, std::uint8_t* to, int bytes) {
    std::memset(to, m->streamSilence, static_cast<std::uint32_t>(bytes));
}
// `bytes` of the file; what it does not give is silence.
void ReadOrSilence(CSoundManager* m, std::uint8_t* to, int bytes) {
    int got = CodecRead(m, to, bytes);
    if (got < bytes) Silence(m, to + got, bytes - got);
}

// The next file takes over: `bytes` of it (or silence) to `to`.
void SwitchStream(CSoundManager* m, std::uint8_t* to, int bytes) {
    m->streamReadPosition = 0;
    m->streamState = 0;
    // Rebirth's StreamPatch (006BB628): the mod's Stream\ folder when it has the file.
    const bool modStream = modfolders::Active(ei::rebirth::Patch::StreamPatch) && modfolders::StreamInMod(m->nextName);
    mfc::CString path = (modStream ? modfolders::StreamFolder() : m->soundPath) + m->nextName;
    if (CompareText(m->nextName, game::EmptyText()) == 0 ||
        m->streamReader.OpenFile(path, reinterpret_cast<std::uint32_t*>(&m->streamFileSize)) == 0) {
        Silence(m, to, bytes);
        m->streamState = 0;
        CodecClose(m);
        gameapi::EnterCriticalSection(Lock(m->nameLock));
        m->streamName = game::EmptyText();
        gameapi::LeaveCriticalSection(Lock(m->nameLock));
        return;
    }
    m->streamState = 1;
    gameapi::EnterCriticalSection(Lock(m->nameLock));
    m->streamName = m->nextName;
    gameapi::LeaveCriticalSection(Lock(m->nameLock));
    m->soundRegistry.GetIntArray(m->streamName, kSwitchMarkers, reinterpret_cast<DwordArray*>(&m->markers));
    m->soundRegistry.GetIntArray(m->streamName, kLoopMarkers, reinterpret_cast<DwordArray*>(&m->markers2));
    m->streamLoopAt = m->markers2.size > 0 ? m->markers2[0] : kNoLoop;
    m->streamSwitchAt = kNeverSwitch;
    CodecClose(m);
    m->streamFile = ei::VirtualSlot<CodecOpenFn>(m->streamCodec, kCodecOpen)(m, ei::Code<void*>(kStreamReadCallback), 0);
    int rate = CodecAttribute(m, kCodecOutputRate);
    int channels = CodecAttribute(m, kCodecOutputChannels);
    int bits = CodecAttribute(m, kCodecOutputBits);
    // DIG_F_16BITS_MASK with DIG_PCM_SIGN, or unsigned 8 bits; DIG_F_STEREO_MASK.
    int format = 0;
    int flags = 0;
    if (bits == 16) {
        format = 1;
        flags = kSignedPcm;
        m->streamSilence = 0;
    } else {
        m->streamSilence = kSilence8;
    }
    if (channels == 2) format |= 2;
    AilSetSampleType()(m->streamSample, format, flags);
    AilSetSamplePlaybackRate()(m->streamSample, rate);
    AilSetSampleVolume()(m->streamSample, kFullLevel);
    AilSetSamplePan()(m->streamSample, kCentrePan);
    m->streamFrameBytes = static_cast<std::int32_t>(static_cast<std::uint32_t>(bits * channels) >> 3);
    m->streamBufferFrames = m->streamBufferSize / m->streamFrameBytes;
    m->streamPosition = 0;
    CodecSeek(m, kSeekKeep);
    ReadOrSilence(m, to, bytes);
}

} // namespace

// The original leaves "from the beginning again" (a local) unwritten when neither a loop point
// nor a switch point is in the buffer, and reads it in one such case: a switch at the file's end
// (mode 1) when the file ends in this buffer. Here that case is the switch, which is what the
// mode asks for.
void CSoundManager::ReadStreamVanilla(void* /*stream*/) {
    auto* const buffer = static_cast<std::uint8_t*>(streamBuffers[streamSpareBuffer]);
    const std::int32_t state = streamState;
    if (state == 0) {
        Silence(this, buffer, streamBufferSize);
        return;
    }
    const int frames = streamBufferFrames;
    int current = frames;   // frames of the playing file as it goes
    int rest = 0;           // frames after its loop point or the switch point
    bool again = false;     // the rest is the playing file from its beginning
    if (state == kStateSwitching && switchMode == kSwitchAfterFade && musicFader.finished != 0) {
        current = 0;
        rest = frames;
        musicFader.SetHold(gameapi::timeGetTime() + kSwitchHoldMs);
    } else if ((state == 1 || (state == kStateSwitching && streamSwitchAt > streamLoopAt)) && streamLoopAt >= streamPosition &&
               streamLoopAt < streamPosition + frames) {
        again = true;
        current = streamLoopAt - streamPosition;
        rest = frames - current;
    } else if (state == kStateSwitching && streamSwitchAt >= streamPosition && streamSwitchAt < streamPosition + frames) {
        current = streamSwitchAt - streamPosition;
        rest = frames - current;
    }
    int currentBytes = current * streamFrameBytes;
    int restBytes = rest * streamFrameBytes;
    if (streamFile == nullptr) {
        currentBytes = 0;
        restBytes = streamBufferSize;
        again = false;
    } else {
        int got = CodecRead(this, buffer, currentBytes);
        if (got < currentBytes) {
            if (streamState == kStateSwitching && switchMode == kSwitchAtEnd) {
                // The file ended: the next one from here.
                currentBytes = got;
                restBytes = streamBufferSize - got;
            } else {
                CodecSeek(this, 0);
                streamPosition = 0;
                ReadOrSilence(this, buffer + got, currentBytes - got);
            }
        }
    }
    if (restBytes == 0) return;
    if (again) {
        CodecSeek(this, 0);
        streamPosition = 0;
        ReadOrSilence(this, buffer + currentBytes, restBytes);
        return;
    }
    SwitchStream(this, buffer + currentBytes, restBytes);
}
EI_REPLACE_METHOD(0x006BB3A0, CSoundManager::ReadStreamVanilla);

// 006B75E0 (A-H25; Miles calls it on its own thread when a voice's sample ends; registered by its
// address in sound_manager.cpp). The voice (the sample's user data 1) is marked finished; the
// original also reads user data 0 (the manager) and drops it.
void __stdcall SoundEndOfSample(void* sample) {
    AilSampleUserData()(sample, 0);
    reinterpret_cast<SoundSlot*>(AilSampleUserData()(sample, 1))->finished = 1;
}
EI_REPLACE(0x006B75E0, SoundEndOfSample);

int __fastcall SoundOpenStream(void* stream, void*, void* driver) { return ManagerOf(stream)->OpenStream(driver); }
int __fastcall SoundCloseStream(void* stream) { return ManagerOf(stream)->CloseStream(); }
EI_REWRITTEN(0x006BAAC0, SoundOpenStream);
EI_REWRITTEN(0x006BAC90, SoundCloseStream);

int CSoundManager::Open(void* window, int useReverb, int deviceIndex, unsigned rate, int /*bits*/) {
    Destroy();   // slot 3: Close
    const bool waveOut = SoundOption() == kWaveOutMode;
    int device = waveOut ? kWaveMapper : 0;
    if (deviceIndex >= 0) {
        lib::AlignedArray<int> devices;
        devices.Construct();
        if (DirectSoundEnumerate()(ei::Code<void*>(kDeviceCallback), &devices) == 0 && deviceIndex < devices.size)
            device = devices[deviceIndex];
        devices.FreeData();
    }
    reverbOn = useReverb;
    if (useReverb != 0) AilSetPreference()(kPrefReverb, kReverbBufferBytes);
    if (rate >= kMaxRate) rate = kMaxRate;
    PcmFormat format{1, 2, rate, rate * 4, 4, 16};
    AilSetPreference()(kPrefMixerChannels, kMixerChannels);
    if (waveOut) AilSetPreference()(kPrefUseWaveOut, 1);
    if (AilWaveOutOpen()(&waveDriver, nullptr, device, &format) != 0) {
        game::DebugLog(AilLastError()());
        return 0;
    }
    AilSetDirectSoundWindow()(waveDriver, window);
    if (SoundOpenStream(StreamOf(this), nullptr, waveDriver) != 0)
        streamThread = mfc::impl::AfxBeginThread(StreamThreadEntry, this, kStreamThreadPriority, 0, 0, nullptr);
    // operator new[]: the count, then the voices.
    auto* block = static_cast<std::int32_t*>(gamecrt::operator_new(sizeof(std::int32_t) + kVoiceCount * sizeof(SoundSlot)));
    SoundSlot* voices = nullptr;
    if (block != nullptr) {
        *block = kVoiceCount;
        voices = reinterpret_cast<SoundSlot*>(block + 1);
        libcrt::ConstructElements(voices, sizeof(SoundSlot), kVoiceCount, reinterpret_cast<libcrt::ElementFunction>(kVoiceConstruct),
                              reinterpret_cast<libcrt::ElementFunction>(kVoiceDestruct));
    }
    slots = voices;
    for (int i = 0; i < kVoiceCount; ++i) {
        SoundSlot& slot = slots[i];
        slot.sample = nullptr;
        slot.volume = -1;
        slot.baseVolume = -1;
        slot.pan = -1;
        slot.priority = -1;
        slot.playing = 0;
        slot.looping = 0;
        slot.speech = 0;
        slot.finished = 0;
        slot.heightFade = 0;
        slot.handle = -1;
        slot.sample = AilAllocateSample()(waveDriver);
        AilSetSampleUserData()(slot.sample, 0, reinterpret_cast<std::intptr_t>(this));
        AilSetSampleUserData()(slot.sample, 1, reinterpret_cast<std::intptr_t>(&slot));
    }
    enabled = 1;
    return 1;
}
EI_REWRITTEN(0x006B6A30, CSoundManager::Open);

void CSoundManager::Close() {
    if (streamThread != nullptr) {
        threadsQuit = 1;
        WaitForThread(streamThread);
    }
    if (midiThread != nullptr) {
        threadsQuit = 1;
        WaitForThread(midiThread);
    }
    if (slots != nullptr) {
        for (int i = 0; i < kVoiceCount; ++i) {
            if (slots[i].playing != 0) EndSlot(&slots[i]);
            if (slots[i].sample != nullptr) AilReleaseSample()(slots[i].sample);
        }
        // operator delete[]: the count stands before the voices.
        std::int32_t* block = reinterpret_cast<std::int32_t*>(slots) - 1;
        libcrt::DestroyElements(slots, sizeof(SoundSlot), *block, reinterpret_cast<libcrt::ElementFunction>(kVoiceDestruct));
        gamecrt::operator_delete(block);
    }
    DeleteDeferred();
    // The original takes each record out by value (RemoveHead 006BC6F0 copies its name) and
    // destroys the copy; here the node's own name is freed.
    while (cacheList.count != 0) {
        AilMemFree()(cacheList.head->data.data);
        WaveRecord record = cacheList.RemoveHead();
        record.Name().~string();
    }
    cache.Clear();   // 006BCE00
    if (midiSequence != nullptr) {
        AilEndSequence()(midiSequence);
        AilReleaseSequence()(midiSequence);
    }
    if (midiData1D0 != nullptr) AilMemFree()(midiData1D0);
    if (midiData1D4 != nullptr) AilMemFree()(midiData1D4);
    if (waveDriver != nullptr) AilWaveOutClose()(waveDriver);
    if (midiDriver != nullptr) AilMidiOutClose()(midiDriver);
    ClearWaveSources();
    streamReader.Close();
    archive074.CloseResource();
    soundRegistry.Close();
    Reset();
}
EI_REWRITTEN(0x006B6D80, CSoundManager::Close);

void CSoundManager::Reset() {
    waveDriver = nullptr;
    midiDriver = nullptr;
    ResetStream();
    streamThread = nullptr;
    midiThread = nullptr;
    threadsQuit = 0;
    slots = nullptr;
    cacheBytes = 0;
    for (SoundEmitter& e : emitters) {
        e.active = 0;
        e.position.x = e.position.y = e.position.z = 0.0f;
        e.innerRadius = 0.0f;
        e.outerRadius = 0.0f;
        e.minInterval = -1;
        e.maxInterval = -1;
        e.lastPlayed = 0;
    }
    for (SoundSequence& q : sequences) {
        q.active = 0;
        q.handle = -1;
        q.position.x = q.position.y = q.position.z = 0.0f;
        q.nearDistance = 0.0f;
        q.farDistance = 0.0f;
        q.remaining = -1;
        q.index = -1;
        q.priority = -1;
        q.random = 0;
        q.chooser = nullptr;
        q.id = -1;
    }
    nextHandle = 0;
    emitterTick = 0;
    midiSequence = nullptr;
    midiState = 0;
    midiData1D0 = nullptr;
    midiData1D4 = nullptr;
    ConstructFader(midiFader);
    speechVolume = kFullVolume;
    effectsVolume = kFullVolume;
    enabled = 0;
    spare = 0;
    reverbOn = 0;
    panDirect = 0;
    reverb[0] = reverb[1] = reverb[2] = 0.0f;
    soundPath = game::EmptyText();
    randomStart = 0;
}
EI_REWRITTEN(0x006B73E0, CSoundManager::Reset);
