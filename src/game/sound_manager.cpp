// The sound manager (CSoundManager): voice volumes and pans from the listener, the per-handle controls,
// ambient emitters, 3D sound sequences, the per-frame tick and the music stream requests, and Play2D /
// Play3D. Floating point follows the x87 order (the goldens depend on it): `double` for values kept on the
// FPU stack, static_cast<float> where it stores to a float.
#include "sound.h"
#include "game_strings.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "../lib/vecmath.h"
#include "random.h"

#include "ei/x87.h"

#include <concepts>
#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include "lib/nan_audit.h"
#include "lib/str.h"

namespace {

inline double D(float f) { return f; }
inline double I(std::int32_t v) { return v; }

float FloatBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatBits(T) = delete;

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

constexpr float kHeightFull = 30.0f;     // 2D height-faded sounds are full below
constexpr float kHeightSilent = 70.0f;   // ... and silent above
constexpr float kOne = 1.0f;
constexpr float kHundred = 100.0f;
constexpr float kMilesScale = std::bit_cast<float>(0x3C5013A9u);  // 0.0127
constexpr float kLevelMax = 127.0f;
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);     // 0.01
constexpr float kEmitterMargin = std::bit_cast<float>(0x3A83126Fu);  // 0.001
constexpr float kEmitterMaxScale = 255.0f;

// pi and pi / 2 (set by a static initialiser).
float Pi() { return 3.1415927f; }
constexpr float kHalfPi = /*007C8808*/ 1.5707964f;
// The fade step every fade request uses (1/96).
std::uint32_t FadeStepBits() { return 0x3C2AAAABu; }
// StopStream's fade-out step, loaded and tested as a float.
float StopFadeStep() { return 0.010416667f; }

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
double HeightFactor(float h) {
    return D(kOne) - (D(h) - kHeightFull) / (D(kHeightSilent) - kHeightFull);
}

// The voice loop of the volume setters and SetListener.
void RefreshVoices(CSoundManager* m) {
    SoundSlot* slot = m->slots;
    for (int i = 0; i < 15; ++i, ++slot) {
        if (slot->playing == 0) continue;
        if (slot->flat == 0) m->UpdatePositional(slot);
        else slot->volume = m->FlatVolume(slot->baseVolume, slot->heightFade, slot->speech);
        m->ApplyVolumePan(slot);
    }
}

// Deletes the deferred record of `handle`, if any (StopSound, RemoveSequence).
void DropDeferred(CSoundManager* m, int handle) {
    auto node = m->deferred.find(handle);
    if (node == m->deferred.end()) return;
    DeferredSound* rec = node->second;
    if (rec == nullptr) return;
    m->deferred.erase(rec->handle);
    FreeDeferred(rec);
}

// Emitter `index` as the game addresses it: index 16 is accepted (one past the array).
SoundEmitter* EmitterAt(CSoundManager* m, int index) {
    return m->emitters + index;
}

SoundSequence* SequenceForId(CSoundManager* m, std::uint32_t id) {
    SoundSequence* s = &m->sequences[id & 0xFF];
    return static_cast<std::uint32_t>(s->id) == id ? s : nullptr;
}

// Moves a sequence's current sound (MoveSequence / UpdateSequence).
void MoveSequenceSound(CSoundManager* m, int handle, const float* position, float nearDist, float farDist) {
    SoundSlot* slot = m->SlotForHandle(handle);
    if (slot != nullptr) {
        if (slot->finished != 0) {
            if (slot->playing != 0) m->EndSlot(slot);
        } else {
            std::memcpy(&slot->position, position, sizeof(slot->position));
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
    std::memcpy(&rec->position, position, sizeof(rec->position));
    rec->flat = 0;
    rec->nearDistance = nearDist;
    rec->farDistance = farDist;
}

constexpr std::int32_t kImaAdpcm = 0x11;

constexpr int kVoiceCount = 15;
constexpr int kNoVoice = -1;

// The level of a new 2D voice: as FlatVolume, except that a NaN camera height keeps the full level (the
// test is "below 70 or unordered" first, then "above 30").
int NewFlatLevel(const CSoundManager* m, int volume, int heightFade, int speech) {
    double v = volume;
    if (heightFade != 0) {
        float h = m->listener.z;
        if (h >= kHeightSilent) v = 0.0;
        else if (h > kHeightFull) v = v * HeightFactor(h);
    }
    v = v * I(speech != 0 ? m->speechVolume : m->effectsVolume);
    v = v * kMilesScale;
    return ClampLevel(x87::fistp(static_cast<float>(v)));
}

// No voice for a looping sound: it waits in the deferred map (silent, flat) until Tick restarts it.
int DeferLooping(CSoundManager* m, std::string_view name, int priority, int volume, int pan, int heightFade, int speech) {
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
int StartLoaded(CSoundManager* m, SoundSlot* slot, int loop) {
    if (loop != 0) gameapi::AIL_set_sample_loop_count(slot->sample, 0);
    m->ApplyVolumePan(slot);
    gameapi::AIL_register_EOS_callback(slot->sample, SoundEndOfSample);
    if (m->reverbOn != 0) gameapi::AIL_set_sample_reverb(slot->sample, m->reverb[0], m->reverb[1], m->reverb[2]);
    if (m->randomStart != 0) {
        std::uint32_t range = static_cast<std::uint32_t>(m->randomStart) + 1;
        gameapi::AIL_set_sample_ms_position(slot->sample, g_random.Next() % range);
    }
    gameapi::AIL_start_sample(slot->sample);
    return slot->handle;
}

void* DecompressCallback() { return reinterpret_cast<void*>(SoundDecompressCallback); }
const char* Mp3Suffix() { return ".mp3"; }
constexpr std::uint32_t kCacheBudget = 0x800000;   // bytes kept unless a wave is still in use
constexpr std::uint32_t kCacheKeep = 60000;        // ms a wave stays cached after its last use

// A cached wave: moved to the front with one more use and the time of now, then given to the voice.
void UseCached(CSoundManager* m, SoundSlot* slot, const std::string& name, std::list<WaveRecord>::iterator node) {
    m->cacheList.splice(m->cacheList.begin(), m->cacheList, node);
    WaveRecord& record = *node;
    record.lastUsed = gameapi::GetTickCount();
    record.uses = record.uses + 1;
    slot->length = static_cast<float>(m->WaveLength(record.data, record.size));
    gameapi::AIL_set_named_sample_file(slot->sample, name.c_str(), record.data, record.size, 0);
}

// Drops unused waves from the least recently used end until the new one fits the budget or the
// next one was used within the last minute.
void Evict(CSoundManager* m, int incoming, std::uint32_t now) {
    std::list<WaveRecord>& list = m->cacheList;
    for (auto after = list.end(); after != list.begin();) {
        auto node = std::prev(after);
        const WaveRecord& record = *node;
        if (static_cast<std::uint32_t>(incoming + m->cacheBytes) < kCacheBudget && now - record.lastUsed < kCacheKeep) return;
        if (record.uses > 0) {
            after = node;
            continue;
        }
        gameapi::AIL_mem_free_lock(record.data);
        m->cacheBytes -= record.size;
        m->cache.erase(record.name);
        list.erase(node);
    }
}

} // namespace

// --- Shared pieces ------------------------------------------------------------------------------

// The volume of a 2D voice (0..127), inlined in every volume setter.
int CSoundManager::FlatVolume(int level, int fade, int isSpeech) const {
    double v = level;
    if (fade != 0) {
        float h = listener.z;
        EI_NAN_AUDIT(h);
        if (h >= kHeightSilent) v = 0.0;
        else if (h > kHeightFull) v = v * HeightFactor(h);
    }
    v = v * (isSpeech != 0 ? speechVolume : effectsVolume);
    v = v * kMilesScale;
    return ClampLevel(x87::fistp(static_cast<float>(v)));
}

// Hands the slot's volume and pan to Miles; the pan is mirrored unless panDirect.
void CSoundManager::ApplyVolumePan(SoundSlot* slot) const {
    int direct = panDirect;
    gameapi::AIL_set_sample_volume(slot->sample, slot->volume);
    int p = slot->pan;
    if (direct == 0) p = -p;
    gameapi::AIL_set_sample_pan(slot->sample, (p + 100) * 0x7F / 200);
}

// Ends a voice: releases its cached wave, stops the sample and frees the slot.
void CSoundManager::EndSlot(SoundSlot* slot) {
    --CacheEntry(cache, slot)->uses;
    gameapi::AIL_end_sample(slot->sample);
    slot->playing = 0;
    slot->finished = 0;
    slot->handle = -1;
}

// The first voice whose handle matches (free voices hold -1), or null.
SoundSlot* CSoundManager::SlotForHandle(int handle) const {
    for (int i = 0; i < 15; ++i)
        if (slots[i].handle == handle) return &slots[i];
    return nullptr;
}

// --- Volumes, listener ----------------------------------------------------------------------------

// A 3D voice's volume and pan from the listener: full inside nearDistance, falling to 0
// at farDistance, and the camera height fade; the pan from the angle between the listener axis and
// the direction to the sound (0 ahead or behind, +-100 to the sides).
void CSoundManager::UpdatePositional(SoundSlot* slot) {
    float dx = static_cast<float>(D(slot->position.x) - listener.x);
    double dyRaw = D(slot->position.y) - listener.y;
    float dy = static_cast<float>(dyRaw);
    double dist2 = dyRaw * dy + D(dx) * dx;
    double farDist = slot->farDistance;
    if (dist2 > farDist * farDist) {
        slot->volume = 0;
        return;
    }
    double dist = std::sqrt(dist2);
    float distF = static_cast<float>(dist);
    float h = listener.z;
    int level;
    EI_NAN_AUDIT(dist, slot->nearDistance, h);
    if (dist < slot->nearDistance) {
        double v = kHundred;
        if (h >= kHeightSilent) v = 0.0;
        else if (h > kHeightFull) v = HeightFactor(h) * kHundred;
        v = v * (slot->speech != 0 ? speechVolume : effectsVolume);
        v = v * kMilesScale;
        level = ClampLevel(x87::fistp(static_cast<float>(v)));
    } else {
        double t = (D(distF) - slot->nearDistance) / (D(slot->farDistance) - slot->nearDistance) * kHundred;
        int falloff = 100 - x87::fistp(static_cast<float>(t));
        slot->volume = falloff;
        double v = falloff;
        if (h >= kHeightSilent) v = 0.0;
        else if (h > kHeightFull) v = v * HeightFactor(h);
        v = v * (slot->speech != 0 ? speechVolume : effectsVolume);
        v = v * kMilesScale;
        level = ClampLevel(x87::fistp(static_cast<float>(v)));
    }
    slot->volume = level;

    float ahead = AbsBits(static_cast<float>(Dot2DUnrounded<double>(dy, dx, listenerAxis.y, listenerAxis.x)));
    if (ahead > distF) {
        slot->pan = 0;
        return;
    }
    EI_NAN_AUDIT(distF);
    if (distF == 0.0f) {
        slot->pan = 0;
        return;
    }
    double angle = libcrt::CiAcos(D(ahead) / distF);
    if (angle > kHalfPi) angle = D(Pi()) - angle;
    double scale = static_cast<float>(D(kHundred) / kHalfPi);   // the division at the main thread's 24 bits
    int pan = x87::fistp(static_cast<float>(scale * angle));
    slot->pan = pan;
    if (D(dy) * listenerAxis.x > D(dx) * listenerAxis.y) slot->pan = -pan;
}

// Options: effects volume 0..100.
void CSoundManager::SetEffectsVolume(int volume) {
    if (enabled == 0) return;
    effectsVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}

// Options: speech volume 0..100.
void CSoundManager::SetSpeechVolume(int volume) {
    if (enabled == 0) return;
    speechVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}

// The camera's position and direction (CCamera::Update every frame).
void CSoundManager::SetListener(const float* position, const float* axis) {
    if (enabled == 0) return;
    std::memcpy(&listener, position, sizeof(listener));
    std::memcpy(&listenerAxis, axis, sizeof(listenerAxis));
    double len = std::sqrt(LengthSquared2DUnrounded<double>(listenerAxis.x, listenerAxis.y));
    EI_NAN_AUDIT(len);
    if (len == 0.0) {
        listenerAxis.x = 1.0f;
        listenerAxis.y = 0.0f;
        listenerAxis.z = 0.0f;
    } else {
        double inv = D(kOne) / len;
        listenerAxis.x = static_cast<float>(inv * listenerAxis.x);
        listenerAxis.y = static_cast<float>(inv * listenerAxis.y);
        listenerAxis.z = static_cast<float>(inv * listenerAxis.z);
    }
    RefreshVoices(this);
}

// Options: music volume 0..100 (0 stops the stream).
void CSoundManager::SetMusicVolume(int volume) {
    int v = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (enabled == 0) return;
    musicFader.volume = v;
    int level = x87::fistp(static_cast<float>(I(v) * musicFader.factor * kLevelMax * kPercent));
    musicFader.level = level;
    level = level > 0x7F ? 0x7F : (level < 0 ? 0 : level);
    musicFader.level = level;
    gameapi::AIL_set_sample_volume(streamSample, level);
    if (v == 0) StopStream(4);
}

// Reverb parameters for new voices (used when reverbOn).
void CSoundManager::SetReverb(float room, float level, float time) {
    std::memcpy(&reverb[0], &room, 4);
    std::memcpy(&reverb[1], &level, 4);
    std::memcpy(&reverb[2], &time, 4);
}

// The game window lost / regained focus.
void CSoundManager::PauseAll() {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) gameapi::AIL_stop_sample(slots[i].sample);
    if (enabled != 0 && streamSample != nullptr) gameapi::AIL_stop_sample(streamSample);
}

void CSoundManager::ResumeAll() {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) gameapi::AIL_resume_sample(slots[i].sample);
    if (enabled != 0 && streamSample != nullptr) gameapi::AIL_resume_sample(streamSample);
}

// --- Per-handle controls --------------------------------------------------------------------------

// The voice playing `handle`; a voice whose sample ended is freed and gives null.
SoundSlot* CSoundManager::FindPlaying(int handle) {
    if (enabled == 0) return nullptr;
    SoundSlot* slot = SlotForHandle(handle);
    if (slot == nullptr) return nullptr;
    if (slot->finished == 0) return slot;
    if (slot->playing != 0) EndSlot(slot);
    return nullptr;
}

// The deferred record of `handle`, or null.
DeferredSound* CSoundManager::FindDeferred(std::uint32_t handle) {
    if (enabled == 0) return nullptr;
    auto node = deferred.find(static_cast<std::int32_t>(handle));
    return node != deferred.end() ? node->second : nullptr;
}

// Makes a sound 2D with the given volume (0..100) and pan.
int CSoundManager::SetSoundVolume(int handle, int volume, int pan) {
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
int CSoundManager::SetSoundPosition(int handle, const float* position, float nearDist, float farDist) {
    if (enabled != 0) {
        SoundSlot* slot = SlotForHandle(handle);
        if (slot != nullptr) {
            if (slot->finished != 0) {
                if (slot->playing != 0) EndSlot(slot);
            } else {
                std::memcpy(&slot->position, position, sizeof(slot->position));
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
            std::memcpy(&rec->position, position, sizeof(rec->position));
            rec->flat = 0;
            rec->nearDistance = nearDist;
            rec->farDistance = farDist;
            return 1;
        }
    }
    return 0;
}

// Stops a sound (or forgets its deferred record); always -1, the "no sound" handle.
int CSoundManager::StopSound(int handle) {
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

void CSoundManager::MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                                int maxInterval) {
    if (index < 0 || index > 16) return;
    SoundEmitter* e = EmitterAt(this, index);
    if (e->active == 0) return;
    std::memcpy(&e->position, position, sizeof(e->position));
    e->innerRadius = inner;
    e->outerRadius = outer;
    e->minInterval = minInterval;
    e->maxInterval = maxInterval;
}

// Returns the emitter index or -1.
int CSoundManager::AddEmitter(const std::vector<std::string>* names, const float* position, float inner, float outer,
                              int minInterval, int maxInterval) {
    if (enabled == 0 || names->empty()) return -1;
    int i = 0;
    while (i < 16 && emitters[i].active != 0) ++i;
    if (i == 16) return -1;
    SoundEmitter* e = &emitters[i];
    e->active = 1;
    e->names = *names;
    MoveEmitter(i, position, inner, outer, minInterval, maxInterval);
    e->lastPlayed = gameapi::GetTickCount();
    return i;
}

// Always -1. The name list is kept.
int CSoundManager::RemoveEmitter(int index) {
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
int SoundSequence::PlayNext(CSoundManager* manager) {
    if (chooser != nullptr) {
        int played = remaining;
        ++remaining;
        int next = chooser(index, played);
        index = next;
        if (next < 0) return 0;
        index = next & 0xBFFFFFFF;
        int h = manager->Play3D(names[next & 0xBFFFFFFF], priority, &position.x, nearDistance, farDistance,
                                next & 0x40000000, 0);
        handle = h;
        return h != -1 ? 1 : 0;
    }
    if (remaining == 0) return 0;
    if (random != 0) index = static_cast<std::int32_t>(g_random.Next() % static_cast<std::uint32_t>(names.size()));
    int h = manager->Play3D(names[index], priority, &position.x, nearDistance, farDistance, 0, 0);
    handle = h;
    if (h == -1) return 0;
    index = (index + 1) % static_cast<int>(names.size());
    if (remaining > 0) --remaining;
    return 1;
}

// Returns the sequence id (generation << 8 | slot) or -1.
int CSoundManager::AddSequence(const std::vector<std::string>* names, int priority, const float* position, float nearDist,
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
    std::memcpy(&s->position, position, sizeof(s->position));
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

int CSoundManager::MoveSequence(std::uint32_t id, const float* position) {
    if (enabled == 0 || (id & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, id);
    if (s == nullptr) return 0;
    std::memcpy(&s->position, position, sizeof(s->position));
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, s->nearDistance, s->farDistance);
    return 1;
}

int CSoundManager::UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count) {
    if (enabled == 0 || (id & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, id);
    if (s == nullptr) return 0;
    std::memcpy(&s->position, position, sizeof(s->position));
    s->nearDistance = nearDist;
    s->farDistance = farDist;
    s->remaining = count;
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, nearDist, farDist);
    return 1;
}

// Always -1.
int CSoundManager::RemoveSequence(std::uint32_t id) {
    if (enabled == 0 || (id & 0xFF) >= 8) return -1;
    SoundSequence* s = SequenceForId(this, id);
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
void SoundSlot::End(CSoundManager* manager) {
    if (playing == 0) return;
    manager->EndSlot(this);
}

// Every world frame: frees the voices whose sample ended, restarts the highest-priority deferred sounds in
// free voices, starts the next sound of every sequence whose sound is over, and every 100 ms (GetTickCount)
// lets the ambient emitters near the listener play a random name of theirs. Emitters and random sequences
// draw from the game's random generator on the wall clock's schedule.
void CSoundManager::Tick() {
    if (enabled == 0) return;
    for (int i = 0; i < 15; ++i) {
        SoundSlot* slot = &slots[i];
        if (slot->finished == 0 || slot->playing == 0) continue;
        --CacheEntry(cache, slot)->uses;
        gameapi::AIL_end_sample(slot->sample);
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
        std::memcpy(&slot->length, &rec->length, 4);
        slot->flat = rec->flat;
        slot->heightFade = rec->heightFade;
        std::memcpy(&slot->position, &rec->position, sizeof(slot->position));
        std::memcpy(&slot->nearDistance, &rec->nearDistance, 4);
        std::memcpy(&slot->farDistance, &rec->farDistance, 4);
        gameapi::AIL_init_sample(slot->sample);
        // A flat sound's level is recomputed from its saved Miles level as if that were 0..100.
        if (slot->flat != 0) slot->volume = FlatVolume(slot->volume, slot->heightFade, slot->speech);
        else UpdatePositional(slot);
        if (LoadSample(slot, i, rec->name.data(), 0) != 0) {
            slot->handle = rec->handle;
            gameapi::AIL_set_sample_loop_count(slot->sample, 0);
            ApplyVolumePan(slot);
            gameapi::AIL_register_EOS_callback(slot->sample, SoundEndOfSample);
            if (reverbOn != 0) gameapi::AIL_set_sample_reverb(slot->sample, reverb[0], reverb[1], reverb[2]);
            gameapi::AIL_set_sample_position(slot->sample, rec->samplePosition);
            gameapi::AIL_start_sample(slot->sample);
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

    std::uint32_t now = gameapi::GetTickCount();
    if (now - emitterTick < 100) return;
    emitterTick = now;
    IsaacRandom& random = g_random;
    for (int k = 0; k < 16; ++k) {
        SoundEmitter* e = &emitters[k];
        if (e->active == 0) continue;
        double dx = D(listener.x) - e->position.x;
        double dy = D(listener.y) - e->position.y;
        double dist = std::sqrt(LengthSquared2DUnrounded(dy, dx));
        float distF = static_cast<float>(dist);
        std::uint32_t minInterval, maxInterval;
        EI_NAN_AUDIT(dist, e->innerRadius);
        if (dist < e->innerRadius) {
            minInterval = static_cast<std::uint32_t>(e->minInterval);
            maxInterval = static_cast<std::uint32_t>(e->maxInterval);
        } else {
            // Out of earshot past outerRadius - 0.001.
            EI_NAN_AUDIT(e->outerRadius);
            if (D(e->outerRadius) - kEmitterMargin < distF) continue;
            double t = (D(e->outerRadius) - e->innerRadius) / (D(e->outerRadius) - distF);
            float tF = static_cast<float>(t);
            if (t > kEmitterMaxScale) continue;
            minInterval = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(I(e->minInterval) * tF)));
            maxInterval = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(I(e->maxInterval) * tF)));
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
void CSoundManager::FadeOutStream() {
    if (enabled == 0) return;
    musicFader.mode = 2;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = FloatBits(FadeStepBits());
    musicFader.holdUntil = 0;
}

void CSoundManager::FadeInStream() {
    if (enabled == 0) return;
    musicFader.mode = 1;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = FloatBits(FadeStepBits());
    musicFader.holdUntil = 0;
}

// Asks the stream thread to switch to `name` (an .mp3 path). `mode` says when:
// 1 after the current file, 2 now with a fade out, 3 at the next marker of the current file,
// 4 at the current buffer position; anything else is ignored. Repeating the playing file only
// cancels a fade out. Returns 0 only when the stream has no codec.
int CSoundManager::RequestStream(std::string_view name, int mode) {
    if (enabled == 0) return 1;
    if (musicFader.volume == 0) return 1;
    if (streamCodec == nullptr) return 0;
    CRITICAL_SECTION* lock = &streamLock;
    gameapi::EnterCriticalSection(lock);
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
                musicFader.step = FloatBits(FadeStepBits());
                musicFader.holdUntil = 0;
            }
            gameapi::LeaveCriticalSection(lock);
            return 1;
        }
    }
    if (streamState == 2 &&
        str::Compare(nextName,
                        name) == 0 &&
        switchMode >= m) {
        gameapi::LeaveCriticalSection(lock);
        return 1;
    }
    std::int32_t at;
    switch (m) {
    case 2:
        musicFader.mode = 2;
        musicFader.finished = 0;
        musicFader.step = 0.0f;
        musicFader.step = FloatBits(FadeStepBits());
        musicFader.holdUntil = 0;
        at = 0x7FFFFFFF;
        break;
    case 3: {
        int n = std::ssize(markers);
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
            gameapi::LeaveCriticalSection(lock);
            return 1;
        }
        break;
    }
    case 4:
        at = streamPosition + streamBufferFrames;
        if (at == -1) {
            gameapi::LeaveCriticalSection(lock);
            return 1;
        }
        break;
    case 1:
        at = 0x7FFFFFFF;
        break;
    default:
        gameapi::LeaveCriticalSection(lock);
        return 1;
    }
    streamState = 2;
    switchMode = m;
    nextName = name;
    streamSwitchAt = at;
    gameapi::LeaveCriticalSection(lock);
    return 1;
}

// Mode 4 stops the stream now (closing the file) and fades the volume back in over 2 s;
// other modes request silence ("") the RequestStream way.
void CSoundManager::StopStream(int mode) {
    if (enabled == 0) return;
    CRITICAL_SECTION* lock = &streamLock;
    gameapi::EnterCriticalSection(lock);
    if (streamState != 0) {
        if (mode == 4) {
            if (streamFile != nullptr) {
                streamCodec->close(streamFile);
                streamFile = nullptr;
            }
            streamState = 0;
            streamName = game::kEmptyText;
            float step = StopFadeStep();
            musicFader.mode = 2;
            musicFader.finished = 0;
            musicFader.step = step;
            EI_NAN_AUDIT(step);
            if (step == 0.0f) musicFader.step = FloatBits(FadeStepBits());
            musicFader.holdUntil = 0;
            std::uint32_t until = gameapi::timeGetTime() + 2000;
            musicFader.holdUntil = until;
            if (until == 0) {
                musicFader.factor = 1.0f;
                musicFader.mode = 0;
                musicFader.finished = 0;
                int level = x87::fistp(static_cast<float>(I(musicFader.volume) * kLevelMax * kPercent));
                musicFader.level = level;
                musicFader.level = level > 0x7F ? 0x7F : (level < 0 ? 0 : level);
            }
        } else {
            RequestStream(game::kEmptyText, mode);
        }
    }
    gameapi::LeaveCriticalSection(lock);
}

// --- Stopping everything, wave lengths -------------------------------------------------------

// Loading a game, leaving a zone, the main menu: every voice and deferred sound goes.
void CSoundManager::StopAll(bool stopStream) {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) EndSlot(&slots[i]);
    nextHandle = 0;
    DeleteDeferred();
    if (stopStream) StopStream(4);
}

// StopAll's loop (Close's is the same with the map's clear() out of line).
void CSoundManager::DeleteDeferred() {
    for (auto& [handle, rec] : deferred)
        if (rec != nullptr) FreeDeferred(rec);
    deferred.clear();
}

// AILSOUNDINFO, filled by AIL_WAV_info.
struct miles::SoundInfo {
    std::int32_t format;        // 0x11: IMA ADPCM
    const void* data;
    std::uint32_t dataLength;
    std::uint32_t rate;
    std::int32_t bits;
    std::int32_t channels;
    std::uint32_t samples;
    std::uint32_t blockSize;
    const void* initial;
};
static_assert(sizeof(miles::SoundInfo) == 0x24);

double CSoundManager::WaveLength(const void* data, int /*size*/) {
    miles::SoundInfo info;
    gameapi::AIL_WAV_info(data, &info);
    std::uint32_t samples;
    if (info.format == kImaAdpcm) {
        samples = info.samples;
    } else {
        std::uint32_t bitsPerFrame = static_cast<std::uint32_t>(info.bits * info.channels);
        if (bitsPerFrame == 0) return 0.0;
        samples = (info.dataLength << 3) / bitsPerFrame;
    }
    return static_cast<double>(static_cast<std::int32_t>(samples)) / static_cast<std::int32_t>(info.rate);
}

// --- The volume envelope ----------------------------------------------------------------------------

// Sets the time at which a fade is cancelled; 0 cancels it now.
void SoundFader::SetHold(std::uint32_t until) {
    holdUntil = until;
    if (until != 0) return;
    factor = 1.0f;
    mode = 0;
    finished = 0;
    int v = x87::fistp(static_cast<float>(I(volume) * kLevelMax * kPercent));
    level = v;
    level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
}

void SoundFader::Recalc() {
    int v = x87::fistp(static_cast<float>(I(volume) * factor * kLevelMax * kPercent));
    level = v;
    level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
}

// One step of the fade (the stream thread, every 100 ms, under the stream lock).
void SoundFader::Update() {
    if (holdUntil != 0 && gameapi::timeGetTime() >= holdUntil) {
        holdUntil = 0;
        factor = 1.0f;
        mode = 0;
        finished = 0;
        int v = x87::fistp(static_cast<float>(I(volume) * kLevelMax * kPercent));
        level = v;
        level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
        return;
    }
    switch (mode) {
    case 0:
        return;
    case 1: {
        double sum = D(step) + factor;
        factor = static_cast<float>(sum);
        if (sum >= kOne) {
            mode = 0;
            factor = 1.0f;
        }
        break;
    }
    case 2: {
        double diff = D(factor) - step;
        factor = static_cast<float>(diff);
        EI_NAN_AUDIT(diff);
        if (diff <= 0.0) {
            mode = 0;
            factor = 0.0f;
            finished = 1;
        }
        break;
    }
    default:
        break;
    }
    Recalc();
}

// --- A voice for a new sound -----------------------------------------------------

int CSoundManager::AllocateVoice(int priority) {
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
        std::memcpy(&rec->length, &slot->length, 4);
        rec->flat = slot->flat;
        rec->heightFade = slot->heightFade;
        std::memcpy(&rec->position, &slot->position, sizeof(rec->position));
        std::memcpy(&rec->nearDistance, &slot->nearDistance, 4);
        std::memcpy(&rec->farDistance, &slot->farDistance, 4);
        rec->handle = slot->handle;
        rec->samplePosition = gameapi::AIL_sample_position(slot->sample);
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
int CSoundManager::Play2D(std::string_view name, int priority, int volume, int pan, int loop, int heightFade, int speech,
                          int compressed) {
    if (enabled == 0) return -1;
    int index = AllocateVoice(priority);
    if (index == -1) {
        if (loop == 0) return -1;
        return DeferLooping(this, name, priority, volume, pan, heightFade, speech);
    }
    SoundSlot* slot = &slots[index];
    gameapi::AIL_init_sample(slot->sample);
    slot->looping = loop;
    slot->baseVolume = volume;
    slot->pan = pan;
    slot->priority = priority;
    slot->flat = 1;
    slot->heightFade = heightFade;
    slot->speech = speech;
    slot->volume = NewFlatLevel(this, volume, heightFade, speech);
    if (volume == 0 && loop == 0) return -1;
    if (LoadSample(slot, index, name, compressed) == 0) return -1;
    return StartLoaded(this, slot, loop);
}

// Plays `name` at `position`: full volume inside `nearDist`, silent beyond `farDist`, the pan from the
// listener (UpdatePositional). Returns the handle or -1 as Play2D. A looping sound without a voice is
// deferred; the record's volume, base volume, pan and length are left as operator new returned them.
int CSoundManager::Play3D(std::string_view name, int priority, const float* position, float nearDist, float farDist, int loop,
                          int speech) {
    if (enabled == 0) return -1;
    int index = AllocateVoice(priority);
    if (index == -1) {
        if (loop == 0) return -1;
        auto* rec = new DeferredSound;
        rec->name.assign(name);
        std::memcpy(&rec->position, position, sizeof(rec->position));
        std::memcpy(&rec->nearDistance, &nearDist, 4);
        std::memcpy(&rec->farDistance, &farDist, 4);
        rec->flat = 0;
        rec->heightFade = 0;
        rec->speech = speech;
        rec->priority = priority;
        rec->handle = nextHandle;
        ++nextHandle;
        rec->samplePosition = 0;
        SetDeferred(deferred, rec->handle, rec);
        return rec->handle;
    }
    SoundSlot* slot = &slots[index];
    gameapi::AIL_init_sample(slot->sample);
    slot->looping = loop;
    std::memcpy(&slot->position, position, sizeof(slot->position));
    std::memcpy(&slot->nearDistance, &nearDist, 4);
    std::memcpy(&slot->farDistance, &farDist, 4);
    slot->flat = 0;
    slot->heightFade = 0;
    slot->speech = speech;
    slot->priority = priority;
    UpdatePositional(slot);
    if (slot->volume == 0 && loop == 0) return -1;
    if (LoadSample(slot, index, name, 0) == 0) return -1;
    return StartLoaded(this, slot, loop);
}

// Gives the voice wave `name`: a cached copy (unless `compressed`, which then leaves the
// voice without a new sample file), or the file from the first archive that has it, decompressed
// from MP3 when `compressed` (kept at the cache's old end), else copied into Miles memory after
// making room (kept at its front). Sets the voice's length, name, playing flag and a new handle.
// Returns 0 when no archive has the file or the decompression fails.
int CSoundManager::LoadSample(SoundSlot* slot, int /*index*/, std::string_view name, int compressed) {
    std::string key(name);    // freed on each return
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        if (compressed == 0) UseCached(this, slot, key, cached->second);
    } else {
        CMappedResource* source = nullptr;
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
        std::uint32_t now = gameapi::GetTickCount();
        if (compressed != 0) {
            void* decoded = nullptr;
            int decodedSize = 0;
            int ok = gameapi::AIL_decompress_ASI(data, size, Mp3Suffix(), &decoded, &decodedSize, DecompressCallback());
            source->CloseFile(data);
            if (ok == 0) return 0;
            size = decodedSize;
            data = decoded;
        } else {
            Evict(this, size, now);
        }
        WaveRecord record;
        record.name = key;
        record.lastUsed = now;
        record.size = size;
        record.uses = 1;
        cacheBytes += size;
        if (compressed == 0) {
            void* memory = gameapi::AIL_mem_alloc_lock(size);
            record.data = memory;
            std::memcpy(memory, data, static_cast<std::size_t>(size));
            source->CloseFile(data);
            cacheList.push_front(record);
            cache[key] = cacheList.begin();
            gameapi::AIL_set_named_sample_file(slot->sample, key.c_str(), memory, size, 0);
        } else {
            record.data = data;
            cacheList.push_back(record);
            cache[key] = std::prev(cacheList.end());
            gameapi::AIL_set_sample_file(slot->sample, data, size);
        }
        slot->length = static_cast<float>(WaveLength(record.data, record.size));
    }
    slot->name = key;
    slot->playing = 1;
    slot->handle = nextHandle;
    ++nextHandle;
    return 1;
}

void CSoundManager::StreamNotify(int /*mode*/) {}

int __stdcall SoundDecompressCallback(int state, DecoderPropertyFn set) {
    if (state == 1) {
        set("Requested bit width", 16);
        set("Requested sample rate", 44100);
        set("Requested # of channels", 2);
    }
    return 1;
}
