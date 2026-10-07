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
