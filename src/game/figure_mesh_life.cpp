// The figure meshes' lifetime: the constructor body the figure classes share, Create and Clear, the classes
// 0x41 and 0x42's constructors, destructors and virtuals, the drawer and owner setters, and the figure
// manager's constructor and its CreateObject, which gives every new figure a drawer. See figure_mesh.h,
// figure_manager.h and the "Figure meshes" section of docs/notes/world.md.
#include "ei/hook.h"
#include "lib/crt.h"

#include "figure_manager.h"
#include "figure_mesh.h"
#include "item.h"
#include "link_figure.h"
#include "scene_loader.h"   // InitFigurePart

#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName

namespace {

constexpr int kFirstFigureClass = 0x41;
constexpr int kLastFigureClass = 0x44;
constexpr int kDrawerClass = 0x45;

} // namespace

// --- Construction -------------------------------------------------------------------------------

void CFigureMesh::SetUpFigure() {
    refCount_ = 0;
    id_ = -1;
    prototypeId_ = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    localRotation_.SetIdentity();
    worldRotation_.SetIdentity();
    object_ = nullptr;
    offsets_.Construct();
    colorsA_.Construct();
    colorsB_.Construct();
    std::memset(&bounds_, 0, sizeof(bounds_));
    std::memset(&boxPivot_, 0, sizeof(boxPivot_));
}

CFigureMesh::CFigureMesh() {
    SetUpFigure();
    CreateMesh();
}
EI_REWRITTEN_SPECIAL(0x005AABC0, CFigureMesh::CFigureMesh);

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
EI_REWRITTEN(0x0059F210, CFigureMesh::CreateMesh);

// The figure classes' Create jumps to CreateMesh (through its address, so its check runs).
int __fastcall CreateMeshJump(CFigureMesh* self) { return self->CreateMesh(); }
EI_REWRITTEN(0x005A6C70, CreateMeshJump);
EI_REWRITTEN(0x005A6E00, CreateMeshJump);

// --- Clearing -----------------------------------------------------------------------------------

void CFigureMesh::Clear() {
    name_.clear();
    directory_.clear();
    BuffersChanged();
    offsets_.SetSize(0);
    colorsA_.SetSize(0);
    colorsB_.SetSize(0);
    std::memset(&buffers_, 0, sizeof(buffers_));
    materialValue_ = 0;
    meshType_ = 0;
    if (drawer_ != nullptr) {
        drawer_->mesh_ = nullptr;
        drawer_->Release();
    }
    drawer_ = nullptr;
    flags_ = 0;
}
EI_REWRITTEN(0x0059F2B0, CFigureMesh::Clear);

void CPlainFigureMesh::Destroy() {
    vertices_.SetSize(0);
    normals_.SetSize(0);
    corners_.SetSize(0);
    words_.SetSize(0);
    indices_.SetSize(0);
    texCoords_.SetSize(0);
    Clear();
}
EI_REWRITTEN(0x005A6C80, CPlainFigureMesh::Destroy);

void CSharedFigureMesh::Destroy() {
    vertices_.SetSize(0);
    normals_.SetSize(0);
    Clear();
}
EI_REWRITTEN(0x005A6E10, CSharedFigureMesh::Destroy);

// --- Destruction --------------------------------------------------------------------------------

void CFigureMesh::TearDownFigure() {
    Clear();
    colorsB_.FreeData();
    colorsA_.FreeData();
    offsets_.FreeData();
}

CFigureMesh::~CFigureMesh() { TearDownFigure(); }
EI_REWRITTEN_SPECIAL(0x0059F030, CFigureMesh::~CFigureMesh);
EI_REWRITTEN_SPECIAL(0x00453470, CFigureMesh::~CFigureMesh);

// --- The classes 0x41 and 0x42 ---------------------------------------------------------------

CSharedFigureMesh::CSharedFigureMesh() {
    vertices_.Construct();
    normals_.Construct();
}

CSharedFigureMesh::~CSharedFigureMesh() {
    normals_.FreeData();
    vertices_.FreeData();
}

EI_REWRITTEN_SPECIAL(0x005AADC0, CSharedFigureMesh::~CSharedFigureMesh);
EI_REWRITTEN_SPECIAL(0x005AADA0, CSharedFigureMesh::~CSharedFigureMesh);

CPlainFigureMesh::CPlainFigureMesh() {
    vertices_.Construct();
    normals_.Construct();
    corners_.Construct();
    words_.Construct();
    indices_.Construct();
    texCoords_.Construct();
}

CPlainFigureMesh::~CPlainFigureMesh() {
    texCoords_.FreeData();
    indices_.FreeData();
    words_.FreeData();
    corners_.FreeData();
    normals_.FreeData();
    vertices_.FreeData();
}

EI_REWRITTEN_SPECIAL(0x00454510, CPlainFigureMesh::~CPlainFigureMesh);
EI_REWRITTEN_SPECIAL(0x004543A0, CPlainFigureMesh::~CPlainFigureMesh);

// CPlainFigureMesh's constructor, as the scene's figure records inline it: the shared
// constructor, then zeros over the mesh's six arrays and the class 0x42 vtable.
void scene::InitFigurePart(CPlainFigureMesh* p) { new (p) CPlainFigureMesh(); }

// --- Small virtuals ----------------------------------------------------------------------------

int CFigureMesh::Create() { return CreateMesh(); }
void CFigureMesh::Destroy() { Clear(); }
const char* CFigureMesh::GetName() { return name_.c_str(); }
const char* CFigureMesh::GetClassName() { return "CFigure"; }
int CFigureMesh::GetClassId() { return 0x44; }

// --- Drawer and owner ---------------------------------------------------------------------------

void CFigureMesh::SetDrawer(FigureMeshDrawer* drawer) {
    if (drawer_ != nullptr) {
        drawer_->mesh_ = nullptr;
        drawer_->Release();
    }
    drawer_ = drawer;
    if (drawer != nullptr) {
        drawer->mesh_ = this;
        ++drawer_->refCount_;
    }
}
EI_REWRITTEN(0x0059F390, CFigureMesh::SetDrawer);

void CFigureMesh::LoadNothing(char**, float, float, float) { flags_ |= 0x20; }
EI_REWRITTEN(0x005A2480, CFigureMesh::LoadNothing);

void CFigureMesh::SetObject(CTransformObject* object) { object_ = object; }
EI_REWRITTEN(0x0059F3D0, CFigureMesh::SetObject);

// --- The figure manager -------------------------------------------------------------------------

CFigureManager::CFigureManager() { models_.InitBuckets(100); }

EI_REWRITTEN_SPECIAL(0x005AA070, CFigureManager::CFigureManager);

CEngineObject* CFigureManager::NewObject(int type) {
    return static_cast<CEngineObject*>(::CreateFigureObject(type));
}

CEngineObject* CFigureManager::CreateFigureObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    if (type < kFirstFigureClass || type > kLastFigureClass || object == nullptr) return object;
    CBaseManager* drawers = context_->SpecificManager();
    auto* drawer = drawers->CreateObjectAs<FigureMeshDrawer>(kDrawerClass, -1);
    static_cast<CFigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
EI_REWRITTEN(0x005AAE00, CFigureManager::CreateFigureObject);
