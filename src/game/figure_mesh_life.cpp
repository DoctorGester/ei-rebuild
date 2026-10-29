// The figure meshes' lifetime: the constructor body the figure classes share, Create and Clear, the classes
// 0x41 and 0x42's constructors, destructors and virtuals, the drawer and owner setters, and the figure
// manager's constructor and its CreateObject, which gives every new figure a drawer. See figure_mesh.h,
// figure_manager.h and the "Figure meshes" section of docs/notes/world.md.

#include "figure_manager.h"
#include "figure_mesh.h"
#include "item.h"
#include "link_figure.h"
#include "manager_context.h"

#include <cstring>
#include <memory>
#include <new>

// --- Construction -------------------------------------------------------------------------------

void CFigureMesh::SetUpFigure() {
    refCount = 0;
    id = -1;
    prototypeId = 0;
    type = 0;
    flags = 0;
    owner = nullptr;
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    object = nullptr;
    std::construct_at(&offsets);
    std::construct_at(&colorsA);
    std::construct_at(&colorsB);
    std::memset(&box.bounds, 0, sizeof(box.bounds));
    box.pivot = {};
}

CFigureMesh::CFigureMesh() {
    SetUpFigure();
    CreateMesh();
}

void CFigureMesh::CreateMesh() {
    radius = 0.0f;
    drawer = nullptr;
    flags = 0;
    materialValue = 0;
    meshType = 0;
    std::memset(&buffers, 0, sizeof(buffers));
    textureOffsetU = 0;
    textureOffsetV = 0;
    flatColour = 0xFFFFFFFF;
    flatSpecular = 0xFFFFFFFF;
    std::memset(&box.bounds, 0, sizeof(box.bounds));
    box.pivot = {};
}

// --- Clearing -----------------------------------------------------------------------------------

void CFigureMesh::Clear() {
    name.clear();
    directory.clear();
    BuffersChanged();
    offsets.clear();
    colorsA.clear();
    colorsB.clear();
    std::memset(&buffers, 0, sizeof(buffers));
    materialValue = 0;
    meshType = 0;
    if (drawer != nullptr) {
        drawer->mesh = nullptr;
        drawer->Release();
    }
    drawer = nullptr;
    flags = 0;
}

void CPlainFigureMesh::Destroy() {
    vertices.clear();
    normals.clear();
    corners.clear();
    words.clear();
    indices.clear();
    texCoords.clear();
    Clear();
}

void CSharedFigureMesh::Destroy() {
    vertices.clear();
    normals.clear();
    Clear();
}

// --- Destruction --------------------------------------------------------------------------------

void CFigureMesh::TearDownFigure() {
    Clear();
    lib::FreeStorage(colorsB);
    lib::FreeStorage(colorsA);
    lib::FreeStorage(offsets);
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

void CFigureMesh::Create() { CreateMesh(); }
void CFigureMesh::Destroy() { Clear(); }
const char* CFigureMesh::GetName() { return name.c_str(); }
const char* CFigureMesh::ClassName() { return "CFigure"; }
ClassId CFigureMesh::GetClassId() { return ClassId::FigureMesh; }

// --- Drawer and owner ---------------------------------------------------------------------------

void CFigureMesh::SetDrawer(FigureMeshDrawer* newDrawer) {
    if (drawer != nullptr) {
        drawer->mesh = nullptr;
        drawer->Release();
    }
    drawer = newDrawer;
    if (newDrawer != nullptr) {
        newDrawer->mesh = this;
        drawer->AddRef();
    }
}

void CFigureMesh::SetObject(CTransformObject* newObject) { object = newObject; }

// --- The figure manager -------------------------------------------------------------------------

CFigureManager::CFigureManager() {}

CEngineObject* CFigureManager::NewObject(int objectType) {
    return static_cast<CEngineObject*>(::CreateFigureObject(objectType));
}

CEngineObject* CFigureManager::CreateFigureObject(int newType, int newId) {
    CEngineObject* object = CBaseManager::CreateObject(newType, newId);
    const auto kind = static_cast<FigureObjectType>(newType);
    if (kind < FigureObjectType::SharedFigureMesh || kind > FigureObjectType::SkyObject || object == nullptr) return object;
    CBaseManager* drawers = context->SpecificManager();
    auto* drawer = drawers->CreateObjectAs<FigureMeshDrawer>(SideObjectType::Drawer, -1);
    static_cast<CFigureMesh*>(object)->SetDrawer(drawer);
    return object;
}
