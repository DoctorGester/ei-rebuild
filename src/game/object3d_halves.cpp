// The halves of a 3D object besides the client half's part sets: the client half's shadow state, the base
// half's empty slots, the server half (CObject3DServer, class id 0x38), the wounds material's destructor and
// the copies of a named texture and a material stage. Layouts: map_object_shadow.h, map_object_figures.h.
#include "core_refcount.h"
#include "map_object_shadow.h"
#include "options.h"

#include "figure_mesh.h"        // VertexPacket
#include "ground_decal.h"       // DecalTexCoord
#include "ground_print.h"       // NamedTexture
#include "map_object_figures.h"
#include "model.h"
#include "object.h"
#include "texture.h"
#include "world_data.h"

#include <cstring>
#include <memory>
#include <new>

namespace {

constexpr const char* kServerClassName = "CObject3DServer";
constexpr int kServerClassId = 0x38;
// The shadow switches by the object's class: units (0x50), class 0x52, any other.
constexpr int kClassUnit = 0x50;
constexpr int kClass52 = 0x52;
constexpr std::uint32_t kHalfShown = 0x20;
constexpr int kOutsideView = 2;

int Setting(const int& setting) { return static_cast<const volatile int&>(setting); }

// The switch for the class of `object` (its slot 12).
int ShadowSwitchOf(CObject3D* object) {
    int classId = object->GetClassId();
    if (classId == kClassUnit) return Setting(options::g_options->unitShadow_);
    if (classId == kClass52) return Setting(options::g_options->floraShadow_);
    return Setting(options::g_options->buildingShadow_);
}

} // namespace

// --- The client half's shadow state -----------------------------------------------------------------

std::uint32_t CObject3DClientSpecific::ShadowsShown() {
    int setting = worlddata::ShadowSetting();
    if (setting <= 0) return static_cast<std::uint32_t>(setting) & 0xFFFFFF00u;
    return ShadowSwitchOf(object) != 0 ? 1u : 0u;
}

void CObject3DClientSpecific::ClearShadow() {
    vertices.clear();
    texCoords.clear();
    lib::FreeStorage(indices);
    CTexture* old = texture;
    texture = nullptr;
    if (old) old->Release();
    bounds[3] = 0.0f;
    bounds[2] = 0.0f;
    bounds[1] = 0.0f;
    bounds[0] = 0.0f;
    std::memset(&sphere, 0, sizeof(sphere));
}

bool CObject3DClientSpecific::ShadowInView() {
    if (worlddata::ShadowSetting() <= 0 || ShadowSwitchOf(object) == 0) return false;
    if (flags_ & kHalfShown) return true;
    const Vector3& at = object->localPosition_;
    sphere.center.x = sphere.center.x + at.x;
    sphere.center.y = at.y + sphere.center.y;
    sphere.center.z = at.z + sphere.center.z;
    int side = ScenePipeline()->ClipTest(&sphere);
    const Vector3& back = object->localPosition_;
    sphere.center.x = sphere.center.x - back.x;
    sphere.center.y = sphere.center.y - back.y;
    sphere.center.z = sphere.center.z - back.z;
    return side != kOutsideView;
}

// --- The base half's empty slots (the server half keeps them) ---------------------------------------

// Nothing is drawn; 0.
int CObject3DSpecific::DrawParts(CSceneRenderer* /*context*/, tagRECT* /*dirty*/) { return 0; }
int CObject3DSpecific::DrawPartsShadow(CSceneRenderer*) { return 0; }
int CObject3DSpecific::BoundsContainPoint(const long*) { return 0; }
int CObject3DSpecific::PartsIntersectRect(const tagRECT*) { return 0; }
int CObject3DSpecific::PartsContainPoint(const long*) { return 0; }

// --- The server half ---------------------------------------------------------------------------------

// The base half's (the engine object's fields, no object). Deleting form: the base half's.
CObject3DServer::CObject3DServer() {}
CObject3DServer::~CObject3DServer() {}

// Slot 3: no object.
int CObject3DServer::Create() { return 1; }
void CObject3DServer::Destroy() { object = nullptr; }

// Slots 11 / 12.
const char* CObject3DServer::GetClassName() { return kServerClassName; }
int CObject3DServer::GetClassId() { return kServerClassId; }

// Slots 13-17: no parts; 0.
int CObject3DServer::OnPartAdded(CFigureMesh*) { return 0; }
CFigureMesh* CObject3DServer::RemoveFromPartSets(const char*) { return nullptr; }
int CObject3DServer::OnPartRemoved(CFigureMesh*) { return 0; }
CFigureMesh* CObject3DServer::FindPart(const char*) { return nullptr; }
CFigureMesh* CObject3DServer::FindPartOfType(int) { return nullptr; }

// --- The wounds material and the stage copies ---------------------------------------------------------

WoundsMaterial::~WoundsMaterial() {
    if (CRenderPreset* old = preset) {
        if (--old->refs_ == 0) delete old;
    }
}

