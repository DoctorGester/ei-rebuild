// The sound manager (CSoundManager, *007AF784): voice volumes and pans from the listener,
// the per-handle controls, ambient emitters, 3D sound sequences and the music stream requests
// (006B69E0-006BB240). The Miles calls go through game.exe's import table; wave loading, the
// voice allocation (006B7D10), Play2D / Play3D (006B8250 / 006B8600) and the per-frame tick
// (006B9B10) stay original.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the
// operation order is kept (/fp:fast may reassociate) and NaN compares stay as written.
#include "sound.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }
inline double I(std::int32_t v) { return v; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

float FloatBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// FCOMP 0; FNSTSW; TEST AH,0x40 not taken: neither zero nor NaN.
bool NonZero(double v) { return v < 0.0 || v > 0.0; }

constexpr float kHeightFull = 30.0f;     // 007440DC: 2D height-faded sounds are full below
constexpr float kHeightSilent = 70.0f;   // 007440E0: ... and silent above
constexpr float kOne = 1.0f;             // 0073B84C
constexpr float kHundred = 100.0f;       // 0073BAB0
constexpr float kMilesScale = std::bit_cast<float>(0x3C5013A9u);  // 0074413C (0.0127)
constexpr float kLevelMax = 127.0f;      // 0073D06C
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);     // 0073D09C (0.01)

// pi (007440D8) and pi / 2 (007C8808, set by a static initialiser).
float Pi() { return *reinterpret_cast<const volatile float*>(0x007440D8); }
float HalfPi() { return *reinterpret_cast<const volatile float*>(0x007C8808); }
// The fade step every fade request uses (007440E4, 1/96), copied as a dword.
std::uint32_t FadeStepBits() { return *reinterpret_cast<const volatile std::uint32_t*>(0x007440E4); }
// StopStream's fade-out step (007440E8), loaded and tested as a float.
float StopFadeStep() { return *reinterpret_cast<const volatile float*>(0x007440E8); }
const char* EmptyString() { return reinterpret_cast<const char*>(0x0079B298); }

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// Miles Sound System through game.exe's import table (all __stdcall).
using SampleIntFn = void(__stdcall*)(void*, int);
using SampleFn = void(__stdcall*)(void*);
SampleIntFn AilSetSampleVolume() { return *reinterpret_cast<SampleIntFn*>(0x0073B774); }
SampleIntFn AilSetSamplePan() { return *reinterpret_cast<SampleIntFn*>(0x0073B77C); }
SampleFn AilEndSample() { return *reinterpret_cast<SampleFn*>(0x0073B7D8); }
SampleFn AilStopSample() { return *reinterpret_cast<SampleFn*>(0x0073B784); }
SampleFn AilResumeSample() { return *reinterpret_cast<SampleFn*>(0x0073B780); }

// _CIacos: the CRT's x87 intrinsic (argument and result in ST0).
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}

// Calls a one-argument function pointer of another module whatever its convention (the ASI
// codec's close, __stdcall in Miles): ESP is restored from EBP afterwards, as the original's
// EBP frame does.
__declspec(naked) void __stdcall CallWithOne(void* /*fn*/, void* /*arg*/) {
    __asm {
        push ebp
        mov ebp, esp
        push dword ptr [ebp + 12]
        call dword ptr [ebp + 8]
        mov esp, ebp
        pop ebp
        ret 8
    }
}

// A sequence's chooser: called with the previous index in ECX and nothing on the stack.
__declspec(naked) int __stdcall CallChooser(void* /*fn*/, int /*previous*/) {
    __asm {
        push ebp
        mov ebp, esp
        mov ecx, dword ptr [ebp + 12]
        call dword ptr [ebp + 8]
        mov esp, ebp
        pop ebp
        ret 8
    }
}

// Not rewritten.
// hash_map<string, cached wave>::operator[] on the slot's wave name (CSoundManager + 0x870);
// returns the value: a pointer to the cache list node, whose +0x20 is the use count.
__declspec(naked) std::uint8_t** __fastcall CacheEntry(void* /*map*/, void*, const SoundSlot*) EI_ORIGINAL(0x006BBDF0)
// hash_map<int, DeferredSound*>::erase(key) (CSoundManager + 0x884).
__declspec(naked) int __fastcall DeferredErase(DeferredMap*, void*, const std::int32_t*) EI_ORIGINAL(0x006BD060)
// Frees an STLport string's storage on the node allocator (the DeferredSound's name).
__declspec(naked) void __fastcall FreeStringStorage(void*) EI_ORIGINAL(0x006B72E0)
// CStringArray::Copy.
__declspec(naked) void __fastcall CopyNames(SoundNameArray*, void*, const SoundNameArray*) EI_ORIGINAL(0x006FEB61)

int ClampLevel(int v) { return v < 0 ? 0 : (v > 0x7F ? 0x7F : v); }

// Heights between 30 and 70 fade 2D sounds flagged heightFade: 1 - (h - 30) / (70 - 30).
double HeightFactor(float h) {
    return D(kOne) - (D(h) - kHeightFull) / (D(kHeightSilent) - kHeightFull);
}

} // namespace

// --- Shared pieces ------------------------------------------------------------------------------

DeferredNode* DeferredMap::Find(std::int32_t key) const {
    std::uint32_t n = static_cast<std::uint32_t>(bucketsEnd - buckets);
    DeferredNode* node = buckets[static_cast<std::uint32_t>(key) % n];
    while (node != nullptr && node->key != key) node = node->next;
    return node;
}

// The volume of a 2D voice (0..127), inlined in every volume setter.
int CSoundManager::FlatVolume(int level, int fade, int isSpeech) const {
    double v = level;
    if (fade != 0) {
        float h = listener[2];
        if (!(h < kHeightSilent)) v = 0.0;
        else if (!(h <= kHeightFull)) v = v * HeightFactor(h);
    }
    v = v * (isSpeech != 0 ? speechVolume : effectsVolume);
    v = v * kMilesScale;
    return ClampLevel(x87::fistp(Store(v)));
}

// Hands the slot's volume and pan to Miles; the pan is mirrored unless panDirect.
void CSoundManager::ApplyVolumePan(SoundSlot* slot) const {
    int direct = panDirect;
    AilSetSampleVolume()(slot->sample, slot->volume);
    int p = slot->pan;
    if (direct == 0) p = -p;
    AilSetSamplePan()(slot->sample, (p + 100) * 0x7F / 200);
}

// Ends a voice: releases its cached wave, stops the sample and frees the slot.
void CSoundManager::EndSlot(SoundSlot* slot) {
    std::uint8_t** entry = CacheEntry(cache, nullptr, slot);
    --*reinterpret_cast<std::int32_t*>(*entry + 0x20);
    AilEndSample()(slot->sample);
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

// 006B7FC0. A 3D voice's volume and pan from the listener: full inside nearDistance, falling to 0
// at farDistance, and the camera height fade; the pan from the angle between the listener axis and
// the direction to the sound (0 ahead or behind, +-100 to the sides).
void CSoundManager::UpdatePositional(SoundSlot* slot) {
    float dx = Store(D(slot->position[0]) - listener[0]);
    double dyRaw = D(slot->position[1]) - listener[1];
    float dy = Store(dyRaw);
    double dist2 = dyRaw * dy + D(dx) * dx;
    double farDist = slot->farDistance;
    if (dist2 > farDist * farDist) {
        slot->volume = 0;
        return;
    }
    double dist = x87::fsqrt(dist2);
    float distF = Store(dist);
    float h = listener[2];
    int level;
    if (!(dist >= slot->nearDistance)) {
        double v = kHundred;
        if (!(h < kHeightSilent)) v = 0.0;
        else if (!(h <= kHeightFull)) v = HeightFactor(h) * kHundred;
        v = v * (slot->speech != 0 ? speechVolume : effectsVolume);
        v = v * kMilesScale;
        level = ClampLevel(x87::fistp(Store(v)));
    } else {
        double t = (D(distF) - slot->nearDistance) / (D(slot->farDistance) - slot->nearDistance) * kHundred;
        int falloff = 100 - x87::fistp(Store(t));
        slot->volume = falloff;
        double v = falloff;
        if (!(h < kHeightSilent)) v = 0.0;
        else if (!(h <= kHeightFull)) v = v * HeightFactor(h);
        v = v * (slot->speech != 0 ? speechVolume : effectsVolume);
        v = v * kMilesScale;
        level = ClampLevel(x87::fistp(Store(v)));
    }
    slot->volume = level;

    float ahead = AbsBits(Store(D(dy) * listenerAxis[1] + D(dx) * listenerAxis[0]));
    if (ahead > distF) {
        slot->pan = 0;
        return;
    }
    if (!NonZero(distF)) {
        slot->pan = 0;
        return;
    }
    double angle = CIacos(D(ahead) / distF);
    if (angle > HalfPi()) angle = D(Pi()) - angle;
    double scale = D(kHundred) / HalfPi();
    int pan = x87::fistp(Store(scale * angle));
    slot->pan = pan;
    if (D(dy) * listenerAxis[0] > D(dx) * listenerAxis[1]) slot->pan = -pan;
}
EI_REPLACE_METHOD(0x006B7FC0, CSoundManager::UpdatePositional);

namespace {
// The voice loop of the volume setters and SetListener (also 006B7610, which nothing calls).
void RefreshVoices(CSoundManager* m) {
    SoundSlot* slot = m->slots;
    for (int i = 0; i < 15; ++i, ++slot) {
        if (slot->playing == 0) continue;
        if (slot->flat == 0) m->UpdatePositional(slot);
        else slot->volume = m->FlatVolume(slot->baseVolume, slot->heightFade, slot->speech);
        m->ApplyVolumePan(slot);
    }
}
} // namespace

// 006B7740. Options: effects volume 0..100.
void CSoundManager::SetEffectsVolume(int volume) {
    if (enabled == 0) return;
    effectsVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}
EI_REPLACE_METHOD(0x006B7740, CSoundManager::SetEffectsVolume);

// 006B78A0. Options: speech volume 0..100.
void CSoundManager::SetSpeechVolume(int volume) {
    if (enabled == 0) return;
    speechVolume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    RefreshVoices(this);
}
EI_REPLACE_METHOD(0x006B78A0, CSoundManager::SetSpeechVolume);

// 006B9750. The camera's position and direction (CCamera::Update every frame).
void CSoundManager::SetListener(const float* position, const float* axis) {
    if (enabled == 0) return;
    std::memcpy(listener, position, sizeof(listener));
    std::memcpy(listenerAxis, axis, sizeof(listenerAxis));
    double len = x87::fsqrt(D(listenerAxis[0]) * listenerAxis[0] + D(listenerAxis[1]) * listenerAxis[1]);
    if (!NonZero(len)) {
        listenerAxis[0] = 1.0f;
        listenerAxis[1] = 0.0f;
        listenerAxis[2] = 0.0f;
    } else {
        double inv = D(kOne) / len;
        listenerAxis[0] = Store(inv * listenerAxis[0]);
        listenerAxis[1] = Store(inv * listenerAxis[1]);
        listenerAxis[2] = Store(inv * listenerAxis[2]);
    }
    RefreshVoices(this);
}
EI_REPLACE_METHOD(0x006B9750, CSoundManager::SetListener);

// 006B7A00. Options: music volume 0..100 (0 stops the stream).
void CSoundManager::SetMusicVolume(int volume) {
    int v = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (enabled == 0) return;
    musicFader.volume = v;
    int level = x87::fistp(Store(I(v) * musicFader.factor * kLevelMax * kPercent));
    musicFader.level = level;
    level = level > 0x7F ? 0x7F : (level < 0 ? 0 : level);
    musicFader.level = level;
    AilSetSampleVolume()(streamSample, level);
    if (v == 0) StopStream(4);
}
EI_REPLACE_METHOD(0x006B7A00, CSoundManager::SetMusicVolume);

// 006B69E0. Reverb parameters for new voices (used when reverbOn).
void CSoundManager::SetReverb(float room, float level, float time) {
    std::memcpy(&reverb[0], &room, 4);
    std::memcpy(&reverb[1], &level, 4);
    std::memcpy(&reverb[2], &time, 4);
}
EI_REPLACE_METHOD(0x006B69E0, CSoundManager::SetReverb);

// 006B7320 / 006B7380. The game window lost / regained focus.
void CSoundManager::PauseAll() {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) AilStopSample()(slots[i].sample);
    if (enabled != 0 && streamSample != nullptr) AilStopSample()(streamSample);
}
EI_REPLACE_METHOD(0x006B7320, CSoundManager::PauseAll);

void CSoundManager::ResumeAll() {
    if (slots != nullptr)
        for (int i = 0; i < 15; ++i)
            if (slots[i].playing != 0) AilResumeSample()(slots[i].sample);
    if (enabled != 0 && streamSample != nullptr) AilResumeSample()(streamSample);
}
EI_REPLACE_METHOD(0x006B7380, CSoundManager::ResumeAll);

// --- Per-handle controls --------------------------------------------------------------------------

// 006B7C10. The voice playing `handle`; a voice whose sample ended is freed and gives null.
SoundSlot* CSoundManager::FindPlaying(int handle) {
    if (enabled == 0) return nullptr;
    SoundSlot* slot = SlotForHandle(handle);
    if (slot == nullptr) return nullptr;
    if (slot->finished == 0) return slot;
    if (slot->playing != 0) EndSlot(slot);
    return nullptr;
}
EI_REPLACE_METHOD(0x006B7C10, CSoundManager::FindPlaying);

// 006B7CB0. The deferred record of `handle`, or null.
DeferredSound* CSoundManager::FindDeferred(std::uint32_t handle) {
    if (enabled == 0) return nullptr;
    DeferredNode* node = deferred.Find(static_cast<std::int32_t>(handle));
    return node != nullptr ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x006B7CB0, CSoundManager::FindDeferred);

// 006B9100. Makes a sound 2D with the given volume (0..100) and pan.
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
        DeferredNode* node = deferred.Find(handle);
        if (node != nullptr && node->value != nullptr) {
            node->value->baseVolume = volume;
            node->value->pan = pan;
            node->value->flat = 1;
            return 1;
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x006B9100, CSoundManager::SetSoundVolume);

// 006B92F0. Makes a sound 3D at `position`.
int CSoundManager::SetSoundPosition(int handle, const float* position, float nearDist, float farDist) {
    if (enabled != 0) {
        SoundSlot* slot = SlotForHandle(handle);
        if (slot != nullptr) {
            if (slot->finished != 0) {
                if (slot->playing != 0) EndSlot(slot);
            } else {
                std::memcpy(slot->position, position, sizeof(slot->position));
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
        DeferredNode* node = deferred.Find(handle);
        if (node != nullptr && node->value != nullptr) {
            DeferredSound* rec = node->value;
            std::memcpy(rec->position, position, sizeof(rec->position));
            rec->flat = 0;
            rec->nearDistance = nearDist;
            rec->farDistance = farDist;
            return 1;
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x006B92F0, CSoundManager::SetSoundPosition);

namespace {
// Deletes the deferred record of `handle`, if any (StopSound, RemoveSequence).
void DropDeferred(CSoundManager* m, int handle) {
    DeferredNode* node = m->deferred.Find(handle);
    if (node == nullptr) return;
    DeferredSound* rec = node->value;
    if (rec == nullptr) return;
    DeferredErase(&m->deferred, nullptr, &rec->handle);
    FreeStringStorage(rec);
    gamecrt::operator_delete(rec);
}
} // namespace

// 006B94A0. Stops a sound (or forgets its deferred record); always -1, the "no sound" handle.
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
EI_REPLACE_METHOD(0x006B94A0, CSoundManager::StopSound);

// --- Ambient emitters -----------------------------------------------------------------------------

namespace {
// Emitter `index` as the original addresses it: index 16 is accepted (one past the array).
SoundEmitter* EmitterAt(CSoundManager* m, int index) {
    return reinterpret_cast<SoundEmitter*>(reinterpret_cast<std::uint8_t*>(m) + 0x26C + index * 0x38);
}
} // namespace

// 006B9A30.
void CSoundManager::MoveEmitter(int index, const float* position, float inner, float outer, int minInterval,
                                int maxInterval) {
    if (index < 0 || index > 16) return;
    SoundEmitter* e = EmitterAt(this, index);
    if (e->active == 0) return;
    std::memcpy(e->position, position, sizeof(e->position));
    e->innerRadius = inner;
    e->outerRadius = outer;
    e->minInterval = minInterval;
    e->maxInterval = maxInterval;
}
EI_REPLACE_METHOD(0x006B9A30, CSoundManager::MoveEmitter);

// 006B9950. Returns the emitter index or -1.
int CSoundManager::AddEmitter(const SoundNameArray* names, const float* position, float inner, float outer,
                              int minInterval, int maxInterval) {
    if (enabled == 0 || names->size < 1) return -1;
    int i = 0;
    while (i < 16 && emitters[i].active != 0) ++i;
    if (i == 16) return -1;
    SoundEmitter* e = &emitters[i];
    e->active = 1;
    CopyNames(&e->names, nullptr, names);
    MoveEmitter(i, position, inner, outer, minInterval, maxInterval);
    e->lastPlayed = gameapi::GetTickCount();
    return i;
}
EI_REPLACE_METHOD(0x006B9950, CSoundManager::AddEmitter);

// 006B9AA0. Always -1. The name list is kept.
int CSoundManager::RemoveEmitter(int index) {
    if (index < 0 || index > 16) return -1;
    SoundEmitter* e = EmitterAt(this, index);
    if (e->active == 0) return -1;
    e->position[0] = 0.0f;
    e->active = 0;
    e->maxInterval = -1;
    e->minInterval = -1;
    e->position[1] = 0.0f;
    e->position[2] = 0.0f;
    e->outerRadius = 0.0f;
    e->innerRadius = 0.0f;
    e->lastPlayed = 0;
    return -1;
}
EI_REPLACE_METHOD(0x006B9AA0, CSoundManager::RemoveEmitter);

// --- Sound sequences ------------------------------------------------------------------------------

// 006BA140. Starts the sequence's next sound; false when it is over or the sound did not start.
int SoundSequence::PlayNext(CSoundManager* manager) {
    if (chooser != nullptr) {
        ++remaining;
        int next = CallChooser(reinterpret_cast<void*>(chooser), index);
        index = next;
        if (next < 0) return 0;
        index = next & 0xBFFFFFFF;
        int h = manager->Play3D(static_cast<const char*>(names.data[next & 0xBFFFFFFF]), priority, position, nearDistance, farDistance,
                                next & 0x40000000, 0);
        handle = h;
        return h != -1 ? 1 : 0;
    }
    if (remaining == 0) return 0;
    if (random != 0) index = static_cast<std::int32_t>(GlobalRandom().Next() % static_cast<std::uint32_t>(names.size));
    int h = manager->Play3D(static_cast<const char*>(names.data[index]), priority, position, nearDistance, farDistance, 0, 0);
    handle = h;
    if (h == -1) return 0;
    index = (index + 1) % names.size;
    if (remaining > 0) --remaining;
    return 1;
}
EI_REPLACE_METHOD(0x006BA140, SoundSequence::PlayNext);

// 006BA2D0. Returns the sequence id (generation << 8 | slot) or -1.
int CSoundManager::AddSequence(const SoundNameArray* names, int priority, const float* position, float nearDist,
                               float farDist, int randomOrder, int count, int(__fastcall* chooser)(int)) {
    if (enabled == 0 || names->size < 1) return -1;
    int i = 0;
    while (i < 8 && sequences[i].active != 0) ++i;
    if (i == 8) return -1;
    SoundSequence* s = &sequences[i];
    ++s->generation;
    s->id = s->generation << 8 | i;
    s->active = 1;
    CopyNames(&s->names, nullptr, names);
    std::memcpy(s->position, position, sizeof(s->position));
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
EI_REPLACE_METHOD(0x006BA2D0, CSoundManager::AddSequence);

namespace {
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
            std::memcpy(slot->position, position, sizeof(slot->position));
            slot->flat = 0;
            slot->nearDistance = nearDist;
            slot->farDistance = farDist;
            m->UpdatePositional(slot);
            m->ApplyVolumePan(slot);
            return;
        }
    }
    if (m->enabled == 0) return;
    DeferredNode* node = m->deferred.Find(handle);
    if (node == nullptr || node->value == nullptr) return;
    DeferredSound* rec = node->value;
    std::memcpy(rec->position, position, sizeof(rec->position));
    rec->flat = 0;
    rec->nearDistance = nearDist;
    rec->farDistance = farDist;
}
} // namespace

// 006BA400.
int CSoundManager::MoveSequence(std::uint32_t id, const float* position) {
    if (enabled == 0 || (id & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, id);
    if (s == nullptr) return 0;
    std::memcpy(s->position, position, sizeof(s->position));
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, s->nearDistance, s->farDistance);
    return 1;
}
EI_REPLACE_METHOD(0x006BA400, CSoundManager::MoveSequence);

// 006BA5E0.
int CSoundManager::UpdateSequence(std::uint32_t id, const float* position, float nearDist, float farDist, int count) {
    if (enabled == 0 || (id & 0xFF) >= 8) return 0;
    SoundSequence* s = SequenceForId(this, id);
    if (s == nullptr) return 0;
    std::memcpy(s->position, position, sizeof(s->position));
    s->nearDistance = nearDist;
    s->farDistance = farDist;
    s->remaining = count;
    if (enabled != 0) MoveSequenceSound(this, s->handle, position, nearDist, farDist);
    return 1;
}
EI_REPLACE_METHOD(0x006BA5E0, CSoundManager::UpdateSequence);

// 006BA7C0. Always -1.
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
EI_REPLACE_METHOD(0x006BA7C0, CSoundManager::RemoveSequence);

// --- Music stream ---------------------------------------------------------------------------------

// 006BAFA0 / 006BAFE0. Starts a fade of the stream (no lock, as in the original).
void CSoundManager::FadeOutStream() {
    if (enabled == 0) return;
    musicFader.mode = 2;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = FloatBits(FadeStepBits());
    musicFader.holdUntil = 0;
}
EI_REPLACE_METHOD(0x006BAFA0, CSoundManager::FadeOutStream);

void CSoundManager::FadeInStream() {
    if (enabled == 0) return;
    musicFader.mode = 1;
    musicFader.finished = 0;
    musicFader.step = 0.0f;
    musicFader.step = FloatBits(FadeStepBits());
    musicFader.holdUntil = 0;
}
EI_REPLACE_METHOD(0x006BAFE0, CSoundManager::FadeInStream);

// 006BB020. Asks the stream thread to switch to `name` (an .mp3 path). `mode` says when:
// 1 after the current file, 2 now with a fade out, 3 at the next marker of the current file,
// 4 at the current buffer position; anything else is ignored. Repeating the playing file only
// cancels a fade out. Returns 0 only when the stream has no codec.
int CSoundManager::RequestStream(const char* name, int mode) {
    if (enabled == 0) return 1;
    if (musicFader.volume == 0) return 1;
    if (streamCodec == nullptr) return 0;
    CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(streamLock);
    gameapi::EnterCriticalSection(lock);
    int m = 4;
    if (streamState != 0) {
        m = mode;
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(streamName)),
                            reinterpret_cast<const unsigned char*>(name)) == 0 &&
            *name != '\0') {
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
        gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(nextName)),
                        reinterpret_cast<const unsigned char*>(name)) == 0 &&
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
        int n = markerCount;
        if (n == 0) {
            m = 1;
            at = 0x7FFFFFFF;
            break;
        }
        at = -1;
        bool found = false;
        if (n > 0) {
            int pos = streamPosition + streamBufferBytes;
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
        at = streamPosition + streamBufferBytes;
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
EI_REPLACE_METHOD(0x006BB020, CSoundManager::RequestStream);

// 006BB240. Mode 4 stops the stream now (closing the file) and fades the volume back in over 2 s;
// other modes request silence ("") the RequestStream way.
void CSoundManager::StopStream(int mode) {
    if (enabled == 0) return;
    CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(streamLock);
    gameapi::EnterCriticalSection(lock);
    if (streamState != 0) {
        if (mode == 4) {
            if (streamFile != nullptr) {
                CallWithOne(streamCodec[3], streamFile);
                streamFile = nullptr;
            }
            streamState = 0;
            streamName = EmptyString();
            float step = StopFadeStep();
            musicFader.mode = 2;
            musicFader.finished = 0;
            musicFader.step = step;
            if (!NonZero(step)) musicFader.step = FloatBits(FadeStepBits());
            musicFader.holdUntil = 0;
            std::uint32_t until = gameapi::timeGetTime() + 2000;
            musicFader.holdUntil = until;
            if (until == 0) {
                musicFader.factor = 1.0f;
                musicFader.mode = 0;
                musicFader.finished = 0;
                int level = x87::fistp(Store(I(musicFader.volume) * kLevelMax * kPercent));
                musicFader.level = level;
                musicFader.level = level > 0x7F ? 0x7F : (level < 0 ? 0 : level);
            }
        } else {
            RequestStream(EmptyString(), mode);
        }
    }
    gameapi::LeaveCriticalSection(lock);
}
EI_REPLACE_METHOD(0x006BB240, CSoundManager::StopStream);

// --- The volume envelope ----------------------------------------------------------------------------

// 006B5E80. Sets the time at which a fade is cancelled; 0 cancels it now.
void SoundFader::SetHold(std::uint32_t until) {
    holdUntil = until;
    if (until != 0) return;
    factor = 1.0f;
    mode = 0;
    finished = 0;
    int v = x87::fistp(Store(I(volume) * kLevelMax * kPercent));
    level = v;
    level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
}
EI_REPLACE_METHOD(0x006B5E80, SoundFader::SetHold);

// 006B5FF0.
void SoundFader::Recalc() {
    int v = x87::fistp(Store(I(volume) * factor * kLevelMax * kPercent));
    level = v;
    level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
}
EI_REPLACE_METHOD(0x006B5FF0, SoundFader::Recalc);

// 006B5EE0. One step of the fade (the stream thread, every 100 ms, under the stream lock).
void SoundFader::Update() {
    if (holdUntil != 0 && gameapi::timeGetTime() >= holdUntil) {
        holdUntil = 0;
        factor = 1.0f;
        mode = 0;
        finished = 0;
        int v = x87::fistp(Store(I(volume) * kLevelMax * kPercent));
        level = v;
        level = v > 0x7F ? 0x7F : (v < 0 ? 0 : v);
        return;
    }
    switch (mode) {
    case 0:
        return;
    case 1: {
        double sum = D(step) + factor;
        factor = Store(sum);
        if (sum >= kOne) {
            mode = 0;
            factor = 1.0f;
        }
        break;
    }
    case 2: {
        double diff = D(factor) - step;
        factor = Store(diff);
        if (!(diff > 0.0)) {
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
EI_REPLACE_METHOD(0x006B5EE0, SoundFader::Update);

// --- Forwarders -------------------------------------------------------------------------------------

__declspec(naked) mfc::CString CSoundManager::CurrentStreamName() EI_ORIGINAL(0x006BAEA0)
__declspec(naked) void CSoundManager::StreamNotify(int) EI_ORIGINAL(0x006BBAC0)
__declspec(naked) int CSoundManager::Play3D(const char*, int, const float*, float, float, int, int) EI_ORIGINAL(0x006B8600)
