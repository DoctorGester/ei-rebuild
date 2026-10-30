// The halves of a 3D object besides the client half's part sets: the client half's shadow state, the base
// half's empty slots, the server half (Object3DServer, class id 0x38), the wounds material's destructor and
// the copies of a named texture and a material stage. Layouts: map_object_shadow.h, map_object_figures.h.
#include "core_refcount.h"
#include "figure_mesh.h"    // VertexPacket
#include "ground_print.h"   // NamedTexture
#include "lib/rect.h"
#include "link_figure.h"   // LinkMesh::screenRect
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "material.h"
#include "model.h"
#include "object.h"
#include "options.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"

#include <cstring>
#include <memory>
#include <new>

namespace {

constexpr const char* kServerClassName = "CObject3DServer";
// The shadow switches by the object's class: units (0x50), class 0x52, any other.
constexpr std::uint32_t kHalfShown = 0x20;
constexpr int kOutsideView = 2;

int Setting(const int& setting) { return static_cast<const volatile int&>(setting); }

// The switch for the class of `object`.
int ShadowSwitchOf(Object3D* object) {
    ClassId classId = object->GetClassId();
    if (classId == ClassId::Unit) return Setting(options::g_options.unitShadow);
    if (classId == ClassId::Tree) return Setting(options::g_options.floraShadow);
    return Setting(options::g_options.buildingShadow);
}

} // namespace

// --- The client half's shadow state -----------------------------------------------------------------

std::uint32_t Object3DClientSpecific::ShadowsShown() {
    int setting = worlddata::ShadowSetting();
    if (setting <= 0) return static_cast<std::uint32_t>(setting) & 0xFFFFFF00u;
    return ShadowSwitchOf(object) != 0 ? 1u : 0u;
}

void Object3DClientSpecific::ClearShadow() {
    shadowMade = false;
    std::memset(&sphere, 0, sizeof(sphere));
}

bool Object3DClientSpecific::ShadowInView() {
    if (worlddata::ShadowSetting() <= 0 || ShadowSwitchOf(object) == 0) return false;
    if (flags & kHalfShown) return true;
    const Vector3& at = object->localPosition;
    sphere.center.x = sphere.center.x + at.x;
    sphere.center.y = at.y + sphere.center.y;
    sphere.center.z = at.z + sphere.center.z;
    int side = g_sceneRenderer.pipeline->ClipTest(&sphere);
    const Vector3& back = object->localPosition;
    sphere.center.x = sphere.center.x - back.x;
    sphere.center.y = sphere.center.y - back.y;
    sphere.center.z = sphere.center.z - back.z;
    return side != kOutsideView;
}

// --- The base half's empty slots (the server half keeps them) ---------------------------------------

// Nothing is drawn; 0.
int Object3DSpecific::DrawParts(SceneRenderer* /*context*/, Rect* /*dirty*/) { return 0; }
int Object3DSpecific::DrawPartsShadow(SceneRenderer*) { return 0; }
int Object3DSpecific::BoundsContainPoint(const std::int32_t*) { return 0; }
int Object3DSpecific::PartsIntersectRect(const Rect*) { return 0; }
int Object3DSpecific::PartsContainPoint(const std::int32_t*) { return 0; }

// --- The server half ---------------------------------------------------------------------------------

// The base half's (the engine object's fields, no object). Deleting form: the base half's.
Object3DServer::Object3DServer() {}
Object3DServer::~Object3DServer() {}

// No object.
void Object3DServer::Create() {}
void Object3DServer::Destroy() { object = nullptr; }

// Class name and id.
const char* Object3DServer::ClassName() { return kServerClassName; }
ClassId Object3DServer::GetClassId() { return ClassId::Object3DServer; }

// The part virtuals: no parts; 0.
int Object3DServer::OnPartAdded(FigureMesh*) { return 0; }
FigureMesh* Object3DServer::RemoveFromPartSets(const char*) { return nullptr; }
int Object3DServer::OnPartRemoved(FigureMesh*) { return 0; }
FigureMesh* Object3DServer::FindPart(std::string_view) { return nullptr; }
FigureMesh* Object3DServer::FindPartOfType(int) { return nullptr; }

// --- The wounds material and the stage copies ---------------------------------------------------------

WoundsMaterial::~WoundsMaterial() { preset.Reset(); }

// --- The client half's extra scale and part materials -----------------------------------------------

Vector3 Object3DClientSpecific::GetScale() { return {figureScale.x, figureScale.y, figureScale.z}; }

void Object3DClientSpecific::RegisterPartMaterials() {
    MaterialManager* manager = WorldContext()->GetMaterialManager();
    if (materials.empty()) return;
    materialCursor = 0;
    for (Material* material = materials[0]; material;) {
        manager->FillMaterial(material, static_cast<std::uint32_t>(material->type));
        ++materialCursor;
        if (std::ssize(materials) <= materialCursor) break;
        material = materials[materialCursor];
    }
}

// --- Screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {
// "Object3DClientSpecific".
const char* ClientClassName() { return "CObject3DClientSpecific"; }

Point AsPoint(const std::int32_t* point) { return Point{point[0], point[1]}; }
const Rect* PartRect(const FigureMesh* part) { return &part->drawer->screen; }

constexpr float kNoScale = 1.0f;

} // namespace

int Object3DClientSpecific::BoundsContainPoint(const std::int32_t* point) {
    if (!(object->flags & MapObjectFlag::Shown)) return 0;
    return rects::Contains(&object->dirtyRect, AsPoint(point));
}

int Object3DClientSpecific::PartsContainPoint(const std::int32_t* point) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (rects::Contains(PartRect(part), AsPoint(point))) return 1;
    }
    return 0;
}

int Object3DClientSpecific::PartRectsMeet(const Rect* rect) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        Rect common;
        rects::Intersect(&common, PartRect(part), rect);
        if (!rects::IsEmpty(&common)) return 1;
    }
    return 0;
}

FigureMesh* Object3DClientSpecific::FindPart(std::string_view name) { return object->FindPart(name); }

FigureMesh* Object3DClientSpecific::FindPartOfType(int partType) { return object->FindPartOfType(partType); }

FigureMesh* Object3D::FindPartOfType(int partType) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i]->type == partType) return parts[i];
    return nullptr;
}

// The class name and id 0x37.
const char* Object3DClientSpecific::ClassName() { return ClientClassName(); }
ClassId Object3DClientSpecific::GetClassId() { return ClassId::Object3DClientSpecific; }

// The base half's destructor (deleting form): the object forgotten.
Object3DSpecific::~Object3DSpecific() { object = nullptr; }

Vector3 Object3DSpecific::GetScale() { return {kNoScale, kNoScale, kNoScale}; }
