#pragma once

// Sound and music: the sound manager (CSoundManager, one instance) built on the Miles Sound System
// (mss32.dll), its MP3 music stream and MIDI threads, and the music state machine (CMusicSystem, world/app
// object + 0x1A8). Names are provisional. See docs/notes/sound.md for the map of the range.

#include "engine_object.h"

#include "../lib/containers.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "../lib/stlp_hash.h"
#include "../lib/vecmath.h"
#include "registry.h"
#include "resource_stream.h"
#include "game_data.h"

// BOOL results are full 0/1 ints in EAX.

#include <cstddef>
#include <cstdint>
#include <windows.h>   // CRITICAL_SECTION

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A volume envelope: the music stream's (CSoundManager + 0x144) and the MIDI
// sequence's. `level` is the Miles volume 0..127 derived from `volume` (0..100) and the
// fade `factor`.
namespace mfc::impl { struct WinThread; }

struct SoundFader {
    std::int32_t volume;        // 0..100
    std::int32_t level;         // 0..127, round(volume * factor * 127 * 0.01)
    float factor;               // fade factor 0..1
    float step;                 // added / subtracted per update (100 ms on the stream thread)
    std::int32_t mode;          // 0 none, 1 fade in, 2 fade out
    std::int32_t finished;      // set when a fade out reached 0
    std::uint32_t holdUntil;    // timeGetTime() at which the fade is cancelled (0: none)

    void SetHold(std::uint32_t until);
    void Update();                      // stream thread
    void Recalc();
};
static_assert(sizeof(SoundFader) == 0x1C);

// One of the 15 Miles sample voices (array at CSoundManager + 0x24C, 0x54 bytes each).
struct SoundSlot {
    stlp::string name;          // the wave name
    void* sample;               // HSAMPLE
    std::int32_t volume;        // Miles volume 0..127
    std::int32_t baseVolume;    // 2D sounds: volume 0..100
    std::int32_t pan;           // -100..100
    std::int32_t priority;
    std::int32_t playing;
    std::int32_t looping;       // keep as a deferred record when the voice is stolen
    std::int32_t speech;        // scaled by the speech volume instead of effects
    float length;               // seconds
    std::int32_t finished;      // set by the end-of-sample callback (Miles thread)
    std::int32_t flat;          // 2D: volume from baseVolume, not from the listener distance
    std::int32_t heightFade;    // 2D sounds fading out as the camera rises (30..70)
    Vector3 position;           // 3D position
    float nearDistance;         // full volume inside
    float farDistance;          // silent outside
    std::int32_t handle;        // sound handle (-1: free)

    void End(struct CSoundManager* manager);
};
static_assert(sizeof(SoundSlot) == 0x54);
static_assert(offsetof(SoundSlot, finished) == 0x30 && offsetof(SoundSlot, handle) == 0x50);

// A sound that lost its voice to a higher priority one and waits to be restarted (0x48 bytes,
// allocated with operator new and kept in the hash map at CSoundManager + 0x884, keyed by handle).
struct DeferredSound {
    stlp::string name;
    std::int32_t volume;
    std::int32_t baseVolume;
    std::int32_t pan;
    std::int32_t priority;
    std::int32_t speech;
    float length;
    std::int32_t flat;
    std::int32_t heightFade;
    Vector3 position;
    float nearDistance;
    float farDistance;
    std::int32_t handle;        // (the map key)
    std::int32_t samplePosition;
};
static_assert(sizeof(DeferredSound) == 0x48);

// MFC CStringArray, as embedded in the sound records.
using SoundNameArray = mfc::CStringArray;

// An ambient emitter (16 at CSoundManager + 0x26C): plays one of its names at random
// intervals while the listener is within `outerRadius`.
struct SoundEmitter {
    std::int32_t active;
    SoundNameArray names;
    Vector3 position;
    float innerRadius;
    float outerRadius;
    std::int32_t minInterval;   // ms
    std::int32_t maxInterval;   // ms
    std::uint32_t lastPlayed;   // GetTickCount()
};
static_assert(sizeof(SoundEmitter) == 0x38);

// A 3D sound sequence (8 at CSoundManager + 0x5F0): plays its names one after the
// other (or at random, or as a callback chooses) at one position.
struct SoundSequence;
// The stream thread's procedure, CSoundManager::StreamThread.
unsigned __cdecl StreamThreadEntry(void* manager);
// The elements' constructors (return this) and destructors, handed to the CRT's eh vector iterators.
SoundEmitter* __fastcall ConstructEmitter(SoundEmitter* emitter);
void __fastcall DestructEmitter(SoundEmitter* emitter);
SoundSequence* __fastcall ConstructSequence(SoundSequence* sequence);
void __fastcall DestructSequence(SoundSequence* sequence);

struct SoundSequence {
    std::int32_t active;
    SoundNameArray names;
    std::int32_t handle;        // current sound
    Vector3 position;
    float nearDistance;
    float farDistance;
    std::int32_t remaining;     // sounds left to play (callback: sounds played)
    std::int32_t index;         // next name
    std::int32_t priority;
    std::int32_t random;        // pick names at random
    // +0x40 returns the next index (bit 30: loop it), < 0 ends; called with the previous index and the
    // number of sounds started before.
    int(__fastcall* chooser)(int previous, int played);
    std::int32_t generation;
    std::int32_t id;            // generation << 8 | slot (-1: free)

    int PlayNext(struct CSoundManager* manager);
};
static_assert(sizeof(SoundSequence) == 0x4C);

// The STLport hash_map<int, DeferredSound*> at CSoundManager + 0x884.
// Find is the inlined bucket walk, Clear the inlined clear() (nodes freed, buckets kept);
// the buckets are at CSoundManager + 0x888, the count at + 0x894.
using DeferredMap = stlp::hash_map<std::int32_t, DeferredSound*, stlp::IntHash>;
using DeferredNode = DeferredMap::Node;
static_assert(sizeof(DeferredMap) == 0x14);

// The wave cache: a list of loaded waves, most recently used first, and a hash_map from
// the wave name to its list node (CSoundManager + 0x870).
// The name stays raw words: the record is copied bitwise by the cache list (CList with raw
// elements, RemoveHead by value) and its name built and freed by hand; Name() is the string.
struct WaveRecord {
    void* data;                 // Miles memory (AIL_mem_alloc_lock), or the decompressed MP3
    std::uint32_t name[3];      // STLport string (Name())
    std::uint32_t lastUsed;     // GetTickCount()
    std::int32_t size;          // bytes
    std::int32_t uses;          // voices playing it (CacheEntry's +0x20 is this field of the node)

    // operator= (sound_manager.cpp): the data pointer, the name by STLport's assign (unless it is the same
    // string), the three counters.
    WaveRecord* Assign(const WaveRecord* from);

    stlp::string& Name() { return *reinterpret_cast<stlp::string*>(name); }
    const stlp::string& Name() const { return *reinterpret_cast<const stlp::string*>(name); }
};
static_assert(sizeof(WaveRecord) == 0x1C);
// CList<WaveRecord>. Its NewNode / FreeNode stay the instantiation's (sound_manager.cpp),
// which copy and destroy the record themselves.
using WaveList = mfc::CList<WaveRecord, mfc::RawElements<WaveRecord>>;
using WaveNode = WaveList::Node;
static_assert(sizeof(WaveNode) == 0x24);
// CSoundManager (0x8C0 bytes, class id 0xE). Only the fields used by the rewritten code are named. A real
// virtual class: the compiler makes its vtable.
// Miles' ASISTAGE (mss.h), the codec stage of a sample's pipeline as the stream uses it: the ASI
// codec's stream functions (all __stdcall) and the handles of the attributes it reports.
struct AsiStage {
    void*(__stdcall* open)(void* user, void* read, int size);       // ASI_stream_open
    int(__stdcall* process)(void* stream, void* buffer, int bytes); // ASI_stream_process: bytes decoded
    void(__stdcall* seek)(void* stream, int position);              // ASI_stream_seek
    void(__stdcall* close)(void* stream);                           // ASI_stream_close
    int(__stdcall* attribute)(void* stream, void* attribute);       // ASI_stream_attribute
    void* setPreference;                                            // ASI_stream_set_preference
    void* inputBitRate;
    void* inputSampleRate;
    void* inputBits;
    void* inputChannels;
    void* outputBitRate;
    void* outputSampleRate;
    void* outputBits;
    void* outputChannels;
};

struct CSoundManager : CEngineObject {
    // CEngineObject's fields, the sub-objects (stream reader, registries, archive, wave source list, the
    // MP3 stream's strings, faders (volume 100), arrays and locks, the MIDI sequence's), the emitters and
    // sequences (their element constructors through the CRT's eh vector iterator), the wave cache list,
    // both hash maps (100 buckets), AIL_startup, then Reset.
    CSoundManager();
    ~CSoundManager() override;
    // The deleting destructor frees into the game's heap.

    int Create() override { return CreateBody(); }
    void Destroy() override { Close(); }
    const char* GetClassName() override;
    int GetClassId() override;                        // class id 0xE

    void* waveDriver;                   // HDIGDRIVER (Open)
    void* midiDriver;                   // HMDIDRIVER (closed by Close; not opened here)
    CResourceStream streamReader;       // reads the music stream's files (StreamRead)
    mfc::CString soundPath;             // the sound folder, ending in a backslash (SetPath)
    CFileRegistry soundRegistry;        // opened by SetPath
    CMappedResource archive074;
    CFileRegistry registry0CC;
    // +0x0E4 the archives LoadSample looks in (CTypedPtrList), filled by AddWaveSource.
    mfc::CTypedPtrList<CMappedResource> waveSources;
    // The MP3 music stream (+0x100, filled on the stream thread).
    volatile std::int32_t streamState;  // 0 idle, 1 playing, 2 switching to nextName
    void* streamSample;                 // HSAMPLE
    void* streamFile;                   // ASI stream
    AsiStage* streamCodec;              // the decoder sample's codec stage
    void* streamBuffers[3];             // Miles memory, streamBufferSize bytes each (OpenStream)
    std::int32_t streamBufferSize;      // twice Miles' minimum buffer; -1 after ResetStream
    std::int32_t streamBufferFrames;    // frames in one buffer: streamBufferSize / streamFrameBytes
    // +0x124 which buffer each half of Miles' double buffer plays, +0x12C the third one (taken next, and
    // what the stream reader fills): -1 after ResetStream; 0, 1 and 2 after OpenStream.
    std::int32_t streamHalfBuffer[2];
    std::int32_t streamSpareBuffer;
    std::int32_t streamPosition;        // frames of the playing file queued so far
    std::int32_t streamSwitchAt;        // position at which nextName starts
    // +0x138 the position at which the playing file starts again from its beginning (the first
    // "Markers2" value of its section in the sound registry; 0x7FFFFFFF without one), +0x13C the
    // bytes in a frame (bits * channels / 8): both -1 after ResetStream. +0x140 the byte that is
    // silence in the file's format (0 for 16 bits, else 0x7F; 0 after ResetStream).
    std::int32_t streamLoopAt;
    std::int32_t streamFrameBytes;
    std::uint8_t streamSilence;
    std::uint8_t unused_141[3];
    SoundFader musicFader;              // music volume (options) and fades
    mfc::CString streamName;            // playing
    mfc::CString nextName;              // requested
    lib::AlignedArray<std::int32_t> markers;    // switch points ("Markers1/2" in the file)
    lib::AlignedArray<std::int32_t> markers2;
    CRITICAL_SECTION nameLock;          // guarding streamName
    std::int32_t switchMode;            // how nextName was requested (RequestStream mode)
    std::uint32_t streamFileSize;       // bytes of the open stream file (StreamRead)
    std::int32_t streamReadPosition;
    void* streamDecoder;                // HSAMPLE whose processor is the MP3 codec (streamCodec is in it)
    CRITICAL_SECTION streamLock;        // guarding the stream and musicFader
    mfc::impl::WinThread* streamThread; // the stream thread (Open), when the MP3 stream opened
    // The MIDI sequence.
    std::int32_t midiState;
    void* midiData1D0;                  // Miles memory (AIL_mem_alloc_lock)
    void* midiData1D4;                  // Miles memory
    void* midiSequence;                 // HSEQUENCE
    mfc::CString midiName;
    mfc::CString midiNextName;
    lib::AlignedArray<std::int32_t> midiArray1E4;
    lib::AlignedArray<std::int32_t> midiArray1F4;
    std::uint8_t unused_204[8];
    SoundFader midiFader;
    std::uint8_t unused_228[4];
    CRITICAL_SECTION midiLock;
    mfc::impl::WinThread* midiThread;   // the MIDI thread
    volatile std::int32_t threadsQuit;  // set by Close; both threads end on it
    SoundSlot* slots;                   // 15 voices (an operator new[] block: the count before them)
    Vector3 listener;                   // listener position (z: camera height)
    Vector3 listenerAxis;               // listener direction, unit length in x/y
    std::int32_t nextHandle;
    SoundEmitter emitters[16];
    std::uint32_t emitterTick;
    SoundSequence sequences[8];
    std::int32_t cacheBytes;            // bytes held by the wave cache
    WaveList cacheList;                 // the cached waves, most recently used first
    stlp::NameMap cache;                // wave name -> its cache list node (WaveNode*)
    DeferredMap deferred;
    std::int32_t enabled;
    std::int32_t spare;                 // 0 after Reset; nothing reads it
    std::int32_t reverbOn;
    std::int32_t panDirect;             // 0: pans are mirrored
    float reverb[3];                    // room type / level / reflect time
    std::int32_t effectsVolume;         // 0..100
    std::int32_t speechVolume;          // 0..100
    std::int32_t randomStart;           // ms

    // Defined in sound_manager.cpp.
    void SetReverb(float room, float level, float time);
    void PauseAll();
    void ResumeAll();
    void SetEffectsVolume(int volume);
    void SetSpeechVolume(int volume);
    void SetMusicVolume(int volume);
    SoundSlot* FindPlaying(int handle);
    DeferredSound* FindDeferred(std::uint32_t handle);
    void UpdatePositional(SoundSlot* slot);
    int SetSoundVolume(int handle, int volume, int pan);
    int SetSoundPosition(int handle, const float* position, float nearDist, float farDist);
    int StopSound(int handle);
    void SetListener(const float* position, const float* axis);
    int AddEmitter(const SoundNameArray* names, const float* position, float inner, float outer, int minInterval,
                   int maxInterval);
    void MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                     int maxInterval);
    int RemoveEmitter(int index);
    int AddSequence(const SoundNameArray* names, int priority, const float* position, float nearDist, float farDist,
                    int random, int count, int(__fastcall* chooser)(int, int));
    int MoveSequence(std::uint32_t id, const float* position);
    int UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count);
    int RemoveSequence(std::uint32_t id);
    void Tick();
    void FadeOutStream();
    void FadeInStream();
    int RequestStream(const char* name, int mode);
    void StopStream(int mode);

    // sound_manager.cpp. Every voice ended (its cached wave released), handles restart at 0, the deferred
    // sounds deleted; with `stopStream` the stream stops too (StopStream(4)).
    void StopAll(bool stopStream);
    // A wave's length in seconds (AIL_WAV_info): the IMA ADPCM sample count, else data bytes * 8 / (bits *
    // channels), divided by the rate. 0 when bits * channels is 0. Returned in ST0. (`this` and `size` are
    // unused.)
    double WaveLength(const void* data, int size);

    // sound_manager_lifetime.cpp. The engine object's Create (named so, rule 4): Reset, then 1.
    int CreateBody();
    // Every wave source removed from the list and deleted.
    void ClearWaveSources();
    // Opens the archive `path` and appends it to the wave sources. 1 (also when
    // sound is off), 0 when it cannot be opened (the archive is deleted again).
    int AddWaveSource(const char* path);
    // The first wave source holding `name`: its data (GetFile, size into
    // *size), the source into *source when not null. Null when none has it.
    void* LoadWaveFile(const char* name, std::uint32_t* size, CMappedResource** source);
    // Rebirth-patched (callers use SoundSystemListFilesOriginal). `out` emptied, then the names matching
    // `pattern` of every wave source appended, source by source.
    void ListWaveFiles(mfc::CStringArray* out, const char* pattern);
    // The sound folder (a backslash appended when missing) and the sound
    // registry opened. 1 when sound is off; else whether the registry opened.
    int SetPath(const char* folder, const char* registryPath);

    // Shared pieces of code the game inlines.
    int FlatVolume(int level, int heightFade, int speech) const;
    void ApplyVolumePan(SoundSlot* slot) const;
    void EndSlot(SoundSlot* slot);
    SoundSlot* SlotForHandle(int handle) const;

    // The playing stream's name under the name lock, empty unless the stream plays or switches.
    mfc::CString CurrentStreamName();

    // `this` is the manager's +0x100, the MP3 stream. The stream state reset: no sample, file or codec, the
    // positions -1, both names empty, switch mode 4, no read position, the music fader at volume 100 with
    // no fade.
    void ResetStream();
    // Open (`this` is the manager's +0x100). The MP3 stream on `driver`: Miles' ASI codec for ".mp3", a
    // sample to play (16-bit stereo, 44100 Hz, silent) and one to decode with, three zeroed buffers, the
    // buffer order 0 / 1 / 2, no position, state 0, the music fader at its volume with no fade. 1; or
    // CloseStream's 0 when the codec or a sample cannot be had.
    int OpenStream(void* driver);
    // OpenStream's failure: the stream file closed through the codec, both samples released, the buffers
    // freed, ResetStream. Returns 0.
    int CloseStream();
    // The stream thread's body. Until Close sets threadsQuit, every 100 ms under the stream lock: a step of
    // the music fader, its level as the sample's volume, and for every half Miles has played out the spare
    // buffer loaded into it (the two change places, the position moves on by streamBufferFrames) and the
    // next one read. Ends the thread (AfxEndThread), so it does not return.
    void StreamThread();
    // The stream thread's reader (`stream` is the manager's +0x100; Rebirth-patched, so hooked in the
    // vanilla launch only): fills the spare buffer with the next streamBufferFrames frames. Idle: silence.
    // Else the playing file's frames up to its loop point (then from its beginning again) or, when a switch
    // is asked, up to the switch point (mode 2: the whole buffer once the fade out has finished, the fade
    // then held for 4 s; mode 1: where the file ends), and the rest from the next file: its name looked up
    // in the sound folder, its markers read from the sound registry, the decoder opened on it and the
    // sample set to its rate and format. No next file (or it does not open): silence, the stream idle and
    // its name empty. A file that ends early otherwise starts again from its beginning.
    void ReadStreamVanilla(void* stream);

    // For Play2D / Play3D: the first free voice; else the voice with the lowest priority (the first of
    // equals) when `priority` is above it: a looping sound there goes to the deferred map (its state, Miles
    // sample position and handle) and the voice is ended. -1 when no voice can be had.
    int AllocateVoice(int priority);

    // sound_manager_lifetime.cpp. Open (called at startup): Close, then Miles on the DirectSound device
    // `deviceIndex` (DirectSoundEnumerateA's order; the default device when out of range or negative; the
    // wave mapper when the sound option is 2), 16-bit stereo at `rate` (at most 44100), reverb when
    // `useReverb`; the MP3 stream and its thread, 15 voices with their Miles samples, `enabled` 1. 0 when
    // the driver cannot be opened (Miles' error to the debug log). `bits` is not read.
    int Open(void* window, int useReverb, int deviceIndex, unsigned rate, int bits);
    // The engine object's Destroy (DestructBody). The stream and MIDI threads told to quit and waited for,
    // every voice ended and its sample released, the voices freed, the deferred sounds deleted, the wave
    // cache freed, the MIDI sequence, memory and both drivers closed, the wave sources deleted, the stream
    // file, archive and sound registry closed, then Reset.
    void Close();
    // Reset (Construct, CreateBody, Close): every field Open / Close manage zeroed: no drivers, threads or
    // voices, the stream reset, emitters and sequences free, volumes 100, no sound folder, sound off.
    void Reset();
    // StopAll's and Close's loop (sound_manager.cpp): every deferred record deleted, the map
    // emptied.
    void DeleteDeferred();

    // Not rewritten.
    void StreamNotify(int mode);                                                          // empty
    int Play3D(const char* name, int priority, const float* position, float nearDist, float farDist, int loop,
               int speech);
    int Play2D(const char* name, int priority, int volume, int pan, int loop, int heightFade, int speech,
               int compressed);
    // Puts wave `name` into the voice (from the cache, or read from the archives; with
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

// A music theme from music.reg: one per allod, open land or dungeon.
struct MusicTheme {
    // operator= (sound_music.cpp; the theme array's element assignment): the members in order, the arrays
    // by CStringArray::Copy.
    void Assign(const MusicTheme* from);

    mfc::CString name;          // allod
    std::int32_t dungeon;       // 0 open land, 1 dungeon
    mfc::CString briefing;
    mfc::CString constructor;
    SoundNameArray calm;        // CalmOpen / CalmDungeon
    SoundNameArray combat;      // Combat
};
static_assert(sizeof(MusicTheme) == 0x38);

// CMusicSystem (0x58 bytes, class id 0xE): picks the music for the game state. A real virtual class: the
// compiler makes its vtable.
struct CMusicSystem : CEngineObject {
    // CEngineObject's fields, an empty theme array, the strings empty, then ResetThemes
    // (theme, +0x3C, is left as it is).
    CMusicSystem();
    ~CMusicSystem() override;
    // The deleting destructor frees into the game's heap.

    int Create() override { return LoadThemes(); }
    void Destroy() override { ResetThemes(); }
    int RunTick() override { return UpdateMusic(); }
    const char* GetClassName() override;
    int GetClassId() override;                           // class id 0xE

    void* themesVtable;                 // CArray<MusicTheme>
    MusicTheme* themes;
    std::int32_t themeCount;
    std::int32_t themeMax;
    std::int32_t themeGrow;
    mfc::CString credits;               // Common\Credits
    mfc::CString mainMenu;              // Common\MainMenu
    std::int32_t state;                 // 0 off, 1 zone, 2 briefing, 3 constructor, 4 main menu, 5 credits
    std::int32_t theme;
    std::int32_t inCombat;
    std::int32_t holding;               // set by SetZoneMode, cleared by the world frame
    mfc::CString combatTrack;
    mfc::CString jingle;
    std::int32_t jinglePlaying;
    std::uint32_t calmDue;              // timeGetTime() for the next calm track (0: not chosen)

    // The engine object's Create: reads music.reg from the text path: Common\Credits and Common\MainMenu,
    // then for every other section an open land theme (CalmOpen) and a dungeon theme (CalmDungeon), both
    // with the section's Briefing, Constructor and Combat. Returns 1, or FatalError's result when the file
    // cannot be opened.
    int LoadThemes();
    void SetZoneMode(const char* allod, int dungeon);
    void Stop();
    void PlayBriefing(const char* allod);
    void PlayConstructor(const char* allod);
    void PlayCredits();
    void PlayMainMenu();
    void PlayJingle(const char* name);
    int UpdateMusic();

    // The engine object's Destroy (named so, rule 4). State 0, the themes destroyed and freed, credits /
    // main menu / jingle / combat track "none", jinglePlaying, calmDue, inCombat and holding 0.
    void ResetThemes();
    // The sound manager's stream fades out / in.
    void FadeOut();
    void FadeIn();
};
static_assert(offsetof(CMusicSystem, credits) == 0x30);
static_assert(offsetof(CMusicSystem, holding) == 0x44);
static_assert(offsetof(CMusicSystem, calmDue) == 0x54);
static_assert(sizeof(CMusicSystem) == 0x58);
// The music stream file's read callback (__stdcall, handed to the ASI decoder by the stream reader; stream
// thread): seeks to `offset` unless it is negative, then reads up to `size` bytes, cut at the end of the
// file. Returns the byte count (0 when the read fails); the read position advances by it.
int __stdcall SoundStreamRead(CSoundManager* manager, void* buffer, int size, int offset);
// The callback the speech and sound loader hands to Miles's AIL_decompress_ASI (its .mp3 path): at state 1
// it asks the decoder through `set` (name, value) for "Requested bit width" 16, "Requested sample rate"
// 44100 and "Requested # of channels" 2. Returns 1.
using DecoderPropertyFn = int(__stdcall*)(const char* name, std::uint32_t value);
int __stdcall SoundDecompressCallback(int state, DecoderPropertyFn set);

namespace sound {
}
