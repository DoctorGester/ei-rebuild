#pragma once

// Sound and music: the sound manager on our audio output (audio/audio.h), its MP3 music stream, and the music state
// machine. See docs/notes/sound.md.

#include "game/core/engine_object.h"
#include "game/data/registry.h"
#include "game/data/resource.h"

#include "lib/containers.h"
#include "lib/hash_map.h"
#include "audio/audio.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
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
    std::int32_t volume = 0;            // 0..100
    std::int32_t level = 0;             // 0..127, round(volume * factor * 127 * 0.01)
    float factor = 1.0f;                // fade factor 0..1; a fade moves it by 1/96 per update (a stream step, 100 ms)
    FadeMode mode = FadeMode::None;
    bool finished = false;              // set when a fade out reached 0
    std::uint32_t holdUntil = 0;        // platform::FrameClock() at which the fade is cancelled (0: none)

    void SetHold(std::uint32_t until);
    void Update(std::uint32_t now);     // a stream step's (StepStream)
    void Recalc();
};

// What a sound plays and how: its voice's (SoundSlot) while it plays, its deferred record's while it waits for one.
struct SoundParams {
    std::string name;                 // the wave name
    std::int32_t volume = 0;          // the output's volume 0..127
    std::int32_t baseVolume = 0;      // 2D sounds: volume 0..100
    std::int32_t pan = 0;             // -100..100
    std::int32_t priority = 0;
    bool speech = false;              // scaled by the speech volume instead of effects
    bool flat = false;                // 2D: volume from baseVolume, not from the listener distance
    bool heightFade = false;          // 2D sounds fading out as the camera rises (30..70)
    Vector3 position{};
    float nearDistance = 0.0f;        // full volume inside
    float farDistance = 0.0f;         // silent outside
};

// One of the kVoiceCount voices.
struct SoundSlot {
    SoundParams params;
    audio::Voice* voice = nullptr;
    bool playing = false;
    bool looping = false;             // kept as a deferred record when the voice is stolen
    std::int32_t handle = kNoSound;   // sound handle (kNoSound: free)
};

// A looping sound with no voice (none was free, or a higher priority one took it), waiting to be restarted.
struct DeferredSound {
    SoundParams params;
    std::int32_t samplePosition = 0;
};

// An ambient emitter (kEmitterCount): plays one of its names at random intervals while the listener is within
// `outerRadius`.
struct SoundEmitter {
    bool active = false;
    std::vector<std::string> names;
    Vector3 position{};
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;
    std::int32_t minInterval = -1;   // ms
    std::int32_t maxInterval = -1;   // ms
    std::uint32_t lastPlayed = 0;    // platform::CoarseClock()
};

// The bit of a sequence chooser's result that loops the sound (bit 30).
inline constexpr int kSequenceLoop = 0x40000000;

// A 3D sound sequence (kSequenceCount): plays its names one after the other (or at random, or as a callback chooses)
// at one position.
struct SoundSequence {
    bool active = false;
    std::vector<std::string> names;
    std::int32_t handle = kNoSound;   // current sound
    Vector3 position{};
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    std::int32_t remaining = -1;      // sounds left to play (callback: sounds played)
    std::int32_t index = -1;          // next name
    std::int32_t priority = -1;
    bool random = false;              // pick names at random
    // Returns the next index (with kSequenceLoop: loop it), < 0 ends; called with the previous index and the
    // number of sounds started before.
    int (*chooser)(int previous, int played) = nullptr;
    std::int32_t generation = 0;
    std::int32_t id = -1;             // generation << 8 | slot (-1: free)

    // Starts the next sound; false when the sequence is over or the sound did not start.
    bool PlayNext(struct SoundManager* manager);
};

// A sequence's chooser (SoundSequence::chooser).
using SequenceChooser = int (*)(int previous, int played);
// The chooser of a sequence of an optional first name and `count` names after it: the first plays once, then the
// others go on, one name looped, two to five picked at random by the client effects' generator (g_clientRandom) as
// each ends; more than five loop the first name.
SequenceChooser ChooserFor(int count, bool withFirst);

// How Play2D plays a sound.
struct Play2DOptions {
    bool loop = false;          // endlessly
    bool heightFade = false;    // fades out as the camera rises
    bool speech = false;        // scaled by the speech volume instead of effects
    bool compressed = false;    // an MP3, decoded whole
};

// By handle, walked in STLport's order (the first of the highest priority plays).
using DeferredMap = lib::HashMap<std::int32_t, DeferredSound>;

// The wave cache: a list of loaded waves, most recently used first, and a hash_map from the wave name to its
// list node.
struct WaveRecord {
    std::vector<std::uint8_t> data;   // the wave file (a decoded MP3's as a 16-bit PCM wave)
    std::string name;
    std::uint32_t lastUsed = 0; // platform::CoarseClock()
    std::int32_t size = 0;      // bytes
    std::int32_t uses = 0;      // voices playing it
};

constexpr int kFullVolume = 100;   // the volumes' top

struct SoundManager : EngineObject {
    SoundManager();
    ~SoundManager() override;

    void Create() override;   // Reset
    void Destroy() override { Close(); }
    const char* ClassName() override;
    ClassId GetClassId() override;

    std::string soundPath;             // ending in a backslash
    FileRegistry soundRegistry;
    std::vector<MappedResource*> waveSources;   // the archives LoadSample looks in
    // The MP3 music stream, stepped on the main thread (FollowClock).
    StreamState streamState = StreamState::Idle;   // Switching: to nextName
    audio::Stream* streamSample = nullptr;
    audio::Mp3Stream* streamFile = nullptr;   // the playing file's decoder
    // The next half's bytes (streamBufferSize of them): queued as the output frees a half, then filled again.
    std::vector<std::uint8_t> streamBuffer;
    std::int32_t streamBufferSize = -1;     // twice the output's minimum half; -1 after ResetStream
    std::int32_t streamBufferFrames = 0;    // frames in one buffer: streamBufferSize / streamFrameBytes; 0 before a file
    std::int32_t streamPosition = -1;       // frames of the playing file queued so far
    std::int32_t streamSwitchAt = -1;       // position at which nextName starts
    // The position at which the playing file starts again from its beginning (the first "Markers2" value of
    // its section in the sound registry; kNoPosition without one) and the bytes in a frame (channels * 2):
    // both -1 after ResetStream.
    std::int32_t streamLoopAt = -1;
    std::int32_t streamFrameBytes = -1;
    SoundFader musicFader;                  // music volume (options) and fades
    std::string streamName;                 // playing
    std::string nextName;                   // requested
    std::vector<std::int32_t> markers;      // switch points ("Markers1/2" in the file)
    std::vector<std::int32_t> markers2;
    StreamSwitch switchMode = StreamSwitch::Now;   // RequestStream's mode
    // PlayStreamOnce's file waits for the stream's next step, which asks for silence after it as it switches to it
    // (StopStream(AtEnd)); any other request clears it.
    bool endAfterSwitch = false;
    std::uint32_t streamSteppedAt = 0;      // the last stream step's time (FollowClock)
    std::unique_ptr<SoundSlot[]> slots;     // kVoiceCount voices while open
    Vector3 listener{};                     // listener position (z: camera height)
    Vector3 listenerAxis{};                 // listener direction, unit length in x/y
    std::int32_t nextHandle = 0;
    SoundEmitter emitters[kEmitterCount];
    std::uint32_t emitterTick = 0;
    SoundSequence sequences[kSequenceCount];
    std::int32_t cacheBytes = 0;            // bytes held by the wave cache
    // The cached waves, most recently used first (a list: `cache` keeps its nodes).
    std::list<WaveRecord> cacheList;
    std::unordered_map<std::string, std::list<WaveRecord>::iterator> cache;   // wave name -> its cache list node
    DeferredMap deferred;
    bool enabled = false;
    bool reverbOn = false;
    bool panDirect = false;                 // false: pans are mirrored
    float reverb[3] = {};                   // room type / level / reflect time
    std::int32_t effectsVolume = kFullVolume;  // 0..100
    std::int32_t speechVolume = kFullVolume;  // 0..100
    std::int32_t randomStart = 0;           // ms

    void SetReverb(float room, float level, float time);
    void PauseAll();
    void ResumeAll();
    void SetEffectsVolume(int volume);
    void SetSpeechVolume(int volume);
    void SetMusicVolume(int volume);
    SoundSlot* FindPlaying(int handle);
    DeferredSound* FindDeferred(int handle);
    // Whether `handle` plays or waits for a voice (a voice whose sample ended is freed first, as FindPlaying does).
    bool IsPlaying(int handle);
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
    // stream's step makes the second request when it switches to the file.
    void PlayStreamOnce(std::string_view name);
    // A PlayStreamOnce file the stream has not switched to yet.
    bool StreamSwitchPending() const { return endAfterSwitch; }

    // Handles restart at 0; with `stopStream` the stream stops too.
    void StopAll(bool stopStream);
    void ClearWaveSources();
    // True (also when sound is off); false when the archive cannot be opened.
    bool AddWaveSource(const std::string& path);
    // The first wave source holding `name`, with its size and the source to close it with; null when none has it.
    void* LoadWaveFile(std::string_view name, std::uint32_t* size, MappedResource** source);
    // Whether a wave source holds `name` (read and closed again).
    bool HasWaveFile(std::string_view name);
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
    std::string CurrentStreamName() const;
    // The music fader starts a fade (none held).
    void StartFade(FadeMode mode);

    // Switch mode Now, the music fader at volume 100 with no fade.
    void ResetStream();
    // 16-bit stereo at 44100 Hz. False when the output has no stream.
    bool OpenStream();
    // Frees what OpenStream made.
    void CloseStream();
    // The stream's steps due by `now` (the frame's time), each at its time 100 ms after the last: from the main loop
    // and from the passes that hold it longer than a half plays (loads, the system's modal loops; app::StepMusic).
    void FollowClock(std::uint32_t now);
    // A step at `now`: a fader step, and every half the output has played out by then gets the spare buffer.
    void StepStream(std::uint32_t now);
    // Fills the next half's buffer: silence when idle; else the playing file up to its loop point or the switch point,
    // and the rest from the next file. No next file (or it does not open): silence, the stream idle. Details:
    // docs/notes/sound.md, "Game side".
    void FillSpareBuffer(std::uint32_t now);

    // The first free voice; else the lowest priority one (the first of equals) when `priority` is above it (a
    // looping sound there is deferred). -1 when no voice can be had.
    int AllocateVoice(int priority);

    // The output at `rate` (at most 44100), the voices in the cave's reverb when `useReverb`.
    void Open(bool useReverb, unsigned rate);
    // Frees everything Open made, then Reset.
    void Close();
    // Volumes 100, sound off.
    void Reset();

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
    // and a dungeon theme (CalmDungeon). A file that cannot be opened ends the game.
    void Create() override;
    void Destroy() override { ResetThemes(); }
    bool RunTick() override {
        UpdateMusic();
        return true;
    }
    const char* ClassName() override;
    ClassId GetClassId() override;

    std::vector<MusicTheme> themes;
    std::string credits;                // Common\Credits
    std::string mainMenu;               // Common\MainMenu
    MusicState state = MusicState::Off;
    std::int32_t theme = 0;
    bool inCombat = false;
    bool holding = false;               // set by SetZoneMode, cleared by the world frame
    std::string combatTrack;
    std::string jingle;
    bool jinglePlaying = false;
    std::uint32_t calmDue = 0;          // platform::FrameClock() for the next calm track (0: not chosen)

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

