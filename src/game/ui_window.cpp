// Interface windows, their regions and the aligned pointer arrays they keep. See ui.h.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_core.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_render.h"

#include <cstring>
#include <initializer_list>
#include <bit>

namespace {
// 32-bit wrapping multiply (IMUL r32, r/m32).
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// Virtual slot 19 of a window (called with the new enabled state).
void CallSlot19(CUiWindow* window, int value) {
    using Fn = void(__thiscall*)(CUiWindow*, int);
    reinterpret_cast<Fn>(window->vtable_[19])(window, value);
}

// The deleting destructor (slot 0) of a region's tooltip.
void DeleteTooltip(CUiTooltip* tip) {
    using Fn = void*(__thiscall*)(CUiTooltip*, int);
    reinterpret_cast<Fn>(tip->vtable_[0])(tip, 1);
}
} // namespace

// --- Aligned storage -------------------------------------------------------------------------

void* AllocAligned(std::size_t bytes) {
    auto* block = static_cast<char*>(gamecrt::operator_new(bytes + 0x24));
    auto* data = reinterpret_cast<char*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(data)[-1] = block;
    return data;
}

void FreeAligned(void* data) { gamecrt::operator_delete(static_cast<void**>(data)[-1]); }

// The original has a separate (identical) copy of SetSize for each of the window's arrays; the two
// CEngineObject lists share one instantiation here.
EI_REPLACE_METHOD(0x006003D0, UiPtrArray<CEngineObject>::SetSize);
EI_REPLACE_METHOD(0x006004E0, UiPtrArray<CEngineObject>::SetSize);
EI_REPLACE_METHOD(0x00600730, UiPtrArray<CUiSurface>::SetSize);
EI_REPLACE_METHOD(0x00600840, UiPtrArray<CUiRegion>::SetSize);
EI_REPLACE_METHOD(0x00600950, UiPtrArray<CUiModel>::SetSize);
EI_REPLACE_METHOD(0x006005F0, UiPtrArray<CEngineObject>::SetAtGrow);
EI_REPLACE_METHOD(0x00601D00, UiPtrArray<CUiWindow>::SetSize);   // the containers' children

// --- CUiTooltip ------------------------------------------------------------------------------

__declspec(naked) void CUiTooltip::OnMouseMove(int, int) EI_ORIGINAL(0x0065E580)

// --- CUiRegion -------------------------------------------------------------------------------

BOOL CUiRegion::HitTest(int x, int y) const {
    x -= offsetX_;
    y -= offsetY_;
    if (!enabled_) return 0;
    if (!gameapi::PtInRect(&bounds_, POINT{x, y})) return 0;
    if (rectOnly_) return 1;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products
    // wrap like the original's 32-bit IMULs.
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
EI_REPLACE_METHOD(0x005FC8F0, CUiRegion::HitTest);

BOOL CUiRegion::HasActiveTip() const {
    if (!tip_) return 0;
    int state = tip_->state_;
    return state != 0 && state != 1;
}
EI_REPLACE_METHOD(0x005FC660, CUiRegion::HasActiveTip);

void CUiRegion::SetRect(int left, int top, int right, int bottom) {
    points_.RemoveAll();
    bounds_.left = left;
    bounds_.right = right;
    bounds_.top = top;
    bounds_.bottom = bottom;
    enabled_ = true;
    rectOnly_ = true;
}
EI_REPLACE_METHOD(0x005FC9E0, CUiRegion::SetRect);

void CUiRegion::ClearShape() {
    points_.RemoveAll();
    enabled_ = false;
    rectOnly_ = false;
}
EI_REPLACE_METHOD(0x005FCA40, CUiRegion::ClearShape);

void CUiRegion::AddPoint(int x, int y) {
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
    // The vertex array is a lib::AlignedArray of 8-byte points (allocated by 004F65B0).
    int index = points_.size;
    points_.SetSize(index + 1);
    points_[index].x = x;
    points_[index].y = y;
    if (points_.size > 2) enabled_ = true;
}
EI_REPLACE_METHOD(0x005FCA70, CUiRegion::AddPoint);

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
EI_REPLACE_METHOD(0x005FC880, CUiRegion::OnMouseMove);

// --- CUiWindow -------------------------------------------------------------------------------

int CUiWindow::RegionAt(int x, int y) const {
    POINT point{x, y};
    interface_->CallScalePointFromScreen(&point);
    for (int i = 0; i < regions_.size; ++i) {
        if (regions_.data[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}
EI_REPLACE_METHOD(0x005FFCC0, CUiWindow::RegionAt);

BOOL CUiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    interface_->CallScalePointFromScreen(&point);
    return gameapi::PtInRect(&rect_, point);
}
EI_REPLACE_METHOD(0x005FFD30, CUiWindow::ContainsPoint);

void CUiWindow::UpdateRegionTips(int) {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->CallScalePointFromScreen(&mouse);
    // Inlines CUiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < regions_.size; ++i) {
        CUiRegion* region = regions_.data[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
    }
}
EI_REPLACE_METHOD(0x005FEBF0, CUiWindow::UpdateRegionTips);

// The same per region, each tooltip then redrawn (its slot 22; the tooltip is re-read).
void CUiWindow::RedrawTips() {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->CallScalePointFromScreen(&mouse);
    for (int i = 0; i < regions_.size; ++i) {
        CUiRegion* region = regions_.data[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
        if (CUiTooltip* tip = regions_.data[i]->tip_) reinterpret_cast<CUiWindow*>(tip)->VirtualRedraw();
    }
}
EI_REPLACE_METHOD(0x005FED00, CUiWindow::RedrawTips);

BOOL CUiWindow::DrawBase(int pass) {
    for (int i = 0; i < sceneObjects_.size; ++i) SpriteDrawOriginal(sceneObjects_.data[i], nullptr, pass);
    for (int i = 0; i < models_.size; ++i) models_.data[i]->Draw(reinterpret_cast<UiDrawPass*>(pass));
    return 1;
}
EI_REPLACE_METHOD(0x005FEB90, CUiWindow::DrawBase);

void CUiWindow::RedrawBase() {
    for (int i = 0; i < surfaces_.size; ++i) {
        CUiSurface* surface = surfaces_.data[i];
        if (surface->Shown()) surface->Blit();
    }
}
EI_REPLACE_METHOD(0x005FECC0, CUiWindow::RedrawBase);

void CUiWindow::SetEnabled(int enabled, int notify) {
    enabled_ = enabled;
    if (notify) CallSlot19(this, enabled);
}
EI_REPLACE_METHOD(0x006002A0, CUiWindow::SetEnabled);

void CUiWindow::AddObject(CEngineObject* object) {
    if (!object) return;
    objects_.Add(object);
    ++object->refCount_;
}
EI_REPLACE_METHOD(0x005FEDD0, CUiWindow::AddObject);

void CUiWindow::RemoveObject(CEngineObject* object) {
    if (!object) return;
    for (int i = 0; i < objects_.size; ++i) {
        if (objects_.data[i] == object) {
            objects_.data[i]->Release();
            objects_.RemoveAt(i);
            return;
        }
    }
}
EI_REPLACE_METHOD(0x005FF110, CUiWindow::RemoveObject);

void CUiWindow::RemoveObjectAt(int index) {
    objects_.data[index]->Release();
    objects_.RemoveAt(index);
}
EI_REPLACE_METHOD(0x005FF180, CUiWindow::RemoveObjectAt);

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
EI_REPLACE_METHOD(0x005FF700, CUiWindow::RemoveSceneObject);

void CUiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects_.data[index]->Release();
    sceneObjects_.RemoveAt(index);
}
EI_REPLACE_METHOD(0x005FF770, CUiWindow::RemoveSceneObjectAt);

void CUiWindow::ClearSceneObjects() {
    for (int i = 0; i < sceneObjects_.size; ++i) sceneObjects_.data[i]->Release();
    sceneObjects_.RemoveAll();
}
EI_REPLACE_METHOD(0x005FF7D0, CUiWindow::ClearSceneObjects);

void CUiWindow::ClearObjects() {
    for (int i = 0; i < objects_.size; ++i) objects_.data[i]->Release();
    objects_.RemoveAll();
}
EI_REPLACE_METHOD(0x005FF1E0, CUiWindow::ClearObjects);

// Returns `surface` (the original leaves its argument in EAX; the titles' Create 0062B7D0 freezes
// the surface through the result).
CUiSurface* CUiWindow::AddSurface(CUiSurface* surface) {
    if (!surface) return nullptr;
    surfaces_.Add(surface);
    return surface;
}
EI_REPLACE_METHOD(0x005FFA90, CUiWindow::AddSurface);

void CUiWindow::RemoveSurfaceAt(int index) {
    if (CUiSurface* surface = surfaces_.data[index]) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    surfaces_.RemoveAt(index);
}
EI_REPLACE_METHOD(0x005FFBF0, CUiWindow::RemoveSurfaceAt);

void CUiWindow::ClearSurfaces() {
    for (int i = 0; i < surfaces_.size; ++i) {
        if (CUiSurface* surface = surfaces_.data[i]) {
            surface->ReleaseContents();
            gamecrt::operator_delete(surface);
        }
    }
    surfaces_.RemoveAll();
}
EI_REPLACE_METHOD(0x005FFC50, CUiWindow::ClearSurfaces);

CEngineObject* CUiWindow::LoadTexture(const char* name, int kind, const char* directory) {
    void* manager = UiTextureManager();
    auto* texture = static_cast<CEngineObject*>(TextureNamedOriginal(manager, nullptr, kind, name));
    if (!texture) return nullptr;
    int loaded = directory ? TextureLoadFromOriginal(manager, nullptr, texture, directory)
                           : TextureLoadOriginal(manager, nullptr, texture);
    if (!loaded) return nullptr;
    objects_.Add(texture);
    ++texture->refCount_;
    return texture;
}
EI_REPLACE_METHOD(0x005FEF20, CUiWindow::LoadTexture);

CEngineObject* CUiWindow::CreateTexture(int width, int height, int kind) {
    auto* texture = reinterpret_cast<CEngineObject*>(
        NewManagedTextureOriginal(UiTextureManager(), nullptr, width, height, kind));
    if (!texture) return nullptr;
    int index = objects_.size;
    objects_.SetSize(index + 1, -1);
    objects_.data[index] = texture;
    texture->flags_ |= 0x40000;
    ++texture->refCount_;
    return texture;
}
EI_REPLACE_METHOD(0x005FF0B0, CUiWindow::CreateTexture);

void CUiWindow::ShowSurfaces(int show) {
    for (int i = 0; i < surfaces_.size; ++i) {
        CUiSurface* surface = surfaces_.data[i];
        if (show == 0 || surface->Shown()) surface->CallSwapContents(show);
    }
}
EI_REPLACE_METHOD(0x006002C0, CUiWindow::ShowSurfaces);

void CUiSurface::CallSwapContents(int restore) { SurfaceSwapContentsOriginal(this, nullptr, restore); }

void CUiSurface::Freeze(int frozen, int restore) {
    if (frozen) {
        flags_ |= kShown;
        if (kept_) CallSwapContents(1);
        return;
    }
    flags_ &= ~kShown;
    if (restore) CallSwapContents(0);
}
EI_REPLACE_METHOD(0x005FD760, CUiSurface::Freeze);

CUiRegion* CUiWindow::AddRegion() {
    auto* region = static_cast<CUiRegion*>(gamecrt::operator_new(sizeof(CUiRegion)));
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
EI_REPLACE_METHOD(0x005FF830, CUiWindow::AddRegion);

void CUiWindow::RemoveRegionAt(int index) {
    if (CUiRegion* region = regions_.data[index]) {
        region->ReleaseContents();
        gamecrt::operator_delete(region);
    }
    regions_.RemoveAt(index);
}
EI_REPLACE_METHOD(0x005FF9C0, CUiWindow::RemoveRegionAt);

void CUiWindow::ClearRegions() {
    for (int i = 0; i < regions_.size; ++i) {
        if (CUiRegion* region = regions_.data[i]) {
            region->ReleaseContents();
            gamecrt::operator_delete(region);
        }
    }
    regions_.RemoveAll();
}
EI_REPLACE_METHOD(0x005FFA20, CUiWindow::ClearRegions);

CUiModel* CUiWindow::AddModel() {
    auto* model = static_cast<CUiModel*>(gamecrt::operator_new(sizeof(CUiModel)));
    if (!model) return nullptr;
    model->object_ = nullptr;   // 005FC1C0, the constructor
    models_.Add(model);
    return model;
}
EI_REPLACE_METHOD(0x005FFD80, CUiWindow::AddModel);

void CUiWindow::ClearModels() {
    for (int i = 0; i < models_.size; ++i) {
        if (CUiModel* model = models_.data[i]) {
            model->ReleaseObject();
            gamecrt::operator_delete(model);
        }
    }
    models_.RemoveAll();
}
EI_REPLACE_METHOD(0x005FFF40, CUiWindow::ClearModels);

// --- 005FF460: ReplaceSceneObject ------------------------------------------------------------------

namespace {
// 005AC1A0: a new object of the model manager by name, or null.
std::uint8_t* NewModelObject(const char* name) {
    return reinterpret_cast<std::uint8_t*>(ModelManagerNewObjectOriginal(UiModelManager(), nullptr, name));
}
// 0059F3D0 (thiscall on the object): attaches it to the window.
void __fastcall AttachToWindow(std::uint8_t* a0, void*, CUiWindow* a2) { FigurePartSetOwnerOriginal((void*)a0, nullptr, (void*)a2); }
// 005E70B0 (thiscall): the sprite's scale (+0x23C) in ST0; 005E7060 sets it.
double __fastcall SpriteScale(void* a0) { return SpriteScaleOriginal((const void*)a0); }
void __fastcall SetSpriteScale(void* a0, void*, float a2) { SpriteSetScaleOriginal(a0, nullptr, std::bit_cast<std::uint32_t>(a2)); }

// FLD / FSTP of a float field (as the original copies +0xCC and +0xD0).
void CopyFloat(void* to, const void* from) {
    __asm {
        mov eax, from
        mov ecx, to
        fld dword ptr [eax]
        fstp dword ptr [ecx]
    }
}
} // namespace

CEngineObject* CUiWindow::ReplaceSceneObject(CEngineObject* object, const char* name) {
    for (int i = 0; i < sceneObjects_.size; ++i) {
        if (sceneObjects_.data[i] != object) continue;
        std::uint8_t* made = NewModelObject(name);
        if (!made) return nullptr;
        int references;
        std::memcpy(&references, made + 4, 4);
        ++references;
        std::memcpy(made + 4, &references, 4);
        AttachToWindow(made, nullptr, this);
        auto* old = reinterpret_cast<std::uint8_t*>(object);
        std::memcpy(made + 0x27C, reinterpret_cast<std::uint8_t*>(this) + 0xEC, 4);
        CEngineObject* texture = objects_.size > 0 ? objects_.data[0] : nullptr;
        std::memcpy(made + 0x280, &texture, 4);
        std::memcpy(made + 0x1C, old + 0x1C, 12);
        CopyFloat(made + 0xCC, old + 0xCC);
        CopyFloat(made + 0xD0, old + 0xD0);
        std::memcpy(made + 0x34, old + 0x34, 16);
        std::memcpy(made + 0x280, old + 0x280, 4);
        volatile float scale = static_cast<float>(SpriteScale(old));
        SetSpriteScale(made, nullptr, scale);
        sceneObjects_.RemoveAt(i);
        using ReleaseFn = void(__thiscall*)(void*);
        reinterpret_cast<ReleaseFn>((*reinterpret_cast<void***>(object))[4])(object);
        // Back in at the same place: the original grows the array as SetSize does (inlined when
        // the place is the end) and shifts the rest up with memmove.
        int size = sceneObjects_.size;
        if (i < size) {
            sceneObjects_.SetSize(size + 1, -1);
            std::memmove(&sceneObjects_.data[i + 1], &sceneObjects_.data[i], static_cast<std::size_t>(size - i) * 4);
        } else {
            sceneObjects_.SetSize(i + 1, -1);
        }
        sceneObjects_.data[i] = reinterpret_cast<CEngineObject*>(made);
        return reinterpret_cast<CEngineObject*>(made);
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x005FF460, CUiWindow::ReplaceSceneObject);

// --- 005FF3C0: AddSprite ---------------------------------------------------------------------------

CUiMesh* CUiWindow::AddSprite(const char* name) {
    auto* object = reinterpret_cast<CEngineObject*>(NewModelObject(name));
    if (!object) return nullptr;
    bool listed = false;
    for (int i = 0; i < sceneObjects_.size && !listed; ++i) listed = sceneObjects_.data[i] == object;
    if (!listed) {
        sceneObjects_.SetAtGrow(sceneObjects_.size, object);
        ++object->refCount_;
        AttachToWindow(reinterpret_cast<std::uint8_t*>(object), nullptr, this);
    }
    auto* sprite = reinterpret_cast<CUiMesh*>(object);
    sprite->material_ = blackMaterial_;
    sprite->texture_ = objects_.size > 0 ? objects_.data[0] : nullptr;
    return sprite;
}
EI_REPLACE_METHOD(0x005FF3C0, CUiWindow::AddSprite);

// --- 005FE700: the constructor -----------------------------------------------------------------

namespace {
// 0040CB20: the engine object's base constructor (thiscall).
// The material manager (007AF740, thiscall): 0068E970 makes a material (RET 4,
// MaterialCreateOriginal), 0068EAF0 registers it (RET 8, MaterialRegisterOriginal).

void SetDword(void* p, int offset, std::uint32_t value) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, 4); }

// A material's colour (+0x50..+0x58) and a reference (+0x04).
void PrepareMaterial(std::uint8_t* material, std::uint32_t colour) {
    SetDword(material, 0x50, colour);
    SetDword(material, 0x54, colour);
    SetDword(material, 0x58, colour);
    std::uint32_t references;
    std::memcpy(&references, material + 4, 4);
    SetDword(material, 4, references + 1);
}
} // namespace

CUiWindow* CUiWindow::Construct(CInterface3D* ui) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    ConstructEngineObject(this);
    for (int offset : {0x34, 0x44}) {   // two identity rotations (0040CB00, inlined here)
        SetDword(self, offset, 0x3F800000);
        SetDword(self, offset + 4, 0);
        SetDword(self, offset + 8, 0);
        SetDword(self, offset + 12, 0);
    }
    interface_ = ui;
    inputVtable_ = reinterpret_cast<void**>(0x007401D0);
    std::memset(self + 0xA0, 0, 0x38);
    for (int offset = 0xF8; offset <= 0x138; offset += 0x10) {   // the five pointer arrays
        SetDword(self, offset, 0);
        SetDword(self, offset + 0xC, 0);
        SetDword(self, offset + 8, 0);
        SetDword(self, offset + 4, 0);
    }
    vtable_ = reinterpret_cast<void**>(0x00740F60);
    inputVtable_ = reinterpret_cast<void**>(0x00740F2C);
    SetDword(self, 0x08, 0);
    rect_ = RECT{0, 0, 0, 0};
    std::memcpy(self + 0x34, reinterpret_cast<const void*>(0x007C7E58), 16);
    SetDword(self, 0x1C, 0);
    SetDword(self, 0xF4, 0);
    SetDword(self, 0x20, 0);
    SetDword(self, 0x24, 0);
    void* materials = *reinterpret_cast<void* const*>(0x007AF740);
    std::uint8_t* white = MaterialCreateOriginal(materials, nullptr, 0x16);
    SetDword(self, 0xF0, reinterpret_cast<std::uintptr_t>(white));
    MaterialRegisterOriginal(materials, nullptr, white, 0x16);
    PrepareMaterial(*reinterpret_cast<std::uint8_t**>(self + 0xF0), 0x3F800000);
    std::uint8_t* black = MaterialCreateOriginal(materials, nullptr, 0x16);
    SetDword(self, 0xEC, reinterpret_cast<std::uintptr_t>(black));
    MaterialRegisterOriginal(materials, nullptr, black, 0x16);
    PrepareMaterial(*reinterpret_cast<std::uint8_t**>(self + 0xEC), 0);
    enabled_ = 1;
    CallSlot19(this, 1);
    return this;
}
EI_REPLACE_METHOD(0x005FE700, CUiWindow::Construct);

// --- 00600D30: CUiContainer::AddChild ------------------------------------------------------------

void CUiContainer::AddChild(CUiWindow* child) {
    if (!child) return;
    children_.Add(child);
}
EI_REPLACE_METHOD(0x00600D30, CUiContainer::AddChild);

// --- 005FE930: the destructor body -------------------------------------------------------------

void CUiWindow::DestroyBase() {
    vtable_ = reinterpret_cast<void**>(0x00740F60);
    inputVtable_ = reinterpret_cast<void**>(0x00740F2C);
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    for (int offset = 0xEC; offset <= 0xF4; offset += 4) {
        CEngineObject* object;
        std::memcpy(&object, self + offset, 4);
        if (object) object->Release();
        std::memset(self + offset, 0, 4);
    }
    for (int i = 0; i < sceneObjects_.size; ++i) sceneObjects_.data[i]->Release();
    sceneObjects_.SetSize(0, -1);
    for (int i = 0; i < surfaces_.size; ++i) {
        if (CUiSurface* surface = surfaces_.data[i]) {
            surface->ReleaseContents();
            gamecrt::operator_delete(surface);
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
    std::memset(self + 0x08, 0, 4);
    // The arrays' destructors (their storage is already freed above, so these find nothing).
    if (models_.data) FreeAligned(models_.data);
    if (regions_.data) FreeAligned(regions_.data);
    if (surfaces_.data) FreeAligned(surfaces_.data);
    if (sceneObjects_.data) FreeAligned(sceneObjects_.data);
    if (objects_.data) FreeAligned(objects_.data);
    // The CTransformObject and CEngineObject levels.
    inputVtable_ = reinterpret_cast<void**>(0x007401D0);
    vtable_ = reinterpret_cast<void**>(0x0073BA38);
    reinterpret_cast<CEngineObject*>(this)->DestructBase();
}
EI_REPLACE_METHOD(0x005FE930, CUiWindow::DestroyBase);
