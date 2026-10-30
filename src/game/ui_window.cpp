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
// surface's own destructor does not run: UiSurface's is not virtual); the block is freed unsized, since it may be a
// derived class's.
void DestroySurface(UiSurface* surface) {
    surface->~UiSurface();
    ::operator delete(surface);
}

// A new object of the model manager by name, or null.
FigureMesh* NewModelObject(std::string_view name) {
    return g_clientWorld.figureManager->NewFigure(name);
}

constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

constexpr const char* kClassName = "Sorry, no other way.";

// A new material of kind 0x16 (filled by the manager) glowing `value` in every channel, referenced.
Material* NewGlowMaterial(float value) {
    CMaterialManager* materials = g_clientWorld.materialManager;
    Material* material = materials->NewMaterial(0x16);
    materials->FillMaterial(material, 0x16);
    material->colors.emissive.x = value;
    material->colors.emissive.y = value;
    material->colors.emissive.z = value;
    material->AddRef();
    return material;
}

} // namespace

// --- UiTooltip ------------------------------------------------------------------------------

// --- UiRegion -------------------------------------------------------------------------------

bool UiRegion::HitTest(int x, int y) const {
    x -= offsetX;
    y -= offsetY;
    if (!enabled) return false;
    if (!gameapi::PtInRect(&bounds, POINT{x, y})) return false;
    if (rectOnly) return true;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products wrap at 32
    // bits like the original's.
    int count = lib::Count(points);
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

bool UiRegion::HasActiveTip() const {
    return tip && tip->Shown();
}

// Rebirth's WideScreen replaces SetRect and AddPoint (addon's replacements in front of the original's body): x
// is moved by the panel offset its panel builders set (ui_widescreen.h; 0 outside them).
void UiRegion::SetRect(int left, int top, int right, int bottom) {
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

void UiRegion::AddPoint(int x, int y) {
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
    int index = lib::Count(points);
    points.resize(index + 1);
    points[index].x = x;
    points[index].y = y;
    if (std::ssize(points) > 2) enabled = true;
}

void UiRegion::OnMouseMove(int x, int y) {
    if (!tip) return;
    const bool inside = HitTest(x, y);
    if (inside) {
        if (!tip->Shown()) tip->state = UiTooltip::State::Hovered;
    } else if (tip->Shown()) {
        tip->state = UiTooltip::State::Disappearing;
    }
    tip->OnMouseMove(x, y);
}

// --- UiWindow -------------------------------------------------------------------------------

int UiWindow::RegionAt(int x, int y) const {
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    for (int i = 0; i < std::ssize(regions); ++i) {
        if (regions[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}

bool UiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    return gameapi::PtInRect(&rect, point) != FALSE;
}

void UiWindow::UpdateRegionTips() {
    POINT mouse{ui->mouseX, ui->mouseY};
    ui->ScalePointFromScreenEntry(&mouse);
    // Inlines UiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < std::ssize(regions); ++i) {
        UiRegion* region = regions[i];
        if (region->tip) region->OnMouseMove(mouse.x, mouse.y);
    }
}

// The same per region, each tooltip then redrawn (the tooltip is re-read).
void UiWindow::RedrawTips() {
    POINT mouse{ui->mouseX, ui->mouseY};
    ui->ScalePointFromScreenEntry(&mouse);
    for (int i = 0; i < std::ssize(regions); ++i) {
        UiRegion* region = regions[i];
        if (region->tip) region->OnMouseMove(mouse.x, mouse.y);
        if (UiTooltip* tip = regions[i]->tip) tip->Redraw();
    }
}

void UiWindow::DrawBase(SceneRenderer* pass) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i)
        SceneMesh(i)->Draw(pass);
    for (int i = 0; i < std::ssize(models); ++i) models[i]->Draw(pass);
}

void UiWindow::Redraw() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        UiSurface* surface = surfaces[i];
        if (surface->Shown()) surface->Blit();
    }
}

void UiWindow::SetEnabled(bool newEnabled, bool notify) {
    enabled = newEnabled;
    if (notify) Show(newEnabled);
}

void UiWindow::AddObject(CTexture* texture) {
    if (!texture) return;
    objects.push_back(texture);
    texture->AddRef();
}

void UiWindow::RemoveObject(CTexture* texture) {
    if (!texture) return;
    for (int i = 0; i < std::ssize(objects); ++i) {
        if (objects[i] == texture) {
            objects[i]->Release();
            objects.erase(objects.begin() + i);
            return;
        }
    }
}

void UiWindow::RemoveObjectAt(int index) {
    objects[index]->Release();
    objects.erase(objects.begin() + index);
}

bool UiWindow::RemoveSceneObject(EngineObject* object) {
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

void UiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects[index]->Release();
    sceneObjects.erase(sceneObjects.begin() + index);
}

void UiWindow::ClearSceneObjects() {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) sceneObjects[i]->Release();
    sceneObjects.clear();
}

void UiWindow::ClearObjects() {
    for (int i = 0; i < std::ssize(objects); ++i) objects[i]->Release();
    objects.clear();
}

// Returns `surface`, which the original leaves as its result; the titles' Create freezes the surface through
// it.
UiSurface* UiWindow::AddSurface(UiSurface* surface) {
    if (!surface) return nullptr;
    surfaces.push_back(surface);
    return surface;
}

void UiWindow::RemoveSurfaceAt(int index) {
    if (UiSurface* surface = surfaces[index]) DestroySurface(surface);
    surfaces.erase(surfaces.begin() + index);
}

void UiWindow::ClearSurfaces() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        if (UiSurface* surface = surfaces[i]) DestroySurface(surface);
    }
    surfaces.clear();
}

CTexture* UiWindow::LoadTexture(std::string_view name, TextureKind kind) {
    CTextureManager* manager = g_clientWorld.textureManager;
    CTexture* texture = manager->FindOrCreate(static_cast<int>(kind), name);
    if (!texture) return nullptr;
    int loaded = manager->LoadTexture(texture);
    if (!loaded) return nullptr;
    objects.push_back(texture);
    texture->AddRef();
    return texture;
}

CTexture* UiWindow::CreateTexture(int width, int height, TextureKind kind) {
    CTexture* texture = g_clientWorld.textureManager->NewTexture(width, height, kind);
    if (!texture) return nullptr;
    int index = lib::Count(objects);
    objects.resize(index + 1);
    objects[index] = texture;
    texture->flags |= TextureFlag::Keep;
    texture->AddRef();
    return texture;
}

void UiWindow::Show(bool show) {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        UiSurface* surface = surfaces[i];
        if (!show || surface->Shown()) surface->SwapContents(show);
    }
}

void UiSurface::Show() {
    shown = true;
    if (kept) SwapContents(true);
}

void UiSurface::Hide(bool release) {
    shown = false;
    if (release) SwapContents(false);
}

UiRegion* UiWindow::AddRegion() {
    auto* region = new UiRegion(ui);
    regions.push_back(region);
    return region;
}

void UiWindow::RemoveRegionAt(int index) {
    delete regions[index];
    regions.erase(regions.begin() + index);
}

void UiWindow::ClearRegions() {
    for (int i = 0; i < std::ssize(regions); ++i) {
        delete regions[i];
    }
    regions.clear();
}

UiModel* UiWindow::AddModel() {
    auto* model = new UiModel();
    models.push_back(model);
    return model;
}

// --- ReplaceSceneObject ----------------------------------------------------------------------------

EngineObject* UiWindow::ReplaceSceneObject(EngineObject* object, std::string_view name) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        if (sceneObjects[i] != object) continue;
        FigureMesh* made = NewModelObject(name);
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

UiMesh* UiWindow::AddSprite(std::string_view name) {
    FigureMesh* object = NewModelObject(name);
    if (!object) return nullptr;
    return AddSceneObject(object);
}

UiMesh* UiWindow::AddBlankSprite() { return AddSceneObject(g_clientWorld.figureManager->NewBlankFigure()); }

UiMesh* UiWindow::AddSceneObject(FigureMesh* object) {
    bool listed = false;
    for (int i = 0; i < std::ssize(sceneObjects) && !listed; ++i) listed = sceneObjects[i] == object;
    if (!listed) {
        lib::SetAtGrow(sceneObjects, lib::Count(sceneObjects), object);
        object->AddRef();
        object->SetObject(this);
    }
    auto* sprite = AsUiMesh(object);
    sprite->material = blackMaterial;
    sprite->texture = !objects.empty() ? objects[0] : nullptr;
    return sprite;
}

// --- The constructor ---------------------------------------------------------------------------------

// The original's closing Show(true) reaches UiWindow's own (the surfaces' list is empty): left out.
UiWindow::UiWindow(Interface3D* ui) : UiInputSide(ui) {
    localRotation = kIdentityRotation;
    worldRotation.SetIdentity();
    localPosition = Vector3{0.0f, 0.0f, 0.0f};
    whiteMaterial = NewGlowMaterial(1.0f);
    blackMaterial = NewGlowMaterial(0.0f);
}

const char* UiWindow::ClassName() { return kClassName; }
ClassId UiWindow::GetClassId() { return ClassId::UiWindow; }

// --- UiContainer::AddChild ------------------------------------------------------------------------

void UiContainer::AddChild(UiWindow* child) {
    if (!child) return;
    children.push_back(child);
}

// --- The destructor ---------------------------------------------------------------------------------

UiWindow::~UiWindow() {
    for (EngineObject** held : {&blackMaterial, &whiteMaterial, &heldObject}) {
        if (*held) (*held)->Release();
        *held = nullptr;
    }
    for (int i = 0; i < std::ssize(sceneObjects); ++i) sceneObjects[i]->Release();
    sceneObjects.clear();
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        if (UiSurface* surface = surfaces[i]) DestroySurface(surface);
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
