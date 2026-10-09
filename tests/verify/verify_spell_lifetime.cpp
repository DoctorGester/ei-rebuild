// Checks for spell_lifetime.cpp (MapEffect, CSpell and CSpellEffect constructors and destructors).
// Scenarios spells and spells_cast (every cast makes a CSpellEffect with its CSpell; spells are
// built by the spell constructor and released again).
// - Constructors: the original on a scratch copy of the memory (bytes neither writes stay equal),
//   ours on the real object; the whole object and the result compared. They allocate nothing.
// - Destructors: ours only (they release references and the world's timed object); the state left
//   is checked. Slot 0's wrappers run ours with bit 0 cleared and free the memory themselves.
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/spell.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

int FirstDifference(const void* a, const void* b, std::size_t n) {
    auto* x = static_cast<const unsigned char*>(a);
    auto* y = static_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < n; ++i)
        if (x[i] != y[i]) return static_cast<int>(i);
    return -1;
}

template <typename T>
T* ConstructBoth(const char* name, T*(__fastcall* original)(T*), T* self, std::size_t size) {
    auto* scratch = static_cast<unsigned char*>(gamecrt::operator_new(size));
    std::memcpy(scratch, self, size);
    T* expected = original(reinterpret_cast<T*>(scratch));
    T* actual = self->Construct();
    int diff = FirstDifference(scratch, self, size);
    ei::verify::Check(name, true, diff < 0 && expected == reinterpret_cast<T*>(scratch) && actual == self,
                      "object %p: first difference at +0x%X", static_cast<void*>(self), diff);
    gamecrt::operator_delete(scratch);
    return actual;
}

// 00688510: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX],CL (6). 0068B1F0 / 0067E510: PUSH -1; PUSH imm32 (7).
MapEffect*(__fastcall* s_effectCtor)(MapEffect*);
CSpell*(__fastcall* s_spellCtor)(CSpell*);
CSpellEffect*(__fastcall* s_castCtor)(CSpellEffect*);

MapEffect* __fastcall EffectCtorVerify(MapEffect* self) {
    return ConstructBoth("MapEffect::Construct", s_effectCtor, self, sizeof(MapEffect));
}
CSpell* __fastcall SpellCtorVerify(CSpell* self) { return ConstructBoth("CSpell::Construct", s_spellCtor, self, sizeof(CSpell)); }
CSpellEffect* __fastcall CastCtorVerify(CSpellEffect* self) {
    return ConstructBoth("CSpellEffect::Construct", s_castCtor, self, sizeof(CSpellEffect));
}

bool Destroyed(const CSpell* s) {
    static const std::int8_t none[8] = {};
    return VtableOf(s) == CEngineObject::kVtableAddress && s->owner_ == nullptr &&
           std::memcmp(s->modifiers_, none, sizeof(none)) == 0;
}
bool Destroyed(CSpellEffect* e) {
    return VtableOf(e) == CSpellEffect::kBaseVtableAddress && !e->visual && !e->sound && !e->light && !e->caster &&
           !e->target && Destroyed(e->Spell());
}

void __fastcall SpellDestructVerify(CSpell* self) {
    self->Destruct();
    ei::verify::Check("CSpell::Destruct", true, Destroyed(self), "spell %p", static_cast<void*>(self));
}
void __fastcall CastDestructVerify(CSpellEffect* self) {
    self->Destruct();
    ei::verify::Check("CSpellEffect::Destruct", true, Destroyed(self), "effect %p", static_cast<void*>(self));
}

template <typename T>
T* DeleteOurs(const char* name, T* self, unsigned flags) {
    T* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check(name, true, result == self && Destroyed(self), "object %p", static_cast<void*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}
CSpell* __fastcall SpellDeleteVerify(CSpell* self, void*, unsigned flags) {
    return DeleteOurs("CSpell::DeletingDestruct", self, flags);
}
CSpellEffect* __fastcall CastDeleteVerify(CSpellEffect* self, void*, unsigned flags) {
    return DeleteOurs("CSpellEffect::DeletingDestruct", self, flags);
}

} // namespace

EI_VERIFY(0x00688510, EffectCtorVerify, 6, s_effectCtor);
EI_VERIFY(0x0068B1F0, SpellCtorVerify, 7, s_spellCtor);
EI_VERIFY(0x0067E510, CastCtorVerify, 7, s_castCtor);
EI_VERIFY(0x0068B2C0, SpellDestructVerify, 0, s_unused);
EI_VERIFY(0x0067E5D0, CastDestructVerify, 0, s_unused);
EI_VERIFY(0x0068B2A0, SpellDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067E5B0, CastDeleteVerify, 0, s_unused);
