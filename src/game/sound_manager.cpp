// The sound manager (SoundManager): voice volumes and pans from the listener, the per-handle controls,
// ambient emitters, 3D sound sequences, the per-frame tick and the music stream requests, and Play2D /
// Play3D.
//
// Float maths, built for SSE2. Two results reach game state: a 3D sound's level (UpdatePositional: Play3D gives a
// silent one-shot no voice) and the ambient emitters' intervals (Tick: an emitter's Play2D takes a voice). With
// every voice taken, a speech's Play2D answers -1, which keeps a unit's talking flag (unit_client_effects). Both are
// basic operations on floats and small integers in the original's order, which the original ran on the main
// thread at 24-bit precision control: there every x87 operation rounds once to a float significand, as a float
// operation does, and lib::RoundEven rounds to an integer as it did. So the levels and intervals are the original's
// (docs/notes/sound.md, "Floating point"). The pans, the axis, the fader and the wave lengths only reach the output.
#include "game_strings.h"
#include "mod_folders.h"
#include "platform/platform.h"
#include "random.h"
#include "sound.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr float kHeightFull = 30.0f;     // 2D height-faded sounds are full below
constexpr float kHeightSilent = 70.0f;   // ... and silent above
constexpr float kHundred = 100.0f;
constexpr float kOutputScale = 0.0127f;   // 0..100 times 0..100 to the output's 0..127
static_assert(std::bit_cast<std::uint32_t>(kOutputScale) == 0x3C5013A9u);
constexpr float kLevelMax = 127.0f;
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kPercent) == 0x3C23D70Au);
constexpr float kEmitterMargin = 0.001f;
static_assert(std::bit_cast<std::uint32_t>(kEmitterMargin) == 0x3A83126Fu);
constexpr float kEmitterMaxScale = 255.0f;

// pi and pi / 2 (set by a static initialiser).
constexpr float kHalfPi = /*007C8808*/ 1.5707964f;
// The fade step every fade request uses (1/96).
std::uint32_t FadeStepBits() { return 0x3C2AAAABu; }
// StopStream's fade-out step, loaded and tested as a float.
constexpr float kStopFadeStep = 0.010416667f;

// The slot of the wave's cache list node (its record's uses counts the voices playing it).
std::list<WaveRecord>::iterator& CacheEntry(std::unordered_map<std::string, std::list<WaveRecord>::iterator>& cache, const SoundSlot* slot) { return cache[slot->name]; }
// May rehash first.
void SetDeferred(DeferredMap& deferred, std::int32_t handle, DeferredSound* rec) {
    deferred[handle] = rec;
}
void FreeDeferred(DeferredSound* rec) {
    delete rec;
}

int ClampLevel(int v) { return v < 0 ? 0 : (v > 0x7F ? 0x7F : v); }

// Heights between 30 and 70 fade 2D sounds flagged heightFade.
float HeightFactor(float h) {
    return 1.0f - (h - kHeightFull) / (kHeightSilent - kHeightFull);
}

// The voice loop of the volume setters and SetListener.
void RefreshVoices(SoundManager* m) {
    SoundSlot* slot = m->slots.get();
    for (int i = 0; i < 15; ++i, ++slot) {
        if (slot->playing == 0) continue;
        if (slot->flat == 0) m->UpdatePositional(slot);
        else slot->volume = m->FlatVolume(slot->baseVolume, slot->heightFade, slot->speech);
        m->ApplyVolumePan(slot);
    }
}

// Deletes the deferred record of `handle`, if any (StopSound, RemoveSequence).
void DropDeferred(SoundManager* m, int handle) {
    auto node = m->deferred.find(handle);
    if (node == m->deferred.end()) return;
    DeferredSound* rec = node->second;
    if (rec == nullptr) return;
    m->deferred.erase(rec->handle);
    FreeDeferred(rec);
}

// Emitter `index` as the game addresses it: index 16 is accepted (one past the array).
SoundEmitter* EmitterAt(SoundManager* m, int index) {
    return m->emitters + index;
}

SoundSequence* SequenceForId(SoundManager* m, std::uint32_t id) {
    SoundSequence* s = &m->sequences[id & 0xFF];
    return static_cast<std::uint32_t>(s->id) == id ? s : nullptr;
}

// Moves a sequence's current sound (MoveSequence / UpdateSequence).
void MoveSequenceSound(SoundManager* m, int handle, const float* position, float nearDist, float farDist) {
    SoundSlot* slot = m->SlotForHandle(handle);
    if (slot != nullptr) {
        if (slot->finished != 0) {
            if (slot->playing != 0) m->EndSlot(slot);
        } else {
            lib::LoadBytes(slot->position, position);
            slot->flat = 0;
            slot->nearDistance = nearDist;
            slot->farDistance = farDist;
            m->UpdatePositional(slot);
            m->ApplyVolumePan(slot);
            return;
        }
    }
    if (m->enabled == 0) return;
    auto node = m->deferred.find(handle);
    if (node == m->deferred.end() || node->second == nullptr) return;
    DeferredSound* rec = node->second;
    lib::LoadBytes(rec->position, position);
    rec->flat = 0;
    rec->nearDistance = nearDist;
    rec->farDistance = farDist;
}

constexpr int kVoiceCount = 15;
constexpr int kNoVoice = -1;

// No voice for a looping sound: it waits in the deferred map (silent, flat) until Tick restarts it.
int DeferLooping(SoundManager* m, std::string_view name, int priority, int volume, int pan, int heightFade, int speech) {
    auto* rec = new DeferredSound;
    rec->name.assign(name);
    rec->baseVolume = volume;
    rec->pan = pan;
    rec->priority = priority;
    rec->speech = speech;
    rec->flat = 1;
    rec->volume = 0;
    rec->length = 0.0f;
    rec->heightFade = heightFade;
    rec->handle = m->nextHandle;
    ++m->nextHandle;
    rec->samplePosition = 0;
    SetDeferred(m->deferred, rec->handle, rec);
    return rec->handle;
}

// The common end of Play2D and Play3D once the wave is in the voice. Returns the voice's handle.
int StartLoaded(SoundManager* m, SoundSlot* slot, int loop) {
    if (loop != 0) platform::audio::SetLoopCount(slot->voice, 0);
    m->ApplyVolumePan(slot);
    platform::audio::SetEndCallback(slot->voice, SoundEndOfSample, slot);
    if (m->reverbOn != 0) platform::audio::SetReverb(slot->voice, m->reverb[0], m->reverb[1], m->reverb[2]);
    if (m->randomStart != 0) {
        std::uint32_t range = static_cast<std::uint32_t>(m->randomStart) + 1;
        platform::audio::SetPositionMs(slot->voice, g_clientRandom.Next() % range);
    }
    platform::audio::Start(slot->voice);
    return slot->handle;
}

constexpr std::uint32_t kCacheBudget = 0x800000;   // bytes kept unless a wave is still in use
constexpr std::uint32_t kCacheKeep = 60000;        // ms a wave stays cached after its last use

// A cached wave: moved to the front with one more use and the time of now, then given to the voice.
void UseCached(SoundManager* m, SoundSlot* slot, std::list<WaveRecord>::iterator node) {
    m->cacheList.splice(m->cacheList.begin(), m->cacheList, node);
    WaveRecord& record = *node;
    record.lastUsed = platform::TickCount();
    record.uses = record.uses + 1;
    slot->length = m->WaveLength(record.data.data(), record.data.size());
    platform::audio::SetWave(slot->voice, record.data.data(), record.data.size());
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

} // namespace

// --- Shared pieces ------------------------------------------------------------------------------

// The output's volume (0..127) of `level` (0..100, a 2D voice's base volume or a 3D voice's distance level), inlined in
// every volume setter. A NaN camera height keeps the full level.
int SoundManager::FlatVolume(int level, int fade, int isSpeech) const {
    float v = static_cast<float>(level);
    if (fade != 0) {
        float h = listener.z;
        EI_NAN_AUDIT(h);
        if (h >= kHeightSilent) v = 0.0f;
        else if (h > kHeightFull) v = v * HeightFactor(h);
    }
    v = v * (isSpeech != 0 ? speechVolume : effectsVolume);
    v = v * kOutputScale;
    return ClampLevel(lib::RoundEven(v));
}

// Hands the slot's volume and pan to the output; the pan is mirrored unless panDirect.
void SoundManager::ApplyVolumePan(SoundSlot* slot) const {
    int direct = panDirect;
    platform::audio::SetVolume(slot->voice, slot->volume);
    int p = slot->pan;
    if (direct == 0) p = -p;
    platform::audio::SetPan(slot->voice, (p + 100) * 0x7F / 200);
}

// Ends a voice: releases its cached wave, stops the voice and frees the slot.
void SoundManager::EndSlot(SoundSlot* slot) {
    --CacheEntry(cache, slot)->uses;
    platform::audio::End(slot->voice);
    slot->playing = 0;
    slot->finished = 0;
    slot->handle = -1;
}

// The first voice whose handle matches (free voices hold -1), or null.
SoundSlot* SoundManager::SlotForHandle(int handle) const {
    for (int i = 0; i < 15; ++i)
        if (slots[i].handle == handle) return &slots[i];
    return nullptr;
}

// --- Volumes, listener ----------------------------------------------------------------------------

// A 3D voice's volume and pan from the listener: full inside nearDistance, falling to 0 at farDistance (a
// distance level 0..100 the 2D voices' way, FlatVolume, with the camera height fade); the pan from the angle
// between the listener axis and the direction to the sound (0 ahead or behind, +-100 to the sides). The volume
// decides whether Play3D's one-shot plays (the file's head comment).
void SoundManager::UpdatePositional(SoundSlot* slot) {
    float dx = slot->position.x - listener.x;
    float dy = slot->position.y - listener.y;
    float dist2 = LengthSquared2DUnrounded(dy, dx);
    if (dist2 > slot->farDistance * slot->farDistance) {
        slot->volume = 0;
        return;
    }
    float dist = std::sqrt(dist2);
    EI_NAN_AUDIT(dist, slot->nearDistance);
    int level = 100;
    if (!(dist < slot->nearDistance))
        level = 100 - lib::RoundEven((dist - slot->nearDistance) / (slot->farDistance - slot->nearDistance) * kHundred);
    slot->volume = FlatVolume(level, 1, slot->speech);

    float ahead = AbsBits(Dot2DUnrounded(dy, dx, listenerAxis.y, listenerAxis.x));
    if (ahead > dist) {
        slot->pan = 0;
        return;
    }
    EI_NAN_AUDIT(dist);
    if (dist == 0.0f) {
        slot->pan = 0;
        return;
    }
    float angle = std::acos(ahead / dist);
    if (angle > kHalfPi) angle = kPi - angle;
    int pan = lib::RoundEven(kHundred / kHalfPi * angle);
    slot->pan = pan;
    if (dy * listenerAxis.x > dx * listenerAxis.y) slot->pan = -pan;
}

// Options: effects volume 0..100.
void SoundManager::SetEffectsVolume(int volume) {
    if (enabled == 0) return;
    effectsVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}

// Options: speech volume 0..100.
void SoundManager::SetSpeechVolume(int volume) {
    if (enabled == 0) return;
    speechVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}

// The camera's position and direction (Camera::Update every frame).
void SoundManager::SetListener(const float* position, const float* axis) {
    if (enabled == 0) return;
    lib::LoadBytes(listener, position);
    lib::LoadBytes(listenerAxis, axis);
    float len = std::sqrt(LengthSquared2DUnrounded(listenerAxis.x, listenerAxis.y));
    EI_NAN_AUDIT(len);
    if (len == 0.0f) {
        listenerAxis.x = 1.0f;
        listenerAxis.y = 0.0f;
        listenerAxis.z = 0.0f;
    } else {
        float inv = 1.0f / len;
        listenerAxis.x = inv * listenerAxis.x;
        listenerAxis.y = inv * listenerAxis.y;
        listenerAxis.z = inv * listenerAxis.z;
    }
    RefreshVoices(this);
}

// Options: music volume 0..100 (0 stops the stream).
void SoundManager::SetMusicVolume(int volume) {
    int v = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (enabled == 0) return;
    musicFader.volume = v;
    musicFader.Recalc();
    if (streamSample != nullptr) platform::audio::SetStreamVolume(streamSample, musicFader.level);
    if (v == 0) StopStream(4);
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
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) platform::audio::Stop(slots[i].voice);
    if (enabled != 0 && streamSample != nullptr) platform::audio::StopStream(streamSample);
}

void SoundManager::ResumeAll() {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) platform::audio::Resume(slots[i].voice);
    if (enabled != 0 && streamSample != nullptr) platform::audio::ResumeStream(streamSample);
}

// --- Per-handle controls --------------------------------------------------------------------------

// The voice playing `handle`; a voice whose sample ended is freed and gives null.
SoundSlot* SoundManager::FindPlaying(int handle) {
    if (enabled == 0) return nullptr;
    SoundSlot* slot = SlotForHandle(handle);
    if (slot == nullptr) return nullptr;
    if (slot->finished == 0) return slot;
    if (slot->playing != 0) EndSlot(slot);
    return nullptr;
}

// The deferred record of `handle`, or null.
DeferredSound* SoundManager::FindDeferred(std::uint32_t handle) {
    if (enabled == 0) return nullptr;
    auto node = deferred.find(static_cast<std::int32_t>(handle));
    return node != deferred.end() ? node->second : nullptr;
}

// Makes a sound 2D with the given volume (0..100) and pan.
int SoundManager::SetSoundVolume(int handle, int volume, int pan) {
    if (enabled != 0) {
        SoundSlot* slot = SlotForHandle(handle);
        if (slot != nullptr) {
            if (slot->finished != 0) {
                if (slot->playing != 0) EndSlot(slot);
            } else {
                slot->baseVolume = volume;
                int level = FlatVolume(volume, slot->heightFade, slot->speech);
                slot->volume = level;
                slot->pan = pan;
                slot->flat = 1;
                ApplyVolumePan(slot);
                return 1;
            }
        }
    }
    if (enabled != 0) {
        auto node = deferred.find(handle);
        if (node != deferred.end() && node->second != nullptr) {
            node->second->baseVolume = volume;
            node->second->pan = pan;
            node->second->flat = 1;
            return 1;
        }
    }
    return 0;
}

// Makes a sound 3D at `position`.
int SoundManager::SetSoundPosition(int handle, const float* position, float nearDist, float farDist) {
    if (enabled != 0) {
        SoundSlot* slot = SlotForHandle(handle);
        if (slot != nullptr) {
            if (slot->finished != 0) {
                if (slot->playing != 0) EndSlot(slot);
            } else {
                lib::LoadBytes(slot->position, position);
                slot->nearDistance = nearDist;
                slot->farDistance = farDist;
                slot->flat = 0;
                UpdatePositional(slot);
                ApplyVolumePan(slot);
                return 1;
            }
        }
    }
    if (enabled != 0) {
        auto node = deferred.find(handle);
        if (node != deferred.end() && node->second != nullptr) {
            DeferredSound* rec = node->second;
            lib::LoadBytes(rec->position, position);
            rec->flat = 0;
            rec->nearDistance = nearDist;
            rec->farDistance = farDist;
            return 1;
        }
    }
    return 0;
}

// Stops a sound (or forgets its deferred record); always -1, the "no sound" handle.
int SoundManager::StopSound(int handle) {
    if (enabled != 0) {
        SoundSlot* slot = SlotForHandle(handle);
        if (slot != nullptr) {
            if (slot->finished == 0) {
                if (slot->playing != 0) EndSlot(slot);
                return -1;
            }
            if (slot->playing != 0) EndSlot(slot);
        }
    }
    if (enabled != 0) DropDeferred(this, handle);
    return -1;
}

// --- Ambient emitters -----------------------------------------------------------------------------

void SoundManager::MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                                int maxInterval) {
    if (index < 0 || index > 16) return;
    SoundEmitter* e = EmitterAt(this, index);
    if (e->active == 0) return;
    lib::LoadBytes(e->position, position);
    e->innerRadius = inner;
    e->outerRadius = outer;
    e->minInterval = minInterval;
    e->maxInterval = maxInterval;
}

// Returns the emitter index or -1.
int SoundManager::AddEmitter(const std::vector<std::string>* names, const float* position, float inner, float outer,
                              int minInterval, int maxInterval) {
    if (enabled == 0 || names->empty()) return -1;
    int i = 0;
    while (i < 16 && emitters[i].active != 0) ++i;
    if (i == 16) return -1;
    SoundEmitter* e = &emitters[i];
    e->active = 1;
    e->names = *names;
    MoveEmitter(i, position, inner, outer, minInterval, maxInterval);
    e->lastPlayed = platform::TickCount();
    return i;
}

// Always -1. The name list is kept.
int SoundManager::RemoveEmitter(int index) {
    if (index < 0 || index > 16) return -1;
    SoundEmitter* e = EmitterAt(this, index);
    if (e->active == 0) return -1;
    e->position.x = 0.0f;
    e->active = 0;
    e->maxInterval = -1;
    e->minInterval = -1;
    e->position.y = 0.0f;
    e->position.z = 0.0f;
    e->outerRadius = 0.0f;
    e->innerRadius = 0.0f;
    e->lastPlayed = 0;
    return -1;
}

// --- Sound sequences ------------------------------------------------------------------------------

// Starts the sequence's next sound; false when it is over or the sound did not start.
int SoundSequence::PlayNext(SoundManager* manager) {
    if (chooser != nullptr) {
        int played = remaining;
        ++remaining;
        int next = chooser(index, played);
        index = next;
        if (next < 0) return 0;
        index = next & ~kSequenceLoop;
        int h = manager->Play3D(names[index], priority, &position.x, nearDistance, farDistance,
                                next & kSequenceLoop);
        handle = h;
        return h != -1 ? 1 : 0;
    }
    if (remaining == 0) return 0;
    if (random != 0) index = static_cast<std::int32_t>(g_clientRandom.Next() % static_cast<std::uint32_t>(names.size()));
    int h = manager->Play3D(names[index], priority, &position.x, nearDistance, farDistance, 0);
    handle = h;
    if (h == -1) return 0;
    index = (index + 1) % static_cast<int>(names.size());
    if (remaining > 0) --remaining;
    return 1;
}

// Returns the sequence id (generation << 8 | slot) or -1.
int SoundManager::AddSequence(const std::vector<std::string>* names, int priority, const float* position, float nearDist,
                               float farDist, int randomOrder, int count, int(*chooser)(int, int)) {
    if (enabled == 0 || names->empty()) return -1;
    int i = 0;
    while (i < 8 && sequences[i].active != 0) ++i;
    if (i == 8) return -1;
    SoundSequence* s = &sequences[i];
    ++s->generation;
    s->id = s->generation << 8 | i;
    s->active = 1;
    s->names = *names;
    lib::LoadBytes(s->position, position);
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
    if (s->PlayNext(this) != 0) return s->id;
    s->id = -1;
    s->active = 0;
    return -1;
}

int SoundManager::MoveSequence(std::uint32_t sequenceId, const float* position) {
    if (enabled == 0 || (sequenceId & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return 0;
    lib::LoadBytes(s->position, position);
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, s->nearDistance, s->farDistance);
    return 1;
}

int SoundManager::UpdateSequence(std::uint32_t sequenceId, const float* position, float nearDist, float farDist, int count) {
    if (enabled == 0 || (sequenceId & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return 0;
    lib::LoadBytes(s->position, position);
    s->nearDistance = nearDist;
    s->farDistance = farDist;
    s->remaining = count;
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, nearDist, farDist);
    return 1;
}

// Always -1.
int SoundManager::RemoveSequence(std::uint32_t sequenceId) {
    if (enabled == 0 || (sequenceId & 0xFF) >= 8) return -1;
    SoundSequence* s = SequenceForId(this, sequenceId);
    if (s == nullptr) return -1;
    int handle = s->handle;
    s->active = 0;
    s->id = -1;
    if (enabled == 0) return -1;
    SoundSlot* slot = SlotForHandle(handle);
    if (slot != nullptr) {
        if (slot->finished == 0) {
            if (slot->playing != 0) EndSlot(slot);
            return -1;
        }
        if (slot->playing != 0) EndSlot(slot);
    }
    if (enabled != 0) DropDeferred(this, handle);
    return -1;
}

// --- The per-frame tick ---------------------------------------------------------------------------

// Ends the voice if it plays (the sequences' check in Tick).
void SoundSlot::End(SoundManager* manager) {
    if (playing == 0) return;
    manager->EndSlot(this);
}

// Every world frame: frees the voices whose sample ended, restarts the highest-priority deferred sounds in
// free voices, starts the next sound of every sequence whose sound is over, and every 100 ms (GetTickCount)
// lets the ambient emitters near the listener play a random name of theirs. Emitters and random sequences
// draw from the client effects' generator (g_clientRandom) on the wall clock's schedule.
void SoundManager::Tick() {
    if (enabled == 0) return;
    for (int i = 0; i < 15; ++i) {
        SoundSlot* slot = &slots[i];
        if (slot->finished == 0 || slot->playing == 0) continue;
        --CacheEntry(cache, slot)->uses;
        platform::audio::End(slot->voice);
        slot->handle = -1;
        slot->playing = 0;
        slot->finished = 0;
    }

    for (int i = 0; i < 15; ++i) {
        if (deferred.size() == 0) break;
        if (slots[i].playing != 0) continue;
        // The highest priority (the first one met on ties).
        std::int32_t best = deferred.begin()->second->priority;
        std::int32_t key = deferred.begin()->second->handle;
        for (auto& [handle, candidate] : deferred) {
            if (candidate->priority > best) {
                best = candidate->priority;
                key = candidate->handle;
            }
        }
        DeferredSound* rec = deferred[key];
        deferred.erase(key);

        SoundSlot* slot = &slots[i];
        slot->volume = rec->volume;
        slot->baseVolume = rec->baseVolume;
        slot->pan = rec->pan;
        slot->priority = rec->priority;
        slot->speech = rec->speech;
        slot->looping = 1;
        slot->length = rec->length;
        slot->flat = rec->flat;
        slot->heightFade = rec->heightFade;
        slot->position = rec->position;
        slot->nearDistance = rec->nearDistance;
        slot->farDistance = rec->farDistance;
        platform::audio::ResetVoice(slot->voice);
        // A flat sound's level is recomputed from its saved output level as if that were 0..100.
        if (slot->flat != 0) slot->volume = FlatVolume(slot->volume, slot->heightFade, slot->speech);
        else UpdatePositional(slot);
        if (LoadSample(slot, rec->name.data(), 0) != 0) {
            slot->handle = rec->handle;
            platform::audio::SetLoopCount(slot->voice, 0);
            ApplyVolumePan(slot);
            platform::audio::SetEndCallback(slot->voice, SoundEndOfSample, slot);
            if (reverbOn != 0) platform::audio::SetReverb(slot->voice, reverb[0], reverb[1], reverb[2]);
            platform::audio::SetPosition(slot->voice, rec->samplePosition);
            platform::audio::Start(slot->voice);
        }
        if (rec != nullptr) FreeDeferred(rec);
    }

    for (int k = 0; k < 8; ++k) {
        SoundSequence* s = &sequences[k];
        if (s->active == 0) continue;
        int handle = s->handle;
        if (handle != -1) {
            if (enabled != 0) {
                SoundSlot* slot = SlotForHandle(handle);
                if (slot != nullptr) {
                    if (slot->finished == 0) continue;
                    slot->End(this);
                }
            }
            if (FindDeferred(static_cast<std::uint32_t>(handle)) != nullptr) continue;
        }
        if (s->PlayNext(this) == 0) {
            s->id = -1;
            s->active = 0;
        }
    }

    std::uint32_t now = platform::TickCount();
    if (now - emitterTick < 100) return;
    emitterTick = now;
    IsaacRandom& random = g_clientRandom;
    for (int k = 0; k < 16; ++k) {
        SoundEmitter* e = &emitters[k];
        if (e->active == 0) continue;
        float dx = listener.x - e->position.x;
        float dy = listener.y - e->position.y;
        float dist = std::sqrt(LengthSquared2DUnrounded(dy, dx));
        std::uint32_t minInterval, maxInterval;
        EI_NAN_AUDIT(dist, e->innerRadius);
        if (dist < e->innerRadius) {
            minInterval = static_cast<std::uint32_t>(e->minInterval);
            maxInterval = static_cast<std::uint32_t>(e->maxInterval);
        } else {
            // Out of earshot past outerRadius - 0.001.
            EI_NAN_AUDIT(e->outerRadius);
            if (e->outerRadius - kEmitterMargin < dist) continue;
            float t = (e->outerRadius - e->innerRadius) / (e->outerRadius - dist);
            if (t > kEmitterMaxScale) continue;
            // The original's products while the intervals are below 2^24 ms (exact as floats).
            minInterval = static_cast<std::uint32_t>(lib::RoundEven(e->minInterval * t));
            maxInterval = static_cast<std::uint32_t>(lib::RoundEven(e->maxInterval * t));
        }
        std::uint32_t elapsed = now - e->lastPlayed;
        if (elapsed < minInterval) continue;
        if (elapsed < maxInterval) {
            std::uint32_t span = (maxInterval - minInterval) / 100;
            if (span > 1 && random.Next() % span != 0) continue;
        }
        e->lastPlayed = now;
        std::uint32_t index = 0;
        if (e->names.size() > 1) index = random.Next() % static_cast<std::uint32_t>(e->names.size());
        std::uint32_t panRoll = random.Next();
        std::uint32_t volumeRoll = random.Next();
        Play2D(e->names[index], 1, static_cast<int>(volumeRoll % 51 + 50),
               static_cast<int>(panRoll % 101), 0, 0, 0, 0);
    }
}

// --- Music stream ---------------------------------------------------------------------------------

// Starts a fade of the stream (no lock).
void SoundManager::FadeOutStream() {
    if (enabled == 0) return;
    musicFader.mode = 2;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = lib::FloatFromBits(FadeStepBits());
    musicFader.holdUntil = 0;
}

void SoundManager::FadeInStream() {
    if (enabled == 0) return;
    musicFader.mode = 1;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = lib::FloatFromBits(FadeStepBits());
    musicFader.holdUntil = 0;
}

// Asks the stream thread to switch to `name` (an .mp3 path). `mode` says when:
// 1 after the current file, 2 now with a fade out, 3 at the next marker of the current file,
// 4 at the current buffer position; anything else is ignored. Repeating the playing file only
// cancels a fade out. Returns 0 only when the output has no stream.
int SoundManager::RequestStream(std::string_view name, int mode) {
    if (enabled == 0) return 1;
    if (musicFader.volume == 0) return 1;
    if (streamSample == nullptr) return 0;
    std::lock_guard<std::recursive_mutex> guard(streamLock);
    int m = 4;
    if (streamState != 0) {
        m = mode;
        if (str::Compare(streamName,
                            name) == 0 &&
            !name.empty()) {
            streamState = 1;
            if (switchMode == 2) {
                musicFader.mode = 1;
                musicFader.finished = 0;
                musicFader.step = 0.0f;
                musicFader.step = lib::FloatFromBits(FadeStepBits());
                musicFader.holdUntil = 0;
            }
            return 1;
        }
    }
    if (streamState == 2 &&
        str::Compare(nextName,
                        name) == 0 &&
        switchMode >= m) {
        return 1;
    }
    std::int32_t at;
    switch (m) {
    case 2:
        musicFader.mode = 2;
        musicFader.finished = 0;
        musicFader.step = 0.0f;
        musicFader.step = lib::FloatFromBits(FadeStepBits());
        musicFader.holdUntil = 0;
        at = 0x7FFFFFFF;
        break;
    case 3: {
        int n = lib::Count(markers);
        if (n == 0) {
            m = 1;
            at = 0x7FFFFFFF;
            break;
        }
        at = -1;
        bool found = false;
        if (n > 0) {
            int pos = streamPosition + streamBufferFrames;
            for (int i = 0; i < n; ++i) {
                if (markers[i] >= pos) {
                    at = markers[i];
                    found = at != -1;
                    break;
                }
            }
        }
        if (!found) at = markers[0];
        if (at == -1) {
            return 1;
        }
        break;
    }
    case 4:
        at = streamPosition + streamBufferFrames;
        if (at == -1) {
            return 1;
        }
        break;
    case 1:
        at = 0x7FFFFFFF;
        break;
    default:
        return 1;
    }
    streamState = 2;
    switchMode = m;
    nextName = name;
    streamSwitchAt = at;
    return 1;
}

// Mode 4 stops the stream now (closing the file) and fades the volume back in over 2 s;
// other modes request silence ("") the RequestStream way.
void SoundManager::StopStream(int mode) {
    if (enabled == 0) return;
    std::lock_guard<std::recursive_mutex> guard(streamLock);
    if (streamState != 0) {
        if (mode == 4) {
            if (streamFile != nullptr) {
                platform::audio::CloseMp3(streamFile);
                streamFile = nullptr;
            }
            streamState = 0;
            streamName = game::kEmptyText;
            float step = kStopFadeStep;
            musicFader.mode = 2;
            musicFader.finished = 0;
            musicFader.step = step;
            EI_NAN_AUDIT(step);
            if (step == 0.0f) musicFader.step = lib::FloatFromBits(FadeStepBits());
            musicFader.SetHold(platform::Milliseconds() + 2000);
        } else {
            RequestStream(game::kEmptyText, mode);
        }
    }
}

// --- Stopping everything, wave lengths -------------------------------------------------------

// Loading a game, leaving a zone, the main menu: every voice and deferred sound goes.
void SoundManager::StopAll(bool stopStream) {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) EndSlot(&slots[i]);
    nextHandle = 0;
    DeleteDeferred();
    if (stopStream) StopStream(4);
}

// StopAll's loop (Close's is the same with the map's clear() out of line).
void SoundManager::DeleteDeferred() {
    for (auto& [handle, rec] : deferred)
        if (rec != nullptr) FreeDeferred(rec);
    deferred.clear();
}

float SoundManager::WaveLength(const void* data, std::size_t size) {
    const platform::audio::WaveInfo info = platform::audio::ReadWaveInfo(data, size);
    std::uint32_t samples;
    if (info.format == platform::audio::kImaAdpcm) {
        samples = info.samples;
    } else {
        std::uint32_t bitsPerFrame = static_cast<std::uint32_t>(info.bits * info.channels);
        if (bitsPerFrame == 0) return 0.0f;
        samples = (info.dataLength << 3) / bitsPerFrame;
    }
    return static_cast<float>(static_cast<std::int32_t>(samples)) / static_cast<float>(static_cast<std::int32_t>(info.rate));
}

// --- The volume envelope ----------------------------------------------------------------------------

// Sets the time at which a fade is cancelled; 0 cancels it now.
void SoundFader::SetHold(std::uint32_t until) {
    holdUntil = until;
    if (until != 0) return;
    factor = 1.0f;
    mode = 0;
    finished = 0;
    Recalc();
}

void SoundFader::Recalc() {
    level = ClampLevel(lib::RoundEven(volume * factor * kLevelMax * kPercent));
}

// One step of the fade (the stream thread, every 100 ms, under the stream lock).
void SoundFader::Update() {
    if (holdUntil != 0 && platform::Milliseconds() >= holdUntil) {
        SetHold(0);
        return;
    }
    switch (mode) {
    case 0:
        return;
    case 1:
        factor = step + factor;
        if (factor >= 1.0f) {
            mode = 0;
            factor = 1.0f;
        }
        break;
    case 2:
        factor = factor - step;
        EI_NAN_AUDIT(factor);
        if (factor <= 0.0f) {
            mode = 0;
            factor = 0.0f;
            finished = 1;
        }
        break;
    default:
        break;
    }
    Recalc();
}

// --- A voice for a new sound -----------------------------------------------------

int SoundManager::AllocateVoice(int priority) {
    int lowest = 0x7FFFFFFF;
    int index = 0;
    for (int i = 0; i < kVoiceCount; ++i) {
        if (slots[i].playing == 0) return i;
        if (slots[i].priority < lowest) {
            lowest = slots[i].priority;
            index = i;
        }
    }
    if (priority <= lowest) return kNoVoice;
    SoundSlot* slot = &slots[index];
    if (slot->looping != 0) {
        // The stolen looping sound waits in the deferred map with its state and sample position.
        auto* rec = new DeferredSound;
        rec->name = slot->name;
        rec->volume = slot->volume;
        rec->baseVolume = slot->baseVolume;
        rec->pan = slot->pan;
        rec->priority = slot->priority;
        rec->speech = slot->speech;
        rec->length = slot->length;
        rec->flat = slot->flat;
        rec->heightFade = slot->heightFade;
        rec->position = slot->position;
        rec->nearDistance = slot->nearDistance;
        rec->farDistance = slot->farDistance;
        rec->handle = slot->handle;
        rec->samplePosition = platform::audio::Position(slot->voice);
        SetDeferred(deferred, rec->handle, rec);
    }
    if (slot->playing != 0) EndSlot(slot);
    return index;
}

// --- Play2D, Play3D ---------------------------------------------------------

// Plays `name` without position: `volume` 0..100 (scaled by the effects or speech volume,
// and with `heightFade` by the camera height), `pan` -100..100, `loop` endlessly, `compressed` an
// MP3 to decompress (handed to the wave loader). Returns the sound handle, or -1 (sound off, no voice for a one-shot
// sound, silent one-shot, or the wave did not load).
int SoundManager::Play2D(std::string_view name, int priority, int volume, int pan, int loop, int heightFade, int speech,
                          int compressed) {
    if (enabled == 0) return -1;
    int index = AllocateVoice(priority);
    if (index == -1) {
        if (loop == 0) return -1;
        return DeferLooping(this, name, priority, volume, pan, heightFade, speech);
    }
    SoundSlot* slot = &slots[index];
    platform::audio::ResetVoice(slot->voice);
    slot->looping = loop;
    slot->baseVolume = volume;
    slot->pan = pan;
    slot->priority = priority;
    slot->flat = 1;
    slot->heightFade = heightFade;
    slot->speech = speech;
    slot->volume = FlatVolume(volume, heightFade, speech);
    if (volume == 0 && loop == 0) return -1;
    if (LoadSample(slot, name, compressed) == 0) return -1;
    return StartLoaded(this, slot, loop);
}

// Plays `name` at `position`: full volume inside `nearDist`, silent beyond `farDist`, the pan from the
// listener (UpdatePositional). Returns the handle or -1 as Play2D. A looping sound without a voice is
// deferred; the record's volume, base volume, pan and length are left as operator new returned them.
int SoundManager::Play3D(std::string_view name, int priority, const float* position, float nearDist, float farDist, int loop) {
    if (enabled == 0) return -1;
    int index = AllocateVoice(priority);
    if (index == -1) {
        if (loop == 0) return -1;
        auto* rec = new DeferredSound;
        rec->name.assign(name);
        lib::LoadBytes(rec->position, position);
        rec->nearDistance = nearDist;
        rec->farDistance = farDist;
        rec->flat = 0;
        rec->heightFade = 0;
        rec->speech = 0;
        rec->priority = priority;
        rec->handle = nextHandle;
        ++nextHandle;
        rec->samplePosition = 0;
        SetDeferred(deferred, rec->handle, rec);
        return rec->handle;
    }
    SoundSlot* slot = &slots[index];
    platform::audio::ResetVoice(slot->voice);
    slot->looping = loop;
    lib::LoadBytes(slot->position, position);
    slot->nearDistance = nearDist;
    slot->farDistance = farDist;
    slot->flat = 0;
    slot->heightFade = 0;
    slot->speech = 0;
    slot->priority = priority;
    UpdatePositional(slot);
    if (slot->volume == 0 && loop == 0) return -1;
    if (LoadSample(slot, name, 0) == 0) return -1;
    return StartLoaded(this, slot, loop);
}

// Gives the voice wave `name`: a cached copy (unless `compressed`, which then leaves the
// voice without a new wave), or the file from the first archive that has it, decoded
// from MP3 when `compressed` (kept at the cache's old end), else copied after
// making room (kept at its front). Sets the voice's length, name, playing flag and a new handle.
// Returns 0 when no archive has the file or the decoding fails.
int SoundManager::LoadSample(SoundSlot* slot, std::string_view name, int compressed) {
    std::string key(name);    // freed on each return
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        if (compressed == 0) UseCached(this, slot, cached->second);
    } else {
        MappedResource* source = nullptr;
        void* data = nullptr;
        std::uint32_t fileSize = 0;
        for (auto n = waveSources.begin();;) {
            if (n == waveSources.end()) return 0;
            auto current = n;
            ++n;
            source = *current;
            data = source->GetFile(name, &fileSize);
            if (data != nullptr) break;
        }
        int size = static_cast<int>(fileSize);
        std::uint32_t now = platform::TickCount();
        WaveRecord record;
        if (compressed != 0) {
            const bool ok = platform::audio::DecodeMp3File(data, fileSize, &record.data);
            source->CloseFile(data);
            if (!ok) return 0;
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
        if (compressed == 0) {
            cacheList.push_front(std::move(record));
            node = cacheList.begin();
        } else {
            cacheList.push_back(std::move(record));
            node = std::prev(cacheList.end());
        }
        cache[key] = node;
        platform::audio::SetWave(slot->voice, node->data.data(), node->data.size());
        slot->length = WaveLength(node->data.data(), node->data.size());
    }
    slot->name = key;
    slot->playing = 1;
    slot->handle = nextHandle;
    ++nextHandle;
    return 1;
}

void SoundManager::StreamNotify() {}

// --- The engine object's virtuals, the wave sources and sound folder, the music stream's file reading,
// the lifetime and the output's setup Open / Close / Reset.

namespace {
constexpr const char* kSoundManagerClassNameAddress = "CSoundManager";
const char* const kBackslash = "\\";

constexpr int kListBlockSize = 10;
constexpr int kFullVolume = 100;
constexpr int kNoSwitch = 4;   // RequestStream's "switch at the current position"

void ConstructFader(SoundFader& fader) {
    fader.volume = kFullVolume;
    fader.Recalc();
    fader.SetHold(0);
}

constexpr unsigned kMaxRate = 44100;
constexpr int kStreamThreadPriority = 15;   // THREAD_PRIORITY_TIME_CRITICAL

constexpr int kStreamRate = 44100;
constexpr int kStreamChannels = 2;
constexpr int kStreamBits = 16;

// The stream thread's (StreamThread).
constexpr std::uint32_t kStreamPeriodMs = 100;
constexpr int kNoHalfReady = -1;

constexpr std::uint32_t kSwitchHoldMs = 4000;
constexpr int kNoLoop = 0x7FFFFFFF;
constexpr int kNeverSwitch = -1;
constexpr int kFullLevel = 0x7F;
constexpr int kCentrePan = 0x40;
constexpr std::uint8_t kSilence8 = 0x7F;
constexpr int kStateSwitching = 2;
constexpr int kSwitchAtEnd = 1;        // RequestStream's modes
constexpr int kSwitchAfterFade = 2;
const char* const kLoopMarkers = "Markers2";
const char* const kSwitchMarkers = "Markers1";

void CodecClose(SoundManager* m) {
    if (m->streamFile == nullptr) return;
    platform::audio::CloseMp3(m->streamFile);
    m->streamFile = nullptr;
}
void Silence(SoundManager* m, std::uint8_t* to, int bytes) {
    std::memset(to, m->streamSilence, static_cast<std::uint32_t>(bytes));
}
// `bytes` of the file; what it does not give is silence.
void ReadOrSilence(SoundManager* m, std::uint8_t* to, int bytes) {
    int got = platform::audio::DecodeMp3(m->streamFile, to, bytes);
    if (got < bytes) Silence(m, to + got, bytes - got);
}

// The next file takes over: `bytes` of it (or silence) to `to`.
void SwitchStream(SoundManager* m, std::uint8_t* to, int bytes) {
    m->streamReadPosition = 0;
    m->streamState = 0;
    // Rebirth's StreamPatch: the mod's Stream\ folder when it has the file.
    const bool modStream = ei::rebirth::Enabled(ei::rebirth::Patch::StreamPatch) && modfolders::StreamInMod(m->nextName);
    std::string path = (modStream ? modfolders::StreamFolder() : m->soundPath) + m->nextName;
    CodecClose(m);
    if (str::Compare(m->nextName, game::kEmptyText) != 0 && m->streamReader.OpenFile(path, &m->streamFileSize) != 0)
        m->streamFile = platform::audio::OpenMp3(SoundStreamRead, m);
    if (m->streamFile == nullptr) {
        Silence(m, to, bytes);
        m->streamState = 0;
        {
            std::lock_guard<std::recursive_mutex> guard(m->nameLock);
            m->streamName = game::kEmptyText;
        }
        return;
    }
    m->streamState = 1;
    {
        std::lock_guard<std::recursive_mutex> guard(m->nameLock);
        m->streamName = m->nextName;
    }
    m->soundRegistry.GetIntArray(m->streamName.c_str(), kSwitchMarkers, &m->markers);
    m->soundRegistry.GetIntArray(m->streamName.c_str(), kLoopMarkers, &m->markers2);
    m->streamLoopAt = !m->markers2.empty() ? m->markers2[0] : kNoLoop;
    m->streamSwitchAt = kNeverSwitch;
    const unsigned rate = platform::audio::Mp3Rate(m->streamFile);
    const int channels = platform::audio::Mp3Channels(m->streamFile);
    const int bits = platform::audio::Mp3Bits(m->streamFile);
    // 16 bits are signed, 8 bits unsigned (silence at 0x7F).
    m->streamSilence = bits == 16 ? 0 : kSilence8;
    platform::audio::SetStreamFormat(m->streamSample, rate, channels, bits);
    platform::audio::SetStreamVolume(m->streamSample, kFullLevel);
    platform::audio::SetStreamPan(m->streamSample, kCentrePan);
    m->streamFrameBytes = static_cast<std::int32_t>(static_cast<std::uint32_t>(bits * channels) >> 3);
    m->streamBufferFrames = m->streamBufferSize / m->streamFrameBytes;
    m->streamPosition = 0;
    ReadOrSilence(m, to, bytes);
}

} // namespace

const char* SoundManager::ClassName() { return kSoundManagerClassNameAddress; }
ClassId SoundManager::GetClassId() { return ClassId::SoundManager; }

void SoundManager::Create() {
    Reset();
}

// Every archive deleted, the first first.
void SoundManager::ClearWaveSources() {
    for (MappedResource* source : waveSources) delete source;
    waveSources.clear();
}

// Called at startup for the two sound archives. A failed allocation is not tested (the open call goes
// through a null object).
int SoundManager::AddWaveSource(const std::string& path) {
    if (enabled == 0) return 1;
    auto* source = new MappedResource();
    if (source->OpenResource(path) == 0) {
        delete source;
        return 0;
    }
    waveSources.push_back(source);
    return 1;
}

// From the map's ambient sounds.
void* SoundManager::LoadWaveFile(const char* name, std::uint32_t* size, MappedResource** source) {
    for (auto node = waveSources.begin(); node != waveSources.end();) {
        MappedResource* current = *node;
        ++node;
        void* data = current->GetFile(name, size);
        if (data != nullptr) {
            if (source != nullptr) *source = current;
            return data;
        }
    }
    return nullptr;
}

// Rebirth redirects its ListFiles call; this is the vanilla body. From the unit database's sound counts, on
// the game's main thread.
std::vector<std::string> SoundManager::ListWaveFiles(const char* pattern) {
    std::vector<std::string> waves;
    std::vector<std::string> names;
    for (auto node = waveSources.begin(); node != waveSources.end();) {
        MappedResource* source = *node;
        ++node;
        source->ListFiles(&names, pattern);
        waves.insert(waves.end(), names.begin(), names.end());
    }
    return waves;
}

// Called at startup.
int SoundManager::SetPath(std::string_view folder, const std::string& registryPath) {
    if (enabled == 0) return 1;
    soundPath = folder;
    bool missing;
    {
        std::string last = str::Right(soundPath, 1);
        missing = str::Compare(last, kBackslash) != 0;
    }
    if (missing) soundPath += kBackslash;
    return soundRegistry.Open(registryPath) != 0 ? 1 : 0;
}

std::string SoundManager::CurrentStreamName() {
    std::lock_guard<std::recursive_mutex> guard(nameLock);
    std::string name;
    if (streamState == 1 || streamState == 2) name = streamName;
    else name = game::kEmptyText;
    return name;
}

int SoundStreamRead(void* user, char* buffer, int size, int offset) {
    auto* manager = static_cast<SoundManager*>(user);
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

// --- Constructor, destructor, stream reset ---------------------------------------------------

// The stream reader, the strings and the archive are members the compiler constructs before the body.
SoundManager::SoundManager() {
    musicFader.volume = kFullVolume;
    musicFader.Recalc();
    musicFader.SetHold(0);
    Reset();
}

// The archive, the strings and the stream reader are destroyed after the body.
SoundManager::~SoundManager() {
    Close();
    cacheList.clear();
    waveSources.clear();
}

void SoundManager::ResetStream() {
    streamState = 0;
    streamSample = nullptr;
    streamFile = nullptr;
    for (std::vector<std::uint8_t>& buffer : streamBuffers) buffer.clear();
    streamBufferSize = -1;
    streamSpareBuffer = -1;
    streamHalfBuffer[1] = -1;
    streamHalfBuffer[0] = -1;
    streamPosition = -1;
    streamSwitchAt = -1;
    streamLoopAt = -1;
    streamFrameBytes = -1;
    streamSilence = 0;
    streamName = game::kEmptyText;
    nextName = game::kEmptyText;
    streamReadPosition = 0;
    streamFileSize = 0;
    switchMode = kNoSwitch;
    ConstructFader(musicFader);
}

// --- Open, Close, Reset ------------------------------------------------------------------

int SoundManager::CloseStream() {
    if (streamFile != nullptr) platform::audio::CloseMp3(streamFile);
    if (streamSample != nullptr) platform::audio::DestroyStream(streamSample);
    ResetStream();
    return 0;
}

int SoundManager::OpenStream() {
    streamSample = platform::audio::CreateStream();
    if (streamSample == nullptr) return CloseStream();

    streamBufferSize = platform::audio::MinimumHalfBytes(kStreamRate, kStreamChannels, kStreamBits) * 2;
    for (std::vector<std::uint8_t>& buffer : streamBuffers) buffer.assign(static_cast<std::size_t>(streamBufferSize), 0);
    platform::audio::ResetStream(streamSample);
    platform::audio::SetStreamFormat(streamSample, kStreamRate, kStreamChannels, kStreamBits);
    platform::audio::SetStreamVolume(streamSample, 0);
    platform::audio::SetStreamPan(streamSample, 0);
    streamHalfBuffer[0] = 0;
    streamHalfBuffer[1] = 1;
    streamSpareBuffer = 2;
    streamPosition = 0;
    streamState = 0;
    // No fade and no hold; the level from the music volume.
    musicFader.SetHold(0);
    return 1;
}

void SoundManager::StreamThread() {
    std::uint32_t last = platform::Milliseconds();
    while (threadsQuit == 0) {
        std::uint32_t now = platform::Milliseconds();
        std::uint32_t elapsed = now - last;
        last = now;
        if (elapsed < kStreamPeriodMs) platform::Sleep(kStreamPeriodMs - elapsed);

        streamLock.lock();
        musicFader.Update();
        platform::audio::SetStreamVolume(streamSample, musicFader.level);
        for (;;) {
            int half = platform::audio::ReadyHalf(streamSample);
            if (half == kNoHalfReady) break;
            // The spare buffer goes into that half; the one it played becomes the spare.
            int buffer = streamSpareBuffer;
            std::int32_t& playing = streamHalfBuffer[half == 0 ? 0 : 1];
            streamSpareBuffer = playing;
            playing = buffer;
            streamPosition += streamBufferFrames;
            platform::audio::QueueHalf(streamSample, half, streamBuffers[buffer].data(), streamBufferSize);
            ReadStreamVanilla();
        }
        streamLock.unlock();
    }
}

// The stream thread's procedure: the manager's StreamThread, then 0.
unsigned StreamThreadEntry(void* manager) {
    auto* self = static_cast<SoundManager*>(manager);
    self->StreamThread();
    self->streamStopped.Set();
    return 0;
}

// --- The stream's reader -----------------------------------------------------------------

// The game leaves "from the beginning again" (a local) unwritten when neither a loop point nor a switch
// point is in the buffer, and reads it in one such case: a switch at the file's end (mode 1) when the file
// ends in this buffer. Here that case is the switch, which is what the mode asks for.
void SoundManager::ReadStreamVanilla() {
    std::uint8_t* const buffer = streamBuffers[streamSpareBuffer].data();
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
        musicFader.SetHold(platform::Milliseconds() + kSwitchHoldMs);
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
        int got = platform::audio::DecodeMp3(streamFile, buffer, currentBytes);
        if (got < currentBytes) {
            if (streamState == kStateSwitching && switchMode == kSwitchAtEnd) {
                // The file ended: the next one from here.
                currentBytes = got;
                restBytes = streamBufferSize - got;
            } else {
                platform::audio::RewindMp3(streamFile);
                streamPosition = 0;
                ReadOrSilence(this, buffer + got, currentBytes - got);
            }
        }
    }
    if (restBytes == 0) return;
    if (again) {
        platform::audio::RewindMp3(streamFile);
        streamPosition = 0;
        ReadOrSilence(this, buffer + currentBytes, restBytes);
        return;
    }
    SwitchStream(this, buffer + currentBytes, restBytes);
}

void SoundEndOfSample(void* slot) { static_cast<SoundSlot*>(slot)->finished = 1; }

int SoundManager::Open(void* window, int useReverb, unsigned rate) {
    Destroy();   // Close
    reverbOn = useReverb;
    if (rate >= kMaxRate) rate = kMaxRate;
    std::string error;
    if (!platform::audio::OpenOutput(window, rate, useReverb != 0, &error)) {
        game::DebugLog("%s\n", error.c_str());
        return 0;
    }
    if (OpenStream() != 0) {
        streamStopped.Create();
        streamThread.Start(StreamThreadEntry, this, kStreamThreadPriority);
    }
    slots = std::make_unique<SoundSlot[]>(kVoiceCount);
    for (int i = 0; i < kVoiceCount; ++i) {
        SoundSlot& slot = slots[i];
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
        slot.voice = platform::audio::CreateVoice();
    }
    enabled = 1;
    return 1;
}

void SoundManager::Close() {
    if (streamThread.Started()) {
        threadsQuit = 1;
        streamStopped.Wait();
        streamThread.Close();
        streamStopped.Close();
    }
    CloseStream();
    if (slots != nullptr) {
        for (int i = 0; i < kVoiceCount; ++i) {
            if (slots[i].playing != 0) EndSlot(&slots[i]);
            if (slots[i].voice != nullptr) platform::audio::DestroyVoice(slots[i].voice);
        }
        slots.reset();
    }
    DeleteDeferred();
    cacheList.clear();
    cache.clear();
    if (enabled != 0) platform::audio::CloseOutput();
    ClearWaveSources();
    streamReader.Close();
    soundRegistry.Close();
    Reset();
}

void SoundManager::Reset() {
    ResetStream();
    streamThread.Close();
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
    speechVolume = kFullVolume;
    effectsVolume = kFullVolume;
    enabled = 0;
    spare = 0;
    reverbOn = 0;
    panDirect = 0;
    reverb[0] = reverb[1] = reverb[2] = 0.0f;
    soundPath = game::kEmptyText;
    randomStart = 0;
}
