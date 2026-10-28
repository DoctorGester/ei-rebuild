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
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>
#include <initializer_list>
#include <bit>

namespace {
// 32-bit wrapping multiply.
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
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
    int count = points_.size;
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
    points_.RemoveAll();
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
    if (points_.size == 0) {
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
    // The vertex array is a lib::AlignedArray of 8-byte points.
    int index = points_.size;
    points_.SetSize(index + 1);
    points_[index].x = x;
    points_[index].y = y;
    if (points_.size > 2) enabled_ = true;
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
    for (int i = 0; i < regions_.size; ++i) {
        if (regions_.data[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}

BOOL CUiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    return gameapi::PtInRect(&rect_, point);
}

void CUiWindow::UpdateRegionTips(CSceneRenderer*) {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->ScalePointFromScreenEntry(&mouse);
    // Inlines CUiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < regions_.size; ++i) {
        CUiRegion* region = regions_.data[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
    }
}

// The same per region, each tooltip then redrawn (the tooltip is re-read).
void CUiWindow::RedrawRegionTips() {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->ScalePointFromScreenEntry(&mouse);
    for (int i = 0; i < regions_.size; ++i) {
        CUiRegion* region = regions_.data[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
        if (CUiTooltip* tip = regions_.data[i]->tip_) tip->Redraw();
    }
}

BOOL CUiWindow::DrawBase(CSceneRenderer* pass) {
    for (int i = 0; i < sceneObjects_.size; ++i)
        SceneMesh(i)->Draw(pass);
    for (int i = 0; i < models_.size; ++i) models_.data[i]->Draw(pass);
    return 1;
}

void CUiWindow::RedrawBase() {
    for (int i = 0; i < surfaces_.size; ++i) {
        CUiSurface* surface = surfaces_.data[i];
        if (surface->Shown()) surface->Blit();
    }
}

void CUiWindow::SetEnabled(int enabled, int notify) {
    enabled_ = enabled;
    if (notify) Show(enabled);
}

void CUiWindow::AddObject(CTexture* texture) {
    if (!texture) return;
    objects_.Add(texture);
    ++texture->refCount_;
}

void CUiWindow::RemoveObject(CTexture* texture) {
    if (!texture) return;
    for (int i = 0; i < objects_.size; ++i) {
        if (objects_.data[i] == texture) {
            objects_.data[i]->Release();
            objects_.RemoveAt(i);
            return;
        }
    }
}

void CUiWindow::RemoveObjectAt(int index) {
    objects_.data[index]->Release();
    objects_.RemoveAt(index);
}

BOOL CUiWindow::RemoveSceneObject(CEngineObject* object) {
    for (int i = 0; i < sceneObjects_.size; ++i) {
        if (sceneObjects_.data[i] == object) {
            // Unlike RemoveObject, the slot is removed before the release.
            sceneObjects_.RemoveAt(i);
            object->Release();
            return 1;
        }
    }
    return 0;
}

void CUiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects_.data[index]->Release();
    sceneObjects_.RemoveAt(index);
}

void CUiWindow::ClearSceneObjects() {
    for (int i = 0; i < sceneObjects_.size; ++i) sceneObjects_.data[i]->Release();
    sceneObjects_.RemoveAll();
}

void CUiWindow::ClearObjects() {
    for (int i = 0; i < objects_.size; ++i) objects_.data[i]->Release();
    objects_.RemoveAll();
}

// Returns `surface`, which the original leaves as its result; the titles' Create freezes the surface through
// it.
void (*g_uiSurfaceOwnerObserver)(const CUiWindow* window, const CUiSurface* surface, bool added) = nullptr;

CUiSurface* CUiWindow::AddSurface(CUiSurface* surface) {
    if (!surface) return nullptr;
    surfaces_.Add(surface);
    if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, true);
    return surface;
}

void CUiWindow::RemoveSurfaceAt(int index) {
    if (CUiSurface* surface = surfaces_.data[index]) {
        if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
        delete surface;
    }
    surfaces_.RemoveAt(index);
}

void CUiWindow::ClearSurfaces() {
    for (int i = 0; i < surfaces_.size; ++i) {
        if (CUiSurface* surface = surfaces_.data[i]) {
            if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
            delete surface;
        }
    }
    surfaces_.RemoveAll();
}

CTexture* CUiWindow::LoadTexture(const char* name, int kind, const char* directory) {
    CTextureManager* manager = TheTextureManager();
    CTexture* texture = manager->FindOrCreate(kind, name);
    if (!texture) return nullptr;
    int loaded = directory ? manager->LoadTextureFrom(texture, directory) : manager->LoadTexture(texture);
    if (!loaded) return nullptr;
    objects_.Add(texture);
    ++texture->refCount_;
    return texture;
}

CTexture* CUiWindow::CreateTexture(int width, int height, int kind) {
    CTexture* texture = TheTextureManager()->NewTexture(width, height, kind);
    if (!texture) return nullptr;
    int index = objects_.size;
    objects_.SetSize(index + 1, -1);
    objects_.data[index] = texture;
    texture->flags_ |= 0x40000;
    ++texture->refCount_;
    return texture;
}

void CUiWindow::ShowSurfaces(int show) {
    for (int i = 0; i < surfaces_.size; ++i) {
        CUiSurface* surface = surfaces_.data[i];
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
    auto* region = gamecrt::operator_new_as<CUiRegion>();
    if (region) {
        // The bounds are left uninitialised, as in the original.
        region->interface_ = interface_;
        region->points_ = {};
        region->tip_ = nullptr;
        region->enabled_ = false;
        region->rectOnly_ = false;
        region->offsetX_ = 0;
        region->offsetY_ = 0;
    }
    regions_.Add(region);
    return region;
}

void CUiWindow::RemoveRegionAt(int index) {
    if (CUiRegion* region = regions_.data[index]) {
        region->ReleaseContents();
        gamecrt::operator_delete(region);
    }
    regions_.RemoveAt(index);
}

void CUiWindow::ClearRegions() {
    for (int i = 0; i < regions_.size; ++i) {
        if (CUiRegion* region = regions_.data[i]) {
            region->ReleaseContents();
            gamecrt::operator_delete(region);
        }
    }
    regions_.RemoveAll();
}

CUiModel* CUiWindow::AddModel() {
    auto* model = gamecrt::operator_new_as<CUiModel>();
    if (!model) return nullptr;
    model->object_ = nullptr;   // the constructor
    models_.Add(model);
    return model;
}

// --- ReplaceSceneObject ----------------------------------------------------------------------------

namespace {
// A new object of the model manager by name, or null.
CFigureMesh* NewModelObject(const char* name) {
    return TheFigureManager()->NewFigure(name);
}

} // namespace

CEngineObject* CUiWindow::ReplaceSceneObject(CEngineObject* object, const char* name) {
    for (int i = 0; i < sceneObjects_.size; ++i) {
        if (sceneObjects_.data[i] != object) continue;
        CFigureMesh* made = NewModelObject(name);
        if (!made) return nullptr;
        ++made->refCount_;
        made->SetObject(this);
        auto* sprite = AsUiMesh(made);
        auto* old = AsUiMesh(object);
        sprite->material_ = blackMaterial_;
        sprite->texture_ = objects_.size > 0 ? objects_.data[0] : nullptr;
        std::memcpy(&sprite->localPosition_, &old->localPosition_, 12);
        std::memcpy(&sprite->textureOffsetU_, &old->textureOffsetU_, sizeof(float));
        std::memcpy(&sprite->textureOffsetV_, &old->textureOffsetV_, sizeof(float));
        std::memcpy(&sprite->localRotation_, &old->localRotation_, 16);
        sprite->texture_ = old->texture_;
        float scale = old->GetScaleX();
        AsUiMesh(made)->SetScale(scale);
        sceneObjects_.RemoveAt(i);
        object->Release();
        // Back in at the same place: the original grows the array as SetSize does (inlined when
        // the place is the end) and shifts the rest up with memmove.
        int size = sceneObjects_.size;
        if (i < size) {
            sceneObjects_.SetSize(size + 1, -1);
            std::memmove(&sceneObjects_.data[i + 1], &sceneObjects_.data[i], static_cast<std::size_t>(size - i) * 4);
        } else {
            sceneObjects_.SetSize(i + 1, -1);
        }
        sceneObjects_.data[i] = made;
        return made;
    }
    return nullptr;
}

// --- AddSprite -------------------------------------------------------------------------------------

CUiMesh* CUiWindow::AddSprite(const char* name) {
    CFigureMesh* object = NewModelObject(name);
    if (!object) return nullptr;
    bool listed = false;
    for (int i = 0; i < sceneObjects_.size && !listed; ++i) listed = sceneObjects_.data[i] == object;
    if (!listed) {
        sceneObjects_.SetAtGrow(sceneObjects_.size, object);
        ++object->refCount_;
        object->SetObject(this);
    }
    auto* sprite = AsUiMesh(object);
    sprite->material_ = blackMaterial_;
    sprite->texture_ = objects_.size > 0 ? objects_.data[0] : nullptr;
    return sprite;
}

// --- The constructor ---------------------------------------------------------------------------------

namespace {
constexpr Quaternion kIdentityRotation = /*007C7E58*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

namespace {

void PrepareMaterial(CMaterial* material, float value) {
    material->colors.emissive.x = value;
    material->colors.emissive.y = value;
    material->colors.emissive.z = value;
    ++material->refCount_;
}

// The constructor's empty pointer array (data, growth step, capacity, size in its order).
template <typename T>
void ClearArray(UiPtrArray<T>& array) {
    array.data = nullptr;
    array.growBy = 0;
    array.capacity = 0;
    array.size = 0;
}
} // namespace

CUiWindow::CUiWindow(CInterface3D* ui) : UiInputSide(ui) {
    ClearFields();
    localRotation_.SetIdentity();   // two identity rotations
    worldRotation_.SetIdentity();
    std::memset(spareA0_, 0, sizeof(spareA0_));
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
    CMaterialManager* materials = TheMaterialManager();
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
const char* CUiWindow::GetClassName() { return UiObjectClassName(this); }
int CUiWindow::GetClassId() { return UiObjectClassId(this); }

// --- CUiContainer::AddChild ------------------------------------------------------------------------

void CUiContainer::AddChild(CUiWindow* child) {
    if (!child) return;
    children_.Add(child);
}

// --- The destructor ---------------------------------------------------------------------------------

CUiWindow::~CUiWindow() {
    for (CEngineObject** held : {&blackMaterial_, &whiteMaterial_, &heldObject_}) {
        if (*held) (*held)->Release();
        *held = nullptr;
    }
    for (int i = 0; i < sceneObjects_.size; ++i) sceneObjects_.data[i]->Release();
    sceneObjects_.SetSize(0, -1);
    for (int i = 0; i < surfaces_.size; ++i) {
        if (CUiSurface* surface = surfaces_.data[i]) {
            if (g_uiSurfaceOwnerObserver) g_uiSurfaceOwnerObserver(this, surface, false);
            delete surface;
        }
    }
    surfaces_.SetSize(0, -1);
    for (int i = 0; i < regions_.size; ++i) {
        if (CUiRegion* region = regions_.data[i]) {
            region->ReleaseContents();
            gamecrt::operator_delete(region);
        }
    }
    regions_.SetSize(0, -1);
    for (int i = 0; i < objects_.size; ++i) objects_.data[i]->Release();
    objects_.SetSize(0, -1);
    for (int i = 0; i < models_.size; ++i) {
        if (CUiModel* model = models_.data[i]) {
            model->ReleaseObject();
            gamecrt::operator_delete(model);
        }
    }
    models_.SetSize(0, -1);
    flags_ = 0;
    // The arrays' destructors (their storage is already freed above, so these find nothing).
    if (models_.data) lib::FreeAligned32(models_.data);
    if (regions_.data) lib::FreeAligned32(regions_.data);
    if (surfaces_.data) lib::FreeAligned32(surfaces_.data);
    if (sceneObjects_.data) lib::FreeAligned32(sceneObjects_.data);
    if (objects_.data) lib::FreeAligned32(objects_.data);
}
