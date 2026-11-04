#pragma once

// Sound and music: the sound manager on the platform's audio output (platform/platform.h), its MP3 music stream thread,
// and the music state machine. See docs/notes/sound.md.

#include "engine_object.h"
#include "registry.h"
#include "resource_stream.h"

#include "lib/containers.h"
#include "lib/hash_map.h"
#include "audio/audio.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// A sound handle that names no sound (Play2D / Play3D's failure, a free voice; handles count up from 0).
inline constexpr int kNoSound = -1;

// A stream position no file reaches: no loop point, a switch at the file's end.
inline constexpr int kNoPosition = 0x7FFFFFFF;

inline constexpr int kVoiceCount = 15;
inline constexpr int kEmitterCount = 16;
inline constexpr int kSequenceCount = 8;

// The music stream's fade.
enum class FadeMode { None, In, Out };

// What the music stream does (SoundManager::streamState).
enum class StreamState { Idle, Playing, Switching };

// When RequestStream's file takes over (StopStream's modes: Now stops at once, the others ask for silence).
enum class StreamSwitch {
    AtEnd = 1,      // after the current file
    AfterFade,      // now, with a fade out
    AtMarker,       // at the next marker of the current file
    Now,            // at the current buffer position
};

// The music system's state (MusicSystem::state).
enum class MusicState { Off, Zone, Briefing, Constructor, MainMenu, Credits };

// The music stream's volume envelope.
struct SoundFader {
    std::int32_t volume;        // 0..100
    std::int32_t level;         // 0..127, round(volume * factor * 127 * 0.01)
    float factor;               // fade factor 0..1; a fade moves it by 1/96 per update (a stream step, 100 ms)
    FadeMode mode;
    bool finished;              // set when a fade out reached 0
    std::uint32_t holdUntil;    // platform::Milliseconds() at which the fade is cancelled (0: none)

    void SetHold(std::uint32_t until);
    void Update(std::uint32_t now);     // a stream step's (StepStream)
    void Recalc();
};

// One of the kVoiceCount voices.
struct SoundSlot {
    std::string name;           // the wave name
    audio::Voice* voice;
    std::int32_t volume;        // the output's volume 0..127
    std::int32_t baseVolume;    // 2D sounds: volume 0..100
    std::int32_t pan;           // -100..100
    std::int32_t priority;
    bool playing;
    bool looping;               // kept as a deferred record when the voice is stolen
    bool speech;                // scaled by the speech volume instead of effects
    bool finished;              // set by the voice's end callback (SoundEndOfSample)
    bool flat;                  // 2D: volume from baseVolume, not from the listener distance
    bool heightFade;            // 2D sounds fading out as the camera rises (30..70)
    Vector3 position;
    float nearDistance;         // full volume inside
    float farDistance;          // silent outside
    std::int32_t handle;        // sound handle (kNoSound: free)
};

// A looping sound that lost its voice to a higher priority one and waits to be restarted.
struct DeferredSound {
    std::string name;
    std::int32_t volume = 0;
    std::int32_t baseVolume = 0;
    std::int32_t pan = 0;
    std::int32_t priority = 0;
    bool speech = false;
    bool flat = false;
    bool heightFade = false;
    Vector3 position{};
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    std::int32_t handle = kNoSound;   // the map key
    std::int32_t samplePosition = 0;
};

// An ambient emitter (kEmitterCount): plays one of its names at random intervals while the listener is within
// `outerRadius`.
struct SoundEmitter {
    bool active;
    std::vector<std::string> names;
    Vector3 position;
    float innerRadius;
    float outerRadius;
    std::int32_t minInterval;   // ms
    std::int32_t maxInterval;   // ms
    std::uint32_t lastPlayed;   // platform::TickCount()
};

// The bit of a sequence chooser's result that loops the sound (bit 30).
inline constexpr int kSequenceLoop = 0x40000000;

// A 3D sound sequence (kSequenceCount): plays its names one after the other (or at random, or as a callback chooses)
// at one position.
struct SoundSequence {
    bool active;
    std::vector<std::string> names;
    std::int32_t handle;        // current sound
    Vector3 position;
    float nearDistance;
    float farDistance;
    std::int32_t remaining;     // sounds left to play (callback: sounds played)
    std::int32_t index;         // next name
    std::int32_t priority;
    bool random;                // pick names at random
    // Returns the next index (with kSequenceLoop: loop it), < 0 ends; called with the previous index and the
    // number of sounds started before.
    int(*chooser)(int previous, int played);
    std::int32_t generation = 0;
    std::int32_t id;            // generation << 8 | slot (-1: free)

    // Starts the next sound; false when the sequence is over or the sound did not start.
    bool PlayNext(struct SoundManager* manager);
};

// A music request of the game thread (SoundManager::RequestStream, StopStream, PlayStreamOnce, FadeOutStream,
// FadeInStream, SetMusicVolume): made at once, or kept while the stream thread fills a buffer (lateRequests).
struct StreamRequest {
    enum class Kind { Switch, Stop, PlayOnce, FadeOut, FadeIn, Volume };
    Kind kind;
    StreamSwitch mode = StreamSwitch::Now;   // Switch, Stop
    std::int32_t volume = 0;                 // Volume
    std::string name;                        // Switch, PlayOnce
};

// How Play2D plays a sound.
struct Play2DOptions {
    bool loop = false;          // endlessly
    bool heightFade = false;    // fades out as the camera rises
    bool speech = false;        // scaled by the speech volume instead of effects
    bool compressed = false;    // an MP3, decoded whole
};

// Walked in STLport's order (the first of the highest priority plays).
using DeferredMap = lib::HashMap<std::int32_t, DeferredSound*>;

// The wave cache: a list of loaded waves, most recently used first, and a hash_map from the wave name to its
// list node.
struct WaveRecord {
    std::vector<std::uint8_t> data;   // the wave file (a decoded MP3's as a 16-bit PCM wave)
    std::string name;
    std::uint32_t lastUsed = 0; // GetTickCount()
    std::int32_t size = 0;      // bytes
    std::int32_t uses = 0;      // voices playing it
};

struct SoundManager : EngineObject {
    SoundManager();
    ~SoundManager() override;

    void Create() override;   // Reset
    void Destroy() override { Close(); }
    const char* ClassName() override;
    ClassId GetClassId() override;

    ResourceStream streamReader;       // reads the music stream's files
    std::string soundPath;             // ending in a backslash
    FileRegistry soundRegistry;
    std::vector<MappedResource*> waveSources;   // the archives LoadSample looks in
    // The MP3 music stream, filled on the stream thread.
    std::atomic<StreamState> streamState;   // Switching: to nextName
    audio::Stream* streamSample;
    audio::Mp3Stream* streamFile;   // the playing file's decoder
    std::vector<std::uint8_t> streamBuffers[3];   // streamBufferSize bytes each
    std::int32_t streamBufferSize;      // twice the output's minimum half; -1 after ResetStream
    std::int32_t streamBufferFrames;    // frames in one buffer: streamBufferSize / streamFrameBytes; 0 before a file
    // Which buffer each half of the stream plays (the first two) and the third one (taken next, and what the stream
    // reader fills): -1 after ResetStream; 0, 1 and 2 after OpenStream.
    std::int32_t streamHalfBuffer[2];
    std::int32_t streamSpareBuffer;
    std::int32_t streamPosition;        // frames of the playing file queued so far
    std::int32_t streamSwitchAt;        // position at which nextName starts
    // The position at which the playing file starts again from its beginning (the first "Markers2" value of
    // its section in the sound registry; kNoPosition without one) and the bytes in a frame (bits * channels / 8):
    // both -1 after ResetStream.
    std::int32_t streamLoopAt;
    std::int32_t streamFrameBytes;
    SoundFader musicFader;              // music volume (options) and fades
    std::string streamName;            // playing
    std::string nextName;              // requested
    std::vector<std::int32_t> markers;    // switch points ("Markers1/2" in the file)
    std::vector<std::int32_t> markers2;
    std::recursive_mutex nameLock;      // guarding streamName
    StreamSwitch switchMode;            // RequestStream's mode
    std::uint32_t streamFileSize;
    std::int32_t streamReadPosition;
    // Guards the stream's state and musicFader, except while the stream thread fills the spare buffer (streamBusy):
    // it then owns them without the lock (decoding, opening the next file), and the game thread's requests wait in
    // lateRequests, made in order as it hands them back, so the game thread never waits for MP3 work.
    std::mutex streamLock;
    bool streamBusy;
    std::vector<StreamRequest> lateRequests;
    std::atomic<bool> onceQueued;       // a PlayStreamOnce among lateRequests
    // PlayStreamOnce's file waits for the stream thread, which asks for silence after it as it switches to it
    // (StopStream(1)); any other request clears it.
    std::atomic<bool> endAfterSwitch;
    std::thread streamThread;           // started when the MP3 stream opened with a device, joined by Close
    // Without a device the main loop steps the stream (FollowClock); the last step's time.
    bool streamOnMainLoop = false;
    std::uint32_t streamSteppedAt = 0;
    std::atomic<bool> threadsQuit;      // set by Close; the stream thread ends on it
    std::unique_ptr<SoundSlot[]> slots; // kVoiceCount voices while open
    Vector3 listener;                   // listener position (z: camera height)
    Vector3 listenerAxis;               // listener direction, unit length in x/y
    std::int32_t nextHandle;
    SoundEmitter emitters[kEmitterCount];
    std::uint32_t emitterTick;
    SoundSequence sequences[kSequenceCount];
    std::int32_t cacheBytes;            // bytes held by the wave cache
    // The cached waves, most recently used first (a list: `cache` keeps its nodes).
    std::list<WaveRecord> cacheList;
    std::unordered_map<std::string, std::list<WaveRecord>::iterator> cache;                    // wave name -> its cache list node
    DeferredMap deferred;
    bool enabled;
    bool reverbOn;
    bool panDirect;                     // false: pans are mirrored
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
    DeferredSound* FindDeferred(int handle);
    void UpdatePositional(SoundSlot* slot);
    // Make a sound 2D (volume 0..100) / 3D; false when `handle` plays nowhere.
    bool SetSoundVolume(int handle, int volume, int pan);
    bool SetSoundPosition(int handle, const Vector3& position, float nearDist, float farDist);
    // Stops a sound (or forgets its deferred record).
    void StopSound(int handle);
    void SetListener(const Vector3& position, const Vector3& axis);
    // The emitter index or -1.
    int AddEmitter(const std::vector<std::string>& names, const Vector3& position, float inner, float outer, int minInterval,
                   int maxInterval);
    void MoveEmitter(int index, const Vector3& position, float inner, float outer, int minInterval,
                     int maxInterval);
    // The name list is kept.
    void RemoveEmitter(int index);
    // The sequence id or -1.
    int AddSequence(const std::vector<std::string>& names, int priority, const Vector3& position, float nearDist, float farDist,
                    bool random, int count, int(*chooser)(int, int));
    bool MoveSequence(std::uint32_t id, const Vector3& position);
    bool UpdateSequence(std::uint32_t id, const Vector3& position, float nearDist, float farDist, int count);
    void RemoveSequence(std::uint32_t id);
    void Tick();
    void FadeOutStream();
    void FadeInStream();
    void RequestStream(std::string_view name, StreamSwitch mode);
    void StopStream(StreamSwitch mode);
    // `name` from the current position (RequestStream mode 4), then silence once it has played (StopStream(1)): the
    // stream thread makes the second request when it switches to the file, so the caller never waits for the device.
    void PlayStreamOnce(std::string_view name);
    // A PlayStreamOnce file the stream thread has not switched to yet.
    bool StreamSwitchPending() const { return endAfterSwitch || onceQueued; }

    // Handles restart at 0; with `stopStream` the stream stops too.
    void StopAll(bool stopStream);
    void ClearWaveSources();
    // True (also when sound is off); false when the archive cannot be opened.
    bool AddWaveSource(const std::string& path);
    // The first wave source holding `name`; `size` and `source` may be null. Null when none has it.
    void* LoadWaveFile(std::string_view name, std::uint32_t* size, MappedResource** source);
    // Source by source.
    std::vector<std::string> ListWaveFiles(const char* pattern);
    // True when sound is off; else whether the registry opened.
    bool SetPath(std::string_view folder, const std::string& registryPath);

    int FlatVolume(int level, bool heightFade, bool speech) const;
    void ApplyVolumePan(SoundSlot* slot) const;
    // Ends the voice if it plays: releases its cached wave, stops the voice and frees the slot.
    void EndSlot(SoundSlot* slot);
    SoundSlot* SlotForHandle(int handle) const;

    // Empty unless the stream plays or switches.
    std::string CurrentStreamName();

    // The music requests' bodies, on the stream's state: under streamLock, or on the stream thread while it is busy.
    void MakeRequest(const StreamRequest& request);
    void MakeSwitch(std::string_view name, StreamSwitch mode);
    void MakeStop(StreamSwitch mode);
    void MakePlayOnce(std::string_view name);
    void MakeFade(FadeMode mode);
    void MakeVolume(int volume);
    // `request` made now, or kept in lateRequests while the stream thread is busy.
    void Submit(StreamRequest request);

    // Switch mode Now, the music fader at volume 100 with no fade.
    void ResetStream();
    // 16-bit stereo at 44100 Hz. False when the output has no stream.
    bool OpenStream();
    // Frees what OpenStream made.
    void CloseStream();
    // With a device: a step every 100 ms of real time until Close sets threadsQuit. The thread ends when it returns.
    void StreamThread();
    // Without a device (no stream thread), from the main loop with the frame's time: the steps due by `now`, each at
    // its time 100 ms after the last, as the thread would have made them.
    void FollowClock(std::uint32_t now);
    // A step at `now`: a fader step, and every half the output has played out by then gets the spare buffer.
    void StepStream(std::uint32_t now);
    // Fills the spare buffer: silence when idle; else the playing file up to its loop point or the switch point,
    // and the rest from the next file. No next file (or it does not open): silence, the stream idle. Details:
    // docs/notes/sound.md, "Game side".
    void FillSpareBuffer(std::uint32_t now);

    // The first free voice; else the lowest priority one (the first of equals) when `priority` is above it (a
    // looping sound there is deferred). -1 when no voice can be had.
    int AllocateVoice(int priority);

    // The default output device, 16-bit stereo at `rate` (at most 44100), with the voices' reverb when `useReverb`.
    // False when the device cannot be opened.
    bool Open(void* window, bool useReverb, unsigned rate);
    // Waits for the threads to quit and frees everything Open made, then Reset.
    void Close();
    // Volumes 100, sound off.
    void Reset();
    void DeleteDeferred();

    int Play3D(std::string_view name, int priority, const Vector3& position, float nearDist, float farDist, bool loop);
    int Play2D(std::string_view name, int priority, int volume, int pan, const Play2DOptions& options = {});
    // From the cache, or read from the archives (with `compressed` an MP3, decoded whole); a new handle. False when
    // the wave is nowhere.
    bool LoadSample(SoundSlot* slot, std::string_view name, bool compressed);
};

// A music theme from music.reg: one per allod, open land or dungeon.
struct MusicTheme {
    std::string name;          // allod
    bool dungeon = false;       // else open land
    std::string briefing;
    std::string constructor;
    std::vector<std::string> calm;        // CalmOpen / CalmDungeon
    std::vector<std::string> combat;      // Combat
};

// Picks the music for the game state.
struct MusicSystem : EngineObject {
    MusicSystem();
    ~MusicSystem() override;

    // music.reg: Common\Credits and Common\MainMenu, then per other section an open land theme (CalmOpen)
    // and a dungeon theme (CalmDungeon). FatalError when the file cannot be opened.
    void Create() override;
    void Destroy() override { ResetThemes(); }
    bool RunTick() override {
        UpdateMusic();
        return true;
    }
    const char* ClassName() override;
    ClassId GetClassId() override;

    std::vector<MusicTheme> themes;
    std::string credits;               // Common\Credits
    std::string mainMenu;              // Common\MainMenu
    MusicState state;
    std::int32_t theme;
    bool inCombat;
    bool holding;                       // set by SetZoneMode, cleared by the world frame
    std::string combatTrack;
    std::string jingle;
    bool jinglePlaying;
    std::uint32_t calmDue;              // platform::Milliseconds() for the next calm track (0: not chosen)

    void SetZoneMode(std::string_view allod, bool dungeon);
    void Stop();
    void PlayBriefing(std::string_view allod);
    void PlayConstructor(std::string_view allod);
    void PlayMainMenu();
    void PlayJingle(std::string_view name);
    void UpdateMusic();

    // Credits / main menu / jingle / combat track "none".
    void ResetThemes();
};
// The MP3 decoder's read callback on the stream thread (`manager` the SoundManager): seeks to `offset` unless it is
// negative. Returns the byte count (0 when the read fails).
int SoundStreamRead(void* manager, char* buffer, int size, int offset);
// A voice's end callback (`slot` its SoundSlot): the voice is marked finished.
void SoundEndOfSample(void* slot);

