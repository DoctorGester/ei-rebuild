// Checks for world_effect_lifetime.cpp (effect constructors / destructors, the state field).
//
// - Stream and field constructors, DeltaField::Empty: side by side on copies of the bytes.
// - Effect constructors: ours on the real object, then the original on a scratch copy of the fresh
//   memory (our memory; bytes neither writes stay equal); the whole object compared. Nothing they do has
//   outside effects (every Reset they run finds no sound, light, source or bolt).
// - Destructors: ours only, run by the slot 0 wrappers (flag bit 0 cleared, the memory freed by the
//   wrapper): CEngineObject's vtable and no owner, the four streams back to CStream's vtable, and
//   the class's counted pointer or sound handle gone. The bodies' own wrappers run only when
//   something calls a body directly (only slot 0 does).
#include "verify.h"

#include "../../src/game/world_effect.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

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

// --- Streams and the state field ---------------------------------------------------------------------

// 0066C4F0: MOV EAX,ECX (2); XOR ECX,ECX (2); MOV [EAX+8],imm32 (7).
CMemoryStream*(__fastcall* s_queueStream)(CMemoryStream*);
CMemoryStream* __fastcall QueueStreamVerify(CMemoryStream* self) {
    alignas(4) unsigned char scratch[sizeof(CMemoryStream)];
    std::memcpy(scratch, self, sizeof(scratch));
    CMemoryStream* expected = s_queueStream(reinterpret_cast<CMemoryStream*>(scratch));
    CMemoryStream* actual = self->ConstructQueueStream();
    int diff = FirstDifference(scratch, self, sizeof(scratch));
    ei::verify::Check("CMemoryStream::ConstructQueueStream", true,
                      diff < 0 && expected == reinterpret_cast<CMemoryStream*>(scratch) && actual == self,
                      "first difference at +0x%X", diff);
    return actual;
}

// 00677EC0 / 0066A300: PUSH EBX; MOV EAX,ECX; PUSH ESI; PUSH EDI (5).
DeltaField*(__fastcall* s_fieldCtor)(DeltaField*);
DeltaField*(__fastcall* s_fieldCtor2)(DeltaField*);
DeltaField* FieldCtorCheck(const char* name, DeltaField*(__fastcall* original)(DeltaField*), DeltaField* self) {
    alignas(4) unsigned char scratch[sizeof(DeltaField)];
    std::memcpy(scratch, self, sizeof(scratch));
    DeltaField* expected = original(reinterpret_cast<DeltaField*>(scratch));
    DeltaField* actual = self->Construct();
    int diff = FirstDifference(scratch, self, sizeof(scratch));
    ei::verify::Check(name, true, diff < 0 && expected == reinterpret_cast<DeltaField*>(scratch) && actual == self,
                      "first difference at +0x%X", diff);
    return actual;
}
DeltaField* __fastcall FieldCtorVerify(DeltaField* self) { return FieldCtorCheck("DeltaField::Construct", s_fieldCtor, self); }
DeltaField* __fastcall FieldCtor2Verify(DeltaField* self) {
    return FieldCtorCheck("DeltaField::Construct@0066A300", s_fieldCtor2, self);
}

// 00675F70: PUSH ESI; MOV ESI,ECX; PUSH 0 (5). Resize(0) never reallocates, so ours runs first, the
// field is put back and the original runs on it.
void(__fastcall* s_fieldEmpty)(DeltaField*);
void __fastcall FieldEmptyVerify(DeltaField* self) {
    alignas(4) unsigned char before[sizeof(DeltaField)], after[sizeof(DeltaField)];
    std::memcpy(before, self, sizeof(before));
    self->Empty();
    std::memcpy(after, self, sizeof(after));
    std::memcpy(self, before, sizeof(before));
    s_fieldEmpty(self);
    int diff = FirstDifference(after, self, sizeof(after));
    ei::verify::Check("DeltaField::Empty", true, diff < 0, "field %p: first difference at +0x%X", self, diff);
}

// --- Constructors -------------------------------------------------------------------------------------

// Ours on the real object, then the original on scratch memory (ours, holding the same bytes): the
// game reads the heap's address order (SV2f).
template <typename T>
T* ConstructBoth(const char* name, T*(__fastcall* original)(T*), T* self) {
    std::vector<unsigned char> scratchBytes(sizeof(T));
    unsigned char* scratch = scratchBytes.data();
    std::memcpy(scratch, self, sizeof(T));
    T* actual = self->Construct();
    T* expected = original(reinterpret_cast<T*>(scratch));
    int diff = FirstDifference(scratch, self, sizeof(T));
    ei::verify::Check(name, true, diff < 0 && expected == reinterpret_cast<T*>(scratch) && actual == self,
                      "object %p: first difference at +0x%X", self, diff);
    return actual;   // the scratch holds no buffers or references
}

// Each constructor starts with PUSH -1; PUSH imm32 (7).
CEffectSound*(__fastcall* s_soundCtor)(CEffectSound*);
CEffectSoundSeq*(__fastcall* s_seqCtor)(CEffectSoundSeq*);
CEffectPointLight*(__fastcall* s_lightCtor)(CEffectPointLight*);
CEffectParticle*(__fastcall* s_particleCtor)(CEffectParticle*);
CEffectWall*(__fastcall* s_wallCtor)(CEffectWall*);
CEffectLightning*(__fastcall* s_lightningCtor)(CEffectLightning*);
CEffectAmbientSound*(__fastcall* s_ambientCtor)(CEffectAmbientSound*);
CEffectAmbientSound* __fastcall AmbientCtorVerify(CEffectAmbientSound* self) {
    return ConstructBoth("CEffectAmbientSound::Construct", s_ambientCtor, self);
}
CEffectSound* __fastcall SoundCtorVerify(CEffectSound* self) { return ConstructBoth("CEffectSound::Construct", s_soundCtor, self); }
CEffectSoundSeq* __fastcall SeqCtorVerify(CEffectSoundSeq* self) {
    return ConstructBoth("CEffectSoundSeq::Construct", s_seqCtor, self);
}
CEffectPointLight* __fastcall LightCtorVerify(CEffectPointLight* self) {
    return ConstructBoth("CEffectPointLight::Construct", s_lightCtor, self);
}
CEffectParticle* __fastcall ParticleCtorVerify(CEffectParticle* self) {
    return ConstructBoth("CEffectParticle::Construct", s_particleCtor, self);
}
CEffectWall* __fastcall WallCtorVerify(CEffectWall* self) { return ConstructBoth("CEffectWall::Construct", s_wallCtor, self); }
CEffectLightning* __fastcall LightningCtorVerify(CEffectLightning* self) {
    return ConstructBoth("CEffectLightning::Construct", s_lightningCtor, self);
}

// --- Destructors ----------------------------------------------------------------------------------------

// What every effect destructor leaves: CEngineObject's vtable, no owner, CStream's vtable in the
// state field's four streams.
bool DestroyedBase(const CEffect* e) {
    return VtableOf(e) == CEngineObject::kVtableAddress && e->owner_ == nullptr &&
           VtableOf(&e->state_.snapshot[0]) == CStream::kVtableAddress &&
           VtableOf(&e->state_.snapshot[1]) == CStream::kVtableAddress &&
           VtableOf(&e->state_.out[0]) == CStream::kVtableAddress && VtableOf(&e->state_.out[1]) == CStream::kVtableAddress;
}

bool Destroyed(const CEffectSound* e) { return DestroyedBase(e) && e->sound_ == -1 && e->looping_ == 1; }
bool Destroyed(const CEffectSoundSeq* e) { return DestroyedBase(e) && e->sound_ == -1; }
bool Destroyed(const CEffectAmbientSound* e) { return DestroyedBase(e) && e->sound_ == -1; }
bool Destroyed(const CEffectPointLight* e) { return DestroyedBase(e) && e->light_ == nullptr; }
bool Destroyed(const CEffectParticle* e) { return DestroyedBase(e) && e->source_ == nullptr && e->attachedTo_ == -1; }
bool Destroyed(const CEffectLightning* e) { return DestroyedBase(e) && e->bolt_ == nullptr && e->toObject_ == -1; }
bool Destroyed(const CEffectArrow* e);
bool Destroyed(const CEffectArrowFigure* e);
bool Destroyed(const CEffectShowVisibility* e);
bool Destroyed(const CEffectMoshka* e);
bool Destroyed(const CEffectTornado* e);

// A destructor body called directly (only slot 0 calls them).
template <typename T>
void DestructOurs(const char* name, T* self) {
    self->Destruct();
    ei::verify::Check(name, true, Destroyed(self), "object %p", self);
}

// Slot 0: ours with bit 0 cleared, the state checked, then the memory freed here.
template <typename T>
T* DeleteOurs(const char* name, T* self, unsigned flags) {
    std::uintptr_t vtable = VtableOf(self);
    T* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check(name, true, result == self && vtable == T::Vtable() && Destroyed(self), "object %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

void __fastcall SoundDestructVerify(CEffectSound* self) { DestructOurs("CEffectSound::Destruct", self); }
void __fastcall SeqDestructVerify(CEffectSoundSeq* self) { DestructOurs("CEffectSoundSeq::Destruct", self); }
void __fastcall AmbientDestructVerify(CEffectAmbientSound* self) { DestructOurs("CEffectAmbientSound::Destruct", self); }
CEffectAmbientSound* __fastcall AmbientDeleteVerify(CEffectAmbientSound* self, void*, unsigned flags) {
    return DeleteOurs("CEffectAmbientSound::DeletingDestruct", self, flags);
}
void __fastcall LightDestructVerify(CEffectPointLight* self) { DestructOurs("CEffectPointLight::Destruct", self); }
void __fastcall ParticleDestructVerify(CEffectParticle* self) { DestructOurs("CEffectParticle::Destruct", self); }
void __fastcall WallDestructVerify(CEffectWall* self) { DestructOurs("CEffectWall::Destruct", self); }
void __fastcall LightningDestructVerify(CEffectLightning* self) { DestructOurs("CEffectLightning::Destruct", self); }

CEffectSound* __fastcall SoundDeleteVerify(CEffectSound* self, void*, unsigned flags) {
    return DeleteOurs("CEffectSound::DeletingDestruct", self, flags);
}
CEffectSoundSeq* __fastcall SeqDeleteVerify(CEffectSoundSeq* self, void*, unsigned flags) {
    return DeleteOurs("CEffectSoundSeq::DeletingDestruct", self, flags);
}
CEffectPointLight* __fastcall LightDeleteVerify(CEffectPointLight* self, void*, unsigned flags) {
    return DeleteOurs("CEffectPointLight::DeletingDestruct", self, flags);
}
CEffectParticle* __fastcall ParticleDeleteVerify(CEffectParticle* self, void*, unsigned flags) {
    return DeleteOurs("CEffectParticle::DeletingDestruct", self, flags);
}
CEffectWall* __fastcall WallDeleteVerify(CEffectWall* self, void*, unsigned flags) {
    return DeleteOurs("CEffectWall::DeletingDestruct", self, flags);
}
CEffectLightning* __fastcall LightningDeleteVerify(CEffectLightning* self, void*, unsigned flags) {
    return DeleteOurs("CEffectLightning::DeletingDestruct", self, flags);
}

// CEffectArrow (world_effect_missile.cpp; scenario spells): the three parts destroyed as well.
CEffectArrow*(__fastcall* s_arrowCtor)(CEffectArrow*);
CEffectArrow* __fastcall ArrowCtorVerify(CEffectArrow* self) { return ConstructBoth("CEffectArrow::Construct", s_arrowCtor, self); }
bool Destroyed(const CEffectArrow* e) {
    return DestroyedBase(e) && Destroyed(&e->Particle()) && Destroyed(&e->Light()) && Destroyed(&e->Sound()) &&
           e->targetId_ == -1;
}
void __fastcall ArrowDestructVerify(CEffectArrow* self) { DestructOurs("CEffectArrow::Destruct", self); }
CEffectArrow* __fastcall ArrowDeleteVerify(CEffectArrow* self, void*, unsigned flags) {
    return DeleteOurs("CEffectArrow::DeletingDestruct", self, flags);
}

// CEffectArrowFigure (world_effect_missile.cpp; the factory constructs it directly, so only a
// direct call of the bodies reaches these): no figure left.
CEffectArrowFigure*(__fastcall* s_arrowFigureCtor)(CEffectArrowFigure*);
CEffectArrowFigure* __fastcall ArrowFigureCtorVerify(CEffectArrowFigure* self) {
    return ConstructBoth("CEffectArrowFigure::Construct", s_arrowFigureCtor, self);
}
bool Destroyed(const CEffectArrowFigure* e) { return DestroyedBase(e) && e->figure_ == nullptr && e->casterId_ == -1; }
void __fastcall ArrowFigureDestructVerify(CEffectArrowFigure* self) { DestructOurs("CEffectArrowFigure::Destruct", self); }
CEffectArrowFigure* __fastcall ArrowFigureDeleteVerify(CEffectArrowFigure* self, void*, unsigned flags) {
    return DeleteOurs("CEffectArrowFigure::DeletingDestruct", self, flags);
}

// CEffectShowVisibility (world_effect_visibility.cpp; scenario spells_cast): both allocate the
// grid's cells and rows, so those two pointers are compared by shape (rows[0] == cells) and the
// original's blocks are freed with its scratch.
CEffectShowVisibility*(__fastcall* s_visibilityCtor)(CEffectShowVisibility*);
CEffectShowVisibility* __fastcall VisibilityCtorVerify(CEffectShowVisibility* self) {
    // Ours first, the scratch in our memory (SV2f).
    std::vector<unsigned char> scratchBytes(sizeof(CEffectShowVisibility));
    auto* scratch = reinterpret_cast<CEffectShowVisibility*>(scratchBytes.data());
    std::memcpy(scratch, self, sizeof(CEffectShowVisibility));
    CEffectShowVisibility* actual = self->Construct();
    CEffectShowVisibility* expected = s_visibilityCtor(scratch);
    bool shape = scratch->grid_.rows[0] == scratch->grid_.cells && self->grid_.rows[0] == self->grid_.cells;
    gamecrt::operator_delete(scratch->grid_.cells);
    gamecrt::operator_delete(scratch->grid_.rows);
    scratch->grid_.cells = self->grid_.cells;
    scratch->grid_.rows = self->grid_.rows;
    int diff = FirstDifference(scratch, self, sizeof(CEffectShowVisibility));
    ei::verify::Check("CEffectShowVisibility::Construct", true, diff < 0 && shape && expected == scratch && actual == self,
                      "object %p: first difference at +0x%X", self, diff);
    return actual;
}
bool Destroyed(const CEffectShowVisibility* e) {
    return DestroyedBase(e) && e->fog_ == nullptr && e->seerId_ == -1 && e->observerId_ == -1;
}
void __fastcall VisibilityDestructVerify(CEffectShowVisibility* self) {
    DestructOurs("CEffectShowVisibility::Destruct", self);
}
CEffectShowVisibility* __fastcall VisibilityDeleteVerify(CEffectShowVisibility* self, void*, unsigned flags) {
    return DeleteOurs("CEffectShowVisibility::DeletingDestruct", self, flags);
}

// CEffectMoshka (world_effect_moshka.cpp; the client world makes its bird with the constructor,
// 00566C20): no figure, source or followed object left.
CEffectMoshka*(__fastcall* s_moshkaCtor)(CEffectMoshka*);
CEffectMoshka* __fastcall MoshkaCtorVerify(CEffectMoshka* self) {
    return ConstructBoth("CEffectMoshka::Construct", s_moshkaCtor, self);
}
bool Destroyed(const CEffectMoshka* e) {
    return DestroyedBase(e) && e->bird_ == nullptr && e->source_ == nullptr && e->followedId_ == -1;
}
void __fastcall MoshkaDestructVerify(CEffectMoshka* self) { DestructOurs("CEffectMoshka::Destruct", self); }
CEffectMoshka* __fastcall MoshkaDeleteVerify(CEffectMoshka* self, void*, unsigned flags) {
    return DeleteOurs("CEffectMoshka::DeletingDestruct", self, flags);
}

// CEffectTornado (world_effect_tornado.cpp; the factory constructs it directly, so only a direct
// call of the bodies reaches these): no source or sound left.
CEffectTornado*(__fastcall* s_tornadoCtor)(CEffectTornado*);
CEffectTornado* __fastcall TornadoCtorVerify(CEffectTornado* self) {
    return ConstructBoth("CEffectTornado::Construct", s_tornadoCtor, self);
}
bool Destroyed(const CEffectTornado* e) { return DestroyedBase(e) && e->source_ == nullptr && e->sound_ == -1; }
void __fastcall TornadoDestructVerify(CEffectTornado* self) { DestructOurs("CEffectTornado::Destruct", self); }
CEffectTornado* __fastcall TornadoDeleteVerify(CEffectTornado* self, void*, unsigned flags) {
    return DeleteOurs("CEffectTornado::DeletingDestruct", self, flags);
}

// The factory (every effect created; scenarios spells, effects): ours only (it allocates). A new
// object reports the requested class id (slot 12) and carries it in type_.
CEngineObject* __fastcall EffectNewObjectVerify(CBaseManager* manager, void*, int type) {
    CEngineObject* object = EffectNewObject(manager, nullptr, type);
    bool ok = object == nullptr || (object->type_ == type && object->GetClassId() == type);
    ei::verify::Check("EffectNewObject", true, ok, "type %X object %p", type, object);
    return object;
}

} // namespace

EI_VERIFY(0x0067E170, EffectNewObjectVerify, 0, s_unused);
EI_VERIFY(0x0067D670, TornadoCtorVerify, 7, s_tornadoCtor);
EI_VERIFY(0x0067D800, TornadoDestructVerify, 0, s_unused);
EI_VERIFY(0x0067D7E0, TornadoDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067C950, MoshkaCtorVerify, 7, s_moshkaCtor);
EI_VERIFY(0x0067CAF0, MoshkaDestructVerify, 0, s_unused);
EI_VERIFY(0x0067CAD0, MoshkaDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067C340, VisibilityCtorVerify, 7, s_visibilityCtor);
EI_VERIFY(0x0067C490, VisibilityDestructVerify, 0, s_unused);
EI_VERIFY(0x0067C470, VisibilityDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067BB90, ArrowFigureCtorVerify, 7, s_arrowFigureCtor);
EI_VERIFY(0x0067BCD0, ArrowFigureDestructVerify, 0, s_unused);
EI_VERIFY(0x0067BCB0, ArrowFigureDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067AE30, ArrowCtorVerify, 7, s_arrowCtor);
EI_VERIFY(0x0067B1C0, ArrowDestructVerify, 0, s_unused);
EI_VERIFY(0x0067B1A0, ArrowDeleteVerify, 0, s_unused);

EI_VERIFY(0x0066C4F0, QueueStreamVerify, 11, s_queueStream);
EI_VERIFY(0x00677EC0, FieldCtorVerify, 5, s_fieldCtor);
EI_VERIFY(0x0066A300, FieldCtor2Verify, 5, s_fieldCtor2);
EI_VERIFY(0x00675F70, FieldEmptyVerify, 5, s_fieldEmpty);

EI_VERIFY(0x00675DD0, SoundCtorVerify, 7, s_soundCtor);
EI_VERIFY(0x00676760, SeqCtorVerify, 7, s_seqCtor);
EI_VERIFY(0x00677510, AmbientCtorVerify, 7, s_ambientCtor);
EI_VERIFY(0x00677D30, LightCtorVerify, 7, s_lightCtor);
EI_VERIFY(0x006787B0, ParticleCtorVerify, 7, s_particleCtor);
EI_VERIFY(0x006797E0, WallCtorVerify, 7, s_wallCtor);
EI_VERIFY(0x00679FC0, LightningCtorVerify, 7, s_lightningCtor);

EI_VERIFY(0x006760A0, SoundDestructVerify, 0, s_unused);
EI_VERIFY(0x00676980, SeqDestructVerify, 0, s_unused);
EI_VERIFY(0x006776E0, AmbientDestructVerify, 0, s_unused);
EI_VERIFY(0x00677FC0, LightDestructVerify, 0, s_unused);
EI_VERIFY(0x00678990, ParticleDestructVerify, 0, s_unused);
EI_VERIFY(0x006799E0, WallDestructVerify, 0, s_unused);
EI_VERIFY(0x0067A0B0, LightningDestructVerify, 0, s_unused);

EI_VERIFY(0x00676080, SoundDeleteVerify, 0, s_unused);
EI_VERIFY(0x00676960, SeqDeleteVerify, 0, s_unused);
EI_VERIFY(0x006776C0, AmbientDeleteVerify, 0, s_unused);
EI_VERIFY(0x00677FA0, LightDeleteVerify, 0, s_unused);
EI_VERIFY(0x00678970, ParticleDeleteVerify, 0, s_unused);
EI_VERIFY(0x006799C0, WallDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067A090, LightningDeleteVerify, 0, s_unused);

// --- The base's out-of-line pieces (A-H25) ------------------------------------------------------------
// No live object has the abstract effect or missile vtable and the rewritten constructors and Resets
// inline these, so nothing reaches 0067B050, 00675FB0 / 0067B390, 00676910 / 0067B430 or 0067AA00
// outside unwinding; the wrappers are there for a caller that turns up. 00676900 is slot 6 of
// CEffectAmbientSound (every tick of a placed ambient sound); ReadSavedStrings runs when a save
// with a sound sequence or an ambient sound is loaded.

namespace {

// 0067B050: PUSH -1; PUSH imm32 (7).
CEffect*(__fastcall* s_effectCtor)(CEffect*);
CEffect* __fastcall EffectCtorVerify(CEffect* self) { return ConstructBoth("CEffect::Construct", s_effectCtor, self); }

// Ours only (frees the streams' buffers).
void __fastcall EffectDestructVerify(CEffect* self) {
    self->DestructEffect();
    ei::verify::Check("CEffect::DestructEffect", true, DestroyedBase(self), "effect %p", static_cast<void*>(self));
}
CEffect* __fastcall EffectDeleteVerify(CEffect* self, void*, unsigned flags) {
    CEffect* result = self->DeletingDestructEffect(flags & ~1u);
    ei::verify::Check("CEffect::DeletingDestructEffect", true, result == self && DestroyedBase(self), "effect %p",
                      static_cast<void*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

// 00676900: MOV EAX,1 (5).
int(__fastcall* s_tickDefault)(CEffect*);
int __fastcall TickDefaultVerify(CEffect* self) {
    int expected = s_tickDefault(self);
    ei::verify::Check("CEffect::TickDefault", expected, self->TickDefault(), "effect %p", static_cast<void*>(self));
    return expected;
}

// Ours only (the state field's streams are resized).
void __fastcall ResetMissileVerify(CEffectMissile* self) {
    self->ResetMissile();
    const bool ok = self->arrived_ == 0 && self->targetId_ == -1 && self->fromCaster_ == 0 && self->moving_ == 0 &&
                    self->startTick_ == 0 && self->changeStamp_ == -1;
    ei::verify::Check("CEffectMissile::ResetMissile", true, ok, "missile %p", static_cast<void*>(self));
}

// 006773A0: MOV EAX,FS:[0] (6). Ours appends to the real array; the stream is put back where it
// was and the original appends to an empty scratch array (same vtable and grow-by). The new
// elements must be the scratch's texts and the stream must end at the same place.
void(__fastcall* s_readSavedStrings)(mfc::CStringArray*, CStream*);
void __fastcall ReadSavedStringsVerify(mfc::CStringArray* array, CStream* stream) {
    alignas(mfc::CStringArray) unsigned char raw[sizeof(mfc::CStringArray)];
    auto* scratch = reinterpret_cast<mfc::CStringArray*>(raw);
    scratch->vtable = array->vtable;
    scratch->data = nullptr;
    scratch->size = 0;
    scratch->maxSize = 0;
    scratch->growBy = array->growBy;
    // Ours first, then the original from the same place (the game reads the heap's address order,
    // SV2f).
    const std::uint32_t start = stream->Position();
    const int before = array->size;
    ReadSavedStrings(array, stream);
    const std::uint32_t end = stream->Position();
    stream->Seek(start);
    s_readSavedStrings(scratch, stream);
    bool same = stream->Position() == end && array->size == before + scratch->size;
    for (int i = 0; same && i < scratch->size; ++i)
        same = std::strcmp(static_cast<const char*>((*scratch)[i]), static_cast<const char*>((*array)[before + i])) == 0;
    ei::verify::Check("ReadSavedStrings", true, same, "%d read, %d -> %d elements", scratch->size, before, array->size);
    scratch->SetSize(0, -1);
}

} // namespace

EI_VERIFY(0x0067B050, EffectCtorVerify, 7, s_effectCtor);
EI_VERIFY(0x00675FB0, EffectDestructVerify, 0, s_unused);
EI_VERIFY(0x0067B390, EffectDestructVerify, 0, s_unused);
EI_VERIFY(0x00676910, EffectDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067B430, EffectDeleteVerify, 0, s_unused);
EI_VERIFY(0x00676900, TickDefaultVerify, 5, s_tickDefault);
EI_VERIFY(0x0067AA00, ResetMissileVerify, 0, s_unused);
EI_VERIFY(0x006773A0, ReadSavedStringsVerify, 6, s_readSavedStrings);
