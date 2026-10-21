// The figure meshes' lifetime (A-W13): the constructor body the figure classes share, Create and
// Clear (vtable slots 2 and 3), the destructor bodies, the drawer and owner setters, and the figure
// manager's constructor and its CreateObject (slot 23), which gives every new figure a drawer.
// See figure_mesh.h, figure_manager.h and the "Figure meshes" section of docs/notes/world.md.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include "figure_manager.h"
#include "figure_mesh.h"
#include "item.h"
#include "link_figure.h"
#include "originals_core.h"
#include "originals_world.h"

#include <cstring>
#include <new>

namespace {

constexpr int kFirstFigureClass = 0x41;
constexpr int kLastFigureClass = 0x44;
constexpr int kDrawerClass = 0x45;

// The drawer and the mesh as the engine objects they are (reference count, Release).
CEngineObject* AsObject(FigureMeshDrawer* drawer) { return reinterpret_cast<CEngineObject*>(drawer); }

} // namespace

// --- Construction -------------------------------------------------------------------------------

CFigureMesh* CFigureMesh::ConstructBase() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    refCount_ = 0;
    id_ = -1;
    prototypeId_ = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    localRotation_.SetIdentity();
    worldRotation_.SetIdentity();
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    object_ = nullptr;
    new (&name_) mfc::CString();
    new (&directory_) mfc::CString();
    offsets_.Construct();
    colorsA_.Construct();
    colorsB_.Construct();
    std::memset(&bounds_, 0, sizeof(bounds_));
    std::memset(&boxPivot_, 0, sizeof(boxPivot_));
    ei::SetVtable(this, kBaseVtableAddress);
    CreateMesh();
    return this;
}
EI_REPLACE_METHOD(0x005AABC0, CFigureMesh::ConstructBase);

int CFigureMesh::CreateMesh() {
    radius_ = 0.0f;
    drawer_ = nullptr;
    flags_ = 0;
    materialValue_ = 0;
    meshType_ = 0;
    std::memset(&buffers_, 0, sizeof(buffers_));
    textureOffsetU_ = 0;
    textureOffsetV_ = 0;
    spareDC_ = 0;
    flatColour_ = 0xFFFFFFFF;
    flatSpecular_ = 0xFFFFFFFF;
    std::memset(&bounds_, 0, sizeof(bounds_));
    std::memset(&boxPivot_, 0, sizeof(boxPivot_));
    return 1;
}
EI_REPLACE_METHOD(0x0059F210, CFigureMesh::CreateMesh);

// 005A6C70 / 005A6E00: the jumps of the figure classes' slot 2 to CreateMesh (through its address,
// so its check runs).
int __fastcall CreateMeshJump(CFigureMesh* self) { return self->CreateMesh(); }
EI_REPLACE(0x005A6C70, CreateMeshJump);
EI_REPLACE(0x005A6E00, CreateMeshJump);

// --- Clearing -----------------------------------------------------------------------------------

void CFigureMesh::Clear() {
    name_.Empty();
    directory_.Empty();
    BuffersChanged();
    offsets_.SetSize(0);
    colorsA_.SetSize(0);
    colorsB_.SetSize(0);
    std::memset(&buffers_, 0, sizeof(buffers_));
    materialValue_ = 0;
    meshType_ = 0;
    if (drawer_ != nullptr) {
        drawer_->mesh_ = nullptr;
        AsObject(drawer_)->Release();
    }
    drawer_ = nullptr;
    flags_ = 0;
}
EI_REPLACE_METHOD(0x0059F2B0, CFigureMesh::Clear);

void CFigureMesh::ClearMesh() {
    vertices_.SetSize(0);
    normals_.SetSize(0);
    corners_.SetSize(0);
    words_.SetSize(0);
    indices_.SetSize(0);
    texCoords_.SetSize(0);
    Clear();
}
EI_REPLACE_METHOD(0x005A6C80, CFigureMesh::ClearMesh);

void CFigureMesh::ClearSharedMesh() {
    vertices_.SetSize(0);
    normals_.SetSize(0);
    Clear();
}
EI_REPLACE_METHOD(0x005A6E10, CFigureMesh::ClearSharedMesh);

// --- Destruction --------------------------------------------------------------------------------

void CFigureMesh::DestructBase() {
    ei::SetVtable(this, kBaseVtableAddress);
    Clear();
    colorsB_.FreeData();
    colorsA_.FreeData();
    offsets_.FreeData();
    directory_.~CString();
    name_.~CString();
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    CEngineObject::DestructBase();
}
EI_REPLACE_METHOD(0x0059F030, CFigureMesh::DestructBase);

void CFigureMesh::DestructMesh() {
    ei::SetVtable(this, kMeshVtableAddress);
    DestructPlainMesh();
}
EI_REPLACE_METHOD(0x005AAD00, CFigureMesh::DestructMesh);

void CFigureMesh::DestructPlainMesh() {
    texCoords_.FreeData();
    indices_.FreeData();
    words_.FreeData();
    corners_.FreeData();
    normals_.FreeData();
    vertices_.FreeData();
    FigureDestructBodyOriginal(this);
}
EI_REPLACE_METHOD(0x00454510, CFigureMesh::DestructPlainMesh);

CFigureMesh* CFigureMesh::DeletingDestructPlainMesh(unsigned flags) {
    DestructPlainMesh();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004543A0, CFigureMesh::DeletingDestructPlainMesh);

void CFigureMesh::DestructSharedMesh() {
    normals_.FreeData();
    vertices_.FreeData();
    FigureDestructBodyOriginal(this);
}
EI_REPLACE_METHOD(0x005AADC0, CFigureMesh::DestructSharedMesh);

CFigureMesh* CFigureMesh::DeletingDestructMesh(unsigned flags) {
    DestructMesh();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005AACE0, CFigureMesh::DeletingDestructMesh);

CFigureMesh* CFigureMesh::DeletingDestructSharedMesh(unsigned flags) {
    DestructSharedMesh();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005AADA0, CFigureMesh::DeletingDestructSharedMesh);

// --- Drawer and owner ---------------------------------------------------------------------------

void CFigureMesh::SetDrawer(FigureMeshDrawer* drawer) {
    if (drawer_ != nullptr) {
        drawer_->mesh_ = nullptr;
        AsObject(drawer_)->Release();
    }
    drawer_ = drawer;
    if (drawer != nullptr) {
        drawer->mesh_ = this;
        ++AsObject(drawer_)->refCount_;
    }
}
EI_REPLACE_METHOD(0x0059F390, CFigureMesh::SetDrawer);

void CFigureMesh::LoadNothing(char**, float, float, float) { flags_ |= 0x20; }
EI_REPLACE_METHOD(0x005A2480, CFigureMesh::LoadNothing);

void CFigureMesh::SetObject(CObject3D* object) { object_ = object; }
EI_REPLACE_METHOD(0x0059F3D0, CFigureMesh::SetObject);

// --- The figure manager -------------------------------------------------------------------------

CFigureManager::CFigureManager() { models_.InitBuckets(100); }

CFigureManager* CFigureManager::Construct() { return new (this) CFigureManager(); }
EI_REPLACE_METHOD(0x005AA070, CFigureManager::Construct);

CEngineObject* CFigureManager::NewObject(int type) {
    return static_cast<CEngineObject*>(::CreateFigureObject(this, nullptr, type));
}

CEngineObject* CFigureManager::CreateFigureObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObjectImpl(type, id);
    if (type < kFirstFigureClass || type > kLastFigureClass || object == nullptr) return object;
    CBaseManager* drawers = static_cast<CManagerContext*>(context_)->SpecificManager();
    auto* drawer = reinterpret_cast<FigureMeshDrawer*>(drawers->CreateObject(kDrawerClass, -1));
    reinterpret_cast<CFigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
EI_REPLACE_METHOD(0x005AAE00, CFigureManager::CreateFigureObject);
