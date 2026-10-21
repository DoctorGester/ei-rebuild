// The halves of a 3D object besides the client half's part sets (A-U12): the client half's shadow
// state, the base half's empty slots (vtable 0073DEFC), the server half (CObject3DServer, class id
// 0x38, vtable 0073DFBC, 0x20 bytes), the wounds material's destructor and the copies of a named
// texture and a material stage. Layouts: map_object_shadow.h, map_object_figures.h.
#include "map_object_shadow.h"

#include "figure_mesh.h"        // VertexPacket
#include "game_data.h"
#include "ground_decal.h"       // DecalTexCoord
#include "ground_print.h"       // NamedTexture
#include "map_object_figures.h"
#include "model.h"
#include "object.h"
#include "texture.h"
#include "world_data.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <new>

namespace {

constexpr std::uintptr_t kSpecificBaseVtable = std::uintptr_t(0x0073DEFC);
constexpr std::uintptr_t kServerVtable = std::uintptr_t(0x0073DFBC);
constexpr const char* kServerClassName = "CObject3DServer";
constexpr int kServerClassId = 0x38;
// The shadow switches by the object's class: units (0x50), class 0x52, any other.
constexpr std::uintptr_t kUnitShadows = ei::DataId(0x0079BD14);
constexpr std::uintptr_t kOtherShadows = ei::DataId(0x0079BD18);
constexpr std::uintptr_t kClass52Shadows = ei::DataId(0x0079BD1C);
constexpr int kClassUnit = 0x50;
constexpr int kClass52 = 0x52;
constexpr std::uint32_t kHalfShown = 0x20;
constexpr int kOutsideView = 2;

int Setting(std::uintptr_t address) { return *ei::GamePointer<const volatile int>(address); }

// The switch for the class of `object` (its slot 12).
int ShadowSwitchOf(CObject3D* object) {
    int classId = object->GetClassId();
    if (classId == kClassUnit) return Setting(kUnitShadows);
    if (classId == kClass52) return Setting(kClass52Shadows);
    return Setting(kOtherShadows);
}

} // namespace

// --- The client half's shadow state -----------------------------------------------------------------

std::uint32_t CObject3DClientSpecific::ShadowsShown() {
    int setting = worlddata::ShadowSetting();
    if (setting <= 0) return static_cast<std::uint32_t>(setting) & 0xFFFFFF00u;   // XOR AL, AL
    return ShadowSwitchOf(object) != 0 ? 1u : 0u;
}
EI_REPLACE_METHOD(0x0051E200, CObject3DClientSpecific::ShadowsShown);

void CObject3DClientSpecific::ClearShadow() {
    vertices.SetSize(0, -1);
    texCoords.SetSize(0, -1);
    if (indices.data) {
        lib::FreeAligned32(indices.data);
        indices.data = nullptr;
    }
    indices.capacity = 0;
    indices.size = 0;
    CEngineObject* old = texture;
    texture = nullptr;
    if (old) old->Release();
    bounds[3] = 0.0f;
    bounds[2] = 0.0f;
    bounds[1] = 0.0f;
    bounds[0] = 0.0f;
    std::memset(&sphere, 0, 0x38);
}
EI_REPLACE_METHOD(0x0051E280, CObject3DClientSpecific::ClearShadow);

bool CObject3DClientSpecific::ShadowInView() {
    if (worlddata::ShadowSetting() <= 0 || ShadowSwitchOf(object) == 0) return false;
    if (flags_ & kHalfShown) return true;
    const Vector3& at = object->position;
    sphere.center.x = sphere.center.x + at.x;
    sphere.center.y = at.y + sphere.center.y;
    sphere.center.z = at.z + sphere.center.z;
    int side = ScenePipeline()->ClipTest(&sphere);
    const Vector3& back = object->position;
    sphere.center.x = sphere.center.x - back.x;
    sphere.center.y = sphere.center.y - back.y;
    sphere.center.z = sphere.center.z - back.z;
    return side != kOutsideView;
}
EI_REPLACE_METHOD(0x0051E300, CObject3DClientSpecific::ShadowInView);

// 0051C110 (RET 4). The scene pipeline's pop (model.h; Draw and the decal projector call it).
EI_REPLACE_METHOD(0x0051C110, CTransformPipeline::PopMatrix);

// 0051E1C0 (ECX = matrix). The identity matrix (DrawShadow's pixel matrix starts from it).
void __fastcall MatrixSetIdentity(Matrix4* matrix) {
    std::memset(matrix, 0, sizeof(Matrix4));
    matrix->m[15] = 1.0f;
    matrix->m[10] = 1.0f;
    matrix->m[5] = 1.0f;
    matrix->m[0] = 1.0f;
}
EI_REPLACE(0x0051E1C0, MatrixSetIdentity);

// --- The base half's empty slots (the server half keeps them) ---------------------------------------

// 0051AC60 (slot 19, RET 8): nothing is drawn; 0.
int CObject3DSpecific::DrawNothing(void* /*context*/, int* /*dirty*/) { return 0; }
EI_REPLACE_METHOD(0x0051AC60, CObject3DSpecific::DrawNothing);
// 0051AC70 (slot 20), 0051AC90 / 0051ACB0 / 0051ACA0 (slots 22-24), and the server half's part
// slots 0051F270 / 0051F290 / 0051F280 / 0051F2B0 / 0051F2A0 (13-17), all RET 4: 0.
int CObject3DSpecific::ReturnZero(void*) { return 0; }
EI_REPLACE_METHOD(0x0051AC70, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051AC90, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051ACB0, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051ACA0, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051F270, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051F290, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051F280, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051F2B0, CObject3DSpecific::ReturnZero);
EI_REPLACE_METHOD(0x0051F2A0, CObject3DSpecific::ReturnZero);
// 0051AC80 (slot 21): nothing. One byte, as 0051E830 (the empty destroy step the containers of
// plain elements call): too short to hook, registered so the address is known.
void __fastcall SpecificBaseNothing(CObject3DSpecific*) {}
EI_REPLACE(0x0051AC80, SpecificBaseNothing);
void __cdecl DestroyNothing() {}
EI_REPLACE(0x0051E830, DestroyNothing);
// 0051ACC0 / 0051ACD0 (slots 25 / 26, RET 4): nothing.
void CObject3DSpecific::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x0051ACC0, CObject3DSpecific::IgnoreArgument);
EI_REPLACE_METHOD(0x0051ACD0, CObject3DSpecific::IgnoreArgument);

// --- The server half ---------------------------------------------------------------------------------

// 0051F240. The constructor (the engine object's fields inline): no object. Returns the half.
CObject3DSpecific* __fastcall Object3DServerConstruct(CObject3DSpecific* self) {
    self->ClearFields();
    self->prototypeId_ = 0;
    self->object = nullptr;
    ei::SetVtable(self, kServerVtable);
    return self;
}
EI_REPLACE(0x0051F240, Object3DServerConstruct);

// 0051F300. The destructor body: the base half's vtable, no object, the engine object's.
void __fastcall Object3DServerDestruct(CObject3DSpecific* self) {
    ei::SetVtable(self, kSpecificBaseVtable);
    self->object = nullptr;
    self->DestructBase();
}
EI_REPLACE(0x0051F300, Object3DServerDestruct);

// 0051F2E0 (slot 0, RET 4).
CObject3DSpecific* __fastcall Object3DServerDelete(CObject3DSpecific* self, void*, unsigned flags) {
    Object3DServerDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0051F2E0, Object3DServerDelete);

// 0051F320 (slot 2): 1. 0051F330 (slot 3): no object.
int __fastcall Object3DServerCreate(CObject3DSpecific*) { return 1; }
EI_REPLACE(0x0051F320, Object3DServerCreate);
void __fastcall Object3DServerDestroy(CObject3DSpecific* self) { self->object = nullptr; }
EI_REPLACE(0x0051F330, Object3DServerDestroy);

// 0051F2C0 / 0051F2D0 (slots 11 / 12).
const char* __fastcall Object3DServerClassName(CObject3DSpecific*) {
    return kServerClassName;
}
EI_REPLACE(0x0051F2C0, Object3DServerClassName);
int __fastcall Object3DServerClassId(CObject3DSpecific*) { return kServerClassId; }
EI_REPLACE(0x0051F2D0, Object3DServerClassId);

// --- The wounds material and the stage copies ---------------------------------------------------------

// 0051C730. The destructor body: every stage destroyed (its texture released, then its name), the
// vector's block freed, the preset released (deleted at its last reference), the root's vtable.
void __fastcall WoundsMaterialDestruct(WoundsMaterial* self) {
    self->stages.clear();
    self->stages.FreeStorage();
    if (CRenderPreset* preset = self->preset) {
        if (--preset->refs_ == 0) preset->DeleteThis(1);
    }
    ei::SetVtable(self, WoundsMaterial::kRootVtableAddress);
}
EI_REPLACE(0x0051C730, WoundsMaterialDestruct);

// 0051C710 (slot 0, RET 4).
WoundsMaterial* __fastcall WoundsMaterialDelete(WoundsMaterial* self, void*, unsigned flags) {
    WoundsMaterialDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0051C710, WoundsMaterialDelete);

// 0051C800. A named texture's destructor: the texture released, the name freed.
void __fastcall NamedTextureDestruct(NamedTexture* self) {
    if (self->texture) self->texture->Release();
    self->name.~string();
}
EI_REPLACE(0x0051C800, NamedTextureDestruct);

// 0051E430 (RET 4). A named texture's copy constructor: the name copied (a block of its length +
// 1), the texture referenced. Returns the copy.
NamedTexture* NamedTexture::Copy(const NamedTexture* other) {
    new (&name) stlp::string(other->name);
    texture = other->texture;
    if (texture) ++texture->refCount_;
    return this;
}
EI_REPLACE_METHOD(0x0051E430, NamedTexture::Copy);

// 0051E970 (ECX = the place, EDX = the stage). A material stage copied into `place` unless it is
// null (the placement copy the stage vector's growth uses).
void __fastcall MaterialStageCopy(MaterialStage* place, const MaterialStage* stage) {
    if (place) new (place) MaterialStage(*stage);
}
EI_REPLACE(0x0051E970, MaterialStageCopy);
