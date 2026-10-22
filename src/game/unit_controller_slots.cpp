// Small virtual slots of a unit's controllers (A-U12): the abstract base (game.exe's vtable 0073EDBC; the
// server's CUnitServer, 0073EF38, and the client's CUnitClientSpecific, 0073ECD8, keep most of its
// empty slots), its lifetime, the client controller's small methods, and the compiled tables of the base
// and the client controller (C19). Slot names: CUnitController (unit.h).
#include "unit.h"
#include "unit_client_models.h"

#include "combat.h"   // CUnitServer

#include "effects.h"   // CParticleSource
#include "engine_object_class.h"
#include "texture.h"   // CTexture

#include "ei/hook.h"
#include "ei/vcall.h"   // SetVtable
#include "lib/crt.h"

#include <bit>
#include <cstdint>

namespace {
constexpr const char* kClientClassName = "CUnitClientSpecific";
constexpr int kClientClassId = 0x3D;

// A marker source stopped, released and forgotten.
void DropMarker(CParticleSource*& marker) {
    if (!marker) return;
    marker->StopAndClear();
    marker->Release();
    marker = nullptr;
}
} // namespace

// --- The base controller's lifetime -----------------------------------------------------------------

// 00533DB0. The destructor body: the base's vtable, no unit, the engine object's.
void __fastcall ControllerDestruct(CUnitController* self) {
    ei::SetVtable(self, CUnitController::Vtable());
    self->unit_ = nullptr;
    self->DestructBase();
}
EI_REPLACE(0x00533DB0, ControllerDestruct);

// 00533D90 (slot 0, RET 4).
CUnitController* CUnitController::DeletingDestruct(unsigned flags) {
    ControllerDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00533D90, CUnitController::DeletingDestruct);

// 00533DD0 (slot 2): no unit; 1. 00533DE0 (slot 3): no unit.
int __fastcall ControllerCreate(CUnitController* self) {
    self->unit_ = nullptr;
    return 1;
}
EI_REWRITTEN(0x00533DD0, ControllerCreate);
void __fastcall ControllerDestroy(CUnitController* self) { self->unit_ = nullptr; }
EI_REWRITTEN(0x00533DE0, ControllerDestroy);

// --- The base controller's empty slots ---------------------------------------------------------------
// Most are under five bytes (too short to hook); the server's and the client's vtables share them
// as listed.

// 00533B90 (slot 13 FiguresLoaded; the server's too): 1.
int __fastcall ControllerFiguresLoaded(CUnitController*) { return 1; }
EI_REWRITTEN(0x00533B90, ControllerFiguresLoaded);

// Plain RET, nothing: slots 15, 22, 23, 28, 38, 41, 43, 44, 45, 48, 49, 52 and 53 (the server keeps
// 15, 41, 43, 48 and 52; the client 22, 23, 28, 38, 44, 45, 49 and 53), and the client's own 17
// (00534130) and 15 (00535580).
void __fastcall ControllerNothing(CUnitController*) {}
EI_REPLACE(0x00533BB0, ControllerNothing);
EI_REPLACE(0x00533C00, ControllerNothing);
EI_REPLACE(0x00533C10, ControllerNothing);
EI_REPLACE(0x00533C20, ControllerNothing);
EI_REPLACE(0x00533CA0, ControllerNothing);
EI_REPLACE(0x00533CD0, ControllerNothing);
EI_REPLACE(0x00533CF0, ControllerNothing);
EI_REPLACE(0x00533D00, ControllerNothing);
EI_REPLACE(0x00533D10, ControllerNothing);
EI_REPLACE(0x00533D40, ControllerNothing);
EI_REPLACE(0x00533D50, ControllerNothing);
EI_REPLACE(0x00533D70, ControllerNothing);
EI_REPLACE(0x00533D80, ControllerNothing);
EI_REPLACE(0x00534130, ControllerNothing);
EI_REPLACE(0x00535580, ControllerNothing);

// RET 4, nothing: slots 14 (RestartAnimation), 18 (PlayerChanged), 40 (AdvanceTime), 42
// (ObjectDeleted; the client's too) and 47 (Wound).
void CUnitController::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x00533BA0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533BC0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533CC0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533CE0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533D30, CUnitController::IgnoreArgument);

// RET 8, nothing: slots 32 (SetArmourModels), 33 (SetWeaponModels) and 51 (Say).
void CUnitController::IgnoreTwoArguments(int, int) {}
EI_REPLACE_METHOD(0x00533C60, CUnitController::IgnoreTwoArguments);
EI_REPLACE_METHOD(0x00533C70, CUnitController::IgnoreTwoArguments);
EI_REPLACE_METHOD(0x00533D60, CUnitController::IgnoreTwoArguments);

// 00533D20 (slot 46 StatChanged, RET 0xC): nothing.
void CUnitController::IgnoreThreeArguments(int, int, int) {}
EI_REPLACE_METHOD(0x00533D20, CUnitController::IgnoreThreeArguments);

// 0: slots 29 (RefreshEquipment), 30 (RefreshModel), 31 (RefreshWounds) and 39 (TamingStage; the
// client's too).
int __fastcall ControllerZero(CUnitController*) { return 0; }
EI_REPLACE(0x00533C30, ControllerZero);
EI_REPLACE(0x00533C40, ControllerZero);
EI_REPLACE(0x00533C50, ControllerZero);
EI_REPLACE(0x00533CB0, ControllerZero);

// 0 with one argument (RET 4): slots 19 (RefreshVitals) and 21 (Heal); the client's too.
int CUnitController::ReturnZeroOne(int) { return 0; }
EI_REWRITTEN(0x00533BD0, CUnitController::ReturnZeroOne);
EI_REWRITTEN(0x00533BF0, CUnitController::ReturnZeroOne);

// 00533BE0 (slot 20 TakeHit, RET 0xC; the client's too): 0.
int CUnitController::ReturnZeroThree(int, int, int) { return 0; }
EI_REWRITTEN(0x00533BE0, CUnitController::ReturnZeroThree);

// False in AL (RET 4): slots 34 (WeaponAdded) and 35 (RemoveWeaponModel).
bool CUnitController::ReturnFalse(int) { return false; }
EI_REWRITTEN(0x00533C80, CUnitController::ReturnFalse);
EI_REWRITTEN(0x00533C90, CUnitController::ReturnFalse);

// --- The client controller -------------------------------------------------------------------------------

// 00533DF0 (slot 16): the order part of the unit's path. Four bytes (too short to hook).
AIPath* __fastcall ClientControllerPath(CUnitClientSpecific* self) { return self->MotionPath(); }
EI_REPLACE(0x00533DF0, ClientControllerPath);

// 00533E00 / 00533E10 (slots 11 / 12).
const char* __fastcall ClientControllerClassName(CUnitClientSpecific*) {
    return kClientClassName;
}
EI_REWRITTEN(0x00533E00, ClientControllerClassName);
int __fastcall ClientControllerClassId(CUnitClientSpecific*) { return kClientClassId; }
EI_REWRITTEN(0x00533E10, ClientControllerClassId);

void CUnitClientSpecific::SetMarkers(CParticleSource* path, CParticleSource* marker) {
    DropMarker(sources[1]);
    DropMarker(sources[2]);
    if (path) {
        sources[1] = path;
        ++path->refCount_;
    }
    if (marker) {
        sources[2] = marker;
        ++marker->refCount_;
    }
}
EI_REPLACE_METHOD(0x00535240, CUnitClientSpecific::SetMarkers);

void CUnitClientSpecific::DropFaceTexture() {
    CTexture* texture = faceTexture;
    faceTexture = nullptr;
    if (texture) texture->Release();
    blinks = 0;
}
EI_REPLACE_METHOD(0x00538EA0, CUnitClientSpecific::DropFaceTexture);

// --- The compiled tables (C19) -------------------------------------------------------------------------
// Compiled tags (engine_object_class.h) in place of game.exe's 0073EDBC (the abstract controller), 0073ECD8
// (CUnitClientSpecific) and 0073EF38 (CUnitServer). ControllerClass declares CUnitController's slots 13-53 (unit.h)
// with the base's bodies (most are empty, or answer 0 / false; the pure ones `_purecall`); each controller's tag
// overrides the slots its game.exe table changes with the bodies that table reached (qualified: no dispatch). A
// slot whose original is a plain RET leaves EAX as the caller had it (CUnit passes the EAX of slots 23, 44, 49 and
// 53 on), so those are void; the server's 28 passes its body's result on the same way.

namespace {

template <typename Self, typename T>
class ControllerClass : public EngineObjectClass<Self, T> {
public:
    int Create() override { return ControllerCreate(Controller()); }   // 2: 00533DD0
    void Destroy() override { ControllerDestroy(Controller()); }        // 3: 00533DE0

    virtual int FiguresLoaded() { return ControllerFiguresLoaded(Controller()); }   // 13: 00533B90
    virtual void RestartAnimation(float) {}                    // 14: 00533BA0 (RET 4)
    virtual void Unused15() {}                                 // 15: 00533BB0
    virtual AIPath* Path() {                                   // 16: pure
        _purecall();
        return nullptr;
    }
    virtual void Unused17() { _purecall(); }                   // 17: pure
    virtual void PlayerChanged(void*) {}                       // 18: 00533BC0 (RET 4)
    virtual int RefreshVitals(int withHitPoints) { return Controller()->ReturnZeroOne(withHitPoints); }   // 19: 00533BD0
    virtual int TakeHit(DamageInfo*, CUnit*, CUnit*) { return 0; }   // 20: 00533BE0 (RET 0xC)
    virtual int Heal(int amountBits) { return Controller()->ReturnZeroOne(amountBits); }   // 21: 00533BF0
    virtual void RepackStats() {}                              // 22: 00533C00
    virtual void UpdateSizeClasses() {}                        // 23: 00533C10
    virtual int Level() { return _purecall(); }                // 24: pure
    virtual int ShowWeapon(std::uint32_t) { return _purecall(); }   // 25: pure
    virtual bool ArmourPutOn(std::uint32_t) {                  // 26: pure
        _purecall();
        return false;
    }
    virtual bool ArmourTakenOff(std::uint32_t) {               // 27: pure
        _purecall();
        return false;
    }
    virtual void ClearOuterArmourLayers() {}                   // 28: 00533C20
    virtual int RefreshEquipment() { return ControllerZero(Controller()); }   // 29: 00533C30
    virtual int RefreshModel() { return ControllerZero(Controller()); }       // 30: 00533C40
    virtual int RefreshWounds() { return ControllerZero(Controller()); }      // 31: 00533C50
    virtual void SetArmourModels(const std::uint32_t*, int) {}   // 32: 00533C60 (RET 8)
    virtual void SetWeaponModels(const std::uint32_t*, int) {}   // 33: 00533C70 (RET 8)
    virtual bool WeaponAdded(std::uint32_t) { return false; }         // 34: 00533C80 (RET 4)
    virtual bool RemoveWeaponModel(std::uint32_t) { return false; }   // 35: 00533C90 (RET 4)
    virtual int HasOtherArmour() { return _purecall(); }       // 36: pure
    virtual int HasMainArmour() { return _purecall(); }        // 37: pure
    virtual void HostileMaskChanged() {}                       // 38: 00533CA0
    virtual int TamingStage() { return ControllerZero(Controller()); }   // 39: 00533CB0
    virtual void AdvanceTime(float) {}                         // 40: 00533CC0 (RET 4)
    virtual void MotionChanged() {}                            // 41: 00533CD0
    virtual void ObjectDeleted(CEngineObject*) {}              // 42: 00533CE0 (RET 4)
    virtual void RestartFadeIn() {}                            // 43: 00533CF0
    virtual void Resettle() {}                                 // 44: 00533D00
    virtual void UpdateMoveFactors() {}                        // 45: 00533D10
    virtual void StatChanged(int, float, std::uint32_t) {}     // 46: 00533D20 (RET 0xC)
    virtual void Wound(float) {}                               // 47: 00533D30 (RET 4)
    virtual void Died() {}                                     // 48: 00533D40
    virtual void EffectsChanged() {}                           // 49: 00533D50
    virtual void GetAttackParams(AttackParams*) { _purecall(); }   // 50: pure
    virtual void Say(const SpeechRecord*, int) {}              // 51: 00533D60 (RET 8)
    virtual void StopSpeech() {}                               // 52: 00533D70
    virtual void MoveFactorsChanged() {}                       // 53: 00533D80

protected:
    CUnitController* Controller() { return this->template As<CUnitController>(); }
};

class ControllerBaseClass final : public ControllerClass<ControllerBaseClass, CUnitController> {};

class CUnitClientSpecificClass final : public ControllerClass<CUnitClientSpecificClass, CUnitClientSpecific> {
public:
    int Create() override { return Client()->CreateClient(); }    // 2: 00533FE0
    void Destroy() override { Client()->DestroyClient(); }        // 3: 005340C0
    int RunTick() override { return Client()->UpdateClient(); }   // 6: 005341A0
    const char* ClassName() override { return ClientControllerClassName(Client()); }   // 11: 00533E00
    int ClassId() override { return ClientControllerClassId(Client()); }               // 12: 00533E10
    int FiguresLoaded() override { return Client()->FiguresLoaded(); }                 // 13: 005344E0
    void RestartAnimation(float time) override { Client()->RestartAnimation(time); }   // 14: 005352B0
    void Unused15() override {}                                   // 15: 00535580 (empty)
    AIPath* Path() override { return ClientControllerPath(Client()); }   // 16: 00533DF0
    void Unused17() override {}                                   // 17: 00534130 (empty)
    void PlayerChanged(void* player) override { Client()->OwnerPlayerChanged(player); }   // 18: 00534140
    int Level() override { return Client()->Level(); }            // 24: 00539160
    int ShowWeapon(std::uint32_t slot) override { return Client()->ChangeWeaponModel(slot); }      // 25: 00535590
    bool ArmourPutOn(std::uint32_t itemId) override { return Client()->WearArmourModel(itemId); }  // 26: 005381B0
    bool ArmourTakenOff(std::uint32_t slot) override { return Client()->RemoveArmourModel(slot); } // 27: 005385A0
    int RefreshEquipment() override { return Client()->RefreshSkins(); }   // 29: 00536100
    int RefreshModel() override { return Client()->RefreshSimpleSkin(); }  // 30: 00536220
    int RefreshWounds() override { return Client()->RefreshWounds(); }     // 31: 00537360
    void SetArmourModels(const std::uint32_t* ids, int count) override { Client()->SetArmourModels(ids, count); }   // 32: 00538140
    void SetWeaponModels(const std::uint32_t* ids, int count) override { Client()->SetWeaponModels(ids, count); }   // 33: 00538860
    bool WeaponAdded(std::uint32_t id) override { return Client()->AddWeaponModel(id); }            // 34: 005388C0
    bool RemoveWeaponModel(std::uint32_t id) override { return Client()->RemoveWeaponModel(id); }   // 35: 00538920
    int HasOtherArmour() override { return Client()->HasWeaponModels(); }   // 36: 005391B0
    int HasMainArmour() override { return Client()->HasArmourModels(); }    // 37: 005391E0
    void AdvanceTime(float time) override { Client()->AdvanceTime(time); }  // 40: 00535B30
    void MotionChanged() override { ClientMotionChanged(Client()); }        // 41: 00535B20
    void RestartFadeIn() override { Client()->RestartFadeIn(); }            // 43: 005344A0 (EAX: its result)
    void StatChanged(int location, float amount, std::uint32_t flags) override {   // 46: 00537500
        Client()->HitEffect(location, amount, flags);
    }
    void Wound(float lost) override { Client()->SparkEffect(std::bit_cast<int>(lost)); }   // 47: 00537C00 (not read)
    void Died() override { Client()->DeathEffect(); }                       // 48: 005373D0
    void GetAttackParams(AttackParams* out) override { Client()->GetAttackParams(out); }   // 50: 00538960
    void Say(const SpeechRecord* speech, int kind) override { Client()->Say(speech, kind); }   // 51: 00537D80
    void StopSpeech() override { Client()->StopSpeech(); }                  // 52: 00538040

private:
    CUnitClientSpecific* Client() { return As<CUnitClientSpecific>(); }
};

class CUnitServerClass final : public ControllerClass<CUnitServerClass, CUnitServer> {
public:
    int Create() override { return Server()->CreateServer(); }    // 2: 00546450
    void Destroy() override { Server()->DestroyServer(); }        // 3: 00546470
    void ClearState() override { Server()->ResetSlot(); }         // 5: 00546080
    int RunTick() override { return Server()->UpdateServer(); }   // 6: 00552300
    void Load(CStream* stream) override { Server()->LoadServer(stream); }   // 9: 00555D10
    void Save(CStream* stream) override { Server()->SaveServer(stream); }   // 10: 005561C0
    const char* ClassName() override { return UnitServerClassName(Server()); }   // 11: 005460C0
    int ClassId() override { return UnitServerClassId(Server()); }               // 12: 005460D0
    AIPath* Path() override { return Server()->MotionSlot(); }    // 16: 00546090
    void Unused17() override { Server()->EmptySlot17(); }         // 17: 005460B0
    int RefreshVitals(int withHitPoints) override { return Server()->RefreshVitals(static_cast<char>(withHitPoints)); }   // 19: 005533B0
    int TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source) override { return Server()->TakeHit(hit, attacker, source); }   // 20: 00553F40
    int Heal(int amountBits) override { return Server()->Heal(std::bit_cast<float>(amountBits)); }   // 21: 005543B0
    void RepackStats() override { Server()->RepackStats(); }               // 22: 00553500
    void UpdateSizeClasses() override { Server()->UpdateSizeClasses(); }   // 23: 00552FB0
    int Level() override { return Server()->Level(); }                     // 24: 00556760
    int ShowWeapon(std::uint32_t slot) override { return Server()->TakeUpWeapon(static_cast<int>(slot)); }   // 25: 00553610
    bool ArmourPutOn(std::uint32_t key) override { return Server()->AddArmourLayerOf(key); }   // 26: 00553560
    bool ArmourTakenOff(std::uint32_t unused) override { return Server()->RebuildArmourLayers(static_cast<int>(unused)); }   // 27: 005535C0
    void ClearOuterArmourLayers() override { Server()->ClearOuterArmourLayers(); }   // 28: 005535E0 (EAX: its result)
    int RefreshEquipment() override { return Server()->RefreshEquipment(); }   // 29: 005535F0
    int HasOtherArmour() override { return Server()->HasOtherArmour(); }       // 36: 005567A0
    int HasMainArmour() override { return Server()->HasMainArmour(); }         // 37: 005567E0
    void HostileMaskChanged() override { Server()->ForgetClaims(); }           // 38: 00552CD0
    int TamingStage() override { return Server()->TameStage(); }               // 39: 005460A0
    void ObjectDeleted(CEngineObject* object) override { Server()->OnObjectDeleted(object); }   // 42: 00552670
    void Resettle() override { Server()->Resettle(); }                         // 44: 00553260
    void UpdateMoveFactors() override { Server()->UpdateMoveFactors(); }       // 45: 005536B0
    void EffectsChanged() override { Server()->UpdateSpellBlockFlag(); }       // 49: 00553220
    void GetAttackParams(AttackParams* out) override { Server()->GetAttackParams(out); }   // 50: 00555A00
    void MoveFactorsChanged() override { Server()->UpdateMoveFactorsSlot53(); }   // 53: 00552F30

private:
    CUnitServer* Server() { return As<CUnitServer>(); }
};

} // namespace

void* CUnitController::Vtable() { return ControllerBaseClass::Vtable(); }
void* CUnitClientSpecific::Vtable() { return CUnitClientSpecificClass::Vtable(); }
void* CUnitServer::Vtable() { return CUnitServerClass::Vtable(); }
