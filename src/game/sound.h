#pragma once

// Sound and music (game.exe 006B4100-006BDxxx): the sound manager (CSoundManager, one instance at
// *007AF784) built on the Miles Sound System (mss32.dll), its MP3 music stream and MIDI threads,
// and the music state machine (CMusicSystem, world/app object + 0x1A8). Names are provisional.
// See docs/notes/sound.md for the map of the range.

#include "../lib/mfc.h"

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
    float position[3];          // +0x3C 3D position
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
    float position[3];          // +0x2C
    float nearDistance;         // +0x38
    float farDistance;          // +0x3C
    std::int32_t handle;        // +0x40 (the map key)
    std::int32_t samplePosition;// +0x44
};
static_assert(sizeof(DeferredSound) == 0x48);

// MFC CStringArray (0x14 bytes), as embedded in the sound records.
struct SoundNameArray {
    void* vtable;
    mfc::CString* data;
    std::int32_t size;
    std::int32_t maxSize;
    std::int32_t growBy;
};
static_assert(sizeof(SoundNameArray) == 0x14);

// An ambient emitter (16 at CSoundManager + 0x26C, 0x38 bytes): plays one of its names at random
// intervals while the listener is within `outerRadius` (006B9B10).
struct SoundEmitter {
    std::int32_t active;        // +0x00
    SoundNameArray names;       // +0x04
    float position[3];          // +0x18
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
    float position[3];          // +0x1C
    float nearDistance;         // +0x28
    float farDistance;          // +0x2C
    std::int32_t remaining;     // +0x30 sounds left to play (callback: sounds played)
    std::int32_t index;         // +0x34 next name
    std::int32_t priority;      // +0x38
    std::int32_t random;        // +0x3C pick names at random
    int(__fastcall* chooser)(int previous);  // +0x40 returns the next index (bit 30: loop it), < 0 ends
    std::int32_t generation;    // +0x44
    std::int32_t id;            // +0x48 generation << 8 | slot (-1: free)

    int PlayNext(struct CSoundManager* manager);  // 006BA140
};
static_assert(sizeof(SoundSequence) == 0x4C);

// The STLport hash_map<int, DeferredSound*> at CSoundManager + 0x884.
struct DeferredNode {
    DeferredNode* next;
    std::int32_t key;
    DeferredSound* value;
};
struct DeferredMap {
    std::uint32_t unknown0;
    DeferredNode** buckets;     // +0x04 (CSoundManager + 0x888)
    DeferredNode** bucketsEnd;  // +0x08
    std::uint32_t unknownC;
    std::int32_t count;         // +0x10 (CSoundManager + 0x894)

    DeferredNode* Find(std::int32_t key) const;  // the inlined bucket walk (006BC440)
};
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
struct WaveNode {
    WaveNode* next;
    WaveNode* prev;
    WaveRecord record;          // +0x08
};
static_assert(sizeof(WaveNode) == 0x24);
struct WaveList {
    std::uint32_t unknown0;
    WaveNode* head;             // +0x04 (CSoundManager + 0x858)
    WaveNode* tail;             // +0x08 (CSoundManager + 0x85C)
};
// An archive the waves are read from: vtable slot 5 loads a file [name, size out] (null when
// missing), slot 6 frees what slot 5 returned.
struct WaveSourceNode {
    WaveSourceNode* next;
    std::uint32_t unknown4;
    void* source;               // +0x08
};

// CSoundManager (0x8C0 bytes, vtable 007440F0, class id 0xE). Only the fields used by the
// rewritten code are named.
struct CSoundManager {
    std::uint8_t base[0x1C];            // +0x000 CEngineObject
    void* waveDriver;                   // +0x01C HDIGDRIVER
    std::uint8_t unknown020[0xC8];
    struct WaveSourceNode* waveSources; // +0x0E8 list of the archives LoadSample looks in
    std::uint8_t unknown0EC[0x14];
    // The MP3 music stream (+0x100, filled by 006BB3A0 on the stream thread 006BB910).
    volatile std::int32_t streamState;  // +0x100 0 idle, 1 playing, 2 switching to nextName
    void* streamSample;                 // +0x104 HSAMPLE
    void* streamFile;                   // +0x108 ASI stream
    void** streamCodec;                 // +0x10C ASI codec functions (slot 3: close)
    std::uint8_t unknown110[0x10];
    std::int32_t streamBufferBytes;     // +0x120
    std::uint8_t unknown124[0xC];
    std::int32_t streamPosition;        // +0x130 bytes queued so far
    std::int32_t streamSwitchAt;        // +0x134 position at which nextName starts
    std::uint8_t unknown138[0xC];
    SoundFader musicFader;              // +0x144 music volume (options) and fades
    mfc::CString streamName;            // +0x160 playing
    mfc::CString nextName;              // +0x164 requested
    std::int32_t* markers;              // +0x168 switch points (CArray<int>; "Markers1/2" in the file)
    std::int32_t markerCount;           // +0x16C
    std::uint8_t unknown170[0x18];
    std::uint8_t nameLock[0x18];        // +0x188 CRITICAL_SECTION guarding streamName
    std::int32_t switchMode;            // +0x1A0 how nextName was requested (RequestStream mode)
    std::uint8_t unknown1A4[0xC];
    std::uint8_t streamLock[0x18];      // +0x1B0 CRITICAL_SECTION guarding the stream and musicFader
    std::uint8_t unknown1C8[0x84];
    SoundSlot* slots;                   // +0x24C 15 voices
    float listener[3];                  // +0x250 listener position (z: camera height)
    float listenerAxis[3];              // +0x25C listener direction, unit length in x/y
    std::int32_t nextHandle;            // +0x268
    SoundEmitter emitters[16];          // +0x26C
    std::uint32_t emitterTick;          // +0x5EC
    SoundSequence sequences[8];         // +0x5F0
    std::int32_t cacheBytes;            // +0x850 bytes held by the wave cache
    WaveList cacheList;                 // +0x854 the cached waves, most recently used first
    std::uint8_t unknown860[0x10];
    std::uint8_t cache[0x14];           // +0x870 hash_map<string, cached wave> (006BBDF0)
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
                    int random, int count, int(__fastcall* chooser)(int));                // 006BA2D0
    int MoveSequence(std::uint32_t id, const float* position);                           // 006BA400
    int UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count);  // 006BA5E0
    int RemoveSequence(std::uint32_t id);                                                 // 006BA7C0
    void Tick();                                                                          // 006B9B10
    void FadeOutStream();                                                                 // 006BAFA0
    void FadeInStream();                                                                  // 006BAFE0
    int RequestStream(const char* name, int mode);                                       // 006BB020
    void StopStream(int mode);                                                            // 006BB240

    // Shared pieces of the original's inlined code.
    int FlatVolume(int level, int heightFade, int speech) const;
    void ApplyVolumePan(SoundSlot* slot) const;
    void EndSlot(SoundSlot* slot);
    SoundSlot* SlotForHandle(int handle) const;

    // Not rewritten.
    mfc::CString CurrentStreamName();                                                     // 006BAEA0
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
static_assert(offsetof(CSoundManager, markerCount) == 0x16C);
static_assert(offsetof(CSoundManager, switchMode) == 0x1A0);
static_assert(offsetof(CSoundManager, streamLock) == 0x1B0);
static_assert(offsetof(CSoundManager, slots) == 0x24C);
static_assert(offsetof(CSoundManager, emitters) == 0x26C);
static_assert(offsetof(CSoundManager, sequences) == 0x5F0);
static_assert(offsetof(CSoundManager, cache) == 0x870);
static_assert(offsetof(CSoundManager, waveSources) == 0xE8);
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
struct CMusicSystem {
    std::uint8_t base[0x1C];            // +0x00 CEngineObject
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

    void SetZoneMode(const char* allod, int dungeon);  // 006B4E30
    void Stop();                                       // 006B4F70
    void PlayBriefing(const char* allod);              // 006B4F90
    void PlayConstructor(const char* allod);           // 006B5080
    void PlayCredits();                                // 006B5170
    void PlayMainMenu();                               // 006B51F0
    void PlayJingle(const char* name);                 // 006B5270
    int UpdateMusic();                                // 006B5350 (vtable slot 6)
};
static_assert(offsetof(CMusicSystem, credits) == 0x30);
static_assert(offsetof(CMusicSystem, holding) == 0x44);
static_assert(offsetof(CMusicSystem, calmDue) == 0x54);
static_assert(sizeof(CMusicSystem) == 0x58);

namespace sound {
inline CSoundManager* Manager() { return *reinterpret_cast<CSoundManager* const*>(0x007AF784); }
}
