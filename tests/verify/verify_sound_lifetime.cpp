// Checks for the music system's and the sound manager's lifetime and engine object slots (A-H13;
// sound_music.cpp, sound_manager_lifetime.cpp). Every scenario reaches them: the application object
// builds both at startup and destroys them at exit; the briefing dialogue (ui_base_dialogue) fades
// the music out and in.
// - Class names and ids: against the original (5 stolen bytes, MOV EAX,imm32).
// - Constructors, destructors, ResetThemes, FadeOut / FadeIn: ours only, the state they leave.
#include "verify.h"

#include "../../src/game/sound.h"

#include <cstring>

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
} // namespace

EI_VERIFY(0x006B4470, MusicNameVerify, 5, s_musicName);
EI_VERIFY(0x006B4480, MusicIdVerify, 5, s_musicId);
EI_VERIFY(0x006B4350, MusicConstructVerify, 0, s_unused);
EI_VERIFY(0x006B44B0, MusicDestructVerify, 0, s_unused);
EI_VERIFY(0x006B4490, MusicDeletingVerify, 0, s_unused);
EI_VERIFY(0x006B4690, MusicResetVerify, 0, s_unused);
EI_VERIFY(0x006B4E10, MusicFadeOutVerify, 0, s_unused);
EI_VERIFY(0x006B4E20, MusicFadeInVerify, 0, s_unused);
