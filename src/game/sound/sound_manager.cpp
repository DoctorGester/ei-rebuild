// The sound manager (SoundManager): voice volumes and pans from the listener, the per-handle controls,
// ambient emitters, 3D sound sequences, the per-frame tick and the music stream requests, and Play2D /
// Play3D.
//
// Float maths, built for SSE2. Two results reach game state: a 3D sound's level (UpdatePositional: Play3D gives a
// silent one-shot no voice) and the ambient emitters' intervals (Tick: an emitter's Play2D takes a voice). With
// every voice taken, a speech's Play2D answers -1, which keeps a unit's talking flag (unit_client_effects). Both are
// basic operations on floats and small integers in the original's order, rounded to integers by lib::RoundEven
// (docs/notes/sound.md, "Floating point"). The pans, the axis and the fader only reach the output.
#include "engine/mods/mod_folders.h"
#include "platform/platform.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr float kHeightFull = 30.0f;     // 2D height-faded sounds are full below
constexpr float kHeightSilent = 70.0f;   // ... and silent above
constexpr float kPercentScale = 100.0f;
constexpr float kOutputScale = 0.0127f;   // 0..100 times 0..100 to the output's 0..127
constexpr float kLevelMax = 127.0f;
constexpr float kPercent = 0.01f;
constexpr float kEmitterMargin = 0.001f;
constexpr float kEmitterMaxScale = 255.0f;

constexpr int kMaxLevel = 0x7F;    // the output's volume top

// The step of every fade of the music (1/96 per stream step).
constexpr float kFadeStep = 1.0f / 96.0f;
static_assert(std::bit_cast<std::uint32_t>(kFadeStep) == 0x3C2AAAABu);

constexpr int kNoVoice = -1;

// A looping sound with no voice deferred under a new handle; the handle.
int AddDeferred(SoundManager* m, SoundParams params) {
    const int handle = m->nextHandle++;
    m->deferred[handle] = DeferredSound{std::move(params)};
    return handle;
}

// Heights between 30 and 70 fade 2D sounds flagged heightFade.
float HeightFactor(float h) {
    return 1.0f - (h - kHeightFull) / (kHeightSilent - kHeightFull);
}

// The voice loop of the volume setters and SetListener.
void RefreshVoices(SoundManager* m) {
    for (SoundSlot& slot : std::span(m->slots.get(), kVoiceCount)) {
        if (!slot.playing) continue;
        if (!slot.params.flat) m->UpdatePositional(&slot);
        else slot.params.volume = m->FlatVolume(slot.params.baseVolume, slot.params.heightFade, slot.params.speech);
        m->ApplyVolumePan(&slot);
    }
}

// The sequence `id` names (its slot's generation must match), or null.
SoundSequence* SequenceForId(SoundManager* m, std::uint32_t id) {
    if ((id & 0xFF) >= kSequenceCount) return nullptr;
    SoundSequence* s = &m->sequences[id & 0xFF];
    return static_cast<std::uint32_t>(s->id) == id ? s : nullptr;
}


// The common end of Play2D and Play3D once the wave is in the voice. Returns the voice's handle.
int StartLoaded(SoundManager* m, SoundSlot* slot, bool loop) {
    if (loop) audio::SetLoopCount(slot->voice, 0);
    m->ApplyVolumePan(slot);
    if (m->reverbOn) audio::SetReverb(slot->voice, m->reverb[0], m->reverb[1], m->reverb[2]);
    if (m->randomStart != 0) {
        std::uint32_t range = static_cast<std::uint32_t>(m->randomStart) + 1;
        audio::SetPositionMs(slot->voice, g_clientRandom.Next() % range);
    }
    audio::Start(slot->voice);
    return slot->handle;
}

constexpr std::uint32_t kCacheBudget = 0x800000;   // bytes kept unless a wave is still in use
constexpr std::uint32_t kCacheKeep = 60000;        // ms a wave stays cached after its last use

// A cached wave: moved to the front with one more use and the time of now, then given to the voice.
void UseCached(SoundManager* m, SoundSlot* slot, std::list<WaveRecord>::iterator node) {
    m->cacheList.splice(m->cacheList.begin(), m->cacheList, node);
    WaveRecord& record = *node;
    record.lastUsed = platform::CoarseClock();
    record.uses += 1;
    audio::SetWave(slot->voice, record.data.data(), record.data.size());
}

// Drops unused waves from the least recently used end until the new one fits the budget or the
// next one was used within the last minute.
void Evict(SoundManager* m, int incoming, std::uint32_t now) {
    std::list<WaveRecord>& list = m->cacheList;
    for (auto after = list.end(); after != list.begin();) {
        auto node = std::prev(after);
        const WaveRecord& record = *node;
        if (static_cast<std::uint32_t>(incoming + m->cacheBytes) < kCacheBudget && now - record.lastUsed < kCacheKeep) return;
        if (record.uses > 0) {
            after = node;
            continue;
        }
        m->cacheBytes -= record.size;
        m->cache.erase(record.name);
        list.erase(node);
    }
}

void CodecClose(SoundManager* m) {
    if (m->streamFile == nullptr) return;
    audio::CloseMp3(m->streamFile);
    m->streamFile = nullptr;
}

constexpr const char* kClassName = "CSoundManager";

constexpr unsigned kMaxRate = 44100;

constexpr int kStreamRate = 44100;
constexpr int kStreamChannels = 2;

// The stream's steps (FollowClock).
constexpr std::uint32_t kStreamPeriodMs = 100;
constexpr int kNoHalfReady = -1;

constexpr std::uint32_t kSwitchHoldMs = 4000;
constexpr int kNeverSwitch = -1;
constexpr int kCentrePan = 0x40;
const char* const kLoopMarkers = "Markers2";
const char* const kSwitchMarkers = "Markers1";

// 16-bit samples are signed: silence is zero bytes.
void Silence(std::uint8_t* to, int bytes) {
    std::memset(to, 0, static_cast<std::uint32_t>(bytes));
}
// `bytes` of the file; what it does not give is silence.
void ReadOrSilence(SoundManager* m, std::uint8_t* to, int bytes) {
    int got = audio::DecodeMp3(m->streamFile, to, bytes);
    if (got < bytes) Silence(to + got, bytes - got);
}

// The next file takes over: `bytes` of it (or silence) to `to`.
void SwitchStream(SoundManager* m, std::uint8_t* to, int bytes) {
    // The mod's Stream\ folder when it has the file (Rebirth's StreamPatch).
    const bool modStream = modfolders::StreamInMod(m->nextName);
    std::string path = (modStream ? modfolders::StreamFolder() : m->soundPath) + m->nextName;
    CodecClose(m);
    std::string file;
    if (!m->nextName.empty() && platform::ReadWholeFile(path, &file)) m->streamFile = audio::OpenMp3(std::move(file));
    if (m->streamFile == nullptr) {
        Silence(to, bytes);
        m->streamState = StreamState::Idle;
        m->endAfterSwitch = false;
        m->streamName.clear();
        return;
    }
    m->streamState = StreamState::Playing;
    m->streamName = m->nextName;
    m->soundRegistry.GetIntArray(m->streamName, kSwitchMarkers, &m->markers);
    m->soundRegistry.GetIntArray(m->streamName, kLoopMarkers, &m->markers2);
    m->streamLoopAt = !m->markers2.empty() ? m->markers2[0] : kNoPosition;
    m->streamSwitchAt = kNeverSwitch;
    const int channels = audio::Mp3Channels(m->streamFile);
    audio::SetStreamFormat(m->streamSample, audio::Mp3Rate(m->streamFile), channels);
    audio::SetStreamVolume(m->streamSample, kMaxLevel);
    audio::SetStreamPan(m->streamSample, kCentrePan);
    m->streamFrameBytes = channels * static_cast<std::int32_t>(sizeof(std::int16_t));
    m->streamBufferFrames = m->streamBufferSize / m->streamFrameBytes;
    m->streamPosition = 0;
    ReadOrSilence(m, to, bytes);
    if (!m->endAfterSwitch) return;
    m->endAfterSwitch = false;
    m->StopStream(StreamSwitch::AtEnd);
}

} // namespace

// --- Shared pieces ------------------------------------------------------------------------------

// The output's volume (0..127) of `level` (0..100, a 2D voice's base volume or a 3D voice's distance level), as every
// volume setter gives it. A NaN camera height keeps the full level.
int SoundManager::FlatVolume(int level, bool fade, bool isSpeech) const {
    float v = static_cast<float>(level);
    if (fade) {
        float h = listener.z;
        if (h >= kHeightSilent) v = 0.0f;
        else if (h > kHeightFull) v = v * HeightFactor(h);
    }
    v *= (isSpeech ? speechVolume : effectsVolume);
    v *= kOutputScale;
    return std::clamp(lib::RoundEven(v), 0, kMaxLevel);
}

// Hands the slot's volume and pan to the output; the pan is mirrored unless panDirect.
void SoundManager::ApplyVolumePan(SoundSlot* slot) const {
    audio::SetVolume(slot->voice, slot->params.volume);
    int p = slot->params.pan;
    if (!panDirect) p = -p;
    audio::SetPan(slot->voice, (p + 100) * 0x7F / 200);
}

void SoundManager::EndSlot(SoundSlot* slot) {
    if (!slot->playing) return;
    --cache[slot->params.name]->uses;
    audio::End(slot->voice);
    slot->playing = false;
    slot->handle = kNoSound;
}

// The first voice whose handle matches, or null (free voices hold kNoSound, which matches none).
SoundSlot* SoundManager::SlotForHandle(int handle) const {
    if (handle == kNoSound) return nullptr;
    for (int i = 0; i < kVoiceCount; ++i)
        if (slots[i].handle == handle) return &slots[i];
    return nullptr;
}

// --- Volumes, listener ----------------------------------------------------------------------------

// A 3D voice's volume and pan from the listener: full inside nearDistance, falling to 0 at farDistance (a
// distance level 0..100 the 2D voices' way, FlatVolume, with the camera height fade); the pan from the angle
// between the listener axis and the direction to the sound (0 ahead or behind, +-100 to the sides). The volume
// decides whether Play3D's one-shot plays (the file's head comment).
void SoundManager::UpdatePositional(SoundSlot* slot) {
    const Vector2 d = XY(slot->params.position) - XY(listener);
    float dist2 = LengthSquared2D(d);
    if (dist2 > slot->params.farDistance * slot->params.farDistance) {
        slot->params.volume = 0;
        return;
    }
    float dist = std::sqrt(dist2);
    int level = kFullVolume;
    if (!(dist < slot->params.nearDistance))
        level = kFullVolume - lib::RoundEven((dist - slot->params.nearDistance) / (slot->params.farDistance - slot->params.nearDistance) * kPercentScale);
    slot->params.volume = FlatVolume(level, true, slot->params.speech);

    float ahead = AbsBits(Dot2D(d, XY(listenerAxis)));
    if (ahead > dist || dist == 0.0f) {
        slot->params.pan = 0;
        return;
    }
    float angle = lib::Acos(ahead / dist);
    if (angle > kHalfPi) angle = kPi - angle;
    int pan = lib::RoundEven(kPercentScale / kHalfPi * angle);
    slot->params.pan = pan;
    if (d.y * listenerAxis.x > d.x * listenerAxis.y) slot->params.pan = -pan;
}

// Options: effects volume 0..100.
void SoundManager::SetEffectsVolume(int volume) {
    if (!enabled) return;
    effectsVolume = std::clamp(volume, 0, kFullVolume);
    RefreshVoices(this);
}

// Options: speech volume 0..100.
void SoundManager::SetSpeechVolume(int volume) {
    if (!enabled) return;
    speechVolume = std::clamp(volume, 0, kFullVolume);
    RefreshVoices(this);
}

// The camera's position and direction (Camera::Update every frame).
void SoundManager::SetListener(const Vector3& position, const Vector3& axis) {
    if (!enabled) return;
    listener = position;
    listenerAxis = axis;
    float len = Length2D(XY(listenerAxis));
    if (len == 0.0f) {
        listenerAxis = {1.0f, 0.0f, 0.0f};
    } else {
        float inv = 1.0f / len;
        listenerAxis *= inv;
    }
    RefreshVoices(this);
}

// Options: music volume 0..100 (0 stops the stream).
void SoundManager::SetMusicVolume(int volume) {
    if (!enabled) return;
    musicFader.volume = std::clamp(volume, 0, kFullVolume);
    musicFader.Recalc();
    if (streamSample != nullptr) audio::SetStreamVolume(streamSample, musicFader.level);
    if (musicFader.volume == 0) StopStream(StreamSwitch::Now);
}

// Reverb parameters for new voices (used when reverbOn).
void SoundManager::SetReverb(float room, float level, float time) {
    reverb[0] = room;
    reverb[1] = level;
    reverb[2] = time;
}

// The game window lost / regained focus.
void SoundManager::PauseAll() {
    if (slots != nullptr)
        for (SoundSlot& slot : std::span(slots.get(), kVoiceCount))
            if (slot.playing) audio::Stop(slot.voice);
    if (enabled && streamSample != nullptr) audio::StopStream(streamSample);
}

void SoundManager::ResumeAll() {
    if (slots != nullptr)
        for (SoundSlot& slot : std::span(slots.get(), kVoiceCount))
            if (slot.playing) audio::Resume(slot.voice);
    if (enabled && streamSample != nullptr) audio::ResumeStream(streamSample);
}

// --- Per-handle controls --------------------------------------------------------------------------

// The voice playing `handle`; a voice whose sample ended is freed and gives null.
SoundSlot* SoundManager::FindPlaying(int handle) {
    if (!enabled) return nullptr;
    SoundSlot* slot = SlotForHandle(handle);
    if (slot == nullptr) return nullptr;
    if (!audio::Ended(slot->voice)) return slot;
    EndSlot(slot);
    return nullptr;
}

// The deferred record of `handle`, or null.
DeferredSound* SoundManager::FindDeferred(int handle) {
    if (!enabled) return nullptr;
    auto node = deferred.find(handle);
    return node != deferred.end() ? &node->second : nullptr;
}

bool SoundManager::SetSoundVolume(int handle, int volume, int pan) {
    if (SoundSlot* slot = FindPlaying(handle)) {
        slot->params.baseVolume = volume;
        slot->params.volume = FlatVolume(volume, slot->params.heightFade, slot->params.speech);
        slot->params.pan = pan;
        slot->params.flat = true;
        ApplyVolumePan(slot);
        return true;
    }
    if (DeferredSound* rec = FindDeferred(handle)) {
        rec->params.baseVolume = volume;
        rec->params.pan = pan;
        rec->params.flat = true;
        return true;
    }
    return false;
}

bool SoundManager::SetSoundPosition(int handle, const Vector3& position, float nearDist, float farDist) {
    if (SoundSlot* slot = FindPlaying(handle)) {
        slot->params.position = position;
        slot->params.nearDistance = nearDist;
        slot->params.farDistance = farDist;
        slot->params.flat = false;
        UpdatePositional(slot);
        ApplyVolumePan(slot);
        return true;
    }
    if (DeferredSound* rec = FindDeferred(handle)) {
        rec->params.position = position;
        rec->params.flat = false;
        rec->params.nearDistance = nearDist;
        rec->params.farDistance = farDist;
        return true;
    }
    return false;
}

bool SoundManager::IsPlaying(int handle) { return FindPlaying(handle) != nullptr || FindDeferred(handle) != nullptr; }

void SoundManager::StopSound(int handle) {
    if (SoundSlot* slot = FindPlaying(handle)) EndSlot(slot);
    else if (enabled) deferred.erase(handle);
}

// --- Ambient emitters -----------------------------------------------------------------------------

void SoundManager::MoveEmitter(int index, const Vector3& position, float inner, float outer, int minInterval,
                                int maxInterval) {
    if (index < 0 || index >= kEmitterCount) return;
    SoundEmitter* e = &emitters[index];
    if (!e->active) return;
    e->position = position;
    e->innerRadius = inner;
    e->outerRadius = outer;
    e->minInterval = minInterval;
    e->maxInterval = maxInterval;
}

int SoundManager::AddEmitter(const std::vector<std::string>& names, const Vector3& position, float inner, float outer,
                              int minInterval, int maxInterval) {
    if (!enabled || names.empty()) return -1;
    int i = 0;
    while (i < kEmitterCount && emitters[i].active) ++i;
    if (i == kEmitterCount) return -1;
    SoundEmitter* e = &emitters[i];
    e->active = true;
    e->names = names;
    MoveEmitter(i, position, inner, outer, minInterval, maxInterval);
    e->lastPlayed = platform::CoarseClock();
    return i;
}

void SoundManager::RemoveEmitter(int index) {
    if (index < 0 || index >= kEmitterCount) return;
    if (emitters[index].active) emitters[index] = SoundEmitter{};
}

// --- Sound sequences ------------------------------------------------------------------------------

namespace {

std::uint32_t NextRandom() { return g_clientRandom.Next(); }

// The choosers ChooserFor gives, called with the previous index and the number of sounds started so far.
int LoopFirst(int, int) { return kSequenceLoop; }
int AnyOf2(int, int) { return static_cast<int>(NextRandom() & 1); }
int AnyOf3(int, int) { return static_cast<int>(NextRandom() % 3); }
int AnyOf4(int, int) { return static_cast<int>(NextRandom() & 3); }
int AnyOf5(int, int) { return static_cast<int>(NextRandom() % 5); }
int FirstThenLoop1(int, int played) { return played != 0 ? kSequenceLoop | 1 : 0; }
int FirstThenOf2(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 1) + 1 : 0; }
int FirstThenOf3(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 3) + 1 : 0; }
int FirstThenOf4(int, int played) { return played != 0 ? static_cast<int>(NextRandom() & 3) + 1 : 0; }
int FirstThenOf5(int, int played) { return played != 0 ? static_cast<int>(NextRandom() % 5) + 1 : 0; }

} // namespace

SequenceChooser ChooserFor(int count, bool withFirst) {
    switch (count) {
    case 1: return withFirst ? &FirstThenLoop1 : &LoopFirst;
    case 2: return withFirst ? &FirstThenOf2 : &AnyOf2;
    case 3: return withFirst ? &FirstThenOf3 : &AnyOf3;
    case 4: return withFirst ? &FirstThenOf4 : &AnyOf4;
    case 5: return withFirst ? &FirstThenOf5 : &AnyOf5;
    default: return &LoopFirst;
    }
}

bool SoundSequence::PlayNext(SoundManager* manager) {
    if (chooser != nullptr) {
        int played = remaining;
        ++remaining;
        int next = chooser(index, played);
        index = next;
        if (next < 0) return false;
        index = next & ~kSequenceLoop;
        handle = manager->Play3D(names[index], priority, position, nearDistance, farDistance, (next & kSequenceLoop) != 0);
        return handle != kNoSound;
    }
    if (remaining == 0) return false;
    if (random) index = static_cast<std::int32_t>(g_clientRandom.Next() % static_cast<std::uint32_t>(names.size()));
    handle = manager->Play3D(names[index], priority, position, nearDistance, farDistance, false);
    if (handle == kNoSound) return false;
    index = (index + 1) % static_cast<int>(names.size());
    if (remaining > 0) --remaining;
    return true;
}

// Returns the sequence id (generation << 8 | slot) or -1.
int SoundManager::AddSequence(const std::vector<std::string>& names, int priority, const Vector3& position, float nearDist,
                               float farDist, bool randomOrder, int count, int(*chooser)(int, int)) {
    if (!enabled || names.empty()) return -1;
    int i = 0;
    while (i < kSequenceCount && sequences[i].active) ++i;
    if (i == kSequenceCount) return -1;
    SoundSequence* s = &sequences[i];
    ++s->generation;
    s->id = s->generation << 8 | i;
    s->active = true;
    s->names = names;
    s->position = position;
    s->nearDistance = nearDist;
    s->farDistance = farDist;
    s->random = randomOrder;
    s->remaining = count;
    s->priority = priority;
    s->index = 0;
    s->chooser = chooser;
    if (chooser != nullptr) {
        s->remaining = 0;
        s->index = -1;
    }
    if (s->PlayNext(this)) return s->id;
    s->id = -1;
    s->active = false;
    return -1;
}

bool SoundManager::MoveSequence(std::uint32_t sequenceId, const Vector3& position) {
    if (!enabled) return false;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return false;
    s->position = position;
    SetSoundPosition(s->handle, position, s->nearDistance, s->farDistance);
    return true;
}

bool SoundManager::UpdateSequence(std::uint32_t sequenceId, const Vector3& position, float nearDist, float farDist, int count) {
    if (!enabled) return false;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return false;
    s->position = position;
    s->nearDistance = nearDist;
    s->farDistance = farDist;
    s->remaining = count;
    SetSoundPosition(s->handle, position, nearDist, farDist);
    return true;
}

void SoundManager::RemoveSequence(std::uint32_t sequenceId) {
    if (!enabled) return;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return;
    s->active = false;
    s->id = -1;
    StopSound(s->handle);
}

// --- The per-frame tick ---------------------------------------------------------------------------

// Every world frame: frees the voices whose sample ended, restarts the highest-priority deferred sounds in
// free voices, starts the next sound of every sequence whose sound is over, and every 100 ms (GetTickCount)
// lets the ambient emitters near the listener play a random name of theirs. Emitters and random sequences
// draw from the client effects' generator (g_clientRandom) on the wall clock's schedule.
void SoundManager::Tick() {
    if (!enabled) return;
    for (SoundSlot& slot : std::span(slots.get(), kVoiceCount))
        if (audio::Ended(slot.voice)) EndSlot(&slot);

    for (int i = 0; i < kVoiceCount; ++i) {
        if (deferred.empty()) break;
        if (slots[i].playing) continue;
        // The highest priority (the first one met on ties).
        auto best = deferred.begin();
        for (auto it = deferred.begin(); it != deferred.end(); ++it)
            if (it->second.params.priority > best->second.params.priority) best = it;
        const int handle = best->first;
        const DeferredSound rec = std::move(best->second);
        deferred.erase(best);

        SoundSlot* slot = &slots[i];
        slot->params = rec.params;
        slot->looping = true;
        audio::ResetVoice(slot->voice);
        // A flat sound's level is recomputed from its 0..100 base level.
        if (slot->params.flat) slot->params.volume = FlatVolume(slot->params.baseVolume, slot->params.heightFade, slot->params.speech);
        else UpdatePositional(slot);
        if (LoadSample(slot, rec.params.name, false)) {
            slot->handle = handle;
            audio::SetLoopCount(slot->voice, 0);
            ApplyVolumePan(slot);
            if (reverbOn) audio::SetReverb(slot->voice, reverb[0], reverb[1], reverb[2]);
            audio::SetPosition(slot->voice, rec.samplePosition);
            audio::Start(slot->voice);
        }
    }

    for (SoundSequence& s : sequences) {
        if (!s.active) continue;
        if (IsPlaying(s.handle)) continue;
        if (!s.PlayNext(this)) {
            s.id = -1;
            s.active = false;
        }
    }

    std::uint32_t now = platform::CoarseClock();
    if (now - emitterTick < 100) return;
    emitterTick = now;
    IsaacRandom& random = g_clientRandom;
    for (SoundEmitter& e : emitters) {
        if (!e.active) continue;
        float dist = std::sqrt(DistanceSquaredXY(listener, e.position));
        std::uint32_t minInterval, maxInterval;
        if (dist < e.innerRadius) {
            minInterval = static_cast<std::uint32_t>(e.minInterval);
            maxInterval = static_cast<std::uint32_t>(e.maxInterval);
        } else {
            // Out of earshot past outerRadius - 0.001.
            if (e.outerRadius - kEmitterMargin < dist) continue;
            float t = (e.outerRadius - e.innerRadius) / (e.outerRadius - dist);
            if (t > kEmitterMaxScale) continue;
            // The original's products while the intervals are below 2^24 ms (exact as floats).
            minInterval = static_cast<std::uint32_t>(lib::RoundEven(e.minInterval * t));
            maxInterval = static_cast<std::uint32_t>(lib::RoundEven(e.maxInterval * t));
        }
        std::uint32_t elapsed = now - e.lastPlayed;
        if (elapsed < minInterval) continue;
        if (elapsed < maxInterval) {
            std::uint32_t span = (maxInterval - minInterval) / 100;
            if (span > 1 && random.Next() % span != 0) continue;
        }
        e.lastPlayed = now;
        std::uint32_t index = 0;
        if (e.names.size() > 1) index = random.Next() % static_cast<std::uint32_t>(e.names.size());
        std::uint32_t panRoll = random.Next();
        std::uint32_t volumeRoll = random.Next();
        Play2D(e.names[index], 1, static_cast<int>(volumeRoll % 51 + 50), static_cast<int>(panRoll % 101));
    }
}

// --- Music stream ---------------------------------------------------------------------------------

// Starts a fade of the stream.
void SoundManager::FadeOutStream() {
    if (!enabled) return;
    StartFade(FadeMode::Out);
}

void SoundManager::FadeInStream() {
    if (!enabled) return;
    StartFade(FadeMode::In);
}

void SoundManager::StartFade(FadeMode mode) {
    musicFader.mode = mode;
    musicFader.finished = false;
    musicFader.holdUntil = 0;
}

// Asks the stream to switch to `name` (an .mp3 path); `mode` says when. Repeating the playing file only
// cancels a fade out. Nothing happens with the music off (volume 0), without a stream, or for a marker switch with no
// marker to take.
void SoundManager::RequestStream(std::string_view name, StreamSwitch mode) {
    if (!enabled || musicFader.volume == 0 || streamSample == nullptr) return;
    endAfterSwitch = false;
    StreamSwitch m = StreamSwitch::Now;
    if (streamState != StreamState::Idle) {
        m = mode;
        if (str::Compare(streamName, name) == 0 && !name.empty()) {
            streamState = StreamState::Playing;
            if (switchMode == StreamSwitch::AfterFade) StartFade(FadeMode::In);
            return;
        }
    }
    if (streamState == StreamState::Switching && str::Compare(nextName, name) == 0 && switchMode >= m) return;
    std::int32_t at = kNoPosition;   // AtEnd; AfterFade (the fade ends it)
    switch (m) {
    case StreamSwitch::AtEnd:
        break;
    case StreamSwitch::AfterFade:
        StartFade(FadeMode::Out);
        break;
    case StreamSwitch::AtMarker: {
        if (markers.empty()) {
            m = StreamSwitch::AtEnd;
            break;
        }
        const int pos = streamPosition + streamBufferFrames;
        const auto next = std::ranges::find_if(markers, [pos](std::int32_t marker) { return marker >= pos; });
        at = next != markers.end() && *next != -1 ? *next : markers[0];
        if (at == -1) return;
        break;
    }
    case StreamSwitch::Now:
        at = streamPosition + streamBufferFrames;
        if (at == -1) return;
        break;
    }
    streamState = StreamState::Switching;
    switchMode = m;
    nextName = name;
    streamSwitchAt = at;
}

// Mode Now stops the stream now (closing the file) and fades the volume back in over 2 s; other modes
// request silence the RequestStream way.
void SoundManager::StopStream(StreamSwitch mode) {
    if (!enabled) return;
    endAfterSwitch = false;
    if (streamState == StreamState::Idle) return;
    if (mode != StreamSwitch::Now) {
        RequestStream({}, mode);
        return;
    }
    CodecClose(this);
    streamState = StreamState::Idle;
    streamName.clear();
    musicFader.mode = FadeMode::Out;
    musicFader.finished = false;
    musicFader.SetHold(platform::FrameClock() + 2000);
}

// A request the stream has yet to take (state Switching) gets its end-after request from SwitchStream; one it has
// already taken or turned down gets it now.
void SoundManager::PlayStreamOnce(std::string_view name) {
    RequestStream(name, StreamSwitch::Now);
    if (streamState == StreamState::Switching) endAfterSwitch = true;
    else StopStream(StreamSwitch::AtEnd);
}

// --- Stopping everything ---------------------------------------------------------------------

// Loading a game, leaving a zone, the main menu: every voice and deferred sound goes.
void SoundManager::StopAll(bool stopStream) {
    if (slots != nullptr)
        for (SoundSlot& slot : std::span(slots.get(), kVoiceCount)) EndSlot(&slot);
    nextHandle = 0;
    deferred.clear();
    if (stopStream) StopStream(StreamSwitch::Now);
}

// --- The volume envelope ----------------------------------------------------------------------------

// Sets the time at which a fade is cancelled; 0 cancels it now.
void SoundFader::SetHold(std::uint32_t until) {
    holdUntil = until;
    if (until != 0) return;
    factor = 1.0f;
    mode = FadeMode::None;
    finished = false;
    Recalc();
}

void SoundFader::Recalc() {
    level = std::clamp(lib::RoundEven(volume * factor * kLevelMax * kPercent), 0, kMaxLevel);
}

// One step of the fade at `now` (StepStream, every 100 ms, under the stream lock).
void SoundFader::Update(std::uint32_t now) {
    if (holdUntil != 0 && now >= holdUntil) {
        SetHold(0);
        return;
    }
    switch (mode) {
    case FadeMode::None:
        return;
    case FadeMode::In:
        factor = kFadeStep + factor;
        if (factor >= 1.0f) {
            mode = FadeMode::None;
            factor = 1.0f;
        }
        break;
    case FadeMode::Out:
        factor -= kFadeStep;
        if (factor <= 0.0f) {
            mode = FadeMode::None;
            factor = 0.0f;
            finished = true;
        }
        break;
    }
    Recalc();
}

// --- A voice for a new sound -----------------------------------------------------

int SoundManager::AllocateVoice(int priority) {
    int lowest = std::numeric_limits<int>::max();
    int index = 0;
    for (int i = 0; i < kVoiceCount; ++i) {
        if (!slots[i].playing) return i;
        if (slots[i].params.priority < lowest) {
            lowest = slots[i].params.priority;
            index = i;
        }
    }
    if (priority <= lowest) return kNoVoice;
    SoundSlot* slot = &slots[index];
    if (slot->looping) {
        // The stolen looping sound waits in the deferred map with its state and sample position.
        deferred[slot->handle] = DeferredSound{slot->params, audio::Position(slot->voice)};
    }
    EndSlot(slot);
    return index;
}

// --- Play2D, Play3D ---------------------------------------------------------

// Plays `name` without position: `volume` 0..100 (scaled by the effects or speech volume,
// and with `heightFade` by the camera height), `pan` -100..100, `loop` endlessly, `compressed` an
// MP3 to decompress (handed to the wave loader). Returns the sound handle, or kNoSound (sound off, no voice for a
// one-shot sound, silent one-shot, or the wave did not load).
int SoundManager::Play2D(std::string_view name, int priority, int volume, int pan, const Play2DOptions& options) {
    if (!enabled) return kNoSound;
    SoundParams params{.name = std::string(name),
                       .baseVolume = volume,
                       .pan = pan,
                       .priority = priority,
                       .speech = options.speech,
                       .flat = true,
                       .heightFade = options.heightFade};
    int index = AllocateVoice(priority);
    if (index == kNoVoice) {
        // A looping sound waits in the deferred map (silent, flat) until Tick restarts it.
        if (!options.loop) return kNoSound;
        return AddDeferred(this, std::move(params));
    }
    SoundSlot* slot = &slots[index];
    audio::ResetVoice(slot->voice);
    slot->looping = options.loop;
    slot->params = std::move(params);
    slot->params.volume = FlatVolume(volume, options.heightFade, options.speech);
    if (volume == 0 && !options.loop) return kNoSound;
    if (!LoadSample(slot, name, options.compressed)) return kNoSound;
    return StartLoaded(this, slot, options.loop);
}

// Plays `name` at `position`: full volume inside `nearDist`, silent beyond `farDist`, the pan from the
// listener (UpdatePositional). Returns the handle or kNoSound as Play2D. A looping sound without a voice is
// deferred; its volume, base volume and pan stay 0 (the original left them as operator new returned them): a
// restart computes the volume and pan from the position, and nothing reads the others.
int SoundManager::Play3D(std::string_view name, int priority, const Vector3& position, float nearDist, float farDist, bool loop) {
    if (!enabled) return kNoSound;
    SoundParams params{.name = std::string(name),
                       .priority = priority,
                       .position = position,
                       .nearDistance = nearDist,
                       .farDistance = farDist};
    int index = AllocateVoice(priority);
    if (index == kNoVoice) {
        if (!loop) return kNoSound;
        return AddDeferred(this, std::move(params));
    }
    SoundSlot* slot = &slots[index];
    audio::ResetVoice(slot->voice);
    slot->looping = loop;
    slot->params = std::move(params);
    UpdatePositional(slot);
    if (slot->params.volume == 0 && !loop) return kNoSound;
    if (!LoadSample(slot, name, false)) return kNoSound;
    return StartLoaded(this, slot, loop);
}

// Gives the voice wave `name`: a cached copy (unless `compressed`, which then leaves the
// voice without a new wave), or the file from the first archive that has it, decoded
// from MP3 when `compressed` (kept at the cache's old end), else copied after
// making room (kept at its front). Sets the voice's name, playing flag and a new handle.
// Returns false when no archive has the file or the decoding fails.
bool SoundManager::LoadSample(SoundSlot* slot, std::string_view name, bool compressed) {
    std::string key(name);
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        if (!compressed) UseCached(this, slot, cached->second);
    } else {
        MappedResource* source = nullptr;
        std::uint32_t fileSize = 0;
        void* data = LoadWaveFile(name, &fileSize, &source);
        if (data == nullptr) return false;
        int size = static_cast<int>(fileSize);
        std::uint32_t now = platform::CoarseClock();
        WaveRecord record;
        if (compressed) {
            const bool ok = audio::DecodeMp3File(data, fileSize, &record.data);
            source->CloseFile(data);
            if (!ok) return false;
            size = static_cast<int>(record.data.size());
        } else {
            Evict(this, size, now);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            record.data.assign(bytes, bytes + fileSize);
            source->CloseFile(data);
        }
        record.name = key;
        record.lastUsed = now;
        record.size = size;
        record.uses = 1;
        cacheBytes += size;
        std::list<WaveRecord>::iterator node;
        if (!compressed) {
            cacheList.push_front(std::move(record));
            node = cacheList.begin();
        } else {
            cacheList.push_back(std::move(record));
            node = std::prev(cacheList.end());
        }
        cache[key] = node;
        audio::SetWave(slot->voice, node->data.data(), node->data.size());
    }
    slot->params.name = key;
    slot->playing = true;
    slot->handle = nextHandle;
    ++nextHandle;
    return true;
}

// --- The engine object's virtuals, the wave sources and sound folder, the lifetime and the output's setup Open /
// Close / Reset.

const char* SoundManager::ClassName() { return kClassName; }
ClassId SoundManager::GetClassId() { return ClassId::SoundManager; }

void SoundManager::Create() {
    Reset();
}

// Every archive deleted, the first first.
void SoundManager::ClearWaveSources() {
    for (MappedResource* source : waveSources) delete source;
    waveSources.clear();
}

// Called at startup for the two sound archives.
bool SoundManager::AddWaveSource(const std::string& path) {
    if (!enabled) return true;
    auto* source = new MappedResource();
    if (source->OpenResource(path) == 0) {
        delete source;
        return false;
    }
    waveSources.push_back(source);
    return true;
}

void* SoundManager::LoadWaveFile(std::string_view name, std::uint32_t* size, MappedResource** source) {
    for (MappedResource* current : waveSources) {
        if (void* data = current->GetFile(name, size)) {
            *source = current;
            return data;
        }
    }
    return nullptr;
}

// The terrain's count of ambient variants.
bool SoundManager::HasWaveFile(std::string_view name) {
    std::uint32_t size = 0;
    MappedResource* source = nullptr;
    void* data = LoadWaveFile(name, &size, &source);
    if (data == nullptr) return false;
    source->CloseFile(data);
    return true;
}

// From the unit database's sound counts, on the game's main thread.
std::vector<std::string> SoundManager::ListWaveFiles(const char* pattern) {
    std::vector<std::string> waves;
    std::vector<std::string> names;
    for (MappedResource* source : waveSources) {
        source->ListFiles(&names, pattern);
        waves.insert(waves.end(), names.begin(), names.end());
    }
    return waves;
}

// Called at startup.
bool SoundManager::SetPath(std::string_view folder, const std::string& registryPath) {
    if (!enabled) return true;
    soundPath = folder;
    if (!soundPath.ends_with('\\')) soundPath += '\\';
    return soundRegistry.Open(registryPath);
}

std::string SoundManager::CurrentStreamName() const {
    if (streamState == StreamState::Playing || streamState == StreamState::Switching) return streamName;
    return {};
}


// --- Constructor, destructor, stream reset ---------------------------------------------------

SoundManager::SoundManager() {
    Reset();
}

SoundManager::~SoundManager() {
    Close();
}

void SoundManager::ResetStream() {
    streamState = StreamState::Idle;
    endAfterSwitch = false;
    streamSteppedAt = 0;
    streamSample = nullptr;
    streamFile = nullptr;
    streamBuffer.clear();
    streamBufferSize = -1;
    streamBufferFrames = 0;   // the original left it unset until a file played: silence counts no frames
    streamPosition = -1;
    streamSwitchAt = -1;
    streamLoopAt = -1;
    streamFrameBytes = -1;
    streamName.clear();
    nextName.clear();
    switchMode = StreamSwitch::Now;
    musicFader.volume = kFullVolume;
    musicFader.SetHold(0);
}

// --- Open, Close, Reset ------------------------------------------------------------------

void SoundManager::CloseStream() {
    CodecClose(this);
    if (streamSample != nullptr) audio::DestroyStream(streamSample);
    ResetStream();
}

bool SoundManager::OpenStream() {
    streamSample = audio::CreateStream();
    if (streamSample == nullptr) {
        CloseStream();
        return false;
    }

    streamBufferSize = audio::MinimumHalfBytes(kStreamRate, kStreamChannels) * 2;
    streamBuffer.assign(static_cast<std::size_t>(streamBufferSize), 0);
    audio::ResetStream(streamSample);
    audio::SetStreamFormat(streamSample, kStreamRate, kStreamChannels);
    audio::SetStreamVolume(streamSample, 0);
    audio::SetStreamPan(streamSample, 0);
    streamPosition = 0;
    streamState = StreamState::Idle;
    // No fade and no hold; the level from the music volume.
    musicFader.SetHold(0);
    return true;
}

void SoundManager::FollowClock(std::uint32_t now) {
    if (streamSample == nullptr) return;
    while (now - streamSteppedAt >= kStreamPeriodMs) {
        streamSteppedAt += kStreamPeriodMs;
        StepStream(streamSteppedAt);
    }
}

void SoundManager::StepStream(std::uint32_t now) {
    musicFader.Update(now);
    audio::SetStreamVolume(streamSample, musicFader.level);
    for (;;) {
        int half = audio::ReadyHalf(streamSample, now);
        if (half == kNoHalfReady) break;
        streamPosition += streamBufferFrames;
        audio::QueueHalf(streamSample, half, streamBuffer.data(), streamBufferSize);
        FillSpareBuffer(now);
    }
}

// --- The stream's reader -----------------------------------------------------------------

// The game leaves "from the beginning again" (a local) unwritten when neither a loop point nor a switch
// point is in the buffer, and reads it in one such case: a switch at the file's end (mode AtEnd) when the file
// ends in this buffer. Here that case is the switch, which is what the mode asks for.
void SoundManager::FillSpareBuffer(std::uint32_t now) {
    std::uint8_t* const buffer = streamBuffer.data();
    const StreamState state = streamState;
    if (state == StreamState::Idle) {
        Silence(buffer, streamBufferSize);
        return;
    }
    const bool switching = state == StreamState::Switching;
    const int frames = streamBufferFrames;
    int current = frames;   // frames of the playing file as it goes
    int rest = 0;           // frames after its loop point or the switch point
    bool again = false;     // the rest is the playing file from its beginning
    if (switching && switchMode == StreamSwitch::AfterFade && musicFader.finished) {
        current = 0;
        rest = frames;
        musicFader.SetHold(now + kSwitchHoldMs);
    } else if ((state == StreamState::Playing || (switching && streamSwitchAt > streamLoopAt)) && streamLoopAt >= streamPosition &&
               streamLoopAt < streamPosition + frames) {
        again = true;
        current = streamLoopAt - streamPosition;
        rest = frames - current;
    } else if (switching && streamSwitchAt >= streamPosition && streamSwitchAt < streamPosition + frames) {
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
        int got = audio::DecodeMp3(streamFile, buffer, currentBytes);
        if (got < currentBytes) {
            if (streamState == StreamState::Switching && switchMode == StreamSwitch::AtEnd) {
                // The file ended: the next one from here.
                currentBytes = got;
                restBytes = streamBufferSize - got;
            } else {
                audio::RewindMp3(streamFile);
                streamPosition = 0;
                ReadOrSilence(this, buffer + got, currentBytes - got);
            }
        }
    }
    if (restBytes == 0) return;
    if (again) {
        audio::RewindMp3(streamFile);
        streamPosition = 0;
        ReadOrSilence(this, buffer + currentBytes, restBytes);
        return;
    }
    SwitchStream(this, buffer + currentBytes, restBytes);
}

void SoundManager::Open(bool useReverb, unsigned rate) {
    Destroy();   // Close
    reverbOn = useReverb;
    audio::OpenOutput(std::min(rate, kMaxRate));
    // The stream steps on the game's clock (FollowClock), with a device or without; without one what UpdateMusic
    // reads of it comes on the same frames in every run (the harness's virtual clock).
    if (OpenStream()) streamSteppedAt = platform::FrameClock();
    slots = std::make_unique<SoundSlot[]>(kVoiceCount);
    for (SoundSlot& slot : std::span(slots.get(), kVoiceCount)) slot.voice = audio::CreateVoice();
    enabled = true;
}

void SoundManager::Close() {
    CloseStream();
    if (slots != nullptr) {
        for (SoundSlot& slot : std::span(slots.get(), kVoiceCount)) {
            EndSlot(&slot);
            if (slot.voice != nullptr) audio::DestroyVoice(slot.voice);
        }
        slots.reset();
    }
    deferred.clear();
    cacheList.clear();
    cache.clear();
    if (enabled) audio::CloseOutput();
    ClearWaveSources();
    soundRegistry.Close();
    Reset();
}

void SoundManager::Reset() {
    ResetStream();
    slots = nullptr;
    cacheBytes = 0;
    for (SoundEmitter& e : emitters) e = SoundEmitter{};
    // The generations go on counting, so an old sequence id names no new sequence.
    for (SoundSequence& q : sequences) q = SoundSequence{.generation = q.generation};
    nextHandle = 0;
    emitterTick = 0;
    speechVolume = kFullVolume;
    effectsVolume = kFullVolume;
    enabled = false;
    reverbOn = false;
    panDirect = false;
    reverb[0] = reverb[1] = reverb[2] = 0.0f;
    soundPath.clear();
    randomStart = 0;
}
