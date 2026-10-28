// Interface windows, their regions and the aligned pointer arrays they keep. See ui.h.
#include "figure_manager.h"
#include "figure_mesh.h"
#include "texture.h"
#include "ui.h"
#include "world_class_slots.h"

#include "material.h"

#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "ui_widescreen.h"
#include "ei/x87.h"
#include "game_singletons.h"

#include <cstring>
#include <memory>
#include <initializer_list>
#include <bit>

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
    return g_clientWorld->figureManager_->NewFigure(name);
}

constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser

void PrepareMaterial(CMaterial* material, float value) {
    material->colors.emissive.x = value;
    material->colors.emissive.y = value;
    material->colors.emissive.z = value;
    ++material->refCount_;
}

// The constructor's empty pointer array (data, growth step, capacity, size in its order).
template <typename T>
void ClearArray(std::vector<T*>& array) {
    std::construct_at(&array);
}

} // namespace

// --- CUiTooltip ------------------------------------------------------------------------------

// --- CUiRegion -------------------------------------------------------------------------------

BOOL CUiRegion::HitTest(int x, int y) const {
    x -= offsetX_;
    y -= offsetY_;
    if (!enabled_) return 0;
    if (!gameapi::PtInRect(&bounds_, POINT{x, y})) return 0;
    if (rectOnly_) return 1;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products wrap at 32
    // bits like the original's.
    int count = std::ssize(points_);
    for (int i = 0; i < count;) {
        const UiPoint& a = points_[i];
        ++i;
        const UiPoint& b = points_[i % count];
        int lhs = Mul(y, b.x) + Mul(b.y, a.x) + Mul(a.y, x);
        int rhs = Mul(a.y, b.x) + Mul(y, a.x) + Mul(b.y, x);
        if (lhs < rhs) return 0;
    }
    return 1;
}

BOOL CUiRegion::HasActiveTip() const {
    if (!tip_) return 0;
    int state = tip_->state_;
    return state != 0 && state != 1;
}

// Rebirth's WideScreen replaces SetRect and AddPoint (addon's replacements in front of the original's body): x
// is moved by the panel offset its panel builders set (ui_widescreen.h; 0 outside them).
void CUiRegion::SetRect(int left, int top, int right, int bottom) {
    points_.clear();
    if (widescreen::Active()) {
        left += widescreen::PanelOffset();
        right += widescreen::PanelOffset();
    }
    bounds_.left = left;
    bounds_.right = right;
    bounds_.top = top;
    bounds_.bottom = bottom;
    enabled_ = true;
    rectOnly_ = true;
}

void CUiRegion::AddPoint(int x, int y) {
    if (widescreen::Active()) x += widescreen::PanelOffset();
    if (points_.empty()) {
        bounds_.left = x;
        bounds_.top = y;
        bounds_.right = x;
        bounds_.bottom = y;
    } else {
        if (x < bounds_.left) bounds_.left = x;
        if (y < bounds_.top) bounds_.top = y;
        if (x > bounds_.right) bounds_.right = x;
        if (y > bounds_.bottom) bounds_.bottom = y;
    }
    int index = std::ssize(points_);
    points_.resize(index + 1);
    points_[index].x = x;
    points_[index].y = y;
    if (std::ssize(points_) > 2) enabled_ = true;
}

void CUiRegion::OnMouseMove(int x, int y) {
    if (!tip_) return;
    BOOL inside = HitTest(x, y);
    int state = tip_->state_;
    if (inside) {
        if (state == 0 || state == 1) tip_->state_ = 1;
    } else if (state != 0 && state != 1) {
        tip_->state_ = 3;
    }
    tip_->OnMouseMove(x, y);
}

// --- CUiWindow -------------------------------------------------------------------------------

int CUiWindow::RegionAt(int x, int y) const {
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    for (int i = 0; i < std::ssize(regions_); ++i) {
        if (regions_[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}

BOOL CUiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    return gameapi::PtInRect(&rect_, point);
}

void CUiWindow::UpdateRegionTips() {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->ScalePointFromScreenEntry(&mouse);
    // Inlines CUiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < std::ssize(regions_); ++i) {
        CUiRegion* region = regions_[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
    }
}

// The same per region, each tooltip then redrawn (the tooltip is re-read).
void CUiWindow::RedrawRegionTips() {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->ScalePointFromScreenEntry(&mouse);
    for (int i = 0; i < std::ssize(regions_); ++i) {
        CUiRegion* region = regions_[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
        if (CUiTooltip* tip = regions_[i]->tip_) tip->Redraw();
    }
}

BOOL CUiWindow::DrawBase(CSceneRenderer* pass) {
    for (int i = 0; i < std::ssize(sceneObjects_); ++i)
        SceneMesh(i)->Draw(pass);
    for (int i = 0; i < std::ssize(models_); ++i) models_[i]->Draw(pass);
    return 1;
}

void CUiWindow::RedrawBase() {
    for (int i = 0; i < std::ssize(surfaces_); ++i) {
        CUiSurface* surface = surfaces_[i];
        if (surface->Shown()) surface->Blit();
    }
}

void CUiWindow::SetEnabled(int enabled, int notify) {
    enabled_ = enabled;
    if (notify) Show(enabled);
}

void CUiWindow::AddObject(CTexture* texture) {
    if (!texture) return;
    objects_.push_back(texture);
    ++texture->refCount_;
}

void CUiWindow::RemoveObject(CTexture* texture) {
    if (!texture) return;
    for (int i = 0; i < std::ssize(objects_); ++i) {
        if (objects_[i] == texture) {
            objects_[i]->Release();
            objects_.erase(objects_.begin() + i);
            return;
        }
    }
}

void CUiWindow::RemoveObjectAt(int index) {
    objects_[index]->Release();
    objects_.erase(objects_.begin() + index);
}

BOOL CUiWindow::RemoveSceneObject(CEngineObject* object) {
    for (int i = 0; i < std::ssize(sceneObjects_); ++i) {
        if (sceneObjects_[i] == object) {
            // Unlike RemoveObject, the slot is removed before the release.
            sceneObjects_.erase(sceneObjects_.begin() + i);
            object->Release();
            return 1;
        }
    }
    return 0;
}

void CUiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects_[index]->Release();
    sceneObjects_.erase(sceneObjects_.begin() + index);
}

void CUiWindow::ClearSceneObjects() {
    for (int i = 0; i < std::ssize(sceneObjects_); ++i) sceneObjects_[i]->Release();
    sceneObjects_.clear();
}

void CUiWindow::ClearObjects() {
    for (int i = 0; i < std::ssize(objects_); ++i) objects_[i]->Release();
    objects_.clear();
}

// Returns `surface`, which the original leaves as its result; the titles' Create freezes the surface through
// it.
void (*g_uiSurfaceOwnerObserver)(const CUiWindow* window, const CUiSurface* surface, bool added) = nullptr;

CUiSurface* CUiWindow::AddSurface(CUiSurface* surface) {
    if (!surface) return nullptr;
    surfaces_.push_back(surface);
    if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, true);
    return surface;
}

void CUiWindow::RemoveSurfaceAt(int index) {
    if (CUiSurface* surface = surfaces_[index]) {
        if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
        DestroySurface(surface);
    }
    surfaces_.erase(surfaces_.begin() + index);
}

void CUiWindow::ClearSurfaces() {
    for (int i = 0; i < std::ssize(surfaces_); ++i) {
        if (CUiSurface* surface = surfaces_[i]) {
            if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
            DestroySurface(surface);
        }
    }
    surfaces_.clear();
}

CTexture* CUiWindow::LoadTexture(std::string_view name, int kind) {
    CTextureManager* manager = g_clientWorld->textureManager_;
    CTexture* texture = manager->FindOrCreate(kind, name);
    if (!texture) return nullptr;
    int loaded = manager->LoadTexture(texture);
    if (!loaded) return nullptr;
    objects_.push_back(texture);
    ++texture->refCount_;
    return texture;
}

CTexture* CUiWindow::CreateTexture(int width, int height, int kind) {
    CTexture* texture = g_clientWorld->textureManager_->NewTexture(width, height, kind);
    if (!texture) return nullptr;
    int index = std::ssize(objects_);
    objects_.resize(index + 1);
    objects_[index] = texture;
    texture->flags_ |= 0x40000;
    ++texture->refCount_;
    return texture;
}

void CUiWindow::ShowSurfaces(int show) {
    for (int i = 0; i < std::ssize(surfaces_); ++i) {
        CUiSurface* surface = surfaces_[i];
        if (show == 0 || surface->Shown()) surface->CallSwapContents(show);
    }
}

void CUiSurface::CallSwapContents(int restore) { SwapContents(restore); }

void CUiSurface::Freeze(int frozen, int restore) {
    if (frozen) {
        flags_ |= kShown;
        if (kept_) CallSwapContents(1);
        return;
    }
    flags_ &= ~kShown;
    if (restore) CallSwapContents(0);
}

CUiRegion* CUiWindow::AddRegion() {
    auto* region = new CUiRegion(interface_);
    regions_.push_back(region);
    return region;
}

void CUiWindow::RemoveRegionAt(int index) {
    delete regions_[index];
    regions_.erase(regions_.begin() + index);
}

void CUiWindow::ClearRegions() {
    for (int i = 0; i < std::ssize(regions_); ++i) {
        delete regions_[i];
    }
    regions_.clear();
}

CUiModel* CUiWindow::AddModel() {
    auto* model = new CUiModel();
    models_.push_back(model);
    return model;
}

// --- ReplaceSceneObject ----------------------------------------------------------------------------

CEngineObject* CUiWindow::ReplaceSceneObject(CEngineObject* object, std::string_view name) {
    for (int i = 0; i < std::ssize(sceneObjects_); ++i) {
        if (sceneObjects_[i] != object) continue;
        CFigureMesh* made = NewModelObject(name);
        if (!made) return nullptr;
        ++made->refCount_;
        made->SetObject(this);
        auto* sprite = AsUiMesh(made);
        auto* old = AsUiMesh(object);
        sprite->material_ = blackMaterial_;
        sprite->texture_ = !objects_.empty() ? objects_[0] : nullptr;
        sprite->localPosition_ = old->localPosition_;
        sprite->textureOffsetU_ = old->textureOffsetU_;
        sprite->textureOffsetV_ = old->textureOffsetV_;
        sprite->localRotation_ = old->localRotation_;
        sprite->texture_ = old->texture_;
        float scale = old->scaleMatrix_.m[0];
        AsUiMesh(made)->SetScale(scale);
        sceneObjects_.erase(sceneObjects_.begin() + i);
        object->Release();
        // Back in at the same place.
        sceneObjects_.insert(sceneObjects_.begin() + i, made);
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

CUiMesh* CUiWindow::AddBlankSprite() { return AddSceneObject(g_clientWorld->figureManager_->NewBlankFigure()); }

CUiMesh* CUiWindow::AddSceneObject(CFigureMesh* object) {
    bool listed = false;
    for (int i = 0; i < std::ssize(sceneObjects_) && !listed; ++i) listed = sceneObjects_[i] == object;
    if (!listed) {
        lib::SetAtGrow(sceneObjects_, std::ssize(sceneObjects_), object);
        ++object->refCount_;
        object->SetObject(this);
    }
    auto* sprite = AsUiMesh(object);
    sprite->material_ = blackMaterial_;
    sprite->texture_ = !objects_.empty() ? objects_[0] : nullptr;
    return sprite;
}

// --- The constructor ---------------------------------------------------------------------------------

CUiWindow::CUiWindow(CInterface3D* ui) : UiInputSide(ui) {
    ClearFields();
    localRotation_.SetIdentity();   // two identity rotations
    worldRotation_.SetIdentity();
    ClearArray(objects_);
    ClearArray(sceneObjects_);
    ClearArray(surfaces_);
    ClearArray(regions_);
    ClearArray(models_);
    flags_ = 0;
    rect_ = RECT{0, 0, 0, 0};
    localRotation_ = kIdentityRotation;
    localPosition_.x = 0.0f;
    heldObject_ = nullptr;
    localPosition_.y = 0.0f;
    localPosition_.z = 0.0f;
    CMaterialManager* materials = g_clientWorld->materialManager_;
    CMaterial* white = materials->NewMaterial(0x16);
    whiteMaterial_ = white;
    materials->FillMaterial(white, 0x16);
    PrepareMaterial(white, 1.0f);
    CMaterial* black = materials->NewMaterial(0x16);
    blackMaterial_ = black;
    materials->FillMaterial(black, 0x16);
    PrepareMaterial(black, 0.0f);
    enabled_ = 1;
    Show(1);
}

int CUiWindow::Create() { return UiObjectCreate(this); }
const char* CUiWindow::ClassName() { return UiObjectClassName(this); }
ClassId CUiWindow::GetClassId() { return kClassUiWindow; }

// --- CUiContainer::AddChild ------------------------------------------------------------------------

void CUiContainer::AddChild(CUiWindow* child) {
    if (!child) return;
    children_.push_back(child);
}

// --- The destructor ---------------------------------------------------------------------------------

CUiWindow::~CUiWindow() {
    for (CEngineObject** held : {&blackMaterial_, &whiteMaterial_, &heldObject_}) {
        if (*held) (*held)->Release();
        *held = nullptr;
    }
    for (int i = 0; i < std::ssize(sceneObjects_); ++i) sceneObjects_[i]->Release();
    sceneObjects_.clear();
    for (int i = 0; i < std::ssize(surfaces_); ++i) {
        if (CUiSurface* surface = surfaces_[i]) {
            if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
            DestroySurface(surface);
        }
    }
    surfaces_.clear();
    for (int i = 0; i < std::ssize(regions_); ++i) {
        delete regions_[i];
    }
    regions_.clear();
    for (int i = 0; i < std::ssize(objects_); ++i) objects_[i]->Release();
    objects_.clear();
    for (int i = 0; i < std::ssize(models_); ++i) {
        delete models_[i];
    }
    models_.clear();
    flags_ = 0;
    // The arrays' destructors (their storage is already freed above, so these find nothing).
}
