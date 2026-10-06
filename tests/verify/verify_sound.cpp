// Checks for the sound manager and the music system (sound_manager.cpp, sound_music.cpp).
//
// - Maths and setters that write only their own object run side by side: the original on a
//   scratch copy (a voice, a fader, or a byte copy of the 0x8C0-byte manager), ours on the real
//   object, results compared.
// - Volume setters and the per-handle controls repeat their Miles calls harmlessly (same volume and
//   pan twice), so they run side by side too: the original first, the voices / record / sequence it
//   wrote captured and restored, then ours, and the two compared. The `finished` flag of a voice is
//   never restored (the Miles end-of-sample callback writes it from its own thread). A call that
//   would end a voice (release its cached wave and sample) runs only ours, as does one where the
//   original turned out to end a voice after all (the callback raced).
// - Everything that allocates, plays, talks to the stream thread or consumes the random generator
//   (StopSound, emitters/sequences added, the stream requests, the music system) runs only ours,
//   followed by a consistency check of the state it leaves.
//
// SoundFader::Update runs on the stream thread (006BB910); the verify counters are locked for that.
#include "verify.h"

#include "../../src/game/sound.h"
#include "../../src/lib/gameapi.h"

#include <cstring>
#include <new>

namespace {
void* s_unused;

constexpr std::size_t kFinishedOffset = offsetof(SoundSlot, finished);

// The 15 voices, minus their `finished` flags.
struct Voices {
    std::uint8_t bytes[15 * sizeof(SoundSlot)];

    void Save(const CSoundManager* m) { std::memcpy(bytes, m->slots, sizeof(bytes)); }
    void Restore(CSoundManager* m) const {
        for (int i = 0; i < 15; ++i) {
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&m->slots[i]);
            const std::uint8_t* src = bytes + i * sizeof(SoundSlot);
            std::memcpy(dst, src, kFinishedOffset);
            std::memcpy(dst + kFinishedOffset + 4, src + kFinishedOffset + 4, sizeof(SoundSlot) - kFinishedOffset - 4);
        }
    }
    // Index of the first voice that differs (ignoring `finished`), or -1.
    int Difference(const Voices& o) const {
        for (int i = 0; i < 15; ++i) {
            const std::uint8_t* a = bytes + i * sizeof(SoundSlot);
            const std::uint8_t* b = o.bytes + i * sizeof(SoundSlot);
            if (std::memcmp(a, b, kFinishedOffset) != 0 ||
                std::memcmp(a + kFinishedOffset + 4, b + kFinishedOffset + 4, sizeof(SoundSlot) - kFinishedOffset - 4) != 0)
                return i;
        }
        return -1;
    }
    int Playing(int i) const {
        std::int32_t v;
        std::memcpy(&v, bytes + i * sizeof(SoundSlot) + offsetof(SoundSlot, playing), 4);
        return v;
    }
};

// Whether a call on `handle` would end its voice (the first voice with the handle has finished
// and still plays).
bool WouldEndVoice(const CSoundManager* m, int handle) {
    if (m->enabled == 0) return false;
    SoundSlot* s = m->SlotForHandle(handle);
    return s != nullptr && s->finished != 0 && s->playing != 0;
}

// True when a voice that played before the call no longer does (the original ended it).
bool EndedVoice(const Voices& before, const CSoundManager* m) {
    for (int i = 0; i < 15; ++i)
        if (before.Playing(i) != 0 && m->slots[i].playing == 0) return true;
    return false;
}

struct Record {
    DeferredSound* rec;
    std::uint8_t bytes[sizeof(DeferredSound)];
    void Save(const CSoundManager* m, int handle) {
        rec = nullptr;
        if (m->enabled == 0) return;
        DeferredNode* n = m->deferred.Find(handle);
        if (n != nullptr) rec = n->value;
        if (rec != nullptr) std::memcpy(bytes, rec, sizeof(bytes));
    }
    void Restore() const {
        if (rec != nullptr) std::memcpy(rec, bytes, sizeof(bytes));
    }
    bool Same(const Record& o) const {
        return rec == o.rec && (rec == nullptr || std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0);
    }
};

// Runs a per-handle control side by side unless it would end a voice. `original` and `ours`
// return the call's result; `extra` saves / restores / compares any further state (the sequence).
template <typename Original, typename Ours>
int VoiceControl(const char* name, CSoundManager* m, int handle, Original original, Ours ours,
                 std::uint8_t* extra = nullptr, std::size_t extraSize = 0) {
    if (WouldEndVoice(m, handle)) return ours();
    Voices v0, vOrig, vOurs;
    Record r0, rOrig, rOurs;
    std::uint8_t e0[0x50], eOrig[0x50], eOurs[0x50];
    v0.Save(m);
    r0.Save(m, handle);
    if (extra) std::memcpy(e0, extra, extraSize);
    int expected = original();
    if (EndedVoice(v0, m)) return expected;  // the end-of-sample callback raced: keep the original's result
    vOrig.Save(m);
    rOrig.rec = r0.rec;
    if (r0.rec) std::memcpy(rOrig.bytes, r0.rec, sizeof(rOrig.bytes));
    if (extra) std::memcpy(eOrig, extra, extraSize);
    v0.Restore(m);
    r0.Restore();
    if (extra) std::memcpy(extra, e0, extraSize);
    int actual = ours();
    vOurs.Save(m);
    rOurs.rec = r0.rec;
    if (r0.rec) std::memcpy(rOurs.bytes, r0.rec, sizeof(rOurs.bytes));
    if (extra) std::memcpy(eOurs, extra, extraSize);
    int voice = vOrig.Difference(vOurs);
    bool same = voice < 0 && rOrig.Same(rOurs) && (extra == nullptr || std::memcmp(eOrig, eOurs, extraSize) == 0) &&
                expected == actual;
    ei::verify::Check(name, true, same, "handle %d result %d/%d voice %d record %p", handle, expected, actual, voice,
                      static_cast<void*>(r0.rec));
    return expected;
}

// The listener and option volumes of the manager.
struct ListenerState {
    float listener[6];
    std::int32_t volumes[2];
    void Save(const CSoundManager* m) {
        std::memcpy(listener, m->listener, sizeof(listener));
        volumes[0] = m->effectsVolume;
        volumes[1] = m->speechVolume;
    }
    void Restore(CSoundManager* m) const {
        std::memcpy(m->listener, listener, sizeof(listener));
        m->effectsVolume = volumes[0];
        m->speechVolume = volumes[1];
    }
    bool Same(const ListenerState& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

// Volume setters and SetListener: no voice ends, every Miles call is repeated with the same values.
template <typename Original, typename Ours>
void VolumeSetter(const char* name, CSoundManager* m, int argument, Original original, Ours ours) {
    if (m->enabled == 0) {
        ours();
        ei::verify::Check(name, true, true, "");
        return;
    }
    Voices v0, vOrig, vOurs;
    ListenerState l0, lOrig, lOurs;
    v0.Save(m);
    l0.Save(m);
    original();
    vOrig.Save(m);
    lOrig.Save(m);
    v0.Restore(m);
    l0.Restore(m);
    ours();
    vOurs.Save(m);
    lOurs.Save(m);
    int voice = vOrig.Difference(vOurs);
    ei::verify::Check(name, true, voice < 0 && lOrig.Same(lOurs), "argument %d voice %d listener %d", argument, voice,
                      lOrig.Same(lOurs) ? 1 : 0);
}

// The original on a byte copy of the manager, ours on the real one; compares [offset, offset+size).
template <typename Original, typename Ours>
void OnManagerCopy(const char* name, CSoundManager* m, std::size_t offset, std::size_t size, Original original,
                   Ours ours) {
    alignas(8) std::uint8_t scratch[sizeof(CSoundManager)];
    std::memcpy(scratch, m, sizeof(scratch));
    original(reinterpret_cast<CSoundManager*>(scratch));
    ours();
    bool same = std::memcmp(scratch + offset, reinterpret_cast<std::uint8_t*>(m) + offset, size) == 0;
    ei::verify::Check(name, true, same, "bytes %X..%X", static_cast<unsigned>(offset), static_cast<unsigned>(offset + size));
}

using VoidFn = void(__fastcall*)(CSoundManager*, void*);
using IntFn = void(__fastcall*)(CSoundManager*, void*, int);

} // namespace

// --- Maths, scratch copies ------------------------------------------------------------------------

// 006B7FC0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes).
using UpdatePositionalFn = void(__fastcall*)(CSoundManager*, void*, SoundSlot*);
static UpdatePositionalFn s_updatePositional;
static void __fastcall UpdatePositionalVerify(CSoundManager* self, void*, SoundSlot* slot) {
    SoundSlot scratch;
    std::memcpy(&scratch, slot, sizeof(scratch));
    s_updatePositional(self, nullptr, &scratch);
    self->CSoundManager::UpdatePositional(slot);
    std::int32_t expected[2] = {scratch.volume, scratch.pan};
    std::int32_t actual[2] = {slot->volume, slot->pan};
    std::uint32_t pos[3], lis[3];
    std::memcpy(pos, slot->position, 12);
    std::memcpy(lis, self->listener, 12);
    ei::verify::Check("CSoundManager::UpdatePositional", expected, actual,
                      "pos %08X %08X %08X listener %08X %08X %08X volume %d/%d pan %d/%d", pos[0], pos[1], pos[2],
                      lis[0], lis[1], lis[2], expected[0], actual[0], expected[1], actual[1]);
}
EI_VERIFY(0x006B7FC0, UpdatePositionalVerify, 6, s_updatePositional);

// 006B5EE0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Stream thread. A hold that expires between
// the two runs' clock reads is not compared.
using FaderFn = void(__fastcall*)(SoundFader*, void*);
static FaderFn s_faderUpdate;
static void __fastcall FaderUpdateVerify(SoundFader* self, void*) {
    SoundFader scratch = *self;
    std::uint32_t before = gameapi::timeGetTime();
    s_faderUpdate(&scratch, nullptr);
    std::uint32_t hold = self->holdUntil;
    self->SoundFader::Update();
    std::uint32_t after = gameapi::timeGetTime();
    if (hold != 0 && before < hold && after >= hold) return;
    ei::verify::Check("SoundFader::Update", scratch, *self, "volume %d mode %d", self->volume, self->mode);
}
EI_VERIFY(0x006B5EE0, FaderUpdateVerify, 6, s_faderUpdate);

// 006B5E80: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[EBP+8] (7 bytes).
using FaderHoldFn = void(__fastcall*)(SoundFader*, void*, std::uint32_t);
static FaderHoldFn s_faderHold;
static void __fastcall FaderHoldVerify(SoundFader* self, void*, std::uint32_t until) {
    SoundFader scratch = *self;
    s_faderHold(&scratch, nullptr, until);
    self->SoundFader::SetHold(until);
    ei::verify::Check("SoundFader::SetHold", scratch, *self, "until %u", until);
}
EI_VERIFY(0x006B5E80, FaderHoldVerify, 7, s_faderHold);

// 006B5FF0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static FaderFn s_faderRecalc;
static void __fastcall FaderRecalcVerify(SoundFader* self, void*) {
    SoundFader scratch = *self;
    s_faderRecalc(&scratch, nullptr);
    self->SoundFader::Recalc();
    ei::verify::Check("SoundFader::Recalc", scratch, *self, "volume %d", self->volume);
}
EI_VERIFY(0x006B5FF0, FaderRecalcVerify, 6, s_faderRecalc);

// 006B7CB0: MOV EAX,[ECX+0x898] (6 bytes). Pure.
using FindDeferredFn = DeferredSound*(__fastcall*)(CSoundManager*, void*, std::uint32_t);
static FindDeferredFn s_findDeferred;
static DeferredSound* __fastcall FindDeferredVerify(CSoundManager* self, void*, std::uint32_t handle) {
    DeferredSound* expected = s_findDeferred(self, nullptr, handle);
    DeferredSound* actual = self->CSoundManager::FindDeferred(handle);
    ei::verify::Check("CSoundManager::FindDeferred", expected, actual, "handle %u", handle);
    return expected;
}
EI_VERIFY(0x006B7CB0, FindDeferredVerify, 6, s_findDeferred);

// 006B9A30: MOV EAX,[ESP+4]; TEST EAX,EAX (6 bytes).
using MoveEmitterFn = void(__fastcall*)(CSoundManager*, void*, int, const float*, float, float, int, int);
static MoveEmitterFn s_moveEmitter;
static void __fastcall MoveEmitterVerify(CSoundManager* self, void*, int index, const float* position, float inner,
                                         float outer, int minInterval, int maxInterval) {
    if (index < 0 || index > 16) {
        self->CSoundManager::MoveEmitter(index, position, inner, outer, minInterval, maxInterval);
        return;
    }
    OnManagerCopy("CSoundManager::MoveEmitter", self, 0x26C + index * 0x38, 0x38,
                  [&](CSoundManager* copy) { s_moveEmitter(copy, nullptr, index, position, inner, outer, minInterval, maxInterval); },
                  [&] { self->CSoundManager::MoveEmitter(index, position, inner, outer, minInterval, maxInterval); });
}
EI_VERIFY(0x006B9A30, MoveEmitterVerify, 6, s_moveEmitter);

// 006B9AA0: MOV EAX,[ESP+4]; SUB ESP,0xC (7 bytes).
using RemoveEmitterFn = int(__fastcall*)(CSoundManager*, void*, int);
static RemoveEmitterFn s_removeEmitter;
static int __fastcall RemoveEmitterVerify(CSoundManager* self, void*, int index) {
    if (index < 0 || index > 16) return self->CSoundManager::RemoveEmitter(index);
    int expected = 0, actual = 0;
    OnManagerCopy("CSoundManager::RemoveEmitter", self, 0x26C + index * 0x38, 0x38,
                  [&](CSoundManager* copy) { expected = s_removeEmitter(copy, nullptr, index); },
                  [&] { actual = self->CSoundManager::RemoveEmitter(index); });
    ei::verify::Check("CSoundManager::RemoveEmitter result", expected, actual, "index %d", index);
    return actual;
}
EI_VERIFY(0x006B9AA0, RemoveEmitterVerify, 7, s_removeEmitter);

// 006B69E0: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
using SetReverbFn = void(__fastcall*)(CSoundManager*, void*, float, float, float);
static SetReverbFn s_setReverb;
static void __fastcall SetReverbVerify(CSoundManager* self, void*, float room, float level, float time) {
    OnManagerCopy("CSoundManager::SetReverb", self, 0x8A8, 12,
                  [&](CSoundManager* copy) { s_setReverb(copy, nullptr, room, level, time); },
                  [&] { self->CSoundManager::SetReverb(room, level, time); });
}
EI_VERIFY(0x006B69E0, SetReverbVerify, 8, s_setReverb);

// 006BAFA0 / 006BAFE0: MOV EDX,[ECX+0x898] (6 bytes). Fader mode, finished, step and hold
// compared (the stream thread may change the level and factor meanwhile).
static VoidFn s_fadeOut;
static void __fastcall FadeOutVerify(CSoundManager* self, void*) {
    OnManagerCopy("CSoundManager::FadeOutStream", self, 0x150, 0x10, [&](CSoundManager* copy) { s_fadeOut(copy, nullptr); },
                  [&] { self->CSoundManager::FadeOutStream(); });
}
EI_VERIFY(0x006BAFA0, FadeOutVerify, 6, s_fadeOut);

static VoidFn s_fadeIn;
static void __fastcall FadeInVerify(CSoundManager* self, void*) {
    OnManagerCopy("CSoundManager::FadeInStream", self, 0x150, 0x10, [&](CSoundManager* copy) { s_fadeIn(copy, nullptr); },
                  [&] { self->CSoundManager::FadeInStream(); });
}
EI_VERIFY(0x006BAFE0, FadeInVerify, 6, s_fadeIn);

// --- Volume setters and the listener: side by side --------------------------------------------------

// 006B7740 / 006B78A0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
static IntFn s_setEffects;
static void __fastcall SetEffectsVolumeVerify(CSoundManager* self, void*, int volume) {
    VolumeSetter("CSoundManager::SetEffectsVolume", self, volume, [&] { s_setEffects(self, nullptr, volume); },
                 [&] { self->CSoundManager::SetEffectsVolume(volume); });
}
EI_VERIFY(0x006B7740, SetEffectsVolumeVerify, 6, s_setEffects);

static IntFn s_setSpeech;
static void __fastcall SetSpeechVolumeVerify(CSoundManager* self, void*, int volume) {
    VolumeSetter("CSoundManager::SetSpeechVolume", self, volume, [&] { s_setSpeech(self, nullptr, volume); },
                 [&] { self->CSoundManager::SetSpeechVolume(volume); });
}
EI_VERIFY(0x006B78A0, SetSpeechVolumeVerify, 6, s_setSpeech);

// 006B9750: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Every frame from CCamera::Update.
using SetListenerFn = void(__fastcall*)(CSoundManager*, void*, const float*, const float*);
static SetListenerFn s_setListener;
static void __fastcall SetListenerVerify(CSoundManager* self, void*, const float* position, const float* axis) {
    VolumeSetter("CSoundManager::SetListener", self, 0, [&] { s_setListener(self, nullptr, position, axis); },
                 [&] { self->CSoundManager::SetListener(position, axis); });
}
EI_VERIFY(0x006B9750, SetListenerVerify, 6, s_setListener);

// 006B7A00: ours only (0 stops the stream). The level must be what the original's Recalc gives.
// The wrapper holds the stream lock so the stream thread's fade cannot run in between.
static IntFn s_unusedMusicVolume;
static void __fastcall SetMusicVolumeVerify(CSoundManager* self, void*, int volume) {
    if (self->enabled == 0 || s_faderRecalc == nullptr) {
        self->CSoundManager::SetMusicVolume(volume);
        return;
    }
    CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(self->streamLock);
    gameapi::EnterCriticalSection(lock);
    self->CSoundManager::SetMusicVolume(volume);
    SoundFader expected = self->musicFader;
    gameapi::LeaveCriticalSection(lock);
    s_faderRecalc(&expected, nullptr);
    int v = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    ei::verify::Check("CSoundManager::SetMusicVolume", true, self->musicFader.volume == v && self->musicFader.level == expected.level,
                      "volume %d level %d/%d", volume, expected.level, self->musicFader.level);
}
EI_VERIFY(0x006B7A00, SetMusicVolumeVerify, 0, s_unusedMusicVolume);

// --- Per-handle controls ------------------------------------------------------------------------------

// 006B7C10: MOV EAX,[ECX+0x898] (6 bytes). Pure unless the voice ended; then ours only.
using FindPlayingFn = SoundSlot*(__fastcall*)(CSoundManager*, void*, int);
static FindPlayingFn s_findPlaying;
static SoundSlot* __fastcall FindPlayingVerify(CSoundManager* self, void*, int handle) {
    if (WouldEndVoice(self, handle)) {
        SoundSlot* r = self->CSoundManager::FindPlaying(handle);
        ei::verify::Check("CSoundManager::FindPlaying (ended)", true, r == nullptr, "handle %d", handle);
        return r;
    }
    SoundSlot* slot = self->enabled != 0 ? self->SlotForHandle(handle) : nullptr;
    int finished = slot != nullptr ? slot->finished : 0;
    SoundSlot* expected = s_findPlaying(self, nullptr, handle);
    if (slot != nullptr && slot->finished != finished) return expected;  // the callback raced
    SoundSlot* actual = self->CSoundManager::FindPlaying(handle);
    ei::verify::Check("CSoundManager::FindPlaying", expected, actual, "handle %d", handle);
    return expected;
}
EI_VERIFY(0x006B7C10, FindPlayingVerify, 6, s_findPlaying);

// 006B9100: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
using SetSoundVolumeFn = int(__fastcall*)(CSoundManager*, void*, int, int, int);
static SetSoundVolumeFn s_setSoundVolume;
static int __fastcall SetSoundVolumeVerify(CSoundManager* self, void*, int handle, int volume, int pan) {
    return VoiceControl("CSoundManager::SetSoundVolume", self, handle,
                        [&] { return s_setSoundVolume(self, nullptr, handle, volume, pan); },
                        [&] { return self->CSoundManager::SetSoundVolume(handle, volume, pan); });
}
EI_VERIFY(0x006B9100, SetSoundVolumeVerify, 6, s_setSoundVolume);

// 006B92F0: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes).
using SetSoundPositionFn = int(__fastcall*)(CSoundManager*, void*, int, const float*, float, float);
static SetSoundPositionFn s_setSoundPosition;
static int __fastcall SetSoundPositionVerify(CSoundManager* self, void*, int handle, const float* position, float nearDist,
                                             float farDist) {
    return VoiceControl("CSoundManager::SetSoundPosition", self, handle,
                        [&] { return s_setSoundPosition(self, nullptr, handle, position, nearDist, farDist); },
                        [&] { return self->CSoundManager::SetSoundPosition(handle, position, nearDist, farDist); });
}
EI_VERIFY(0x006B92F0, SetSoundPositionVerify, 5, s_setSoundPosition);

namespace {
SoundSequence* ValidSequence(CSoundManager* m, std::uint32_t id) {
    if (m->enabled == 0 || (id & 0xFF) >= 8) return nullptr;
    SoundSequence* s = &m->sequences[id & 0xFF];
    return static_cast<std::uint32_t>(s->id) == id ? s : nullptr;
}
} // namespace

// 006BA400: SUB ESP,0xC; PUSH EBP; PUSH EDI (5 bytes).
using MoveSequenceFn = int(__fastcall*)(CSoundManager*, void*, std::uint32_t, const float*);
static MoveSequenceFn s_moveSequence;
static int __fastcall MoveSequenceVerify(CSoundManager* self, void*, std::uint32_t id, const float* position) {
    SoundSequence* s = ValidSequence(self, id);
    int handle = s != nullptr ? s->handle : -1;
    return VoiceControl("CSoundManager::MoveSequence", self, handle,
                        [&] { return s_moveSequence(self, nullptr, id, position); },
                        [&] { return self->CSoundManager::MoveSequence(id, position); },
                        reinterpret_cast<std::uint8_t*>(s), s != nullptr ? sizeof(SoundSequence) : 0);
}
EI_VERIFY(0x006BA400, MoveSequenceVerify, 5, s_moveSequence);

// 006BA5E0: SUB ESP,8; PUSH EBP; PUSH EDI (5 bytes).
using UpdateSequenceFn = int(__fastcall*)(CSoundManager*, void*, std::uint32_t, const float*, float, float, int);
static UpdateSequenceFn s_updateSequence;
static int __fastcall UpdateSequenceVerify(CSoundManager* self, void*, std::uint32_t id, const float* position,
                                           float nearDist, float farDist, int count) {
    SoundSequence* s = ValidSequence(self, id);
    int handle = s != nullptr ? s->handle : -1;
    return VoiceControl("CSoundManager::UpdateSequence", self, handle,
                        [&] { return s_updateSequence(self, nullptr, id, position, nearDist, farDist, count); },
                        [&] { return self->CSoundManager::UpdateSequence(id, position, nearDist, farDist, count); },
                        reinterpret_cast<std::uint8_t*>(s), s != nullptr ? sizeof(SoundSequence) : 0);
}
EI_VERIFY(0x006BA5E0, UpdateSequenceVerify, 5, s_updateSequence);

// 006B94A0: ours only. Afterwards no voice holds the handle and, unless a live voice had it, no
// deferred record either.
using HandleFn = int(__fastcall*)(CSoundManager*, void*, int);
static HandleFn s_unusedStopSound;
static int __fastcall StopSoundVerify(CSoundManager* self, void*, int handle) {
    SoundSlot* before = self->enabled != 0 ? self->SlotForHandle(handle) : nullptr;
    bool live = before != nullptr && before->finished == 0;
    int r = self->CSoundManager::StopSound(handle);
    bool ok = r == -1;
    if (self->enabled != 0 && handle != -1) {
        SoundSlot* after = self->SlotForHandle(handle);
        if (after != nullptr && after->playing != 0) ok = false;
        if (!live && self->deferred.Find(handle) != nullptr) ok = false;
    }
    ei::verify::Check("CSoundManager::StopSound", true, ok, "handle %d result %d", handle, r);
    return r;
}
EI_VERIFY(0x006B94A0, StopSoundVerify, 0, s_unusedStopSound);

// 006BA7C0: ours only. The sequence is free afterwards.
using IdFn = int(__fastcall*)(CSoundManager*, void*, std::uint32_t);
static IdFn s_unusedRemoveSequence;
static int __fastcall RemoveSequenceVerify(CSoundManager* self, void*, std::uint32_t id) {
    SoundSequence* s = ValidSequence(self, id);
    int r = self->CSoundManager::RemoveSequence(id);
    bool ok = r == -1 && (s == nullptr || (s->active == 0 && s->id == -1));
    ei::verify::Check("CSoundManager::RemoveSequence", true, ok, "id %X", id);
    return r;
}
EI_VERIFY(0x006BA7C0, RemoveSequenceVerify, 0, s_unusedRemoveSequence);

// 006B9950: ours only. A new emitter holds the names, position and intervals.
using AddEmitterFn = int(__fastcall*)(CSoundManager*, void*, const SoundNameArray*, const float*, float, float, int, int);
static AddEmitterFn s_unusedAddEmitter;
static int __fastcall AddEmitterVerify(CSoundManager* self, void*, const SoundNameArray* names, const float* position,
                                       float inner, float outer, int minInterval, int maxInterval) {
    int r = self->CSoundManager::AddEmitter(names, position, inner, outer, minInterval, maxInterval);
    bool ok = true;
    if (r >= 0) {
        const SoundEmitter& e = self->emitters[r];
        ok = r < 16 && e.active == 1 && e.names.size == names->size && std::memcmp(e.position, position, 12) == 0 &&
             e.minInterval == minInterval && e.maxInterval == maxInterval;
    } else {
        ok = r == -1;
    }
    ei::verify::Check("CSoundManager::AddEmitter", true, ok, "result %d", r);
    return r;
}
EI_VERIFY(0x006B9950, AddEmitterVerify, 0, s_unusedAddEmitter);

// 006BA2D0: ours only (plays the first sound). A started sequence is active under its new id.
using AddSequenceFn = int(__fastcall*)(CSoundManager*, void*, const SoundNameArray*, int, const float*, float, float, int,
                                       int, int(__fastcall*)(int, int));
static AddSequenceFn s_unusedAddSequence;
static int __fastcall AddSequenceVerify(CSoundManager* self, void*, const SoundNameArray* names, int priority,
                                        const float* position, float nearDist, float farDist, int randomOrder, int count,
                                        int(__fastcall* chooser)(int, int)) {
    int r = self->CSoundManager::AddSequence(names, priority, position, nearDist, farDist, randomOrder, count, chooser);
    bool ok = r == -1 || (ValidSequence(self, static_cast<std::uint32_t>(r)) != nullptr &&
                          self->sequences[r & 0xFF].active == 1 && self->sequences[r & 0xFF].handle != -1);
    ei::verify::Check("CSoundManager::AddSequence", true, ok, "result %X", r);
    return r;
}
EI_VERIFY(0x006BA2D0, AddSequenceVerify, 0, s_unusedAddSequence);

// 006BA140: ours only (plays a sound, may draw a random number).
using PlayNextFn = int(__fastcall*)(SoundSequence*, void*, CSoundManager*);
static PlayNextFn s_unusedPlayNext;
static int __fastcall PlayNextVerify(SoundSequence* self, void*, CSoundManager* manager) {
    int r = self->SoundSequence::PlayNext(manager);
    bool ok = (r == 0 || r == 1) && (r == 0 || self->handle != -1) &&
              (self->chooser != nullptr || self->names.size == 0 || (self->index >= 0 && self->index < self->names.size));
    ei::verify::Check("SoundSequence::PlayNext", true, ok, "result %d index %d handle %d", r, self->index, self->handle);
    return r;
}
EI_VERIFY(0x006BA140, PlayNextVerify, 0, s_unusedPlayNext);

// 006B7320 / 006B7380: ours only (Miles calls only).
static VoidFn s_unusedPause;
static void __fastcall PauseAllVerify(CSoundManager* self, void*) {
    self->CSoundManager::PauseAll();
    ei::verify::Check("CSoundManager::PauseAll", true, true, "");
}
EI_VERIFY(0x006B7320, PauseAllVerify, 0, s_unusedPause);

static VoidFn s_unusedResume;
static void __fastcall ResumeAllVerify(CSoundManager* self, void*) {
    self->CSoundManager::ResumeAll();
    ei::verify::Check("CSoundManager::ResumeAll", true, true, "");
}
EI_VERIFY(0x006B7380, ResumeAllVerify, 0, s_unusedResume);

// --- The music stream ---------------------------------------------------------------------------------

// 006BB020: ours only (shared with the stream thread). 0 exactly when there is no codec.
using RequestStreamFn = int(__fastcall*)(CSoundManager*, void*, const char*, int);
static RequestStreamFn s_unusedRequest;
static int __fastcall RequestStreamVerify(CSoundManager* self, void*, const char* name, int mode) {
    bool active = self->enabled != 0 && self->musicFader.volume != 0;
    int r = self->CSoundManager::RequestStream(name, mode);
    bool ok = active ? (r == (self->streamCodec != nullptr ? 1 : 0)) : r == 1;
    int state = self->streamState;
    ok = ok && state >= 0 && state <= 2 && self->switchMode >= 0 && self->switchMode <= 4;
    ei::verify::Check("CSoundManager::RequestStream", true, ok, "\"%s\" mode %d result %d state %d", name, mode, r, state);
    return r;
}
EI_VERIFY(0x006BB020, RequestStreamVerify, 0, s_unusedRequest);

// 006BB240: ours only.
static IntFn s_unusedStopStream;
static void __fastcall StopStreamVerify(CSoundManager* self, void*, int mode) {
    self->CSoundManager::StopStream(mode);
    int state = self->streamState;
    ei::verify::Check("CSoundManager::StopStream", true, state >= 0 && state <= 2, "mode %d state %d", mode, state);
}
EI_VERIFY(0x006BB240, StopStreamVerify, 0, s_unusedStopStream);

// --- The music system: ours only, the state each call must leave ---------------------------------------

using MusicFn = void(__fastcall*)(CMusicSystem*, void*);
using MusicNameFn = void(__fastcall*)(CMusicSystem*, void*, const char*);

static void* s_unusedZone;
static void __fastcall SetZoneModeVerify(CMusicSystem* self, void*, const char* allod, int dungeon) {
    self->CMusicSystem::SetZoneMode(allod, dungeon);
    bool ok = self->state == 1 && self->holding == 1 && self->theme >= 0 && self->theme < self->themeCount &&
              self->themes[self->theme].dungeon == dungeon && self->inCombat == 0;
    ei::verify::Check("CMusicSystem::SetZoneMode", true, ok, "\"%s\" %d theme %d", allod, dungeon, self->theme);
}
EI_VERIFY(0x006B4E30, SetZoneModeVerify, 0, s_unusedZone);

// 006B4710 (Create, once at startup): ours on the real object, then (when ours succeeded and the
// object had no themes before) the original into a scratch music system with an empty theme array
// and empty strings; the strings and every theme compared. Reading music.reg has no side effects.
// The scratch's themes are freed with the array's own SetSize(0).
namespace {
using LoadThemesFn = int(__fastcall*)(CMusicSystem*, void*);
LoadThemesFn s_loadThemes;
__declspec(naked) void __fastcall ScratchThemesSetSize(void*, void*, int, int) EI_ORIGINAL(0x006B59A0)

bool SameNames(const SoundNameArray& a, const SoundNameArray& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i)
        if (std::strcmp(a.data[i], b.data[i]) != 0) return false;
    return true;
}
} // namespace
static int __fastcall LoadThemesVerify(CMusicSystem* self, void*) {
    bool wasEmpty = self->themes == nullptr && self->themeCount == 0;
    alignas(4) std::uint8_t scratchBytes[sizeof(CMusicSystem)];
    std::memcpy(scratchBytes, self, sizeof(scratchBytes));
    int actual = self->CMusicSystem::LoadThemes();
    if (actual != 1 || !wasEmpty) {
        ei::verify::Check("CMusicSystem::LoadThemes", true, actual != 1 || self->themeCount % 2 == 0, "result %d, %d themes",
                          actual, self->themeCount);
        return actual;
    }
    CMusicSystem* scratch = reinterpret_cast<CMusicSystem*>(scratchBytes);
    scratch->themes = nullptr;
    scratch->themeCount = 0;
    scratch->themeMax = 0;
    new (&scratch->credits) mfc::CString();
    new (&scratch->mainMenu) mfc::CString();
    int expected = s_loadThemes(scratch, nullptr);
    bool ok = expected == actual && std::strcmp(scratch->credits, self->credits) == 0 &&
              std::strcmp(scratch->mainMenu, self->mainMenu) == 0 && scratch->themeCount == self->themeCount;
    int bad = -1;
    for (int i = 0; ok && i < self->themeCount; ++i) {
        const MusicTheme& a = scratch->themes[i];
        const MusicTheme& b = self->themes[i];
        if (std::strcmp(a.name, b.name) != 0 || a.dungeon != b.dungeon || std::strcmp(a.briefing, b.briefing) != 0 ||
            std::strcmp(a.constructor, b.constructor) != 0 || !SameNames(a.calm, b.calm) || !SameNames(a.combat, b.combat)) {
            ok = false;
            bad = i;
        }
    }
    ei::verify::Check("CMusicSystem::LoadThemes", true, ok, "result %d/%d, themes %d/%d, first difference %d", expected, actual,
                      scratch->themeCount, self->themeCount, bad);
    ScratchThemesSetSize(&scratch->themesVtable, nullptr, 0, -1);
    scratch->credits.~CString();
    scratch->mainMenu.~CString();
    return actual;
}
EI_VERIFY(0x006B4710, LoadThemesVerify, 7, s_loadThemes);

#define MUSIC_STATE_VERIFY(Method, addr, expectedState)                                            \
    static MusicFn s_unused##Method;                                                                 \
    static void __fastcall Method##Verify(CMusicSystem* self, void*) {                               \
        self->CMusicSystem::Method();                                                                \
        ei::verify::Check("CMusicSystem::" #Method, true, self->state == (expectedState), "state %d", self->state); \
    }                                                                                                \
    EI_VERIFY(addr, Method##Verify, 0, s_unused##Method);

MUSIC_STATE_VERIFY(Stop, 0x006B4F70, 0)
MUSIC_STATE_VERIFY(PlayCredits, 0x006B5170, 5)
MUSIC_STATE_VERIFY(PlayMainMenu, 0x006B51F0, 4)

static MusicNameFn s_unusedBriefing;
static void __fastcall PlayBriefingVerify(CMusicSystem* self, void*, const char* allod) {
    self->CMusicSystem::PlayBriefing(allod);
    ei::verify::Check("CMusicSystem::PlayBriefing", true, self->state == 2, "state %d", self->state);
}
EI_VERIFY(0x006B4F90, PlayBriefingVerify, 0, s_unusedBriefing);

static MusicNameFn s_unusedConstructor;
static void __fastcall PlayConstructorVerify(CMusicSystem* self, void*, const char* allod) {
    self->CMusicSystem::PlayConstructor(allod);
    ei::verify::Check("CMusicSystem::PlayConstructor", true, self->state == 3, "state %d", self->state);
}
EI_VERIFY(0x006B5080, PlayConstructorVerify, 0, s_unusedConstructor);

static MusicNameFn s_unusedJingle;
static void __fastcall PlayJingleVerify(CMusicSystem* self, void*, const char* name) {
    int state = self->state;
    self->CMusicSystem::PlayJingle(name);
    bool ok = state != 1 || (self->jinglePlaying == 1 && std::strncmp(self->jingle, name, std::strlen(name)) == 0);
    ei::verify::Check("CMusicSystem::PlayJingle", true, ok, "\"%s\" state %d", name, state);
}
EI_VERIFY(0x006B5270, PlayJingleVerify, 0, s_unusedJingle);

// 006B5350 (vtable slot 6, every frame): returns 1; when it ran past the jingle, inCombat follows the
// fight state.
using UpdateMusicFn = int(__fastcall*)(CMusicSystem*, void*);
static UpdateMusicFn s_unusedUpdateMusic;
static int __fastcall UpdateMusicVerify(CMusicSystem* self, void*) {
    bool active = self->state == 1 && self->holding != 1;
    int r = self->CMusicSystem::UpdateMusic();
    int fight = *reinterpret_cast<const volatile int*>(0x007AFC9C) == 2 ? 1 : 0;
    bool ok = r == 1 && (!active || self->jinglePlaying != 0 || self->inCombat == fight);
    ei::verify::Check("CMusicSystem::UpdateMusic", true, ok, "state %d combat %d/%d", self->state, self->inCombat, fight);
    return r;
}
EI_VERIFY(0x006B5350, UpdateMusicVerify, 0, s_unusedUpdateMusic);

// --- The per-frame tick: ours only ------------------------------------------------------------------

// 006B5CD0: ours only (ends the voice).
using SlotEndFn = void(__fastcall*)(SoundSlot*, void*, CSoundManager*);
static SlotEndFn s_unusedSlotEnd;
static void __fastcall SlotEndVerify(SoundSlot* self, void*, CSoundManager* manager) {
    self->SoundSlot::End(manager);
    ei::verify::Check("SoundSlot::End", true, self->playing == 0, "handle %d", self->handle);
}
EI_VERIFY(0x006B5CD0, SlotEndVerify, 0, s_unusedSlotEnd);

// 006B9B10 (every world frame): ours only (starts sounds, deletes deferred records, draws random
// numbers). Afterwards every active sequence's id names its slot and the emitter clock is not ahead
// of GetTickCount. (Starting a looping sequence sound may defer it, so the deferred count can grow;
// it is only logged.)
static VoidFn s_unusedTick;
static void __fastcall TickVerify(CSoundManager* self, void*) {
    int deferredBefore = self->enabled != 0 ? self->deferred.count : 0;
    self->CSoundManager::Tick();
    if (self->enabled == 0) {
        ei::verify::Check("CSoundManager::Tick", true, true, "");
        return;
    }
    bool ok = true;
    for (int k = 0; k < 8; ++k)
        if (self->sequences[k].active != 0 && (self->sequences[k].id & 0xFF) != k) ok = false;
    std::uint32_t now = gameapi::GetTickCount();
    if (now - self->emitterTick > 0x7FFFFFFFu) ok = false;
    ei::verify::Check("CSoundManager::Tick", true, ok, "deferred %d -> %d", deferredBefore, self->deferred.count);
}
EI_VERIFY(0x006B9B10, TickVerify, 0, s_unusedTick);

// 006B8250 plays a sound: ours only. -1, or a flat voice with the arguments and the handle returned,
// or (a looping sound without a voice) a deferred record with that handle.
using Play2DFn = int(__fastcall*)(CSoundManager*, void*, const char*, int, int, int, int, int, int, int);
static Play2DFn s_unusedPlay2D;
static int __fastcall Play2DVerify(CSoundManager* self, void*, const char* name, int priority, int volume, int pan, int loop,
                                   int heightFade, int speech, int samplePosition) {
    int handle = self->CSoundManager::Play2D(name, priority, volume, pan, loop, heightFade, speech, samplePosition);
    bool ok = handle == -1;
    const char* where = "none";
    if (!ok) {
        for (int i = 0; i < 15 && !ok; ++i) {
            const SoundSlot& s = self->slots[i];
            if (s.handle != handle) continue;
            where = "voice";
            ok = s.flat == 1 && s.baseVolume == volume && s.pan == pan && s.looping == loop && s.heightFade == heightFade &&
                 s.speech == speech && s.priority == priority && s.volume >= 0 && s.volume <= 0x7F;
        }
        if (!ok && loop != 0) {
            const DeferredNode* node = self->deferred.Find(handle);
            where = "deferred";
            ok = node && node->value->handle == handle && node->value->baseVolume == volume && node->value->flat == 1;
        }
    }
    ei::verify::Check("CSoundManager::Play2D", true, ok, "\"%s\" volume %d loop %d -> %d (%s)", name, volume, loop, handle,
                      where);
    return handle;
}
EI_VERIFY(0x006B8250, Play2DVerify, 0, s_unusedPlay2D);

// 006B8600 plays a positioned sound: ours only. -1, or a positional voice holding the arguments
// under the returned handle, or (a looping sound without a voice) a deferred record.
using Play3DFn = int(__fastcall*)(CSoundManager*, void*, const char*, int, const float*, float, float, int, int);
static Play3DFn s_unusedPlay3D;
static int __fastcall Play3DVerify(CSoundManager* self, void*, const char* name, int priority, const float* position,
                                   float nearDist, float farDist, int loop, int speech) {
    float where3[3];
    std::memcpy(where3, position, sizeof(where3));
    int handle = self->CSoundManager::Play3D(name, priority, position, nearDist, farDist, loop, speech);
    bool ok = handle == -1;
    const char* where = "none";
    if (!ok) {
        for (int i = 0; i < 15 && !ok; ++i) {
            const SoundSlot& s = self->slots[i];
            if (s.handle != handle) continue;
            where = "voice";
            ok = s.flat == 0 && s.heightFade == 0 && s.looping == loop && s.speech == speech && s.priority == priority &&
                 std::memcmp(s.position, where3, sizeof(where3)) == 0 && std::memcmp(&s.nearDistance, &nearDist, 4) == 0 &&
                 std::memcmp(&s.farDistance, &farDist, 4) == 0 && s.volume >= 0 && s.volume <= 0x7F;
        }
        if (!ok && loop != 0) {
            const DeferredNode* node = self->deferred.Find(handle);
            where = "deferred";
            ok = node && node->value->handle == handle && node->value->flat == 0 &&
                 std::memcmp(node->value->position, where3, sizeof(where3)) == 0;
        }
    }
    ei::verify::Check("CSoundManager::Play3D", true, ok, "\"%s\" loop %d -> %d (%s)", name, loop, handle, where);
    return handle;
}
EI_VERIFY(0x006B8600, Play3DVerify, 0, s_unusedPlay3D);

// 006B89F0 loads waves (archives, Miles memory, the cache): ours only. 0, or 1 with the voice
// playing under the newest handle and named `name`, a cache list node of that name (except for an
// MP3 found already cached), and the cache's byte count not negative.
using LoadSampleFn = int(__fastcall*)(CSoundManager*, void*, SoundSlot*, int, const char*, int);
static LoadSampleFn s_unusedLoadSample;
static int __fastcall LoadSampleVerify(CSoundManager* self, void*, SoundSlot* slot, int index, const char* name,
                                       int compressed) {
    int result = self->CSoundManager::LoadSample(slot, index, name, compressed);
    bool ok = result == 0;
    if (result == 1) {
        auto text = [](const std::uint32_t* s, std::size_t* n) {
            *n = static_cast<std::size_t>(s[1] - s[0]);
            return reinterpret_cast<const char*>(s[0]);
        };
        std::size_t length = std::strlen(name);
        std::size_t n = 0;
        const char* slotName = text(slot->name, &n);
        bool named = n == length && std::memcmp(slotName, name, n) == 0;
        bool cached = false;
        for (const WaveNode* node = self->cacheList.head; node && !cached; node = node->next) {
            const char* s = text(node->data.name, &n);
            cached = n == length && std::memcmp(s, name, n) == 0;
        }
        ok = slot->playing == 1 && slot->handle == self->nextHandle - 1 && named && (cached || compressed != 0) &&
             self->cacheBytes >= 0;
    }
    ei::verify::Check("CSoundManager::LoadSample", true, ok, "\"%s\" compressed %d -> %d (cache %d bytes)", name,
                      compressed, result, self->cacheBytes);
    return result;
}
EI_VERIFY(0x006B89F0, LoadSampleVerify, 0, s_unusedLoadSample);
