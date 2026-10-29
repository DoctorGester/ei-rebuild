// Interface windows, their regions and the aligned pointer arrays they keep. See ui.h.
#include "figure_manager.h"
#include "figure_mesh.h"
#include "game_singletons.h"
#include "material.h"
#include "texture.h"
#include "ui.h"
#include "ui_widescreen.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>
#include <initializer_list>
#include <memory>

namespace {
// 32-bit wrapping multiply.
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// A surface of the window's list, destroyed as a plain surface whatever its class, as the original does (a scrolling
// surface's own destructor does not run: CUiSurface's is not virtual); the block is freed unsized, since it may be a
// derived class's.
void DestroySurface(CUiSurface* surface) {
    surface->~CUiSurface();
    ::operator delete(surface);
}

// A new object of the model manager by name, or null.
CFigureMesh* NewModelObject(std::string_view name) {
    return g_clientWorld.figureManager->NewFigure(name);
}

constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

constexpr const char* kClassName = "Sorry, no other way.";

// A new material of kind 0x16 (filled by the manager) glowing `value` in every channel, referenced.
CMaterial* NewGlowMaterial(float value) {
    CMaterialManager* materials = g_clientWorld.materialManager;
    CMaterial* material = materials->NewMaterial(0x16);
    materials->FillMaterial(material, 0x16);
    material->colors.emissive.x = value;
    material->colors.emissive.y = value;
    material->colors.emissive.z = value;
    material->AddRef();
    return material;
}

} // namespace

// --- CUiTooltip ------------------------------------------------------------------------------

// --- CUiRegion -------------------------------------------------------------------------------

bool CUiRegion::HitTest(int x, int y) const {
    x -= offsetX;
    y -= offsetY;
    if (!enabled) return false;
    if (!gameapi::PtInRect(&bounds, POINT{x, y})) return false;
    if (rectOnly) return true;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products wrap at 32
    // bits like the original's.
    int count = std::ssize(points);
    for (int i = 0; i < count;) {
        const UiPoint& a = points[i];
        ++i;
        const UiPoint& b = points[i % count];
        int lhs = Mul(y, b.x) + Mul(b.y, a.x) + Mul(a.y, x);
        int rhs = Mul(a.y, b.x) + Mul(y, a.x) + Mul(b.y, x);
        if (lhs < rhs) return false;
    }
    return true;
}

bool CUiRegion::HasActiveTip() const {
    return tip && tip->Shown();
}

// Rebirth's WideScreen replaces SetRect and AddPoint (addon's replacements in front of the original's body): x
// is moved by the panel offset its panel builders set (ui_widescreen.h; 0 outside them).
void CUiRegion::SetRect(int left, int top, int right, int bottom) {
    points.clear();
    if (widescreen::Active()) {
        left += widescreen::PanelOffset();
        right += widescreen::PanelOffset();
    }
    bounds.left = left;
    bounds.right = right;
    bounds.top = top;
    bounds.bottom = bottom;
    enabled = true;
    rectOnly = true;
}

void CUiRegion::AddPoint(int x, int y) {
    if (widescreen::Active()) x += widescreen::PanelOffset();
    if (points.empty()) {
        bounds.left = x;
        bounds.top = y;
        bounds.right = x;
        bounds.bottom = y;
    } else {
        if (x < bounds.left) bounds.left = x;
        if (y < bounds.top) bounds.top = y;
        if (x > bounds.right) bounds.right = x;
        if (y > bounds.bottom) bounds.bottom = y;
    }
    int index = std::ssize(points);
    points.resize(index + 1);
    points[index].x = x;
    points[index].y = y;
    if (std::ssize(points) > 2) enabled = true;
}

void CUiRegion::OnMouseMove(int x, int y) {
    if (!tip) return;
    const bool inside = HitTest(x, y);
    if (inside) {
        if (!tip->Shown()) tip->state = CUiTooltip::State::Hovered;
    } else if (tip->Shown()) {
        tip->state = CUiTooltip::State::Disappearing;
    }
    tip->OnMouseMove(x, y);
}

// --- CUiWindow -------------------------------------------------------------------------------

int CUiWindow::RegionAt(int x, int y) const {
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    for (int i = 0; i < std::ssize(regions); ++i) {
        if (regions[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}

bool CUiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    return gameapi::PtInRect(&rect, point) != FALSE;
}

void CUiWindow::UpdateRegionTips() {
    POINT mouse{ui->mouseX, ui->mouseY};
    ui->ScalePointFromScreenEntry(&mouse);
    // Inlines CUiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < std::ssize(regions); ++i) {
        CUiRegion* region = regions[i];
        if (region->tip) region->OnMouseMove(mouse.x, mouse.y);
    }
}

// The same per region, each tooltip then redrawn (the tooltip is re-read).
void CUiWindow::RedrawTips() {
    POINT mouse{ui->mouseX, ui->mouseY};
    ui->ScalePointFromScreenEntry(&mouse);
    for (int i = 0; i < std::ssize(regions); ++i) {
        CUiRegion* region = regions[i];
        if (region->tip) region->OnMouseMove(mouse.x, mouse.y);
        if (CUiTooltip* tip = regions[i]->tip) tip->Redraw();
    }
}

void CUiWindow::DrawBase(CSceneRenderer* pass) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i)
        SceneMesh(i)->Draw(pass);
    for (int i = 0; i < std::ssize(models); ++i) models[i]->Draw(pass);
}

void CUiWindow::Redraw() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        CUiSurface* surface = surfaces[i];
        if (surface->Shown()) surface->Blit();
    }
}

void CUiWindow::SetEnabled(bool newEnabled, bool notify) {
    enabled = newEnabled;
    if (notify) Show(newEnabled);
}

void CUiWindow::AddObject(CTexture* texture) {
    if (!texture) return;
    objects.push_back(texture);
    texture->AddRef();
}

void CUiWindow::RemoveObject(CTexture* texture) {
    if (!texture) return;
    for (int i = 0; i < std::ssize(objects); ++i) {
        if (objects[i] == texture) {
            objects[i]->Release();
            objects.erase(objects.begin() + i);
            return;
        }
    }
}

void CUiWindow::RemoveObjectAt(int index) {
    objects[index]->Release();
    objects.erase(objects.begin() + index);
}

bool CUiWindow::RemoveSceneObject(CEngineObject* object) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        if (sceneObjects[i] == object) {
            // Unlike RemoveObject, the slot is removed before the release.
            sceneObjects.erase(sceneObjects.begin() + i);
            object->Release();
            return true;
        }
    }
    return false;
}

void CUiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects[index]->Release();
    sceneObjects.erase(sceneObjects.begin() + index);
}

void CUiWindow::ClearSceneObjects() {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) sceneObjects[i]->Release();
    sceneObjects.clear();
}

void CUiWindow::ClearObjects() {
    for (int i = 0; i < std::ssize(objects); ++i) objects[i]->Release();
    objects.clear();
}

// Returns `surface`, which the original leaves as its result; the titles' Create freezes the surface through
// it.
CUiSurface* CUiWindow::AddSurface(CUiSurface* surface) {
    if (!surface) return nullptr;
    surfaces.push_back(surface);
    return surface;
}

void CUiWindow::RemoveSurfaceAt(int index) {
    if (CUiSurface* surface = surfaces[index]) DestroySurface(surface);
    surfaces.erase(surfaces.begin() + index);
}

void CUiWindow::ClearSurfaces() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        if (CUiSurface* surface = surfaces[i]) DestroySurface(surface);
    }
    surfaces.clear();
}

CTexture* CUiWindow::LoadTexture(std::string_view name, TextureKind kind) {
    CTextureManager* manager = g_clientWorld.textureManager;
    CTexture* texture = manager->FindOrCreate(static_cast<int>(kind), name);
    if (!texture) return nullptr;
    int loaded = manager->LoadTexture(texture);
    if (!loaded) return nullptr;
    objects.push_back(texture);
    texture->AddRef();
    return texture;
}

CTexture* CUiWindow::CreateTexture(int width, int height, TextureKind kind) {
    CTexture* texture = g_clientWorld.textureManager->NewTexture(width, height, kind);
    if (!texture) return nullptr;
    int index = std::ssize(objects);
    objects.resize(index + 1);
    objects[index] = texture;
    texture->flags |= TextureFlag::Keep;
    texture->AddRef();
    return texture;
}

void CUiWindow::Show(bool show) {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        CUiSurface* surface = surfaces[i];
        if (!show || surface->Shown()) surface->SwapContents(show);
    }
}

void CUiSurface::Show() {
    shown = true;
    if (kept) SwapContents(true);
}

void CUiSurface::Hide(bool release) {
    shown = false;
    if (release) SwapContents(false);
}

CUiRegion* CUiWindow::AddRegion() {
    auto* region = new CUiRegion(ui);
    regions.push_back(region);
    return region;
}

void CUiWindow::RemoveRegionAt(int index) {
    delete regions[index];
    regions.erase(regions.begin() + index);
}

void CUiWindow::ClearRegions() {
    for (int i = 0; i < std::ssize(regions); ++i) {
        delete regions[i];
    }
    regions.clear();
}

CUiModel* CUiWindow::AddModel() {
    auto* model = new CUiModel();
    models.push_back(model);
    return model;
}

// --- ReplaceSceneObject ----------------------------------------------------------------------------

CEngineObject* CUiWindow::ReplaceSceneObject(CEngineObject* object, std::string_view name) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        if (sceneObjects[i] != object) continue;
        CFigureMesh* made = NewModelObject(name);
        if (!made) return nullptr;
        made->AddRef();
        made->SetObject(this);
        auto* sprite = AsUiMesh(made);
        auto* old = AsUiMesh(object);
        sprite->material = blackMaterial;
        sprite->texture = !objects.empty() ? objects[0] : nullptr;
        sprite->localPosition = old->localPosition;
        sprite->textureOffsetU = old->textureOffsetU;
        sprite->textureOffsetV = old->textureOffsetV;
        sprite->localRotation = old->localRotation;
        sprite->texture = old->texture;
        float scale = old->scaleMatrix.m[0];
        AsUiMesh(made)->SetScale(scale);
        sceneObjects.erase(sceneObjects.begin() + i);
        object->Release();
        // Back in at the same place.
        sceneObjects.insert(sceneObjects.begin() + i, made);
        return made;
    }
    return nullptr;
}

// --- AddSprite -------------------------------------------------------------------------------------

CUiMesh* CUiWindow::AddSprite(std::string_view name) {
    CFigureMesh* object = NewModelObject(name);
    if (!object) return nullptr;
    return AddSceneObject(object);
}

CUiMesh* CUiWindow::AddBlankSprite() { return AddSceneObject(g_clientWorld.figureManager->NewBlankFigure()); }

CUiMesh* CUiWindow::AddSceneObject(CFigureMesh* object) {
    bool listed = false;
    for (int i = 0; i < std::ssize(sceneObjects) && !listed; ++i) listed = sceneObjects[i] == object;
    if (!listed) {
        lib::SetAtGrow(sceneObjects, std::ssize(sceneObjects), object);
        object->AddRef();
        object->SetObject(this);
    }
    auto* sprite = AsUiMesh(object);
    sprite->material = blackMaterial;
    sprite->texture = !objects.empty() ? objects[0] : nullptr;
    return sprite;
}

// --- The constructor ---------------------------------------------------------------------------------

// The original's closing Show(true) reaches CUiWindow's own (the surfaces' list is empty): left out.
CUiWindow::CUiWindow(CInterface3D* ui) : UiInputSide(ui) {
    localRotation = kIdentityRotation;
    worldRotation.SetIdentity();
    localPosition = Vector3{0.0f, 0.0f, 0.0f};
    whiteMaterial = NewGlowMaterial(1.0f);
    blackMaterial = NewGlowMaterial(0.0f);
}

const char* CUiWindow::ClassName() { return kClassName; }
ClassId CUiWindow::GetClassId() { return ClassId::UiWindow; }

// --- CUiContainer::AddChild ------------------------------------------------------------------------

void CUiContainer::AddChild(CUiWindow* child) {
    if (!child) return;
    children.push_back(child);
}

// --- The destructor ---------------------------------------------------------------------------------

CUiWindow::~CUiWindow() {
    for (CEngineObject** held : {&blackMaterial, &whiteMaterial, &heldObject}) {
        if (*held) (*held)->Release();
        *held = nullptr;
    }
    for (int i = 0; i < std::ssize(sceneObjects); ++i) sceneObjects[i]->Release();
    sceneObjects.clear();
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        if (CUiSurface* surface = surfaces[i]) DestroySurface(surface);
    }
    surfaces.clear();
    for (int i = 0; i < std::ssize(regions); ++i) {
        delete regions[i];
    }
    regions.clear();
    for (int i = 0; i < std::ssize(objects); ++i) objects[i]->Release();
    objects.clear();
    for (int i = 0; i < std::ssize(models); ++i) {
        delete models[i];
    }
    models.clear();
    flags = 0;
}
