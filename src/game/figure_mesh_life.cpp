// The figure meshes' lifetime: the constructor body the figure classes share, Create and Clear, the classes
// 0x41 and 0x42's constructors, destructors and virtuals, the drawer and owner setters, and the figure
// manager's constructor and its CreateObject, which gives every new figure a drawer. See figure_mesh.h,
// figure_manager.h and the "Figure meshes" section of docs/notes/world.md.

#include "figure_manager.h"
#include "figure_mesh.h"
#include "item.h"
#include "link_figure.h"

#include <cstring>
#include <memory>
#include <new>

namespace {

constexpr int kFirstFigureClass = 0x41;
constexpr int kLastFigureClass = 0x44;

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
    std::construct_at(&offsets_);
    std::construct_at(&colorsA_);
    std::construct_at(&colorsB_);
    std::memset(&box_.bounds, 0, sizeof(box_.bounds));
    box_.pivot = {};
}

CFigureMesh::CFigureMesh() {
    SetUpFigure();
    CreateMesh();
}

int CFigureMesh::CreateMesh() {
    radius_ = 0.0f;
    drawer_ = nullptr;
    flags_ = 0;
    materialValue_ = 0;
    meshType_ = 0;
    std::memset(&buffers_, 0, sizeof(buffers_));
    textureOffsetU_ = 0;
    textureOffsetV_ = 0;
    flatColour_ = 0xFFFFFFFF;
    flatSpecular_ = 0xFFFFFFFF;
    std::memset(&box_.bounds, 0, sizeof(box_.bounds));
    box_.pivot = {};
    return 1;
}

// --- Clearing -----------------------------------------------------------------------------------

void CFigureMesh::Clear() {
    name_.clear();
    directory_.clear();
    BuffersChanged();
    offsets_.clear();
    colorsA_.clear();
    colorsB_.clear();
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

void CPlainFigureMesh::Destroy() {
    vertices_.clear();
    normals_.clear();
    corners_.clear();
    words_.clear();
    indices_.clear();
    texCoords_.clear();
    Clear();
}

void CSharedFigureMesh::Destroy() {
    vertices_.clear();
    normals_.clear();
    Clear();
}

// --- Destruction --------------------------------------------------------------------------------

void CFigureMesh::TearDownFigure() {
    Clear();
    lib::FreeStorage(colorsB_);
    lib::FreeStorage(colorsA_);
    lib::FreeStorage(offsets_);
}

CFigureMesh::~CFigureMesh() { TearDownFigure(); }

// --- The classes 0x41 and 0x42 ---------------------------------------------------------------

CSharedFigureMesh::CSharedFigureMesh() {
}

CSharedFigureMesh::~CSharedFigureMesh() {
}

CPlainFigureMesh::CPlainFigureMesh() {
}

CPlainFigureMesh::~CPlainFigureMesh() {
}

// --- Small virtuals ----------------------------------------------------------------------------

int CFigureMesh::Create() { return CreateMesh(); }
void CFigureMesh::Destroy() { Clear(); }
const char* CFigureMesh::GetName() { return name_.c_str(); }
const char* CFigureMesh::ClassName() { return "CFigure"; }
ClassId CFigureMesh::GetClassId() { return kClassFigureMesh; }

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

void CFigureMesh::SetObject(CTransformObject* object) { object_ = object; }

// --- The figure manager -------------------------------------------------------------------------

CFigureManager::CFigureManager() {}

CEngineObject* CFigureManager::NewObject(int type) {
    return static_cast<CEngineObject*>(::CreateFigureObject(type));
}

CEngineObject* CFigureManager::CreateFigureObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    if (type < kFirstFigureClass || type > kLastFigureClass || object == nullptr) return object;
    CBaseManager* drawers = context_->SpecificManager();
    auto* drawer = drawers->CreateObjectAs<FigureMeshDrawer>(SideObjectType::Drawer, -1);
    static_cast<CFigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
