// The map objects' own tables compiled (C19; engine_object_class.h) in place of game.exe's: the 3D object's 0073DDD4
// (27 slots; config/vtables.csv lists 5, the rest gen_image_cells.py's NOT_CODE) and the map object's 0073DAA0 (61
// slots). Both are abstract (11 / 12 `_purecall`); only CObject3D::Construct / DestructBody and CMapObject::Construct /
// DestructBody store them (map_object_lifetime.cpp). Each slot calls the class's virtual qualified (no dispatch):
// the bodies the classes whose constructor is C++ (the torches, levers and traps) reach through the compiler's
// tables, with the same work as game.exe's table's (map_object_stub_slots.cpp, object3d_slots.cpp).
#include "engine_object_class.h"
#include "map_object.h"

namespace {

// CObject3D's slots over CTransformObject's: 0 its deleting destructor, 2 / 3 / 5 / 14 and 18-26 its own (16 / 17
// are game.exe's 00516350 / 00516360, the same work as the transform's).
template <typename Self, typename T>
class Object3DClass : public TransformObjectClass<Self, T> {
public:
    void* DeleteScalar(unsigned flags) override { return Object3D()->DeleteObject3D(flags); }   // 0: 00515D50
    int Create() override { return Object3D()->CObject3D::Create(); }                         // 2: 00515F70
    void Destroy() override { Object3D()->CObject3D::Destroy(); }                             // 3: 00516080
    void ClearState() override { Object3D()->CObject3D::ClearState(); }                       // 5: 00515E90
    void CombineRotation(const Quaternion& parent) override { Object3D()->CObject3D::CombineRotation(parent); }   // 14
    virtual bool AcceptsAnimation(const void* animation) { return Object3D()->CObject3D::AcceptsAnimation(animation); }   // 18
    virtual std::uintptr_t AttachObject(CEngineObject* object) { return Object3D()->CObject3D::AttachObject(object); }   // 19
    virtual void CopyFigure(const CObject3D* other) { Object3D()->CObject3D::CopyFigure(other); }           // 20: 005161E0
    virtual bool InvisibleSlot() { return Object3D()->CObject3D::InvisibleSlot(); }                         // 21: 00515D40
    virtual int DrawFigure(void* context) { return Object3D()->CObject3D::DrawFigure(context); }            // 22: 00516460
    virtual int DrawCollectDirty(void* context) { return Object3D()->CObject3D::DrawCollectDirty(context); }   // 23
    virtual int DrawFigureShadow(void* context) { return Object3D()->CObject3D::DrawFigureShadow(context); }   // 24
    virtual void AdvanceTime(float time) { Object3D()->CObject3D::AdvanceTime(time); }                      // 25: empty
    virtual std::uintptr_t LoadFigures() { return Object3D()->CObject3D::LoadFigures(); }                   // 26: 00517500

protected:
    CObject3D* Object3D() { return this->template ObjectAs<CObject3D>(); }
};

class Object3DBaseClass final : public Object3DClass<Object3DBaseClass, CObject3D> {};

// CMapObject's slots over CObject3D's: 0, 2, 3, 5, 6, 9, 10, 19 and 21 its own, then 27-60 (game.exe's table ends at
// 60; slot 61, SetScriptName, is the units').
class MapObjectBaseClass final : public Object3DClass<MapObjectBaseClass, CMapObject> {
public:
    void* DeleteScalar(unsigned flags) override { return Map()->DeleteMapObject(flags); }   // 0: 0050E560
    int Create() override { return Map()->CMapObject::Create(); }                         // 2: 0050E770
    void Destroy() override { Map()->CMapObject::Destroy(); }                             // 3: 0050E840
    void ClearState() override { Map()->CMapObject::ClearState(); }                       // 5: 0050E760 (empty)
    int RunTick() override { return Map()->CMapObject::RunTick(); }                       // 6: 0050F1C0
    void Load(CStream* stream) override { Map()->CMapObject::Load(stream); }              // 9: 0050F7A0
    void Save(CStream* stream) override { Map()->CMapObject::Save(stream); }              // 10: 0050F900
    std::uintptr_t AttachObject(CEngineObject* object) override { return Map()->CMapObject::AttachObject(object); }   // 19
    bool InvisibleSlot() override { return Map()->CMapObject::InvisibleSlot(); }          // 21: 0050FA10
    virtual std::uintptr_t RecomputeScale() { return Map()->CMapObject::RecomputeScale(); }     // 27: 0050F2D0
    virtual std::uintptr_t EffectsChanged() { return Map()->CMapObject::EffectsChanged(); }     // 28: 0050E420
    virtual bool IsSynchronised() { return Map()->CMapObject::IsSynchronised(); }               // 29: 0050E430
    virtual std::uint32_t ApplyBaseScale() { return Map()->CMapObject::ApplyBaseScale(); }      // 30: 0050E440
    virtual void ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster) {   // 31: 0050E460
        Map()->CMapObject::ReceiveHit(hit, attacker, caster);
    }
    virtual int StatsChanged() { return Map()->CMapObject::StatsChanged(); }                    // 32: 0050E970
    virtual std::uintptr_t ScaleEffectChanged(int scaleEffect) {                                 // 33: 0050E960
        return Map()->CMapObject::ScaleEffectChanged(scaleEffect);
    }
    virtual void TakePrototype(const char* prototype) { Map()->CMapObject::TakePrototype(prototype); }   // 34
    virtual std::uintptr_t SetStats(const UnitStats* stats) { return Map()->CMapObject::SetStats(stats); }   // 35
    virtual std::uintptr_t SetServerData(const void* data) { return Map()->CMapObject::SetServerData(data); }   // 36
    virtual std::uintptr_t UpdateSizeClasses() { return Map()->CMapObject::UpdateSizeClasses(); }   // 37: 0050E490
    virtual int IsUsable() { return Map()->CMapObject::IsUsable(); }                                // 38: 0050F0C0
    virtual int IsDirectUseBlocked() { return Map()->CMapObject::IsDirectUseBlocked(); }            // 39: 0050F0D0
    virtual std::uint32_t KeyId() { return Map()->CMapObject::KeyId(); }                            // 40: 0050F0E0
    virtual bool ApplyScience(int first, int second) { return Map()->CMapObject::ApplyScience(first, second); }   // 41
    virtual int AcceptsKey(const LockKey* key) { return Map()->CMapObject::AcceptsKey(key); }       // 42: 0050F110
    virtual bool KeepsMapFlag() { return Map()->CMapObject::KeepsMapFlag(); }                       // 43: 004467B0
    virtual void UseSpots(void* out, float userRadius) { Map()->CMapObject::UseSpots(out, userRadius); }   // 44
    virtual std::uintptr_t AddEffect(const MapEffect* effect) { return Map()->CMapObject::AddEffect(effect); }   // 45
    virtual std::uint32_t SetStunned(int on) { return Map()->CMapObject::SetStunned(on); }         // 46: 0050E4A0
    virtual int SetVisibilityMode(int mode) { return Map()->CMapObject::SetVisibilityMode(mode); } // 47: 004467C0
    virtual Vector3* EyePoint(Vector3* out) { return Map()->CMapObject::EyePoint(out); }            // 48: 0050F730
    virtual std::uint32_t ReadNetState(int first) { return Map()->CMapObject::ReadNetState(first); }   // 49: 0050EC30
    virtual void WriteNetState() { Map()->CMapObject::WriteNetState(); }                            // 50: 0050EAF0
    virtual void PlaceOnGround(float offset) { Map()->CMapObject::PlaceOnGround(offset); }          // 51: 0050F020
    virtual Vector3* PositionAt(Vector3* out, int time) { return Map()->CMapObject::PositionAt(out, time); }   // 52
    virtual Vector3* AimPoint(Vector3* out) { return Map()->CMapObject::AimPoint(out); }            // 53: 0050E4D0
    virtual int IsLargeUnit() { return Map()->CMapObject::IsLargeUnit(); }                          // 54: 0050E4F0
    virtual int IsDead() { return Map()->CMapObject::IsDead(); }                                    // 55: 0050E500
    virtual int ForgetObject(CEngineObject* object) { return Map()->CMapObject::ForgetObject(object); }   // 56
    virtual void UpdateMoveFactors() { Map()->CMapObject::UpdateMoveFactors(); }                    // 57: empty
    virtual void RefreshWounds() { Map()->CMapObject::RefreshWounds(); }                            // 58: empty
    virtual double MoveSpeed() { return Map()->CMapObject::MoveSpeed(); }                           // 59: 0050E540
    virtual float Radius() { return Map()->CMapObject::Radius(); }                                  // 60: 0050E550

private:
    CMapObject* Map() { return ObjectAs<CMapObject>(); }
};

} // namespace

void* CObject3D::Object3DVtable() { return Object3DBaseClass::Vtable(); }
void* CMapObject::MapObjectVtable() { return MapObjectBaseClass::Vtable(); }
