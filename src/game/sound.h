#pragma once

// Sound and music: the sound manager built on the Miles Sound System (mss32.dll), its MP3 music stream and
// MIDI threads, and the music state machine. See docs/notes/sound.md.

#include "engine_object.h"

#include "../lib/containers.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "../lib/hash_map.h"
#include "../lib/vecmath.h"
#include "registry.h"
#include "resource_stream.h"
#include "../lib/import_prototypes.h"

#include <list>
#include <unordered_map>
#include <vector>
#include <windows.h>

#undef GetClassName

// A volume envelope: the music stream's and the MIDI sequence's.
struct SoundFader {
    std::int32_t volume;        // 0..100
    std::int32_t level;         // 0..127, round(volume * factor * 127 * 0.01)
    float factor;               // fade factor 0..1
    float step;                 // added / subtracted per update (100 ms on the stream thread)
    std::int32_t mode;          // 0 none, 1 fade in, 2 fade out
    std::int32_t finished;      // set when a fade out reached 0
    std::uint32_t holdUntil;    // timeGetTime() at which the fade is cancelled (0: none)

    void SetHold(std::uint32_t until);
    void Update();                      // on the stream thread
    void Recalc();
};

// One of the 15 Miles sample voices.
struct SoundSlot {
    std::string name;           // the wave name
    miles::Sample* sample;
    std::int32_t volume;        // Miles volume 0..127
    std::int32_t baseVolume;    // 2D sounds: volume 0..100
    std::int32_t pan;           // -100..100
    std::int32_t priority;
    std::int32_t playing;
    std::int32_t looping;       // kept as a deferred record when the voice is stolen
    std::int32_t speech;        // scaled by the speech volume instead of effects
    float length;               // seconds
    std::int32_t finished;      // set by the end-of-sample callback (Miles thread)
    std::int32_t flat;          // 2D: volume from baseVolume, not from the listener distance
    std::int32_t heightFade;    // 2D sounds fading out as the camera rises (30..70)
    Vector3 position;
    float nearDistance;         // full volume inside
    float farDistance;          // silent outside
    std::int32_t handle;        // sound handle (-1: free)

    void End(struct CSoundManager* manager);
};

// A looping sound that lost its voice to a higher priority one and waits to be restarted.
struct DeferredSound {
    std::string name;
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
    std::int32_t handle;        // the map key
    std::int32_t samplePosition;
};

// An ambient emitter (16): plays one of its names at random intervals while the listener is within `outerRadius`.
struct SoundEmitter {
    std::int32_t active;
    std::vector<std::string> names;
    Vector3 position;
    float innerRadius;
    float outerRadius;
    std::int32_t minInterval;   // ms
    std::int32_t maxInterval;   // ms
    std::uint32_t lastPlayed;   // GetTickCount()
};

struct SoundSequence;
unsigned __stdcall StreamThreadEntry(void* manager);
// Handed to the CRT's eh vector iterators.

// A 3D sound sequence (8): plays its names one after the other (or at random, or as a callback chooses) at one
// position.
struct SoundSequence {
    std::int32_t active;
    std::vector<std::string> names;
    std::int32_t handle;        // current sound
    Vector3 position;
    float nearDistance;
    float farDistance;
    std::int32_t remaining;     // sounds left to play (callback: sounds played)
    std::int32_t index;         // next name
    std::int32_t priority;
    std::int32_t random;        // pick names at random
    // Returns the next index (bit 30: loop it), < 0 ends; called with the previous index and the number of
    // sounds started before.
    int(*chooser)(int previous, int played);
    std::int32_t generation;
    std::int32_t id;            // generation << 8 | slot (-1: free)

    int PlayNext(struct CSoundManager* manager);
};

// Walked in STLport's order (the first of the highest priority plays).
using DeferredMap = lib::HashMap<std::int32_t, DeferredSound*>;

// The wave cache: a list of loaded waves, most recently used first, and a hash_map from the wave name to its
// list node.
struct WaveRecord {
    void* data = nullptr;       // Miles memory (AIL_mem_alloc_lock), or the decompressed MP3
    std::string name;
    std::uint32_t lastUsed = 0; // GetTickCount()
    std::int32_t size = 0;      // bytes
    std::int32_t uses = 0;      // voices playing it
};
// Miles' ASISTAGE (mss.h): the ASI codec's stream functions and the handles of the attributes it reports.
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

// Miles' sample (HSAMPLE), as far as the stream reads it: its codec stage.
struct miles::Sample {
    std::uint8_t unused_000[0x144];
    AsiStage codec;
};
static_assert(offsetof(miles::Sample, codec) == 0x144);

struct CSoundManager : CEngineObject {
    // Also runs AIL_startup, then Reset.
    CSoundManager();
    ~CSoundManager() override;

    int Create() override { return CreateBody(); }
    void Destroy() override { Close(); }
    const char* GetClassName() override;
    int GetClassId() override;

    miles::DigitalDriver* waveDriver;
    miles::MidiDriver* midiDriver;      // closed by Close; not opened here
    CResourceStream streamReader;       // reads the music stream's files
    std::string soundPath;             // ending in a backslash
    CFileRegistry soundRegistry;
    std::list<CMappedResource*> waveSources;   // the archives LoadSample looks in
    // The MP3 music stream, filled on the stream thread.
    volatile std::int32_t streamState;  // 0 idle, 1 playing, 2 switching to nextName
    miles::Sample* streamSample;
    void* streamFile;                   // ASI stream
    AsiStage* streamCodec;              // the decoder sample's codec stage
    void* streamBuffers[3];             // Miles memory, streamBufferSize bytes each
    std::int32_t streamBufferSize;      // twice Miles' minimum buffer; -1 after ResetStream
    std::int32_t streamBufferFrames;    // frames in one buffer: streamBufferSize / streamFrameBytes
    // Which buffer each half of Miles' double buffer plays (the first two) and the third one (taken next, and
    // what the stream reader fills): -1 after ResetStream; 0, 1 and 2 after OpenStream.
    std::int32_t streamHalfBuffer[2];
    std::int32_t streamSpareBuffer;
    std::int32_t streamPosition;        // frames of the playing file queued so far
    std::int32_t streamSwitchAt;        // position at which nextName starts
    // The position at which the playing file starts again from its beginning (the first "Markers2" value of
    // its section in the sound registry; 0x7FFFFFFF without one), the bytes in a frame (bits * channels / 8):
    // both -1 after ResetStream; and the byte that is silence in the file's format (0 for 16 bits, else 0x7F;
    // 0 after ResetStream).
    std::int32_t streamLoopAt;
    std::int32_t streamFrameBytes;
    std::uint8_t streamSilence;
    SoundFader musicFader;              // music volume (options) and fades
    std::string streamName;            // playing
    std::string nextName;              // requested
    std::vector<std::int32_t> markers;    // switch points ("Markers1/2" in the file)
    std::vector<std::int32_t> markers2;
    CRITICAL_SECTION nameLock;          // guarding streamName
    std::int32_t switchMode;            // RequestStream's mode
    std::uint32_t streamFileSize;
    std::int32_t streamReadPosition;
    miles::Sample* streamDecoder;       // its processor is the MP3 codec (streamCodec is in it)
    CRITICAL_SECTION streamLock;        // guarding the stream and musicFader
    HANDLE streamThread;                // when the MP3 stream opened (lib::StartThread)
    // The MIDI sequence.
    std::int32_t midiState;
    miles::Sequence* midiSequence;
    SoundFader midiFader;
    CRITICAL_SECTION midiLock;
    HANDLE midiThread;                  // never started
    volatile std::int32_t threadsQuit;  // set by Close; both threads end on it
    SoundSlot* slots;                   // 15 voices (an operator new[] block: the count before them)
    Vector3 listener;                   // listener position (z: camera height)
    Vector3 listenerAxis;               // listener direction, unit length in x/y
    std::int32_t nextHandle;
    SoundEmitter emitters[16];
    std::uint32_t emitterTick;
    SoundSequence sequences[8];
    std::int32_t cacheBytes;            // bytes held by the wave cache
    std::list<WaveRecord> cacheList;                 // the cached waves, most recently used first
    std::unordered_map<std::string, std::list<WaveRecord>::iterator> cache;                    // wave name -> its cache list node
    DeferredMap deferred;
    std::int32_t enabled;
    std::int32_t spare;                 // 0 after Reset; nothing reads it
    std::int32_t reverbOn;
    std::int32_t panDirect;             // 0: pans are mirrored
    float reverb[3];                    // room type / level / reflect time
    std::int32_t effectsVolume;         // 0..100
    std::int32_t speechVolume;          // 0..100
    std::int32_t randomStart;           // ms

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
    int AddEmitter(const std::vector<std::string>* names, const float* position, float inner, float outer, int minInterval,
                   int maxInterval);
    void MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                     int maxInterval);
    int RemoveEmitter(int index);
    int AddSequence(const std::vector<std::string>* names, int priority, const float* position, float nearDist, float farDist,
                    int random, int count, int(*chooser)(int, int));
    int MoveSequence(std::uint32_t id, const float* position);
    int UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count);
    int RemoveSequence(std::uint32_t id);
    void Tick();
    void FadeOutStream();
    void FadeInStream();
    int RequestStream(std::string_view name, int mode);
    void StopStream(int mode);

    // Handles restart at 0; with `stopStream` the stream stops too.
    void StopAll(bool stopStream);
    // In seconds: the IMA ADPCM sample count, else data bytes * 8 / (bits * channels), divided by the rate. 0
    // when bits * channels is 0. `this` is unused.
    double WaveLength(const void* data);

    // Reset, then 1.
    int CreateBody();
    void ClearWaveSources();
    // 1 (also when sound is off), 0 when the archive cannot be opened.
    int AddWaveSource(const std::string& path);
    // The first wave source holding `name`; `source` may be null. Null when none has it.
    void* LoadWaveFile(const char* name, std::uint32_t* size, CMappedResource** source);
    // Rebirth-patched. Source by source.
    void ListWaveFiles(std::vector<std::string>* out, const char* pattern);
    // 1 when sound is off; else whether the registry opened.
    int SetPath(std::string_view folder, const std::string& registryPath);

    int FlatVolume(int level, int heightFade, int speech) const;
    void ApplyVolumePan(SoundSlot* slot) const;
    void EndSlot(SoundSlot* slot);
    SoundSlot* SlotForHandle(int handle) const;

    // Empty unless the stream plays or switches.
    std::string CurrentStreamName();

    // Switch mode 4, the music fader at volume 100 with no fade.
    void ResetStream();
    // 16-bit stereo at 44100 Hz. 0 when the codec or a sample cannot be had.
    int OpenStream(miles::DigitalDriver* driver);
    // OpenStream's failure. Returns 0.
    int CloseStream();
    // Every 100 ms until Close sets threadsQuit: a fader step, and every half Miles has played out gets the
    // spare buffer. The thread ends when it returns.
    void StreamThread();
    // Rebirth-patched. Fills the spare buffer: silence when idle; else the playing file up to its loop point
    // or the switch point, and the rest from the next file. No next file (or it does not open): silence, the
    // stream idle. Details: docs/notes/sound.md, "Header comments moved from sound.h".
    void ReadStreamVanilla();

    // The first free voice; else the lowest priority one (the first of equals) when `priority` is above it (a
    // looping sound there is deferred). -1 when no voice can be had.
    int AllocateVoice(int priority);

    // The DirectSound device `deviceIndex` (DirectSoundEnumerateA's order; the default device when out of
    // range or negative; the wave mapper when the sound option is 2), 16-bit stereo at `rate` (at most
    // 44100). 0 when the driver cannot be opened.
    int Open(void* window, int useReverb, int deviceIndex, unsigned rate);
    // Waits for the threads to quit and frees everything Open made, then Reset.
    void Close();
    // Volumes 100, sound off.
    void Reset();
    void DeleteDeferred();

    void StreamNotify();                                                          // empty
    int Play3D(std::string_view name, int priority, const float* position, float nearDist, float farDist, int loop,
               int speech);
    int Play2D(std::string_view name, int priority, int volume, int pan, int loop, int heightFade, int speech,
               int compressed);
    // From the cache, or read from the archives (with `compressed` an MP3 decompressed through the ASI codec);
    // a new handle. 0 when the wave is nowhere.
    int LoadSample(SoundSlot* slot, std::string_view name, int compressed);
};

// A music theme from music.reg: one per allod, open land or dungeon.
struct MusicTheme {
    std::string name;          // allod
    std::int32_t dungeon = 0;   // 0 open land, 1 dungeon
    std::string briefing;
    std::string constructor;
    std::vector<std::string> calm;        // CalmOpen / CalmDungeon
    std::vector<std::string> combat;      // Combat
};

// Picks the music for the game state.
struct CMusicSystem : CEngineObject {
    CMusicSystem();
    ~CMusicSystem() override;

    int Create() override { return LoadThemes(); }
    void Destroy() override { ResetThemes(); }
    int RunTick() override { return UpdateMusic(); }
    const char* GetClassName() override;
    int GetClassId() override;

    std::vector<MusicTheme> themes;
    std::string credits;               // Common\Credits
    std::string mainMenu;              // Common\MainMenu
    std::int32_t state;                 // 0 off, 1 zone, 2 briefing, 3 constructor, 4 main menu, 5 credits
    std::int32_t theme;
    std::int32_t inCombat;
    std::int32_t holding;               // set by SetZoneMode, cleared by the world frame
    std::string combatTrack;
    std::string jingle;
    std::int32_t jinglePlaying;
    std::uint32_t calmDue;              // timeGetTime() for the next calm track (0: not chosen)

    // music.reg: Common\Credits and Common\MainMenu, then per other section an open land theme (CalmOpen)
    // and a dungeon theme (CalmDungeon). Returns 1, or FatalError's result when the file cannot be opened.
    int LoadThemes();
    void SetZoneMode(std::string_view allod, int dungeon);
    void Stop();
    void PlayBriefing(std::string_view allod);
    void PlayConstructor(std::string_view allod);
    void PlayMainMenu();
    void PlayJingle(std::string_view name);
    int UpdateMusic();

    // Credits / main menu / jingle / combat track "none".
    void ResetThemes();
    void FadeOut();
    void FadeIn();
};
// The ASI decoder's read callback on the stream thread: seeks to `offset` unless it is negative. Returns the
// byte count (0 when the read fails).
int __stdcall SoundStreamRead(CSoundManager* manager, char* buffer, int size, int offset);
// AIL_decompress_ASI's callback: at state 1 it asks for 16 bits, 44100 Hz, 2 channels. Returns 1.
using DecoderPropertyFn = int(__stdcall*)(const char* name, std::uint32_t value);
int __stdcall SoundDecompressCallback(int state, DecoderPropertyFn set);
// Miles calls it when a voice's sample ends: the voice is marked finished.
void __stdcall SoundEndOfSample(miles::Sample* sample);

