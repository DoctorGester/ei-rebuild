// The sound manager's engine object slots, its wave sources and sound folder, and the music stream's file
// reading; the constructor / destructor bodies; the Miles setup Open / Close / Reset.
#include "sound.h"
#include "options.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "record_arrays.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "../lib/stlp_string.h"

#include "ei/hook.h"
#include "lib/mfc_thread.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/game_slots.h"
#include "lib/str.h"

#include <cstring>
#include <new>
#include <string>

#undef GetClassName

namespace {
constexpr int kSoundManagerClassId = 0xE;
constexpr const char* kSoundManagerClassNameAddress = "CSoundManager";
const char* const kBackslash = "\\";

int CompareText(std::string_view a, std::string_view b) {
    return gamecrt::mbscmp(a, b);
}

} // namespace

const char* CSoundManager::GetClassName() { return kSoundManagerClassNameAddress; }
int CSoundManager::GetClassId() { return kSoundManagerClassId; }

int CSoundManager::CreateBody() {
    Reset();
    return 1;
}
EI_REWRITTEN(0x006B6D70, CSoundManager::CreateBody);

EI_REWRITTEN_SPECIAL(0x006B62B0, CSoundManager::~CSoundManager);   // deleting

// The list's RemoveHead (inlined), then the archive's deleting destructor.
void CSoundManager::ClearWaveSources() {
    while (waveSources.count != 0) {
        CMappedResource* source = waveSources.RemoveHead();
        delete source;
    }
}
EI_REWRITTEN(0x006B7060, CSoundManager::ClearWaveSources);

// Called at startup for the two sound archives. A failed allocation is not tested (the open call goes
// through a null object).
int CSoundManager::AddWaveSource(const char* path) {
    if (enabled == 0) return 1;
    CMappedResource* memory = gamecrt::operator_new_as<CMappedResource>();
    CMappedResource* source = memory != nullptr ? new (memory) CMappedResource() : nullptr;
    if (source->OpenResource(path, 0) == 0) {
        delete source;
        return 0;
    }
    waveSources.AddTail(source);
    return 1;
}
EI_REWRITTEN(0x006B7AB0, CSoundManager::AddWaveSource);

// From the map's ambient sounds.
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

// Rebirth redirects its ListFiles call; this is the vanilla body. From the unit database's sound counts, on
// the game's main thread.
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
EI_REWRITTEN(0x006B7110, CSoundManager::ListWaveFiles);

// Called at startup.
int CSoundManager::SetPath(const char* folder, const char* registryPath) {
    if (enabled == 0) return 1;
    soundPath = folder;
    bool missing;
    {
        std::string last = str::Right(soundPath, 1);
        missing = CompareText(last, kBackslash) != 0;
    }
    if (missing) soundPath += kBackslash;
    return soundRegistry.Open(registryPath) != 0 ? 1 : 0;
}
EI_REWRITTEN(0x006BAE20, CSoundManager::SetPath);

mfc::CString CSoundManager::CurrentStreamName() {
    gameapi::EnterCriticalSection(&nameLock);
    std::string name;
    if (streamState == 1 || streamState == 2) name = streamName;
    else name = game::EmptyText();
    gameapi::LeaveCriticalSection(&nameLock);
    return name;
}
EI_REWRITTEN(0x006BAEA0, CSoundManager::CurrentStreamName);

int __stdcall SoundStreamRead(CSoundManager* manager, char* buffer, int size, int offset) {
    if (offset >= 0) {
        manager->streamReader.Seek(offset, 0);
        manager->streamReadPosition = offset;
    }
    int left = static_cast<int>(manager->streamFileSize) - manager->streamReadPosition;
    if (size > left) size = left;
    if (manager->streamReader.Read(buffer, size) < 0) return 0;
    manager->streamReadPosition += size;
    return size;
}
EI_REPLACE(0x006BA950, SoundStreamRead);

// --- Constructor, destructor, stream reset ---------------------------------------------------

namespace {

constexpr int kListBlockSize = 10;
constexpr unsigned kMapBuckets = 100;
constexpr int kFullVolume = 100;
constexpr int kNoSwitch = 4;   // RequestStream's "switch at the current position"
constexpr int kEmitterCount = 16;
constexpr int kSequenceCount = 8;

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
// (compiler-generated: the name array at +4).
SoundEmitter* __fastcall ConstructEmitter(SoundEmitter* emitter) {
    emitter->names.Construct();
    return emitter;
}
EI_REWRITTEN(0x006B63D0, ConstructEmitter);
void __fastcall DestructEmitter(SoundEmitter* emitter) { emitter->names.Destruct(); }
EI_REWRITTEN(0x006B63E0, DestructEmitter);
SoundSequence* __fastcall ConstructSequence(SoundSequence* sequence) {
    sequence->names.Construct();
    return sequence;
}
EI_REWRITTEN(0x006B63F0, ConstructSequence);
void __fastcall DestructSequence(SoundSequence* sequence) { sequence->names.Destruct(); }
EI_REWRITTEN(0x006B6400, DestructSequence);

// The stream reader, the strings and the archive are members the compiler constructs (here before the body;
// the game constructs the stream reader first and the archive between the registries).
CSoundManager::CSoundManager() {
    ClearFields();
    soundRegistry.Construct();
    registry0CC.Construct();
    ConstructList(waveSources, mfc::impl::SoundListClass::Vtable());
    musicFader.volume = kFullVolume;
    musicFader.Recalc();
    musicFader.SetHold(0);
    markers.Construct();
    markers2.Construct();
    gameapi::InitializeCriticalSection(&nameLock);
    gameapi::InitializeCriticalSection(&streamLock);
    midiArray1E4.Construct();
    midiArray1F4.Construct();
    ConstructFader(midiFader);
    gameapi::InitializeCriticalSection(&midiLock);
    // The CRT's eh vector constructor iterator over the emitters and the sequences: each
    // element's constructor in order.
    for (SoundEmitter& emitter : emitters) ConstructEmitter(&emitter);
    for (SoundSequence& sequence : sequences) ConstructSequence(&sequence);
    ConstructList(cacheList, gameslots::WaveListClass::Vtable());
    // hashtable(n, hash, equal, allocator); the functor bytes, never read, are 0 here (the game's are stack
    // bytes).
    cache.Construct(kMapBuckets, 0, 0, 0);
    deferred.Construct(kMapBuckets);
    gameapi::AIL_startup();
    Reset();
}

EI_REWRITTEN_SPECIAL(0x006B6040, CSoundManager::CSoundManager);

// The archive, the strings and the stream reader are destroyed after the body (the archive after the
// registries; the game destroys it between them and the stream reader last).
CSoundManager::~CSoundManager() {
    Close();
    gameapi::AIL_shutdown();
    deferred.Destroy();   // clear, then the buckets
    cache.Destroy();      // clear, then the buckets
    cacheList.vtable = gameslots::WaveListClass::Vtable();
    for (WaveNode* node = cacheList.head; node; node = node->next) recordarrays::Waves::DestructElements(&node->data, 1);
    cacheList.RemoveAll();
    // The CRT's eh vector destructor iterator: each element's destructor, last to first.
    for (int i = kSequenceCount; --i >= 0;) DestructSequence(&sequences[i]);
    for (int i = kEmitterCount; --i >= 0;) DestructEmitter(&emitters[i]);
    gameapi::DeleteCriticalSection(&midiLock);
    midiArray1F4.FreeData();
    midiArray1E4.FreeData();
    gameapi::DeleteCriticalSection(&streamLock);
    gameapi::DeleteCriticalSection(&nameLock);
    markers2.FreeData();
    markers.FreeData();
    waveSources.vtable = mfc::impl::SoundListClass::Vtable();
    waveSources.RemoveAll();
    registry0CC.Destroy();
    soundRegistry.Destroy();
}
EI_REWRITTEN_SPECIAL(0x006B65D0, CSoundManager::~CSoundManager);

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

EI_REWRITTEN(0x006BA9C0, CSoundManager::ResetStream);

// --- Open, Close, Reset ------------------------------------------------------------------

namespace {

constexpr int kVoiceCount = 15;
// operator new[]'s block for the voices: the count, then the voices.
struct VoiceBlock {
    std::int32_t count;
    SoundSlot voices[kVoiceCount];
};
static_assert(offsetof(VoiceBlock, voices) == 4);
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

// The address of the DirectSoundEnumerateA callback that appends each device's GUID pointer to an aligned array.
constexpr std::uintptr_t kDeviceCallback = std::uintptr_t(0x006B68A0);

constexpr int kStreamRate = 44100;
constexpr int kStereo16 = 3;       // DIG_F_STEREO_16
constexpr int kSignedPcm = 1;      // DIG_PCM_SIGN


// The stream thread's (StreamThread).
constexpr std::uint32_t kStreamPeriodMs = 100;
constexpr int kNoHalfReady = -1;

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
    if (streamFile != nullptr && streamCodec != nullptr) streamCodec->close(streamFile);
    if (streamSample != nullptr) gameapi::AIL_release_sample_handle(streamSample);
    if (streamDecoder != nullptr) gameapi::AIL_release_sample_handle(streamDecoder);
    for (void* buffer : streamBuffers)
        if (buffer != nullptr) gameapi::AIL_mem_free_lock(buffer);
    ResetStream();
    return 0;
}

int CSoundManager::OpenStream(miles::DigitalDriver* driver) {
    void* codec = gameapi::RIB_find_file_provider("ASI codec", "Input file types", ".mp3");
    streamSample = gameapi::AIL_allocate_sample_handle(driver);
    streamDecoder = gameapi::AIL_allocate_sample_handle(driver);
    if (codec == nullptr || streamSample == nullptr || streamDecoder == nullptr) return CloseStream();

    streamBufferSize = gameapi::AIL_minimum_sample_buffer_size(driver, kStreamRate, kStereo16) * 2;
    for (void*& buffer : streamBuffers) buffer = gameapi::AIL_mem_alloc_lock(streamBufferSize);
    gameapi::AIL_init_sample(streamSample);
    gameapi::AIL_set_sample_type(streamSample, kStereo16, kSignedPcm);
    gameapi::AIL_set_sample_playback_rate(streamSample, kStreamRate);
    gameapi::AIL_set_sample_volume(streamSample, 0);
    gameapi::AIL_set_sample_pan(streamSample, 0);
    for (void* buffer : streamBuffers) std::memset(buffer, 0, static_cast<std::uint32_t>(streamBufferSize));
    gameapi::AIL_set_sample_processor(streamDecoder, 0, codec);
    gameapi::AIL_init_sample(streamDecoder);
    // (The game also closes the stream when this address is null, which it cannot be.)
    streamCodec = &streamDecoder->codec;
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

        gameapi::EnterCriticalSection(&streamLock);
        musicFader.Update();
        gameapi::AIL_set_sample_volume(streamSample, musicFader.level);
        for (;;) {
            int half = gameapi::AIL_sample_buffer_ready(streamSample);
            if (half == kNoHalfReady) break;
            // The spare buffer goes into that half; the one it played becomes the spare.
            int buffer = streamSpareBuffer;
            std::int32_t& playing = streamHalfBuffer[half == 0 ? 0 : 1];
            streamSpareBuffer = playing;
            playing = buffer;
            streamPosition += streamBufferFrames;
            gameapi::AIL_load_sample_buffer(streamSample, half, streamBuffers[buffer], streamBufferSize);
            ReadStreamVanilla(nullptr);
        }
        gameapi::LeaveCriticalSection(&streamLock);
    }
    mfc::impl::AfxEndThread(0, 1);
}
EI_REWRITTEN(0x006BB910, CSoundManager::StreamThread);

// The stream thread's procedure (cdecl): the manager's StreamThread, then 0 (not reached: StreamThread ends
// the thread).
unsigned __cdecl StreamThreadEntry(void* manager) {
    static_cast<CSoundManager*>(manager)->StreamThread();
    return 0;
}
EI_REWRITTEN(0x006B6A10, StreamThreadEntry);

// kDeviceCallback: DirectSoundEnumerateA's callback in Open. The device's GUID pointer is appended to the
// aligned array `devices` (its SetSize by one); the enumeration goes on (TRUE).
int __stdcall AddSoundDevice(void* guid, const char*, const char*, lib::AlignedArray<int>* list) {
    const int at = list->size;
    list->SetSize(at + 1);
    (*list)[at] = static_cast<int>(reinterpret_cast<std::intptr_t>(guid));
    return 1;
}
EI_REPLACE(0x006B68A0, AddSoundDevice);

// --- The stream's reader -----------------------------------------------------------------

namespace {

// The decoder's read callback (SoundStreamRead).
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

int CodecRead(CSoundManager* m, std::uint8_t* to, int bytes) { return m->streamCodec->process(m->streamFile, to, bytes); }
void CodecSeek(CSoundManager* m, int position) { m->streamCodec->seek(m->streamFile, position); }
// The codec's attribute `name` (one of its handles) of the open file.
int CodecAttribute(CSoundManager* m, void* AsiStage::*name) {
    return m->streamCodec->attribute(m->streamFile, m->streamCodec->*name);
}
void CodecClose(CSoundManager* m) {
    if (m->streamFile == nullptr) return;
    m->streamCodec->close(m->streamFile);
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
    // Rebirth's StreamPatch: the mod's Stream\ folder when it has the file.
    const bool modStream = modfolders::Active(ei::rebirth::Patch::StreamPatch) && modfolders::StreamInMod(m->nextName.c_str());
    std::string path = ((modStream ? modfolders::StreamFolder() : m->soundPath) + m->nextName).GetString();
    if (CompareText(m->nextName, game::EmptyText()) == 0 ||
        m->streamReader.OpenFile(path.c_str(), &m->streamFileSize) == 0) {
        Silence(m, to, bytes);
        m->streamState = 0;
        CodecClose(m);
        gameapi::EnterCriticalSection(&m->nameLock);
        m->streamName = game::EmptyText();
        gameapi::LeaveCriticalSection(&m->nameLock);
        return;
    }
    m->streamState = 1;
    gameapi::EnterCriticalSection(&m->nameLock);
    m->streamName = m->nextName;
    gameapi::LeaveCriticalSection(&m->nameLock);
    m->soundRegistry.GetIntArray(m->streamName.c_str(), kSwitchMarkers, &m->markers);
    m->soundRegistry.GetIntArray(m->streamName.c_str(), kLoopMarkers, &m->markers2);
    m->streamLoopAt = m->markers2.size > 0 ? m->markers2[0] : kNoLoop;
    m->streamSwitchAt = kNeverSwitch;
    CodecClose(m);
    m->streamFile = m->streamCodec->open(m, ei::Code<void*>(kStreamReadCallback), 0);
    int rate = CodecAttribute(m, &AsiStage::outputSampleRate);
    int channels = CodecAttribute(m, &AsiStage::outputChannels);
    int bits = CodecAttribute(m, &AsiStage::outputBits);
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
    gameapi::AIL_set_sample_type(m->streamSample, format, flags);
    gameapi::AIL_set_sample_playback_rate(m->streamSample, rate);
    gameapi::AIL_set_sample_volume(m->streamSample, kFullLevel);
    gameapi::AIL_set_sample_pan(m->streamSample, kCentrePan);
    m->streamFrameBytes = static_cast<std::int32_t>(static_cast<std::uint32_t>(bits * channels) >> 3);
    m->streamBufferFrames = m->streamBufferSize / m->streamFrameBytes;
    m->streamPosition = 0;
    CodecSeek(m, kSeekKeep);
    ReadOrSilence(m, to, bytes);
}

} // namespace

// The game leaves "from the beginning again" (a local) unwritten when neither a loop point nor a switch
// point is in the buffer, and reads it in one such case: a switch at the file's end (mode 1) when the file
// ends in this buffer. Here that case is the switch, which is what the mode asks for.
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
EI_REWRITTEN(0x006BB3A0, CSoundManager::ReadStreamVanilla);

// Miles calls it on its own thread when a voice's sample ends (registered in sound_manager.cpp). The voice
// (the sample's user data 1) is marked finished; the game also reads user data 0 (the manager) and drops
// it.
void __stdcall SoundEndOfSample(miles::Sample* sample) {
    gameapi::AIL_sample_user_data(sample, 0);
    reinterpret_cast<SoundSlot*>(gameapi::AIL_sample_user_data(sample, 1))->finished = 1;
}
EI_REPLACE(0x006B75E0, SoundEndOfSample);

EI_REWRITTEN(0x006BAAC0, CSoundManager::OpenStream);
EI_REWRITTEN(0x006BAC90, CSoundManager::CloseStream);

int CSoundManager::Open(void* window, int useReverb, int deviceIndex, unsigned rate, int /*bits*/) {
    Destroy();   // Close
    const bool waveOut = SoundOption() == kWaveOutMode;
    int device = waveOut ? kWaveMapper : 0;
    if (deviceIndex >= 0) {
        lib::AlignedArray<int> devices;
        devices.Construct();
        if (gameapi::DirectSoundEnumerateA(ei::Code<void*>(kDeviceCallback), &devices) == 0 && deviceIndex < devices.size)
            device = devices[deviceIndex];
        devices.FreeData();
    }
    reverbOn = useReverb;
    if (useReverb != 0) gameapi::AIL_set_preference(kPrefReverb, kReverbBufferBytes);
    if (rate >= kMaxRate) rate = kMaxRate;
    PcmFormat format{1, 2, rate, rate * 4, 4, 16};
    gameapi::AIL_set_preference(kPrefMixerChannels, kMixerChannels);
    if (waveOut) gameapi::AIL_set_preference(kPrefUseWaveOut, 1);
    if (gameapi::AIL_waveOutOpen(&waveDriver, nullptr, device, &format) != 0) {
        game::DebugLog(gameapi::AIL_last_error());
        return 0;
    }
    gameapi::AIL_set_DirectSound_HWND(waveDriver, window);
    if (OpenStream(waveDriver) != 0)
        streamThread = mfc::impl::AfxBeginThread(StreamThreadEntry, this, kStreamThreadPriority, 0, 0, nullptr);
    // operator new[]: the count, then the voices.
    auto* block = gamecrt::operator_new_as<VoiceBlock>();
    SoundSlot* voices = nullptr;
    if (block != nullptr) {
        block->count = kVoiceCount;
        voices = block->voices;
        // The CRT's eh vector constructor iterator: each voice's name string (basic_string()).
        for (int i = 0; i < kVoiceCount; ++i) stlp::StringAbi::ConstructDefault(&voices[i].name);
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
        slot.sample = gameapi::AIL_allocate_sample_handle(waveDriver);
        gameapi::AIL_set_sample_user_data(slot.sample, 0, reinterpret_cast<std::intptr_t>(this));
        gameapi::AIL_set_sample_user_data(slot.sample, 1, reinterpret_cast<std::intptr_t>(&slot));
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
            if (slots[i].sample != nullptr) gameapi::AIL_release_sample_handle(slots[i].sample);
        }
        // operator delete[]: the count stands before the voices.
        std::int32_t* block = reinterpret_cast<std::int32_t*>(slots) - 1;
        // The CRT's eh vector destructor iterator: each voice's name string, last to first.
        for (int i = *block; --i >= 0;) slots[i].name.~string();
        gamecrt::operator_delete(block);
    }
    DeleteDeferred();
    // The game takes each record out by value (RemoveHead copies its name) and destroys the copy; here the
    // node's own name is freed.
    while (cacheList.count != 0) {
        gameapi::AIL_mem_free_lock(cacheList.head->data.data);
        cacheList.RemoveHead();   // the copy taken out is destroyed at once
    }
    cache.Clear();
    if (midiSequence != nullptr) {
        gameapi::AIL_end_sequence(midiSequence);
        gameapi::AIL_release_sequence_handle(midiSequence);
    }
    if (midiData1D0 != nullptr) gameapi::AIL_mem_free_lock(midiData1D0);
    if (midiData1D4 != nullptr) gameapi::AIL_mem_free_lock(midiData1D4);
    if (waveDriver != nullptr) gameapi::AIL_waveOutClose(waveDriver);
    if (midiDriver != nullptr) gameapi::AIL_midiOutClose(midiDriver);
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
