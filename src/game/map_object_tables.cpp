// The 3D object's own table compiled (C19; engine_object_class.h) in place of game.exe's 0073DDD4 (27 slots;
// config/vtables.csv lists 5, the rest gen_image_cells.py's NOT_CODE). The class is abstract (11 / 12 `_purecall`);
// only CObject3D::Construct / DestructBody store it (map_object_lifetime.cpp): Construct is registered (00515BD0, the
// harness names it). Each slot calls the class's virtual qualified (no dispatch), with the same work as game.exe's
// table's (object3d_slots.cpp). The map object's 0073DAA0 is CMapObject's own since C28h.
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

} // namespace

void* CObject3D::Object3DVtable() { return Object3DBaseClass::Vtable(); }
