#pragma once

// Sound and music (game.exe 006B4100-006BDxxx): the sound manager (CSoundManager, one instance at
// *007AF784) built on the Miles Sound System (mss32.dll), its MP3 music stream and MIDI threads,
// and the music state machine (CMusicSystem, world/app object + 0x1A8). Names are provisional.
// See docs/notes/sound.md for the map of the range.

#include "engine_object.h"

#include "../lib/containers.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "../lib/vecmath.h"
#include "registry.h"
#include "resource_stream.h"

// BOOL results are full 0/1 ints in EAX.

#include <cstddef>
#include <cstdint>

// A volume envelope (0x1C bytes): the music stream's (CSoundManager + 0x144) and the MIDI
// sequence's (+0x20C). `level` is the Miles volume 0..127 derived from `volume` (0..100) and the
// fade `factor`.
struct SoundFader {
    std::int32_t volume;        // +0x00 0..100
    std::int32_t level;         // +0x04 0..127, round(volume * factor * 127 * 0.01)
    float factor;               // +0x08 fade factor 0..1
    float step;                 // +0x0C added / subtracted per update (100 ms on the stream thread)
    std::int32_t mode;          // +0x10 0 none, 1 fade in, 2 fade out
    std::int32_t finished;      // +0x14 set when a fade out reached 0
    std::uint32_t holdUntil;    // +0x18 timeGetTime() at which the fade is cancelled (0: none)

    void SetHold(std::uint32_t until);  // 006B5E80
    void Update();                      // 006B5EE0 (stream thread)
    void Recalc();                      // 006B5FF0
};
static_assert(sizeof(SoundFader) == 0x1C);

// One of the 15 Miles sample voices (array at CSoundManager + 0x24C, 0x54 bytes each).
struct SoundSlot {
    std::uint32_t name[3];      // +0x00 STLport string (start, finish, end of storage): the wave name
    void* sample;               // +0x0C HSAMPLE
    std::int32_t volume;        // +0x10 Miles volume 0..127
    std::int32_t baseVolume;    // +0x14 2D sounds: volume 0..100
    std::int32_t pan;           // +0x18 -100..100
    std::int32_t priority;      // +0x1C
    std::int32_t playing;       // +0x20
    std::int32_t looping;       // +0x24 keep as a deferred record when the voice is stolen
    std::int32_t speech;        // +0x28 scaled by the speech volume (+0x8B8) instead of effects (+0x8B4)
    float length;               // +0x2C seconds
    std::int32_t finished;      // +0x30 set by the end-of-sample callback (006B75E0, Miles thread)
    std::int32_t flat;          // +0x34 2D: volume from baseVolume, not from the listener distance
    std::int32_t heightFade;    // +0x38 2D sounds fading out as the camera rises (30..70)
    Vector3 position;           // +0x3C 3D position
    float nearDistance;         // +0x48 full volume inside
    float farDistance;          // +0x4C silent outside
    std::int32_t handle;        // +0x50 sound handle (-1: free)

    void End(struct CSoundManager* manager);  // 006B5CD0
};
static_assert(sizeof(SoundSlot) == 0x54);
static_assert(offsetof(SoundSlot, finished) == 0x30 && offsetof(SoundSlot, handle) == 0x50);

// A sound that lost its voice to a higher priority one and waits to be restarted (0x48 bytes,
// allocated with operator new and kept in the hash map at CSoundManager + 0x884, keyed by handle).
struct DeferredSound {
    std::uint32_t name[3];      // +0x00 STLport string
    std::int32_t volume;        // +0x0C
    std::int32_t baseVolume;    // +0x10
    std::int32_t pan;           // +0x14
    std::int32_t priority;      // +0x18
    std::int32_t speech;        // +0x1C
    float length;               // +0x20
    std::int32_t flat;          // +0x24
    std::int32_t heightFade;    // +0x28
    Vector3 position;           // +0x2C
    float nearDistance;         // +0x38
    float farDistance;          // +0x3C
    std::int32_t handle;        // +0x40 (the map key)
    std::int32_t samplePosition;// +0x44
};
static_assert(sizeof(DeferredSound) == 0x48);

// MFC CStringArray (0x14 bytes), as embedded in the sound records.
using SoundNameArray = mfc::CStringArray;

// An ambient emitter (16 at CSoundManager + 0x26C, 0x38 bytes): plays one of its names at random
// intervals while the listener is within `outerRadius` (006B9B10).
struct SoundEmitter {
    std::int32_t active;        // +0x00
    SoundNameArray names;       // +0x04
    Vector3 position;           // +0x18
    float innerRadius;          // +0x24
    float outerRadius;          // +0x28
    std::int32_t minInterval;   // +0x2C ms
    std::int32_t maxInterval;   // +0x30 ms
    std::uint32_t lastPlayed;   // +0x34 GetTickCount()
};
static_assert(sizeof(SoundEmitter) == 0x38);

// A 3D sound sequence (8 at CSoundManager + 0x5F0, 0x4C bytes): plays its names one after the
// other (or at random, or as a callback chooses) at one position.
struct SoundSequence {
    std::int32_t active;        // +0x00
    SoundNameArray names;       // +0x04
    std::int32_t handle;        // +0x18 current sound
    Vector3 position;           // +0x1C
    float nearDistance;         // +0x28
    float farDistance;          // +0x2C
    std::int32_t remaining;     // +0x30 sounds left to play (callback: sounds played)
    std::int32_t index;         // +0x34 next name
    std::int32_t priority;      // +0x38
    std::int32_t random;        // +0x3C pick names at random
    // +0x40 returns the next index (bit 30: loop it), < 0 ends; called with the previous index (ECX) and
    // the number of sounds started before (EDX).
    int(__fastcall* chooser)(int previous, int played);
    std::int32_t generation;    // +0x44
    std::int32_t id;            // +0x48 generation << 8 | slot (-1: free)

    int PlayNext(struct CSoundManager* manager);  // 006BA140
};
static_assert(sizeof(SoundSequence) == 0x4C);

// The STLport hash_map<int, DeferredSound*> at CSoundManager + 0x884.
// Find is the inlined bucket walk (006BC440), Clear the inlined clear() (nodes freed, buckets kept);
// the buckets are at CSoundManager + 0x888, the count at + 0x894.
using DeferredMap = stlp::hash_map<std::int32_t, DeferredSound*, stlp::IntHash>;
using DeferredNode = DeferredMap::Node;
static_assert(sizeof(DeferredMap) == 0x14);

// The wave cache (006B89F0): a list of loaded waves, most recently used first, and a hash_map from
// the wave name to its list node (CSoundManager + 0x870).
struct WaveRecord {
    void* data;                 // +0x00 Miles memory (AIL_mem_alloc_lock), or the decompressed MP3
    std::uint32_t name[3];      // +0x04 STLport string
    std::uint32_t lastUsed;     // +0x10 GetTickCount()
    std::int32_t size;          // +0x14 bytes
    std::int32_t uses;          // +0x18 voices playing it (CacheEntry's +0x20 is this field of the node)
};
static_assert(sizeof(WaveRecord) == 0x1C);
// CList<WaveRecord> (0x1C bytes). Its NewNode / FreeNode stay the instantiation's (006BD4F0 /
// 006BD560, sound_manager.cpp), which copy and destroy the record themselves.
using WaveList = mfc::CList<WaveRecord, mfc::RawElements<WaveRecord>>;
using WaveNode = WaveList::Node;
static_assert(sizeof(WaveNode) == 0x24);
// CSoundManager (0x8C0 bytes, vtable 007440F0, class id 0xE). Only the fields used by the
// rewritten code are named.
struct CSoundManager : CEngineObject {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x007440F0);

    void* waveDriver;                   // +0x01C HDIGDRIVER (Open)
    void* midiDriver;                   // +0x020 HMDIDRIVER (closed by Close; not opened here)
    CResourceStream streamReader;       // +0x024 reads the music stream's files (StreamRead)
    mfc::CString soundPath;             // +0x058 the sound folder, ending in a backslash (SetPath)
    CFileRegistry soundRegistry;        // +0x05C opened by SetPath
    CMappedResource archive074;         // +0x074
    CFileRegistry registry0CC;          // +0x0CC
    // +0x0E4 the archives LoadSample looks in (CTypedPtrList, vtable 00744130), filled by
    // AddWaveSource.
    mfc::CTypedPtrList<CMappedResource> waveSources;
    // The MP3 music stream (+0x100, filled by 006BB3A0 on the stream thread 006BB910).
    volatile std::int32_t streamState;  // +0x100 0 idle, 1 playing, 2 switching to nextName
    void* streamSample;                 // +0x104 HSAMPLE
    void* streamFile;                   // +0x108 ASI stream
    void** streamCodec;                 // +0x10C ASI codec functions (slot 3: close)
    void* streamBuffers[3];             // +0x110 Miles memory, streamBufferSize bytes each (OpenStream)
    std::int32_t streamBufferSize;      // +0x11C twice Miles' minimum buffer; -1 after ResetStream
    std::int32_t streamBufferFrames;    // +0x120 frames in one buffer: streamBufferSize / streamFrameBytes
    // +0x124 which buffer each half of Miles' double buffer plays, +0x12C the third one (taken
    // next, and what 006BB3A0 fills): -1 after ResetStream; 0, 1 and 2 after OpenStream.
    std::int32_t streamHalfBuffer[2];
    std::int32_t streamSpareBuffer;
    std::int32_t streamPosition;        // +0x130 frames of the playing file queued so far
    std::int32_t streamSwitchAt;        // +0x134 position at which nextName starts
    // +0x138 the position at which the playing file starts again from its beginning (the first
    // "Markers2" value of its section in the sound registry; 0x7FFFFFFF without one), +0x13C the
    // bytes in a frame (bits * channels / 8): both -1 after ResetStream. +0x140 the byte that is
    // silence in the file's format (0 for 16 bits, else 0x7F; 0 after ResetStream).
    std::int32_t streamLoopAt;
    std::int32_t streamFrameBytes;
    std::uint8_t streamSilence;
    std::uint8_t unknown141[3];
    SoundFader musicFader;              // +0x144 music volume (options) and fades
    mfc::CString streamName;            // +0x160 playing
    mfc::CString nextName;              // +0x164 requested
    lib::AlignedArray<std::int32_t> markers;    // +0x168 switch points ("Markers1/2" in the file)
    lib::AlignedArray<std::int32_t> markers2;   // +0x178
    std::uint8_t nameLock[0x18];        // +0x188 CRITICAL_SECTION guarding streamName
    std::int32_t switchMode;            // +0x1A0 how nextName was requested (RequestStream mode)
    std::int32_t streamFileSize;        // +0x1A4 bytes of the open stream file (StreamRead)
    std::int32_t streamReadPosition;    // +0x1A8
    void* streamDecoder;                // +0x1AC HSAMPLE whose processor is the MP3 codec (streamCodec is in it)
    std::uint8_t streamLock[0x18];      // +0x1B0 CRITICAL_SECTION guarding the stream and musicFader
    mfc::CWinThread* streamThread;      // +0x1C8 006BB910's thread (Open), when the MP3 stream opened
    // The MIDI sequence (+0x1CC; 006BBA00-006BBDF0).
    std::int32_t midiState;             // +0x1CC
    void* midiData1D0;                  // +0x1D0 Miles memory (AIL_mem_alloc_lock)
    void* midiData1D4;                  // +0x1D4 Miles memory
    void* midiSequence;                 // +0x1D8 HSEQUENCE
    mfc::CString midiName;              // +0x1DC
    mfc::CString midiNextName;          // +0x1E0
    lib::AlignedArray<std::int32_t> midiArray1E4;   // +0x1E4
    lib::AlignedArray<std::int32_t> midiArray1F4;   // +0x1F4
    std::uint8_t unknown204[8];
    SoundFader midiFader;               // +0x20C
    std::uint8_t unknown228[4];
    std::uint8_t midiLock[0x18];        // +0x22C CRITICAL_SECTION
    mfc::CWinThread* midiThread;        // +0x244 006BBCB0's thread
    volatile std::int32_t threadsQuit;  // +0x248 set by Close; both threads end on it
    SoundSlot* slots;                   // +0x24C 15 voices (an operator new[] block: the count before them)
    Vector3 listener;                   // +0x250 listener position (z: camera height)
    Vector3 listenerAxis;               // +0x25C listener direction, unit length in x/y
    std::int32_t nextHandle;            // +0x268
    SoundEmitter emitters[16];          // +0x26C
    std::uint32_t emitterTick;          // +0x5EC
    SoundSequence sequences[8];         // +0x5F0
    std::int32_t cacheBytes;            // +0x850 bytes held by the wave cache
    WaveList cacheList;                 // +0x854 the cached waves, most recently used first
    std::uint8_t cache[4];              // +0x870 hash_map<string, cached wave> (006BBDF0): functor bytes
    std::uint8_t cacheBuckets[0x10];    // +0x874 its bucket vector and element count
    DeferredMap deferred;               // +0x884
    std::int32_t enabled;               // +0x898
    std::int32_t unknown89C;
    std::int32_t reverbOn;              // +0x8A0
    std::int32_t panDirect;             // +0x8A4 0: pans are mirrored
    float reverb[3];                    // +0x8A8 room type / level / reflect time (006B69E0)
    std::int32_t effectsVolume;         // +0x8B4 0..100
    std::int32_t speechVolume;          // +0x8B8 0..100
    std::int32_t randomStart;           // +0x8BC ms

    // Rewritten (sound_manager.cpp).
    void SetReverb(float room, float level, float time);                                  // 006B69E0
    void PauseAll();                                                                      // 006B7320
    void ResumeAll();                                                                     // 006B7380
    void SetEffectsVolume(int volume);                                                    // 006B7740
    void SetSpeechVolume(int volume);                                                     // 006B78A0
    void SetMusicVolume(int volume);                                                      // 006B7A00
    SoundSlot* FindPlaying(int handle);                                                   // 006B7C10
    DeferredSound* FindDeferred(std::uint32_t handle);                                    // 006B7CB0
    void UpdatePositional(SoundSlot* slot);                                               // 006B7FC0
    int SetSoundVolume(int handle, int volume, int pan);                                 // 006B9100
    int SetSoundPosition(int handle, const float* position, float nearDist, float farDist);  // 006B92F0
    int StopSound(int handle);                                                            // 006B94A0
    void SetListener(const float* position, const float* axis);                           // 006B9750
    int AddEmitter(const SoundNameArray* names, const float* position, float inner, float outer, int minInterval,
                   int maxInterval);                                                      // 006B9950
    void MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                     int maxInterval);                                                    // 006B9A30
    int RemoveEmitter(int index);                                                         // 006B9AA0
    int AddSequence(const SoundNameArray* names, int priority, const float* position, float nearDist, float farDist,
                    int random, int count, int(__fastcall* chooser)(int, int));                // 006BA2D0
    int MoveSequence(std::uint32_t id, const float* position);                           // 006BA400
    int UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count);  // 006BA5E0
    int RemoveSequence(std::uint32_t id);                                                 // 006BA7C0
    void Tick();                                                                          // 006B9B10
    void FadeOutStream();                                                                 // 006BAFA0
    void FadeInStream();                                                                  // 006BAFE0
    int RequestStream(const char* name, int mode);                                       // 006BB020
    void StopStream(int mode);                                                            // 006BB240

    // A-H14 (sound_manager.cpp).
    // 006B71C0 (RET 4). Every voice ended (its cached wave released), handles restart at 0, the
    // deferred sounds deleted; with `stopStream` the stream stops too (StopStream(4)).
    void StopAll(bool stopStream);
    // 006B96E0 (RET 8; `this` and `size` unused). A wave's length in seconds (AIL_WAV_info): the
    // IMA ADPCM sample count, else data bytes * 8 / (bits * channels), divided by the rate. 0 when
    // bits * channels is 0. In ST0.
    double WaveLength(const void* data, int size);

    // A-H14 (sound_manager_lifetime.cpp).
    // 006B6D70 (slot 2, the engine object's Create; named so, rule 4): Reset, then 1.
    int CreateBody();
    // 006B62B0 (slot 0, RET 4). The destructor body 006B65D0, then operator delete when bit 0 of
    // `flags` is set. Returns this.
    CSoundManager* DeletingDestruct(unsigned flags);
    // 006B7060. Every wave source removed from the list and deleted (slot 0).
    void ClearWaveSources();
    // 006B7AB0 (RET 4). Opens the archive `path` and appends it to the wave sources. 1 (also when
    // sound is off), 0 when it cannot be opened (the archive is deleted again).
    int AddWaveSource(const char* path);
    // 006B8960 (RET 0xC). The first wave source holding `name`: its data (GetFile, size into
    // *size), the source into *source when not null. Null when none has it.
    void* LoadWaveFile(const char* name, std::uint32_t* size, CMappedResource** source);
    // 006B7110 (RET 8; A-H26, Rebirth-patched: callers use SoundSystemListFilesOriginal). `out`
    // emptied, then the names matching `pattern` of every wave source appended, source by source.
    void ListWaveFiles(mfc::CStringArray* out, const char* pattern);
    // 006BAE20 (RET 8). The sound folder (a backslash appended when missing) and the sound
    // registry opened. 1 when sound is off; else whether the registry opened.
    int SetPath(const char* folder, const char* registryPath);

    // Shared pieces of the original's inlined code.
    int FlatVolume(int level, int heightFade, int speech) const;
    void ApplyVolumePan(SoundSlot* slot) const;
    void EndSlot(SoundSlot* slot);
    SoundSlot* SlotForHandle(int handle) const;

    // 006BAEA0 (A-H14, sound_manager_lifetime.cpp). The playing stream's name under the name lock,
    // empty unless the stream plays or switches.
    mfc::CString CurrentStreamName();

    // A-H15 (sound_manager_lifetime.cpp).
    // 006B6040. Constructor body: CEngineObject's fields, the sub-objects (stream reader,
    // registries, archive, wave source list, the MP3 stream's strings, faders (volume 100), arrays
    // and locks, the MIDI sequence's), the emitters and sequences (their element constructors
    // 006B63D0 / 006B63F0 through the CRT's eh vector iterator), the wave cache list, both hash
    // maps (100 buckets), the vtable, AIL_startup, then Reset. Returns this.
    CSoundManager* Construct();
    // 006B65D0. Destructor body: Close, AIL_shutdown, the hash maps, the cache list (each record's
    // DestructElements 006BE210), the sequences and emitters, the MIDI and stream parts, the
    // wave source list, the registries and archive (006714A0 through its address: Rebirth),
    // the sound folder, the stream reader, CEngineObject's.
    void DestructBody();
    // 006BA9C0 (`this` is the manager's +0x100, the MP3 stream). The stream state reset: no
    // sample, file or codec, the positions -1, both names empty, switch mode 4, no read
    // position, the music fader at volume 100 with no fade.
    void ResetStream();
    // 006BAAC0 (RET 4; A-H25, sound_manager_lifetime.cpp; `this` is the manager's +0x100; Open).
    // The MP3 stream on `driver`: Miles' ASI codec for ".mp3", a sample to play (16-bit stereo,
    // 44100 Hz, silent) and one to decode with, three zeroed buffers, the buffer order 0 / 1 / 2,
    // no position, state 0, the music fader at its volume with no fade. 1; or CloseStream's 0
    // when the codec or a sample cannot be had.
    int OpenStream(void* driver);
    // 006BAC90 (A-H25; OpenStream's failure). The stream file closed through the codec, both
    // samples released, the buffers freed, ResetStream. Returns 0.
    int CloseStream();
    // 006BB910 (A-H25; the stream thread's entry 006B6A10 calls it). Until Close sets
    // threadsQuit, every 100 ms under the stream lock: a step of the music fader, its level as
    // the sample's volume, and for every half Miles has played out the spare buffer loaded into
    // it (the two change places, the position moves on by streamBufferFrames) and the next one
    // read (006BB3A0). Ends the thread (AfxEndThread), so it does not return.
    void StreamThread();
    // 006BB3A0 (A-H26; Rebirth-patched, so hooked in the vanilla launch only and called through
    // its address; `stream` is the manager's +0x100). The stream thread's reader: fills the spare
    // buffer with the next streamBufferFrames frames. Idle: silence. Else the playing file's
    // frames up to its loop point (then from its beginning again) or, when a switch is asked, up
    // to the switch point (mode 2: the whole buffer once the fade out has finished, the fade then
    // held for 4 s; mode 1: where the file ends), and the rest from the next file: its name
    // looked up in the sound folder, its markers read from the sound registry, the decoder opened
    // on it and the sample set to its rate and format. No next file (or it does not open):
    // silence, the stream idle and its name empty. A file that ends early otherwise starts again
    // from its beginning.
    void ReadStreamVanilla(void* stream);

    // 006B7D10 (A-H17, sound_manager.cpp; Play2D / Play3D). The first free voice; else the voice
    // with the lowest priority (the first of equals) when `priority` is above it: a looping sound
    // there goes to the deferred map (its state, Miles sample position and handle) and the voice
    // is ended. -1 when no voice can be had.
    int AllocateVoice(int priority);

    // A-H18 (sound_manager_lifetime.cpp).
    // 006B6A30 (RET 0x14; startup 00456440). Close (slot 3), then Miles on the DirectSound device
    // `deviceIndex` (DirectSoundEnumerateA's order; the default device when out of range or
    // negative; the wave mapper when the sound option is 2), 16-bit stereo at `rate` (at most
    // 44100), reverb when `useReverb`; the MP3 stream and its thread, 15 voices with their Miles
    // samples, `enabled` 1. 0 when the driver cannot be opened (Miles' error to the debug log).
    // `bits` is not read.
    int Open(void* window, int useReverb, int deviceIndex, unsigned rate, int bits);
    // 006B6D80 (slot 3, the engine object's Destroy; DestructBody). The stream and MIDI threads
    // told to quit and waited for, every voice ended and its sample released, the voices freed,
    // the deferred sounds deleted, the wave cache freed, the MIDI sequence, memory and both
    // drivers closed, the wave sources deleted, the stream file, archive and sound registry
    // closed, then Reset.
    void Close();
    // 006B73E0 (Construct, CreateBody, Close; EI_Plugin calls it from 006B6272). Every field
    // Open / Close manage zeroed: no drivers, threads or voices, the stream reset, emitters and
    // sequences free, volumes 100, no sound folder, sound off.
    void Reset();
    // StopAll's and Close's loop (sound_manager.cpp): every deferred record deleted, the map
    // emptied.
    void DeleteDeferred();

    // Not rewritten.
    void StreamNotify(int mode);                                                          // 006BBAC0 (empty)
    int Play3D(const char* name, int priority, const float* position, float nearDist, float farDist, int loop,
               int speech);                                                               // 006B8600
    int Play2D(const char* name, int priority, int volume, int pan, int loop, int heightFade, int speech,
               int compressed);                                                          // 006B8250
    // 006B89F0. Puts wave `name` into the voice (from the cache, or read from the archives; with
    // `compressed` an MP3 decompressed through the ASI codec), sets its length, marks it playing
    // and gives it a new handle. 0 when the wave is nowhere.
    int LoadSample(SoundSlot* slot, int index, const char* name, int compressed);
};
static_assert(offsetof(CSoundManager, streamState) == 0x100);
static_assert(offsetof(CSoundManager, musicFader) == 0x144);
static_assert(offsetof(CSoundManager, streamName) == 0x160);
static_assert(offsetof(CSoundManager, markers) == 0x168);
static_assert(offsetof(CSoundManager, switchMode) == 0x1A0);
static_assert(offsetof(CSoundManager, streamLock) == 0x1B0);
static_assert(offsetof(CSoundManager, slots) == 0x24C);
static_assert(offsetof(CSoundManager, emitters) == 0x26C);
static_assert(offsetof(CSoundManager, sequences) == 0x5F0);
static_assert(offsetof(CSoundManager, cache) == 0x870);
static_assert(offsetof(CSoundManager, streamReader) == 0x24 && offsetof(CSoundManager, soundPath) == 0x58);
static_assert(offsetof(CSoundManager, archive074) == 0x74 && offsetof(CSoundManager, registry0CC) == 0xCC);
static_assert(offsetof(CSoundManager, waveSources) == 0xE4);
static_assert(offsetof(CSoundManager, streamReadPosition) == 0x1A8);
static_assert(offsetof(CSoundManager, streamBufferSize) == 0x11C && offsetof(CSoundManager, streamSilence) == 0x140);
static_assert(offsetof(CSoundManager, markers2) == 0x178 && offsetof(CSoundManager, streamThread) == 0x1C8);
static_assert(offsetof(CSoundManager, midiName) == 0x1DC && offsetof(CSoundManager, midiArray1F4) == 0x1F4);
static_assert(offsetof(CSoundManager, midiFader) == 0x20C && offsetof(CSoundManager, midiLock) == 0x22C);
static_assert(offsetof(CSoundManager, cacheList) == 0x854);
static_assert(offsetof(CSoundManager, deferred) == 0x884);
static_assert(offsetof(CSoundManager, enabled) == 0x898);
static_assert(offsetof(CSoundManager, randomStart) == 0x8BC);
static_assert(sizeof(CSoundManager) == 0x8C0);

// A music theme (0x38 bytes) from music.reg: one per allod, open land or dungeon.
struct MusicTheme {
    mfc::CString name;          // +0x00 allod
    std::int32_t dungeon;       // +0x04 0 open land, 1 dungeon
    mfc::CString briefing;      // +0x08
    mfc::CString constructor;   // +0x0C
    SoundNameArray calm;        // +0x10 CalmOpen / CalmDungeon
    SoundNameArray combat;      // +0x24 Combat
};
static_assert(sizeof(MusicTheme) == 0x38);

// CMusicSystem (0x58 bytes, vtable 00744098, class id 0xE): picks the music for the game state.
struct CMusicSystem : CEngineObject {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744098);
    static constexpr std::uintptr_t kThemeArrayVtable = std::uintptr_t(0x007440CC);

    void* themesVtable;                 // +0x1C CArray<MusicTheme>
    MusicTheme* themes;                 // +0x20
    std::int32_t themeCount;            // +0x24
    std::int32_t themeMax;              // +0x28
    std::int32_t themeGrow;             // +0x2C
    mfc::CString credits;               // +0x30 Common\Credits
    mfc::CString mainMenu;              // +0x34 Common\MainMenu
    std::int32_t state;                 // +0x38 0 off, 1 zone, 2 briefing, 3 constructor, 4 main menu, 5 credits
    std::int32_t theme;                 // +0x3C
    std::int32_t inCombat;              // +0x40
    std::int32_t holding;               // +0x44 set by SetZoneMode, cleared by the world frame (005641F0)
    mfc::CString combatTrack;           // +0x48
    mfc::CString jingle;                // +0x4C
    std::int32_t jinglePlaying;         // +0x50
    std::uint32_t calmDue;              // +0x54 timeGetTime() for the next calm track (0: not chosen)

    // 006B4710 (vtable slot 2, the engine object's Create): reads music.reg from the text path
    // (007AF45C): Common\Credits and Common\MainMenu, then for every other section an open land
    // theme (CalmOpen) and a dungeon theme (CalmDungeon), both with the section's Briefing,
    // Constructor and Combat. Returns 1, or FatalError's result when the file cannot be opened.
    int LoadThemes();
    void SetZoneMode(const char* allod, int dungeon);  // 006B4E30
    void Stop();                                       // 006B4F70
    void PlayBriefing(const char* allod);              // 006B4F90
    void PlayConstructor(const char* allod);           // 006B5080
    void PlayCredits();                                // 006B5170
    void PlayMainMenu();                               // 006B51F0
    void PlayJingle(const char* name);                 // 006B5270
    int UpdateMusic();                                // 006B5350 (vtable slot 6)

    // A-H13 (sound_music.cpp).
    // 006B4350. Constructor body: CEngineObject's fields, an empty theme array, the strings
    // constructed empty, the vtable, then ResetThemes (theme, +0x3C, is left as it is).
    // Returns this.
    CMusicSystem* Construct();
    // 006B44B0. Destructor body: ResetThemes (the array through SetSize(0)), the four strings
    // destroyed, the theme array's destructor (006B56C0), CEngineObject's.
    void DestructBody();
    // 006B4490 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CMusicSystem* DeletingDestruct(unsigned flags);
    // 006B4690 (slot 3, the engine object's Destroy; named so, rule 4). State 0, the themes
    // destroyed and freed, credits / main menu / jingle / combat track "none", jinglePlaying,
    // calmDue, inCombat and holding 0.
    void ResetThemes();
    // 006B4E10 / 006B4E20 (ECX not read). The sound manager's stream fades out / in.
    void FadeOut();
    void FadeIn();
};
static_assert(offsetof(CMusicSystem, credits) == 0x30);
static_assert(offsetof(CMusicSystem, holding) == 0x44);
static_assert(offsetof(CMusicSystem, calmDue) == 0x54);
static_assert(sizeof(CMusicSystem) == 0x58);
// 006B4470 / 006B4480 (slots 11, 12): "CMusicSystem" (007967F4), class id 0xE.
const char* __fastcall MusicSystemClassName(CMusicSystem* self);
// 006B6290 / 006B62A0 (slots 11, 12): "CSoundManager" (007968C0), class id 0xE.
const char* __fastcall SoundManagerClassName(CSoundManager* self);
int __fastcall SoundManagerClassId(CSoundManager* self);
// 006BA950. The music stream file's read callback (__stdcall, handed to the ASI decoder by the
// stream fill 006BB3A0; stream thread): seeks to `offset` unless it is negative, then reads up to
// `size` bytes, cut at the end of the file. Returns the byte count (0 when the read fails); the
// read position advances by it.
int __stdcall SoundStreamRead(CSoundManager* manager, void* buffer, int size, int offset);
int __fastcall MusicSystemClassId(CMusicSystem* self);

namespace sound {
inline CSoundManager* Manager() { return *reinterpret_cast<CSoundManager* const*>(0x007AF784); }
}
