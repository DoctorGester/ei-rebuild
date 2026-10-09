// Verify wrappers for the landscape's ambient sounds (terrain_ambience.cpp). Terrain::PlayAmbience
// runs every frame of a zone whose terrain has ambient sounds (flag 8; zone1 does), StopAmbience
// when they are switched off (00578940, the client's leave / zone change), AmbienceTimeOfDay with
// each one-shot.
#include "verify.h"

#include "../../src/game/terrain.h"
#include "../../src/game/world.h"

#include <cstring>

namespace {

// ---- CWorldClient::AmbienceTimeOfDay -------------------------------------------------------------
// 006CBC00: PUSH ECX; FILD [ECX+0x64]; PUSH ESI (5 bytes). Constructs a CString: the original into
// a scratch one (destroyed after), ours into `out`; the texts compared.
mfc::CString*(__fastcall* s_timeOfDay)(CWorldClient*, void*, mfc::CString*);

mfc::CString* __fastcall TimeOfDayVerify(CWorldClient* self, void*, mfc::CString* out) {
    alignas(mfc::CString) unsigned char scratch[sizeof(mfc::CString)];
    auto* original = s_timeOfDay(self, nullptr, reinterpret_cast<mfc::CString*>(scratch));
    mfc::CString* ours = self->AmbienceTimeOfDay(out);
    ei::verify::Check("CWorldClient::AmbienceTimeOfDay", 0,
                      std::strcmp(static_cast<const char*>(*original), static_cast<const char*>(*ours)), "%s / %s",
                      static_cast<const char*>(*original), static_cast<const char*>(*ours));
    ei::verify::Check("CWorldClient::AmbienceTimeOfDay", true, ours == out, "returns out");
    original->~CString();
    return ours;
}
EI_VERIFY(0x006CBC00, TimeOfDayVerify, 5, s_timeOfDay);

// ---- Terrain::PlayAmbience / StopAmbience ----------------------------------------------------------
// They play and stop sounds and draw from the random generator: ours only; afterwards the three
// loop slots hold distinct classes 0..15 (with a sound) or -1.
void(__fastcall* s_playAmbience)(Terrain*, void*);
void(__fastcall* s_stopAmbience)(Terrain*, void*);

bool SlotsConsistent(const Terrain* t) {
    for (int i = 0; i < 3; ++i) {
        int c = t->ambientClasses[i];
        if (c < -1 || c > 15) return false;
        for (int j = 0; j < i; ++j) {
            if (c >= 0 && t->ambientClasses[j] == c) return false;
        }
    }
    return true;
}

void __fastcall PlayAmbienceVerify(Terrain* self, void*) {
    self->PlayAmbience();
    ei::verify::Check("Terrain::PlayAmbience", true, SlotsConsistent(self), "loop slots %d %d %d",
                      self->ambientClasses[0], self->ambientClasses[1], self->ambientClasses[2]);
}
EI_VERIFY(0x006CB390, PlayAmbienceVerify, 0, s_playAmbience);

void __fastcall StopAmbienceVerify(Terrain* self, void*) {
    self->StopAmbience();
    bool cleared = (self->landscapeFlags & 8) == 0;
    for (int i = 0; i < 3; ++i) cleared = cleared && self->ambientClasses[i] == -1;
    ei::verify::Check("Terrain::StopAmbience", true, cleared, "flag and slots cleared");
}
EI_VERIFY(0x006CBCD0, StopAmbienceVerify, 0, s_stopAmbience);

} // namespace
