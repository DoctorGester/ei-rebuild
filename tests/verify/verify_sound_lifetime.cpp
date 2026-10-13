// Checks for the music system's and the sound manager's lifetime and engine object slots (A-H13;
// sound_music.cpp, sound_manager_lifetime.cpp). Every scenario reaches them: the application object
// builds both at startup and destroys them at exit; the briefing dialogue (ui_base_dialogue) fades
// the music out and in.
// - Class names and ids: against the original (5 stolen bytes, MOV EAX,imm32).
// - Constructors, destructors, ResetThemes, FadeOut / FadeIn: ours only, the state they leave.
// A-H14, the sound manager (sound_manager_lifetime.cpp): class name / id against the original;
// CurrentStreamName side by side (7 stolen bytes: PUSH -1; PUSH imm32); Create, the deleting
// destructor (exit), ClearWaveSources, AddWaveSource / SetPath (startup, 00456440), LoadWaveFile
// (map ambience) and the stream read callback (stream thread, any music) ours only.
#include "verify.h"

#include "../../src/game/sound.h"

#include "../../src/lib/gameapi.h"

#include <cstring>
#include <new>

namespace {
void* s_unused;
const char* (__fastcall* s_musicName)(CMusicSystem*);
int(__fastcall* s_musicId)(CMusicSystem*);

bool IsNone(const mfc::CString& s) { return std::strcmp(s, "none") == 0; }
bool Reset(const CMusicSystem* m) {
    return m->state == 0 && m->themes == nullptr && m->themeCount == 0 && m->themeMax == 0 && IsNone(m->credits) &&
           IsNone(m->mainMenu) && IsNone(m->jingle) && IsNone(m->combatTrack) && m->jinglePlaying == 0 &&
           m->calmDue == 0 && m->inCombat == 0 && m->holding == 0;
}
std::uintptr_t VtableOf(const void* object) { return *static_cast<const std::uintptr_t*>(object); }

const char* __fastcall MusicNameVerify(CMusicSystem* self) {
    const char* expected = s_musicName(self);
    ei::verify::Check("CMusicSystem::GetClassName", expected, MusicSystemClassName(self), "music %p", self);
    return expected;
}
int __fastcall MusicIdVerify(CMusicSystem* self) {
    int expected = s_musicId(self);
    ei::verify::Check("CMusicSystem::GetClassId", expected, MusicSystemClassId(self), "music %p", self);
    return expected;
}

CMusicSystem* __fastcall MusicConstructVerify(CMusicSystem* self) {
    CMusicSystem* result = self->Construct();
    bool ok = result == self && VtableOf(self) == CMusicSystem::kVtableAddress && self->id_ == -1 &&
              self->themesVtable == reinterpret_cast<void*>(CMusicSystem::kThemeArrayVtable) && Reset(self);
    ei::verify::Check("CMusicSystem::Construct", true, ok, "music %p", self);
    return result;
}
void __fastcall MusicDestructVerify(CMusicSystem* self) {
    self->DestructBody();
    ei::verify::Check("CMusicSystem::DestructBody", true, VtableOf(self) == CEngineObject::kVtableAddress,
                      "music %p", self);
}
CMusicSystem* __fastcall MusicDeletingVerify(CMusicSystem* self, void*, unsigned flags) {
    CMusicSystem* result = self->DeletingDestruct(flags);
    ei::verify::Check("CMusicSystem::DeletingDestruct", self, result, "flags %u", flags);
    return result;
}
void __fastcall MusicResetVerify(CMusicSystem* self) {
    self->ResetThemes();
    ei::verify::Check("CMusicSystem::ResetThemes", true, Reset(self), "music %p", self);
}
void __fastcall MusicFadeOutVerify(CMusicSystem* self) {
    self->FadeOut();
    ei::verify::Check("CMusicSystem::FadeOut", true, true, "");
}
void __fastcall MusicFadeInVerify(CMusicSystem* self) {
    self->FadeIn();
    ei::verify::Check("CMusicSystem::FadeIn", true, true, "");
}

const char* (__fastcall* s_soundName)(CSoundManager*);
int(__fastcall* s_soundId)(CSoundManager*);
mfc::CString*(__fastcall* s_streamName)(CSoundManager*, void*, mfc::CString*);

const char* __fastcall SoundNameVerify(CSoundManager* self) {
    const char* expected = s_soundName(self);
    ei::verify::Check("CSoundManager::GetClassName", expected, SoundManagerClassName(self), "manager %p", self);
    return expected;
}
int __fastcall SoundIdVerify(CSoundManager* self) {
    int expected = s_soundId(self);
    ei::verify::Check("CSoundManager::GetClassId", expected, SoundManagerClassId(self), "manager %p", self);
    return expected;
}
int __fastcall SoundCreateVerify(CSoundManager* self) {
    int result = self->CreateBody();
    ei::verify::Check("CSoundManager::CreateBody", 1, result, "manager %p", self);
    return result;
}
CSoundManager* __fastcall SoundDeletingVerify(CSoundManager* self, void*, unsigned flags) {
    CSoundManager* result = self->DeletingDestruct(flags);
    ei::verify::Check("CSoundManager::DeletingDestruct", self, result, "flags %u", flags);
    return result;
}
void __fastcall ClearSourcesVerify(CSoundManager* self) {
    self->ClearWaveSources();
    const auto& list = self->waveSources;
    ei::verify::Check("CSoundManager::ClearWaveSources", true,
                      list.count == 0 && list.head == nullptr && list.tail == nullptr && list.blocks == nullptr, "count %d",
                      list.count);
}
int __fastcall AddSourceVerify(CSoundManager* self, void*, const char* path) {
    int before = self->waveSources.count;
    int result = self->AddWaveSource(path);
    bool ok;
    if (self->enabled == 0) ok = result == 1 && self->waveSources.count == before;
    else if (result == 0) ok = self->waveSources.count == before;
    else ok = result == 1 && self->waveSources.count == before + 1 && self->waveSources.tail != nullptr &&
              self->waveSources.tail->data != nullptr;
    ei::verify::Check("CSoundManager::AddWaveSource", true, ok, "%s: result %d, sources %d -> %d", path, result, before,
                      self->waveSources.count);
    return result;
}
void* __fastcall LoadWaveVerify(CSoundManager* self, void*, const char* name, std::uint32_t* size,
                                CMappedResource** source) {
    CMappedResource* found = nullptr;
    void* data = self->LoadWaveFile(name, size, &found);
    bool ok = true;
    if (data != nullptr) {
        ok = found != nullptr && self->waveSources.Find(found) != nullptr;
        if (source != nullptr) *source = found;
    }
    ei::verify::Check("CSoundManager::LoadWaveFile", true, ok, "%s: data %p source %p", name, data, found);
    return data;
}
int __fastcall SetPathVerify(CSoundManager* self, void*, const char* folder, const char* registryPath) {
    int result = self->SetPath(folder, registryPath);
    bool ok = result == 0 || result == 1;
    if (self->enabled != 0) {
        int n = self->soundPath.GetLength();
        ok = ok && n > 0 && static_cast<const char*>(self->soundPath)[n - 1] == '\\';
    }
    ei::verify::Check("CSoundManager::SetPath", true, ok, "%s %s: result %d", folder, registryPath, result);
    return result;
}
mfc::CString* __fastcall StreamNameVerify(CSoundManager* self, void*, mfc::CString* result) {
    s_streamName(self, nullptr, result);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* ours = new (storage) mfc::CString(self->CurrentStreamName());
    ei::verify::Check("CSoundManager::CurrentStreamName", std::strcmp(*result, *ours) == 0, true, "original \"%s\" ours \"%s\"",
                      static_cast<const char*>(*result), static_cast<const char*>(*ours));
    ours->~CString();
    return result;
}
int __stdcall StreamReadVerify(CSoundManager* manager, void* buffer, int size, int offset) {
    int start = offset >= 0 ? offset : manager->streamReadPosition;
    int result = SoundStreamRead(manager, buffer, size, offset);
    bool ok = result == 0 || (result <= size && manager->streamReadPosition == start + result);
    ei::verify::Check("SoundStreamRead", true, ok, "size %d offset %d result %d position %d", size, offset, result,
                      manager->streamReadPosition);
    return result;
}
} // namespace

EI_VERIFY(0x006B6290, SoundNameVerify, 5, s_soundName);
EI_VERIFY(0x006B62A0, SoundIdVerify, 5, s_soundId);
EI_VERIFY(0x006B6D70, SoundCreateVerify, 0, s_unused);
EI_VERIFY(0x006B62B0, SoundDeletingVerify, 0, s_unused);
EI_VERIFY(0x006B7060, ClearSourcesVerify, 0, s_unused);
EI_VERIFY(0x006B7AB0, AddSourceVerify, 0, s_unused);
EI_VERIFY(0x006B8960, LoadWaveVerify, 0, s_unused);
EI_VERIFY(0x006BAE20, SetPathVerify, 0, s_unused);
EI_VERIFY(0x006BAEA0, StreamNameVerify, 7, s_streamName);
EI_VERIFY(0x006BA950, StreamReadVerify, 0, s_unused);
EI_VERIFY(0x006B4470, MusicNameVerify, 5, s_musicName);
EI_VERIFY(0x006B4480, MusicIdVerify, 5, s_musicId);
EI_VERIFY(0x006B4350, MusicConstructVerify, 0, s_unused);
EI_VERIFY(0x006B44B0, MusicDestructVerify, 0, s_unused);
EI_VERIFY(0x006B4490, MusicDeletingVerify, 0, s_unused);
EI_VERIFY(0x006B4690, MusicResetVerify, 0, s_unused);
EI_VERIFY(0x006B4E10, MusicFadeOutVerify, 0, s_unused);
EI_VERIFY(0x006B4E20, MusicFadeInVerify, 0, s_unused);

// A-H15: the sound manager's constructor / destructor body (startup / exit) and the stream reset
// 006BA9C0 (Reset 006B73E0 at startup and every zone load, the stream close 006BAC90): ours only.
void __fastcall SoundResetStream(void* stream);
namespace {
CSoundManager* __fastcall SoundConstructVerify(CSoundManager* self) {
    CSoundManager* result = self->Construct();
    bool ok = result == self && *reinterpret_cast<std::uintptr_t*>(self) == CSoundManager::kVtableAddress &&
              self->waveSources.count == 0 && self->cacheList.count == 0 && self->cacheList.blockSize == 10 &&
              self->markers.data == nullptr && self->musicFader.volume == 100;
    ei::verify::Check("CSoundManager::Construct", true, ok, "manager %p", self);
    return result;
}
void __fastcall SoundDestructVerify(CSoundManager* self) {
    self->DestructBody();
    bool ok = *reinterpret_cast<std::uintptr_t*>(self) == CEngineObject::kVtableAddress && self->cacheList.head == nullptr &&
              self->waveSources.head == nullptr;
    ei::verify::Check("CSoundManager::DestructBody", true, ok, "manager %p", self);
}
void __fastcall SoundResetStreamVerify(void* stream) {
    SoundResetStream(stream);
    auto* m = reinterpret_cast<CSoundManager*>(static_cast<char*>(stream) - offsetof(CSoundManager, streamState));
    bool ok = m->streamState == 0 && m->streamSample == nullptr && m->switchMode == 4 && m->streamPosition == -1 &&
              m->musicFader.volume == 100 && m->musicFader.level == 127 && m->musicFader.mode == 0 &&
              m->streamName.GetLength() == 0 && m->nextName.GetLength() == 0;
    ei::verify::Check("CSoundManager::ResetStream", true, ok, "manager %p level %d", m, m->musicFader.level);
}
} // namespace
EI_VERIFY(0x006B6040, SoundConstructVerify, 0, s_unused);
EI_VERIFY(0x006B65D0, SoundDestructVerify, 0, s_unused);
EI_VERIFY(0x006BA9C0, SoundResetStreamVerify, 0, s_unused);

// A-H18: Open (startup 00456440, when the sound option is on), Close (Open's first call, the
// destructor at exit) and Reset (the constructor, Close): ours only, the state they leave. Every
// scenario reaches them.
namespace {
bool VoicesFree(const CSoundManager* m) {
    for (int i = 0; i < 15; ++i)
        if (m->slots[i].handle != -1 || m->slots[i].playing != 0) return false;
    return true;
}
bool ResetState(const CSoundManager* m) {
    bool ok = m->waveDriver == nullptr && m->midiDriver == nullptr && m->streamThread == nullptr && m->midiThread == nullptr &&
              m->slots == nullptr && m->enabled == 0 && m->nextHandle == 0 && m->effectsVolume == 100 &&
              m->speechVolume == 100 && m->midiFader.volume == 100 && m->midiFader.level == 127 &&
              m->soundPath.GetLength() == 0 && m->streamState == 0 && m->switchMode == 4;
    for (const SoundEmitter& e : m->emitters) ok = ok && e.active == 0 && e.minInterval == -1;
    for (const SoundSequence& q : m->sequences) ok = ok && q.active == 0 && q.id == -1 && q.handle == -1;
    return ok;
}
int __fastcall SoundOpenVerify(CSoundManager* self, void*, void* window, int reverb, int device, unsigned rate, int bits) {
    int result = self->Open(window, reverb, device, rate, bits);
    bool ok;
    if (result == 0) ok = ResetState(self);
    else ok = result == 1 && self->enabled == 1 && self->waveDriver != nullptr && self->slots != nullptr &&
              self->reverbOn == reverb && VoicesFree(self);
    ei::verify::Check("CSoundManager::Open", true, ok, "result %d device %d rate %u stream thread %p", result, device, rate,
                      self->streamThread);
    return result;
}
void __fastcall SoundCloseVerify(CSoundManager* self) {
    self->Close();
    bool ok = ResetState(self) && self->cacheList.count == 0 && self->deferred.count == 0 && self->waveSources.count == 0;
    ei::verify::Check("CSoundManager::Close", true, ok, "manager %p", self);
}
void __fastcall SoundResetVerify(CSoundManager* self) {
    self->Reset();
    ei::verify::Check("CSoundManager::Reset", true, ResetState(self), "manager %p", self);
}
// A-H25: the MP3 stream's open (Open, so every start with sound on) and close (only when the
// codec or a sample cannot be had: no scenario). They take and give back Miles samples and
// memory: ours only, the state they leave.
bool StreamClosed(const CSoundManager* m) {
    return m->streamState == 0 && m->streamSample == nullptr && m->streamDecoder == nullptr && m->streamFile == nullptr &&
           m->streamCodec == nullptr && m->streamBuffers[0] == nullptr && m->streamBuffers[1] == nullptr &&
           m->streamBuffers[2] == nullptr && m->streamBufferSize == -1 && m->switchMode == 4;
}
bool StreamOpened(const CSoundManager* m) {
    bool ok = m->streamState == 0 && m->streamSample != nullptr && m->streamDecoder != nullptr && m->streamCodec != nullptr &&
              m->streamBufferSize > 0 && m->unknown124 == 0 && m->unknown128 == 1 && m->unknown12C == 2 &&
              m->streamPosition == 0 && m->musicFader.factor == 1.0f && m->musicFader.mode == 0 &&
              m->musicFader.finished == 0 && m->musicFader.holdUntil == 0 && m->musicFader.level >= 0 &&
              m->musicFader.level <= 127;
    for (void* buffer : m->streamBuffers) {
        if (buffer == nullptr || !ok) return false;
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        ok = bytes[0] == 0 && bytes[m->streamBufferSize - 1] == 0;
    }
    return ok;
}
CSoundManager* ManagerOfStream(void* stream) {
    return reinterpret_cast<CSoundManager*>(static_cast<char*>(stream) - offsetof(CSoundManager, streamState));
}
int __fastcall SoundOpenStreamVerify(void* stream, void*, void* driver) {
    CSoundManager* m = ManagerOfStream(stream);
    int result = m->OpenStream(driver);
    bool ok = result == 1 ? StreamOpened(m) : result == 0 && StreamClosed(m);
    ei::verify::Check("CSoundManager::OpenStream", true, ok, "result %d buffers of %d level %d", result, m->streamBufferSize,
                      m->musicFader.level);
    return result;
}
int __fastcall SoundCloseStreamVerify(void* stream) {
    CSoundManager* m = ManagerOfStream(stream);
    int result = m->CloseStream();
    ei::verify::Check("CSoundManager::CloseStream", true, result == 0 && StreamClosed(m), "result %d", result);
    return result;
}
} // namespace
EI_VERIFY(0x006BAAC0, SoundOpenStreamVerify, 0, s_unused);
EI_VERIFY(0x006BAC90, SoundCloseStreamVerify, 0, s_unused);
EI_VERIFY(0x006B6A30, SoundOpenVerify, 0, s_unused);
EI_VERIFY(0x006B6D80, SoundCloseVerify, 0, s_unused);
EI_VERIFY(0x006B73E0, SoundResetVerify, 0, s_unused);
