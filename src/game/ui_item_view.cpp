// The item screens' model view and the description panel's drawing. See
// ui_item_info.h and docs/notes/ui.md ("Item descriptions").
// Floating point follows the original's x87 code: values the original game keeps on the FPU stack stay
// unrounded (or truncated with the game's _ftol), values it stores as float are rounded with
// static_cast<float>, products keep their operand order (precise floating point).
#include "app.h"
#include "core_refcount.h"
#include "texture.h"
#include "ui_item_info.h"
#include "plugin_fixes.h"

#include "device.h"
#include "dx_surface.h"
#include "item.h"
#include "random.h"
#include "spell.h"
#include "unit_db.h"

#include "ei/rebirth.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"

#include <cmath>
#include <bit>

namespace {

inline double D(float f) { return f; }
constexpr float kFullTurn = /*007C7D20*/ 6.2831855f;   // a static initialiser: pi + pi


int ScaledFtol(float value, float scale) { return static_cast<int>(libcrt::Ftol(static_cast<double>(value) * scale)); }

std::uint32_t PackColour(const float* rgb) {
    std::uint32_t r = static_cast<std::uint32_t>(ScaledFtol(rgb[0], 255.0f /*0073C370*/));
    std::uint32_t g = static_cast<std::uint32_t>(ScaledFtol(rgb[1], 255.0f /*0073C370*/));
    std::uint32_t b = static_cast<std::uint32_t>(ScaledFtol(rgb[2], 255.0f /*0073C370*/));
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The cached object for `name` (its use time refreshed), or a new one put in the cache, which then drops its
// least recently used entries beyond its limit. Null when it cannot be made. `make` is the object's maker.
template <typename Object>
Object* CachedObject(ItemObjectCache<Object>* cache, std::string_view name, Object* (*make)(std::string_view)) {
    auto node = cache->map.find(std::string(name));
    if (node != cache->map.end()) {
        node->second.lastUse = gameapi::timeGetTime();
        return node->second.object;
    }
    Object* object = make(name);
    if (!object) return nullptr;
    std::uint32_t time = gameapi::timeGetTime();
    {
        std::string key(name);
        ItemCacheEntry<Object>& value = cache->map[key];
        value.object = object;
        value.lastUse = time;
    }
    TrimItemCache(cache);
    return object;
}
CUiMesh* CachedModel(ItemModelCache* cache, std::string_view name) { return CachedObject(cache, name, ItemModel); }
CTexture* CachedTexture(ItemTextureCache* cache, std::string_view name) { return CachedObject(cache, name, ItemFigureTexture); }

// A new turning state with a random glow phase: word * 2 pi (as float) * 2^-32 in that order, stored as
// float.
float RandomPhase(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * kFullTurn * 2.3283064370807974e-10 /*0073C468*/);
}

// The spell glow: the phase advances by dt * 5 and wraps at 2 pi (_CIfmod); its sine scales the school's
// colour (kSchoolGlow) into 0xFFrrggbb. The phase is stored as float but the sine and the colour use the
// unrounded value.
// The spell schools' glow colours (red, green, blue and an unused 1 each).
constexpr float kSchoolGlow[8][4] = /*00740358*/ {
    {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};

std::uint32_t Glow(float dt, ItemTurn* turn, const float* colour) {
    const double advanced = static_cast<double>(dt) * 5.0 /*007403F0*/ + turn->glow;
    const double pi = 3.141592653589793 /*007402B8*/;
    const double phase = std::fmod(advanced, pi + pi);
    turn->glow = static_cast<float>(phase);
    const double strength = (x87::fsin(phase) + 1.0 /*0073BAC8*/) * 0.15000000596046448 /*007403E8*/;
    const auto channel = [&](int i) { return libcrt::Ftol(strength * colour[i] * 255.0f /*0073C370*/); };
    const std::uint32_t r = channel(0);
    const std::uint32_t g = channel(1);
    const std::uint32_t b = channel(2);
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The turn: on the hovered item the speed grows by 8 per second; elsewhere it brakes towards the
// next full turn (4 * (2 pi - angle)), or stops below 0.1. At most 5. The angle advances by
// dt * speed and wraps at 2 pi (compared unrounded, stored as float).
void Turn(ItemTurn* turn, float dt, bool hovered) {
    EI_NAN_AUDIT(turn->speed);
    if (hovered) {
        turn->speed = static_cast<float>(D(8.0f /*007402F4*/) * D(dt) + D(turn->speed));
    } else if (turn->speed < 0.1f /*007402F0*/) {
        turn->angle = 0.0f;
        turn->speed = 0.0f;
    } else {
        double pi = D(3.1415927f /*007402C0*/);
        turn->speed = static_cast<float>((pi + pi - D(turn->angle)) * D(4.0f /*0073BAA0*/));
    }
    if (turn->speed > 5.0f /*007402EC*/) turn->speed = 5.0f /*007402EC*/;
    // The angle is stored and compared unrounded with 2 pi: wraps while above, not when equal or unordered.
    double angle = static_cast<double>(dt) * turn->speed + turn->angle;
    turn->angle = static_cast<float>(angle);
    while (angle > kFullTurn) {
        angle = static_cast<double>(turn->angle) - kFullTurn;
        turn->angle = static_cast<float>(angle);
    }
}

Quaternion HalfTurn(float ax, float ay, float az) {
    const Vector3 axis = {ax, ay, az};
    Quaternion q;
    q.SetAxisAngle(axis, 3.1415927f /*007402C0*/);
    return q;
}

// The two products the original game inlines (a * b), each with its own order of the w sum.
Quaternion ProductA(const Quaternion& a, const Quaternion& b) {   // weapons
    Quaternion r;
    r.w = static_cast<float>(D(a.w) * D(b.w) - DotUnrounded<Sum3::XYZ, double>(a.x, a.y, a.z, b.x, b.y, b.z));
    r.x = static_cast<float>(((D(b.y) * D(a.z) - D(a.y) * D(b.z)) + D(a.w) * D(b.x)) + D(a.x) * D(b.w));
    r.y = static_cast<float>(((D(a.x) * D(b.z) - D(b.x) * D(a.z)) + D(a.w) * D(b.y)) + D(a.y) * D(b.w));
    r.z = static_cast<float>(((D(a.y) * D(b.x) - D(a.x) * D(b.y)) + D(b.w) * D(a.z)) + D(a.w) * D(b.z));
    return r;
}
Quaternion ProductB(const Quaternion& a, const Quaternion& b) {   // other items
    Quaternion r;
    r.w = static_cast<float>(D(a.w) * D(b.w) - DotUnrounded<Sum3::ZYX, double>(a.x, a.y, a.z, b.x, b.y, b.z));
    r.x = static_cast<float>(((D(b.y) * D(a.z) - D(b.z) * D(a.y)) + D(a.w) * D(b.x)) + D(b.w) * D(a.x));
    r.y = static_cast<float>(((D(b.z) * D(a.x) - D(b.x) * D(a.z)) + D(a.w) * D(b.y)) + D(b.w) * D(a.y));
    r.z = static_cast<float>(((D(b.x) * D(a.y) - D(b.y) * D(a.x)) + D(a.z) * D(b.w)) + D(a.w) * D(b.z));
    return r;
}

void SetOrientation(CUiMesh* model, const Quaternion& q) { model->localRotation_ = q; }

bool IsRecordLoot(const CItem* item, std::uint32_t recordType) {
    if (item->type_ != kItemLoot) return false;
    const auto* loot = AsLoot(item);
    return loot->kind_ == 2 && loot->recordType_ == recordType;
}

bool LiesFlat(CItem* item) {
    if (item->type_ == kItemSpell) return true;
    return IsRecordLoot(item, 3) || IsRecordLoot(item, 4);
}

void SetShade(CUiMesh* sprite, float grey) {
    sprite->tint_[0] = grey;
    sprite->tint_[1] = grey;
    sprite->tint_[2] = grey;
}

void SetSpecular(int on) {
    device::SetRenderState(device::D3D(TheRenderDevice()), D3DRENDERSTATE_SPECULARENABLE, static_cast<DWORD>(on));
}

bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

constexpr std::uint32_t kPnt3 = kMmpFormatPnt3;

// The texture's pixels for one fill: the texture manager's textures have only the new renderer's
// memory, written directly (DirectPixels; the original: a Lock with flags 0 and the Unlock after). 0,
// or the error the original's report gets when there is no memory (none is made without it).
long LockTexture(CDXSurface* holder, SurfacePixels* pixels) {
    return DirectPixels(holder, pixels) ? 0 : E_FAIL;
}

} // namespace

void CItemScreen::DrawItem(CSceneRenderer* pass, ItemCell* cell) {
    CItem* item = cell->item;
    CUiMesh* model;
    {
        std::string name = item->PictureName();
        model = CachedModel(&models_, name);
    }
    CTexture* texture;
    {
        std::string name = item->FigureName();
        texture = CachedTexture(&textures_, name);
    }
    if (!model || !texture) return;

    model->texture_ = texture;
    model->Place(cell->x, cell->y, cell->depth);
    model->SetScale(static_cast<float>(D(0.48157462f /*007402C4*/) * D(cell->size) * D(cell->depth) * D(0.0009375f /*00740408*/)));
    model->material_ = blackMaterial_;

    if (ItemTurnOf(item) == nullptr) {
        auto* turn = new ItemTurn();
        turn->angle = 0.0f;
        turn->speed = 0.0f;
        turn->flag = false;
        std::uint32_t word = g_random.Next();
        turn->glow = RandomPhase(word);
        AssignCounted(item->link_, turn);
    }

    if (cell->mode == 9) {
        // Greyed: half bright, lying still, scaled to fit the cell by the model's larger extent.
        SetShade(model, 0.5f);
        SetOrientation(model, LiesFlat(item) ? HalfTurn(0.0f, 0.0f, 1.0f) : HalfTurn(1.0f, 0.0f, 0.0f));
        // The larger of the box's x and y extents: y when x is below it (or NaN), selected as dwords.
        const Vector3& extents = model->box_.bounds.boxMax;
        EI_NAN_AUDIT(extents.y, extents.x);
        float extent = extents.x < extents.y ? extents.y : extents.x;
        double half = D(0.5f /*0073B86C*/);
        float y = static_cast<float>(D(model->box_.bounds.center.y) * D(cell->size) * half + D(cell->y));
        float x = static_cast<float>(D(cell->x) - D(model->box_.bounds.center.x) * D(cell->size) * half);
        model->Place(x, y, cell->depth);
        model->SetScale(static_cast<float>(D(0.48157462f /*007402C4*/) * D(cell->size) * D(cell->depth) *
                                        D(0.00125f /*00740164*/) / D(extent)));
    } else {
        SetShade(model, 1.0f);
        float dt = static_cast<float>(FrameTime());
        ItemTurn* turn = ItemTurnOf(item);
        int kind = item->type_;
        CSpell* spell = item->spell_;
        if (spell && (kind == kItemWeapon || kind == kItemArmor)) {
            int school = spell->GetPrototype()->school;
            if (school < 8)
                model->flatSpecular_ = Glow(dt, turn, kSchoolGlow[school]);
            else
                turn->glow = 0.0f;
        }
        Turn(turn, dt, item == hoveredItem_);

        if (LiesFlat(item)) {
            SetOrientation(model, HalfTurn(0.0f, 0.0f, 1.0f));
        } else if (kind == kItemWeapon) {
            // Weapons turn about (1, 1, 0) after a half turn about x.
            Quaternion a = HalfTurn(1.0f, 0.0f, 0.0f);
            const Vector3 axis = {1.0f, 1.0f, 0.0f};
            Quaternion b;
            b.SetAxisAngle(axis, turn->angle);
            SetOrientation(model, ProductA(a, b));
        } else if (IsRecordLoot(item, 0) || IsRecordLoot(item, 1) || IsRecordLoot(item, 2)) {
            SetOrientation(model, HalfTurn(1.0f, 0.0f, 0.0f));   // blueprints lie still
        } else {
            // Others turn about y after a half turn about x.
            Quaternion a = HalfTurn(1.0f, 0.0f, 0.0f);
            const Vector3 axis = {0.0f, 1.0f, 0.0f};
            Quaternion b;
            b.SetAxisAngle(axis, turn->angle);
            SetOrientation(model, ProductB(a, b));
        }

        float frameDepth = static_cast<float>(D(cell->depth) * D(1.3f /*0073FF20*/));
        CUiMesh* frame = AddBlankSprite();
        double half = D(35.0f /*007403E0*/);
        frame->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                            static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
        frame->texture_ = objects_[2];
        frame->Draw(pass);
        RemoveSceneObject(frame);
    }

    model->flatColour_ = PackColour(model->tint_);
    model->flags_ |= 0x100;
    if (item->spell_) SetSpecular(1);
    model->Draw(pass);
    if (item->spell_) SetSpecular(0);

    CSpell* spell = item->spell_;
    if (item->type_ == kItemSpell && spell && spell->GetModifierCount() > 0) {
        float px, py, pz;
        model->GetPlacement(&px, &py, &pz);
        float scale = model->GetScaleX();
        CUiMesh* mark = AddBlankSprite();
        mark->SetRectangle(static_cast<float>(D(pz) * D(0.9f /*0073CD70*/)), 100.0f, 0.0f, 0.0f, 35.0f, 35.0f, 0, 0,
                           0x20, 0x20);
        mark->texture_ = objects_[1];
        mark->SetScale(scale);
        SetShade(mark, cell->mode == 9 ? 0.5f : 1.0f);
        float step = static_cast<float>(D(scale) * D(550.0f /*007403DC*/));
        for (int i = 0; i < spell->GetModifierCount(); ++i) {
            int code = spell->modifiers_[i] - 1;   // the modifier's index (stored as index + 1)
            double cellSize = D(0.125f /*0073C520*/);
            mark->textureOffsetU_ = static_cast<float>(static_cast<double>(code & 7) * cellSize);
            mark->textureOffsetV_ = static_cast<float>(static_cast<double>(-(code >> 3)) * cellSize);
            double d = D(step) / D(pz);
            float dx;
            if (i == 0 || i == 4)
                dx = 0.0f;
            else if (i == 5 || i == 6 || i == 7)
                dx = -static_cast<float>(d);
            else
                dx = static_cast<float>(d);
            double dy;
            if (i == 2 || i == 6)
                dy = D(0.0f /*0073B858*/);
            else if (i == 0 || i == 1 || i == 7)
                dy = -d;
            else
                dy = d;
            float depth = static_cast<float>(D(pz) * D(0.9f /*0073CD70*/));
            mark->Place(static_cast<float>(D(dx) + D(px)), static_cast<float>(dy + D(py)), depth);
            mark->Draw(pass);
        }
        RemoveSceneObject(mark);
    }

    if (cell->mode != 9 && !CanEquip(item, cell->mode)) {
        float depth = static_cast<float>(D(cell->depth) * D(1.25f /*0073CF50*/));
        if (LiesFlat(item)) depth = static_cast<float>(D(cell->depth) * D(0.8f /*0073CCA8*/));
        CUiMesh* shade = AddBlankSprite();
        double half = D(35.0f /*007403E0*/);
        shade->SetRectangle(depth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                            static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0, 0, 0x100, 0x100);
        shade->texture_ = nullptr;
        shade->flags_ |= 0x100;
        shade->flatColour_ = 0x82804040;
        shade->Draw(pass);
        RemoveSceneObject(shade);
    }
}

BOOL CItemInfoPanel::Draw(CSceneRenderer* pass) {
    if (CItem* item = FirstItem()) {
        ItemCell cell;
        cell.item = item;
        cell.x = static_cast<float>(static_cast<double>(x_) + D(100.0f /*0073BAB0*/));
        cell.depth = 3.0f;
        cell.size = 120.0f;
        cell.mode = 9;
        cell.y = static_cast<float>(static_cast<double>(y_) + D(200.0f /*0073CF58*/));
        owner_->DrawItem(pass, &cell);
    }
    return 1;
}

// --- A hero's face -----------------------------------------------------------------------------

Quaternion* MultiplyQuaternions(Quaternion*, const Quaternion*, const Quaternion*);

void CItemScreen::FaceNames(const char* prototype, std::string* model, std::string* texture) {
    UnitPrototypeRecord* unit;
    {
        std::string key(prototype);
        unit = g_unitDatabase->prototypes.byName[key];   // an unknown name gets a null entry
    }
    UnitModelRecord* figure;
    {
        std::string key(unit->modelName);
        figure = unitdb::ModelNamed(key);
    }
    std::string sex;
    const char* figureModel = figure->figureName.c_str();
    const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes: the model's fifth character
    if (fixes ? !pluginfixes::ModelIsFemale(figureModel) : SameText(figureModel, "unhuma"))
        sex = "m";
    else if (fixes || SameText(figureModel, "unhufe"))
        sex = "f";
    *model = str::Format("infa%sme%dface", figureModel, unit->hair + 1);
    *texture = str::Format("face%s%02d", sex.c_str(), unit->skin);
}

void CItemScreen::DrawHeroFace(CSceneRenderer* pass, HeroFaceCell* cell) {
    std::string modelName, textureName;
    FaceNames(cell->prototype, &modelName, &textureName);
    CUiMesh* model = CachedModel(&models_, modelName);
    CTexture* texture = CachedTexture(&textures_, textureName);
    if (!model || !texture) return;

    model->texture_ = texture;
    model->Place(cell->x, cell->y, cell->depth);
    model->SetScale(static_cast<float>(D(cell->size) * D(cell->depth) * D(0.48157462f /*007402C4*/) * D(0.0009375f /*00740408*/)));
    model->material_ = blackMaterial_;
    SetShade(model, 1.0f);
    Quaternion turn = HalfTurn(1.0f, 0.0f, 0.0f);
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    tilt.SetAxisAngle(axis, static_cast<float>((D(cell->y) - D(300.0f /*0073FA98*/)) * D(0.001f /*0074040C*/)));
    Quaternion orientation;
    SetOrientation(model, *MultiplyQuaternions(&orientation, &tilt, &turn));

    float frameDepth = static_cast<float>(D(cell->depth) * D(1.3f /*0073FF20*/));
    CUiMesh* frame = AddBlankSprite();
    double half = D(35.0f /*007403E0*/);
    frame->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                        static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
    frame->texture_ = objects_[2];
    frame->Draw(pass);
    RemoveSceneObject(frame);
    model->Draw(pass);
}

// --- An item's figure texture -------------------------------------------------------------

CTexture* ItemFigureTexture(std::string_view name) {
    CTextureManager* manager = TheTextureManager();
    CTexture* texture = manager->NewTexture(0x100, 0x100, 0x20);
    if (!texture) return nullptr;
    ++texture->refCount_;
    CDXSurface* holder = texture->surface_;
    const MmpHeader* image;
    {
        std::string file = std::string(name) + ".mmp";
        image = manager->GetFileData(file);
    }
    int width, height;
    if (!image) {
        height = 0x100;
        width = 0x100;
    } else {
        width = static_cast<int>(image->width);
        height = static_cast<int>(image->height);
    }
    std::uint32_t* pixels = nullptr;
    if (image) {
        std::uint32_t count = static_cast<std::uint32_t>(height) * static_cast<std::uint32_t>(width);
        pixels = new std::uint32_t[count];
        std::memset(pixels, 0, count * 4);
        PixelConverter()->UnpackTo32(pixels, image + 1, count, image->format == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(&g_deviceLock);
    SurfacePixels target;
    long hr = LockTexture(holder, &target);
    if (hr != 0 && app::FatalErrorCode(hr, "Can't lock surface.\n") == 0) {
        // The original leaves the device lock held here.
        if (pixels) delete[] pixels;
        texture->Release();
        return nullptr;
    }
    std::uint8_t* at = target.bits + (static_cast<std::uint32_t>(0x100 - height) << 9);
    if (!image) {
        for (int row = height; row > 0; --row) {
            std::memset(at, 0xFF, static_cast<std::uint32_t>(width) * 2);
            at += 0x200;
        }
    } else {
        const std::uint32_t* from = pixels;
        for (int row = height; row > 0; --row) {
            auto* to = reinterpret_cast<std::uint16_t*>(at);
            for (int i = width; i > 0; --i) {
                std::uint32_t p = *from++;
                *to++ = static_cast<std::uint16_t>(((p >> 9) & 0x7C00) | ((p >> 6) & 0x3E0) | ((p >> 3) & 0x1F) |
                                                   ((p >> 16) & 0x8000));
            }
            at = reinterpret_cast<std::uint8_t*>(to + (0x100 - width));
        }
    }
    if (pixels) delete[] pixels;
    gameapi::LeaveCriticalSection(&g_deviceLock);
    manager->FreeAllFiles();
    return texture;
}

// --- An image into part of a texture -------------------------------------------------------

int CUiWindow::CopyImage(CTexture* texture, std::string_view name, int imageWidth, int imageHeight, int width,
                         int height, int x, int y, int, int) {
    CDXSurface* holder = texture->surface_;
    const MmpHeader* image;
    {
        std::string base(name);
        std::string file = base + ".mmp";
        image = TheTextureManager()->GetFileData(file);
    }
    std::uint32_t* pixels = nullptr;
    if (image) {
        std::uint32_t count = static_cast<std::uint32_t>(imageWidth) * static_cast<std::uint32_t>(imageHeight);
        pixels = new std::uint32_t[count];
        std::memset(pixels, 0, count * 4);
        interface_->renderer_->converter_->UnpackTo32(pixels, image + 1, count, image->format == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(&g_deviceLock);
    SurfacePixels target;
    long hr = LockTexture(holder, &target);
    if (hr != 0 && app::FatalErrorCode(hr, "Can't lock surface.\n") == 0) {
        // The original leaves the device lock held here.
        if (pixels) delete[] pixels;
        return 0;
    }
    std::uint8_t* at = target.bits + (static_cast<std::uint32_t>(y) * 0x100 + static_cast<std::uint32_t>(x)) * 2;
    if (!image) {
        for (int row = height; row > 0; --row) {
            std::memset(at, 0xFF, static_cast<std::uint32_t>(width) * 2);
            at += 0x200;
        }
    } else {
        const std::uint32_t* from = pixels;
        for (int row = height; row > 0; --row) {
            auto* to = reinterpret_cast<std::uint16_t*>(at);
            for (int i = width; i > 0; --i) {
                std::uint32_t p = *from++;
                *to++ = static_cast<std::uint16_t>(((p >> 9) & 0x7C00) | ((p >> 6) & 0x3E0) | ((p >> 3) & 0x1F) |
                                                   ((p >> 16) & 0x8000));
            }
            at = reinterpret_cast<std::uint8_t*>(to + (0x100 - width));
            from += imageWidth - width;
        }
    }
    if (pixels) delete[] pixels;
    gameapi::LeaveCriticalSection(&g_deviceLock);
    TheTextureManager()->FreeAllFiles();
    return image != nullptr;
}
