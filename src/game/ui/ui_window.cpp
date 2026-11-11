// Interface windows, their regions and the aligned pointer arrays they keep. A region's tooltip (UiRegion, ui.h) is
// at the end: made from a text id ("tip <id>" in the texts) or a string, and its text changed. Every screen and HUD
// panel builder gives its regions tooltips. See ui.h and docs/notes/ui.md ("Interface manager and window base").
#include "engine/models/figure_manager.h"
#include "engine/models/figure_mesh.h"
#include "game/app/singletons.h"
#include "engine/data/text_manager.h"
#include "engine/models/material.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/screen_layout.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"

#include <bit>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>

namespace {
// 32-bit wrapping multiply.
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// A new object of the model manager by name, or null.
UiMesh* NewModelObject(std::string_view name) {
    return NewFigureSprite(g_clientWorld.figureManager.get(), name);
}

constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

constexpr const char* kClassName = "Sorry, no other way.";

constexpr const char* kTipText = "tip %d";

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

void UiRegion::SetRect(const Rect& area) {
    points.clear();
    bounds = area;
    enabled = true;
    rectOnly = true;
}

void UiRegion::SetRect(int left, int top, int right, int bottom) { SetRect(Rect{left, top, right, bottom}); }

void UiRegion::AddPoint(int x, int y) {
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

UiRegion* UiWindow::RegionUnder(Point point) const {
    point = screen_layout::FromScreen(point);
    for (const auto& region : regions) {
        if (region->HitTest(point)) return region.get();
    }
    return nullptr;
}

bool UiWindow::ContainsPoint(Point point) const {
    point = screen_layout::FromScreen(point);
    return rect.Contains(point);
}

UiRegion* UiWindow::HitRegion(UiEvent& event, int count) {
    Point point = screen_layout::FromScreen(event.at);
    for (int i = 0; i < std::ssize(regions) && i < count; ++i) {
        if (regions[i]->HitTest(point)) {
            event.handled = true;
            return regions[i].get();
        }
    }
    return nullptr;
}

UiRegion* UiWindow::PressRegion(UiEvent& event, int count) {
    UiRegion* region = HitRegion(event, count);
    if (region) CaptureMouse();
    return region;
}

UiRegion* UiWindow::PressedRegion(UiEvent& event, int count) {
    if (!ReleaseMouse()) return nullptr;
    return HitRegion(event, count);
}

void UiWindow::DrawTips() {
    Point mouse = screen_layout::FromScreen(g_interface.mouse);
    for (const auto& region : regions) {
        if (!region->tip) continue;
        region->OnMouseMove(mouse);
        region->tip->Draw();
    }
}

void UiWindow::DrawBase(SceneRenderer* pass) {
    for (UiMesh* sprite : sceneObjects)
        sprite->Draw(pass);
    for (const auto& model : models) model->Draw(pass);
}

void UiWindow::DrawSurfaces() {
    for (const auto& surface : surfaces) {
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

void UiWindow::ClearSceneObjects() {
    for (UiMesh* sprite : sceneObjects) sprite->Release();
    sceneObjects.clear();
}

void UiWindow::ClearObjects() {
    for (auto* object : objects) object->Release();
    objects.clear();
}

void UiWindow::ClearSurfaces() { surfaces.clear(); }

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

UiRegion* UiWindow::AddRegion() { return regions.emplace_back(std::make_unique<UiRegion>()).get(); }

void UiWindow::RemoveRegion(UiRegion* region) {
    const int index = RegionIndex(regions, region);
    if (index >= 0) regions.erase(regions.begin() + index);
}

void UiWindow::ClearRegions() { regions.clear(); }

UiModel* UiWindow::AddModel() { return models.emplace_back(std::make_unique<UiModel>()).get(); }

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

UiMesh* UiWindow::AddBlankSprite() { return AddSceneObject(NewBlankSprite(g_clientWorld.figureManager.get())); }

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

UiWindow::UiWindow() {
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
    ReleaseAndClear(sceneObjects);
    ReleaseAndClear(objects);
    if (g_interface.mouseCapture == this) g_interface.mouseCapture = nullptr;
}

// --- A new tooltip --------------------------------------------------------

void UiRegion::SetTip(int textId, int command) {
    if (textId == 0) {
        tip = std::make_unique<UiTooltip>(this, "", 0);
        return;
    }
    std::string text = str::Format(kTipText, textId);
    TextManager* texts = g_clientWorld.texts.get();
    if (texts->Open(text)) text = texts->ReadToHash();
    tip = std::make_unique<UiTooltip>(this, text, command);
}

void UiRegion::SetTipString(std::string_view text, int command) { tip = std::make_unique<UiTooltip>(this, text, command); }

// --- The tooltip's text --------------------------------------------------------------

void UiRegion::SetTipText(std::string_view text) {
    if (tip) tip->SetText(text);
}
