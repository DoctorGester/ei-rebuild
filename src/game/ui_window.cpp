// Interface windows, their regions and the aligned pointer arrays they keep. See ui.h.
#include "figure_manager.h"
#include "figure_mesh.h"
#include "game_singletons.h"
#include "material.h"
#include "texture.h"
#include "ui.h"
#include "screen_layout.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"

#include "lib/rebirth.h"

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
UiMesh* NewModelObject(std::string_view name) {
    return g_clientWorld.figureManager->NewFigure(name);
}

constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

constexpr const char* kClassName = "Sorry, no other way.";

// A new material of kind 0x16 (filled by the manager) glowing `value` in every channel, referenced.
Material* NewGlowMaterial(float value) {
    MaterialManager* materials = g_clientWorld.materialManager.get();
    Material* material = materials->NewMaterial(0x16);
    materials->FillMaterial(material, 0x16);
    material->colors.emissive = {value, value, value, material->colors.emissive.w};
    material->AddRef();
    return material;
}

} // namespace


// --- UiRegion -------------------------------------------------------------------------------

bool UiRegion::HitTest(Point point) const {
    point.x -= offsetX;
    point.y -= offsetY;
    if (!enabled) return false;
    if (!bounds.Contains(Point{point.x, point.y})) return false;
    if (rectOnly) return true;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products wrap at 32
    // bits like the original's.
    int count = lib::Count(points);
    for (int i = 0; i < count;) {
        const Point& a = points[i];
        ++i;
        const Point& b = points[i % count];
        int lhs = Mul(point.y, b.x) + Mul(b.y, a.x) + Mul(a.y, point.x);
        int rhs = Mul(a.y, b.x) + Mul(point.y, a.x) + Mul(b.y, point.x);
        if (lhs < rhs) return false;
    }
    return true;
}

bool UiRegion::HasActiveTip() const {
    return tip && tip->Shown();
}

// Rebirth's WideScreen replaces SetRect and AddPoint (addon's replacements in front of the original's body): x
// is moved by the panel offset its panel builders set (screen_layout.h; 0 outside them).
void UiRegion::SetRect(const Rect& area) {
    points.clear();
    bounds = area;
    bounds.Offset(screen_layout::PanelOffset(), 0);
    enabled = true;
    rectOnly = true;
}

void UiRegion::SetRect(int left, int top, int right, int bottom) { SetRect(Rect{left, top, right, bottom}); }

void UiRegion::AddPoint(int x, int y) {
    x += screen_layout::PanelOffset();
    if (points.empty()) {
        bounds = {x, y, x, y};
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

void UiRegion::OnMouseMove(Point point) {
    if (!tip) return;
    const bool inside = HitTest(point);
    if (inside) {
        if (!tip->Shown()) tip->state = UiTooltip::State::Hovered;
    } else if (tip->Shown()) {
        tip->state = UiTooltip::State::Disappearing;
    }
    tip->OnMouseMove(point);
}

// --- UiWindow -------------------------------------------------------------------------------

int UiWindow::RegionAt(Point point) const {
    ui->ScalePointFromScreen(&point);
    for (int i = 0; i < std::ssize(regions); ++i) {
        if (regions[i]->HitTest(point)) return i;
    }
    return -1;
}

UiRegion* UiWindow::RegionUnder(Point point) const {
    const int index = RegionAt(point);
    return index < 0 ? nullptr : regions[index];
}

bool UiWindow::ContainsPoint(Point point) const {
    ui->ScalePointFromScreen(&point);
    return rect.Contains(point);
}

UiRegion* UiWindow::HitRegion(Point point, int count) {
    const int index = RegionAt(point);
    if (index < 0 || index >= count) return nullptr;
    handled = true;
    return regions[index];
}

UiRegion* UiWindow::PressRegion(Point point, int count) {
    UiRegion* region = HitRegion(point, count);
    if (region) CaptureMouse();
    return region;
}

UiRegion* UiWindow::PressedRegion(Point point, int count) {
    if (!ReleaseMouse()) return nullptr;
    return HitRegion(point, count);
}

void UiWindow::DrawTips() {
    Point mouse = ui->mouse;
    ui->ScalePointFromScreen(&mouse);
    for (int i = 0; i < std::ssize(regions); ++i) {
        UiRegion* region = regions[i];
        if (region->tip) region->OnMouseMove(mouse);
        if (UiTooltip* tip = regions[i]->tip) tip->DrawSurfaces();
    }
}

void UiWindow::DrawBase(SceneRenderer* pass) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i)
        sceneObjects[i]->Draw(pass);
    for (int i = 0; i < std::ssize(models); ++i) models[i]->Draw(pass);
}

void UiWindow::DrawSurfaces() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        UiSurface* surface = surfaces[i];
        if (surface->Shown()) surface->Blit();
    }
}

void UiWindow::SetEnabled(bool newEnabled) { enabled = newEnabled; }

void UiWindow::AddObject(Texture* texture) {
    if (!texture) return;
    objects.push_back(texture);
    texture->AddRef();
}

void UiWindow::RemoveObject(Texture* texture) {
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

void UiWindow::ClearSurfaces() {
    for (int i = 0; i < std::ssize(surfaces); ++i) {
        if (UiSurface* surface = surfaces[i]) DestroySurface(surface);
    }
    surfaces.clear();
}

Texture* UiWindow::LoadTexture(std::string_view name, TextureKind kind) {
    TextureManager* manager = g_clientWorld.textureManager.get();
    Texture* texture = manager->FindOrCreate(static_cast<int>(kind), name);
    if (!texture) return nullptr;
    int loaded = manager->LoadTexture(texture);
    if (!loaded) return nullptr;
    objects.push_back(texture);
    texture->AddRef();
    return texture;
}

Texture* UiWindow::CreateTexture(int width, int height, TextureKind kind) {
    Texture* texture = g_clientWorld.textureManager->NewTexture(width, height, kind);
    if (!texture) return nullptr;
    int index = lib::Count(objects);
    objects.resize(index + 1);
    objects[index] = texture;
    texture->flags |= TextureFlag::Keep;
    texture->AddRef();
    return texture;
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

UiMesh* UiWindow::ReplaceSceneObject(UiMesh* object, std::string_view name) {
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        if (sceneObjects[i] != object) continue;
        UiMesh* made = NewModelObject(name);
        if (!made) return nullptr;
        made->AddRef();
        made->SetObject(this);
        UiMesh* old = sceneObjects[i];
        made->material = blackMaterial;
        made->texture = !objects.empty() ? objects[0] : nullptr;
        made->localPosition = old->localPosition;
        made->textureOffsetU = old->textureOffsetU;
        made->textureOffsetV = old->textureOffsetV;
        made->localRotation = old->localRotation;
        made->texture = old->texture;
        float scale = old->scaleMatrix.m[0];
        made->SetScale(scale);
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
    UiMesh* object = NewModelObject(name);
    if (!object) return nullptr;
    return AddSceneObject(object);
}

UiMesh* UiWindow::AddBlankSprite() { return AddSceneObject(g_clientWorld.figureManager->NewBlankFigure()); }

UiMesh* UiWindow::AddSceneObject(UiMesh* object) {
    bool listed = false;
    for (int i = 0; i < std::ssize(sceneObjects) && !listed; ++i) listed = sceneObjects[i] == object;
    if (!listed) {
        sceneObjects.push_back(object);
        object->AddRef();
        object->SetObject(this);
    }
    object->material = blackMaterial;
    object->texture = !objects.empty() ? objects[0] : nullptr;
    return object;
}

// --- The constructor ---------------------------------------------------------------------------------

UiWindow::UiWindow(Interface3D* ui) : UiInputSide(ui) {
    localRotation = kIdentityRotation;
    worldRotation.SetIdentity();
    localPosition = Vector3{0.0f, 0.0f, 0.0f};
    whiteMaterial = NewGlowMaterial(1.0f);
    blackMaterial = NewGlowMaterial(0.0f);
}

const char* UiWindow::ClassName() { return kClassName; }
ClassId UiWindow::GetClassId() { return ClassId::UiWindow; }


// --- The destructor ---------------------------------------------------------------------------------

UiWindow::~UiWindow() {
    for (Material** held : {&blackMaterial, &whiteMaterial}) {
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
