// The item screens' model view (005ECA10) and the description panel's drawing (005EE5C0). See
// ui_item_info.h and docs/notes/ui.md ("Item descriptions").
//
// Floating point follows the original's x87 code: values the original keeps on the FPU stack stay
// there (inline assembly where a value is stored and still used unrounded, or truncated with the
// game's _ftol), values it stores as float are rounded with static_cast<float>, products keep their
// operand order (precise floating point), constants are read from the exe's memory.
#include "app.h"
#include "texture.h"
#include "ui_item_info.h"
#include "game_strings.h"
#include "plugin_fixes.h"

#include "device.h"
#include "dx_surface.h"
#include "item.h"
#include "random.h"
#include "spell.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/vecmath.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_core.h"
#include "originals_units.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <bit>

namespace {

inline double D(float f) { return f; }
float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }


// The game's _ftol of a float times the float at `scale`, the product unrounded.
int ScaledFtol(float value, float scale) { return static_cast<int>(gamecrt::Ftol(static_cast<double>(value) * scale)); }

// A colour 0xFFrrggbb from three floats in 0..1 (each times 255, truncated).
std::uint32_t PackColour(const float* rgb) {
    std::uint32_t r = static_cast<std::uint32_t>(ScaledFtol(rgb[0], 255.0f /*0073C370*/));
    std::uint32_t g = static_cast<std::uint32_t>(ScaledFtol(rgb[1], 255.0f /*0073C370*/));
    std::uint32_t b = static_cast<std::uint32_t>(ScaledFtol(rgb[2], 255.0f /*0073C370*/));
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The draw pass is the scene renderer.
CSceneRenderer* PassRenderer(int pass) { return reinterpret_cast<CSceneRenderer*>(static_cast<std::intptr_t>(pass)); }

// 005EBFB0 / 005EC020 (__fastcall): an item's model (counted) and its figure texture, or null.

// The item's names (virtual slots 19 and 20; return a CString through the hidden pointer).
void ItemName(CItem* item, int slot, mfc::CString* out) {
    using NameFn = mfc::CString*(__thiscall*)(CItem*, mfc::CString*);
    ei::ObjectSlot<NameFn>(item, slot)(item, out);
}

// The cached object for `name` (its use time refreshed), or a new one put in the cache (find
// 005F3730 / 005F3B50, operator[] 005F3000 / 005F3170), which then drops its least recently used
// entries beyond its limit. Null when it cannot be made. `textures` picks the texture maker.
void* CachedObject(ItemObjectCache* cache, const char* name, bool textures) {
    ItemObjectCache::Node* node;
    {
        stlp::string key(name);
        node = cache->map.find(key);
    }
    if (node != cache->map.end()) {
        node->second.lastUse = gameapi::timeGetTime();
        return node->second.object;
    }
    void* object = textures ? ItemFigureTexture(name) : ItemModel(name);
    if (!object) return nullptr;
    std::uint32_t time = gameapi::timeGetTime();
    {
        stlp::string key(name);
        ItemCacheEntry& value = cache->map.Subscript(key);
        value.object = object;
        value.lastUse = time;
    }
    TrimItemCache(cache);
    return object;
}

// A new turning state with a random glow phase: word * 2 pi (as float) * 2^-32 (FILD of the
// unsigned word; FMUL float; FMUL double; FSTP float).
float RandomPhase(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * GameFloat(0x007C7D20) * 2.3283064370807974e-10 /*0073C468*/);
}

// The spell glow: the phase advances by dt * 5 and wraps at 2 pi (_CIfmod); its sine scales the
// school's colour (kSchoolGlow) into 0xFFrrggbb. The phase is stored as float
// but the sine and the colour use the unrounded value, as the original.
// The spell schools' glow colours (red, green, blue and an unused 1 each).
constexpr float kSchoolGlow[8][4] = /*00740358*/ {
    {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};

std::uint32_t Glow(float dt, ItemTurn* turn, const float* colour) {
    const double advanced = static_cast<double>(dt) * 5.0 /*007403F0*/ + turn->glow;
    const double pi = 3.141592653589793 /*007402B8*/;
    const double phase = libcrt::CiFmod(advanced, pi + pi);
    turn->glow = static_cast<float>(phase);
    const double strength = (x87::fsin(phase) + 1.0 /*0073BAC8*/) * 0.15000000596046448 /*007403E8*/;
    const auto channel = [&](int i) { return gamecrt::Ftol(strength * colour[i] * 255.0f /*0073C370*/); };
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
    // The angle is stored (FST) and compared unrounded with 2 pi (FCOMP; TEST AH,0x41: wraps while
    // above, not when equal or unordered).
    double angle = static_cast<double>(dt) * turn->speed + turn->angle;
    turn->angle = static_cast<float>(angle);
    while (angle > GameFloat(0x007C7D20)) {
        angle = static_cast<double>(turn->angle) - GameFloat(0x007C7D20);
        turn->angle = static_cast<float>(angle);
    }
}

// The rotation about `axis` by pi (the float at 007402C0).
Quaternion HalfTurn(float ax, float ay, float az) {
    const Vector3 axis = {ax, ay, az};
    Quaternion q;
    q.SetAxisAngle(axis, 3.1415927f /*007402C0*/);
    return q;
}

// The two products the original inlines (a * b), each with its own order of the w sum.
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

// A loot item that stands for a database record (kind 2) of `recordType` (0 weapon .. 4 spell
// modifier).
bool IsRecordLoot(const CItem* item, std::uint32_t recordType) {
    if (item->type_ != kItemLoot) return false;
    const auto* loot = AsLoot(item);
    return loot->kind_ == 2 && loot->recordType_ == recordType;
}

// Spell records, scrolls and spell containers lie flat (turned about z).
bool LiesFlat(CItem* item) {
    if (item->type_ == kItemSpell) return true;
    return IsRecordLoot(item, 3) || IsRecordLoot(item, 4);
}

void SetShade(CUiMesh* sprite, float grey) {
    sprite->tint_[0] = grey;
    sprite->tint_[1] = grey;
    sprite->tint_[2] = grey;
}

// The renderer's Direct3D device (007AF080 +0x54): SetRenderState (slot 20).
void SetSpecular(int on) {
    device::SetRenderState(device::D3D(TheRenderDevice()), D3DRENDERSTATE_SPECULARENABLE, static_cast<DWORD>(on));
}

} // namespace

// --- 005ECA10 -----------------------------------------------------------------------------------------

void CItemScreen::DrawItem(int pass, ItemCell* cell) {
    CItem* item = cell->item;
    CUiMesh* model;
    {
        mfc::CString name;
        ItemName(item, 20, &name);
        model = static_cast<CUiMesh*>(CachedObject(&models_, name, false));
    }
    CEngineObject* texture;
    {
        mfc::CString name;
        ItemName(item, 19, &name);
        texture = static_cast<CEngineObject*>(CachedObject(&textures_, name, true));
    }
    if (!model || !texture) return;

    model->texture_ = texture;
    model->Place(cell->x, cell->y, cell->depth);
    model->SetScale(static_cast<float>(D(0.48157462f /*007402C4*/) * D(cell->size) * D(cell->depth) * D(0.0009375f /*00740408*/)));
    model->material_ = blackMaterial_;

    if (ItemTurnOf(item) == nullptr) {
        // A new turning state with a random glow phase. The original writes the count before
        // checking the allocation.
        auto* turn = gamecrt::operator_new_as<ItemTurn>();
        reinterpret_cast<volatile int*>(turn)[1] = 0;
        if (turn) turn->vtable = ItemTurn::Vtable();
        turn->angle = 0.0f;
        turn->speed = 0.0f;
        turn->flag = false;
        std::uint32_t word = RandomNext(&IsaacRandom::Global());
        turn->glow = RandomPhase(word);
        RefPtrAssign(&item->link_, nullptr, turn);   // the item's RefPtr slot
    }

    if (cell->mode == 9) {
        // Greyed: half bright, lying still, scaled to fit the cell by the model's larger extent.
        SetShade(model, 0.5f);
        SetOrientation(model, LiesFlat(item) ? HalfTurn(0.0f, 0.0f, 1.0f) : HalfTurn(1.0f, 0.0f, 0.0f));
        // The larger of the box's x and y extents: FCOMP of x with y; C0 (below) picks y (a
        // branch-free select of the dwords).
        const Vector3& extents = model->bounds_.boxMax;
        EI_NAN_AUDIT(extents.y, extents.x);
        float extent = extents.x < extents.y ? extents.y : extents.x;
        double half = D(0.5f /*0073B86C*/);
        float y = static_cast<float>(D(model->bounds_.center.y) * D(cell->size) * half + D(cell->y));
        float x = static_cast<float>(D(cell->x) - D(model->bounds_.center.x) * D(cell->size) * half);
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

        // The frame behind the item (texture 2 of the screen), drawn once and removed.
        float frameDepth = static_cast<float>(D(cell->depth) * D(1.3f /*0073FF20*/));
        CUiMesh* frame = AddSprite(0);
        double half = D(35.0f /*007403E0*/);
        frame->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                            static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
        frame->texture_ = objects_.data[2];
        frame->Draw(PassRenderer(pass));
        RemoveSceneObject(frame);
    }

    model->flatColour_ = PackColour(model->tint_);
    model->flags_ |= 0x100;
    if (item->spell_) SetSpecular(1);
    model->Draw(PassRenderer(pass));
    if (item->spell_) SetSpecular(0);

    // A spell container's modifiers: a mark per modifier around the model (eight places).
    CSpell* spell = item->spell_;
    if (item->type_ == kItemSpell && spell && spell->GetModifierCount() > 0) {
        float px, py, pz;
        model->GetPlacement(&px, &py, &pz);
        float scale = model->GetScaleX();
        CUiMesh* mark = AddSprite(0);
        mark->SetRectangle(static_cast<float>(D(pz) * D(0.9f /*0073CD70*/)), 100.0f, 0.0f, 0.0f, 35.0f, 35.0f, 0, 0,
                           0x20, 0x20);
        mark->texture_ = objects_.data[1];
        mark->SetScale(scale);
        SetShade(mark, cell->mode == 9 ? 0.5f : 1.0f);
        float step = static_cast<float>(D(scale) * D(550.0f /*007403DC*/));
        for (int i = 0; i < spell->GetModifierCount(); ++i) {
            int code = spell->modifiers_[i] - 1;   // the modifier's index (+0x54: index + 1 each)
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
            mark->Draw(PassRenderer(pass));
        }
        RemoveSceneObject(mark);
    }

    // Items the strip's mode cannot take get a dark square in front.
    using CanEquipFn = bool(__thiscall*)(CItemScreen*, CItem*, int);
    if (cell->mode != 9 && !ei::VirtualSlot<CanEquipFn>(vtable_, 43)(this, item, cell->mode)) {
        float depth = static_cast<float>(D(cell->depth) * D(1.25f /*0073CF50*/));
        if (LiesFlat(item)) depth = static_cast<float>(D(cell->depth) * D(0.8f /*0073CCA8*/));
        CUiMesh* shade = AddSprite(0);
        double half = D(35.0f /*007403E0*/);
        shade->SetRectangle(depth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                            static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0, 0, 0x100, 0x100);
        shade->texture_ = nullptr;
        shade->flags_ |= 0x100;
        shade->flatColour_ = 0x82804040;
        shade->Draw(PassRenderer(pass));
        RemoveSceneObject(shade);
    }
}
EI_REWRITTEN(0x005ECA10, CItemScreen::DrawItem);

// --- 005EE5C0: the description panel's slot 20 --------------------------------------------------------

BOOL CItemInfoPanel::Draw(int pass) {
    if (itemCount_ && items_[0]) {
        ItemCell cell;
        cell.item = items_[0];
        cell.x = static_cast<float>(static_cast<double>(x_) + D(100.0f /*0073BAB0*/));   // + 100
        cell.depth = 3.0f;
        cell.size = 120.0f;
        cell.mode = 9;
        cell.y = static_cast<float>(static_cast<double>(y_) + D(200.0f /*0073CF58*/));   // + 200
        static_cast<CItemScreen*>(owner_)->DrawItem(pass, &cell);   // through its address, so the wrapper sees the call
    }
    return 1;
}

EI_REPLACE_METHOD(0x005EE5C0, CItemInfoPanel::Draw);

// --- 005ED7B0: a hero's face ------------------------------------------------------------------------

Quaternion* __fastcall MultiplyQuaternions(Quaternion*, const Quaternion*, const Quaternion*);
namespace {


// The inlined strcmp(a, b) == 0.
bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

} // namespace

void CItemScreen::FaceNames(const char* prototype, mfc::CString* model, mfc::CString* texture) {
    UnitPrototypeRecord* unit;
    {
        stlp::string key(prototype);
        unit = unitdb::PrototypeTable().byName.Subscript(key);   // an unknown name gets a null entry
    }
    UnitModelRecord* figure;
    {
        stlp::string key(unit->modelName.GetString());
        figure = unitdb::ModelNamed(key);
    }
    mfc::CString sex;
    const char* figureModel = figure->figureName;
    const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes (005ED889): the model's fifth character
    if (fixes ? !pluginfixes::ModelIsFemale(figureModel) : SameText(figureModel, "unhuma"))
        sex = "m";
    else if (fixes || SameText(figureModel, "unhufe"))
        sex = "f";
    model->Format("infa%sme%dface", figureModel, unit->hair + 1);
    texture->Format("face%s%02d", sex.GetString(), unit->skin);
}

void CItemScreen::DrawHeroFace(int pass, HeroFaceCell* cell) {
    mfc::CString modelName, textureName;
    FaceNames(cell->prototype, &modelName, &textureName);
    auto* model = static_cast<CUiMesh*>(CachedObject(&models_, modelName, false));
    auto* texture = static_cast<CEngineObject*>(CachedObject(&textures_, textureName, true));
    if (!model || !texture) return;

    model->texture_ = texture;
    model->Place(cell->x, cell->y, cell->depth);
    model->SetScale(static_cast<float>(D(cell->size) * D(cell->depth) * D(0.48157462f /*007402C4*/) * D(0.0009375f /*00740408*/)));
    model->material_ = blackMaterial_;
    SetShade(model, 1.0f);
    // A half turn about x, then tilted about -x by (y - 300) / 1000.
    Quaternion turn = HalfTurn(1.0f, 0.0f, 0.0f);
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    tilt.SetAxisAngle(axis, static_cast<float>((D(cell->y) - D(300.0f /*0073FA98*/)) * D(0.001f /*0074040C*/)));
    Quaternion orientation;
    SetOrientation(model, *MultiplyQuaternions(&orientation, &tilt, &turn));

    // The frame behind the face (texture 2 of the screen), drawn once and removed, then the face.
    float frameDepth = static_cast<float>(D(cell->depth) * D(1.3f /*0073FF20*/));
    CUiMesh* frame = AddSprite(0);
    double half = D(35.0f /*007403E0*/);
    frame->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                        static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
    frame->texture_ = objects_.data[2];
    frame->Draw(PassRenderer(pass));
    RemoveSceneObject(frame);
    model->Draw(PassRenderer(pass));
}
EI_REPLACE_METHOD(0x005ED7B0, CItemScreen::DrawHeroFace);
// EI_Plugin.asi patches the sex test of the face's picture (005ED889) in a Rebirth launch.

// --- 005EC020: an item's figure texture -------------------------------------------------------------

namespace {

void* PixelDecoder() { return ei::GameData<void* const>(0x007AF06C); }


// 004800E0 (cdecl): a fatal DirectX error; what it returns decides whether the caller goes on.

template <typename... Args>
void VirtualCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    reinterpret_cast<void(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

constexpr std::uint32_t kPnt3 = 0x33544E50;   // "PNT3"

// The texture's pixels for one fill: the texture manager's textures have only the new renderer's
// memory (00691D80, ticket R8), written directly (DirectPixels; the original: a Lock with flags 0
// and the Unlock after). 0, or the error the original's report gets when there is no memory (none
// is made without it).
long LockTexture(CDXSurface* holder, SurfacePixels* pixels) {
    return DirectPixels(holder, pixels) ? 0 : E_FAIL;
}

} // namespace

void* __fastcall ItemFigureTexture(const char* name) {
    CTextureManager* manager = TheTextureManager();
    CTexture* texture = manager->NewTexture(0x100, 0x100, 0x20);
    if (!texture) return nullptr;
    ++texture->refCount_;
    CDXSurface* holder = texture->surface_;
    const MmpHeader* image;
    {
        mfc::CString base(name);
        mfc::CString file = base + ".mmp";
        image = static_cast<const MmpHeader*>(manager->GetFileData(file));
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
        // The image decoded to 32-bit pixels (decoder slot 5: out, image data, count, PNT3).
        std::uint32_t count = static_cast<std::uint32_t>(height) * static_cast<std::uint32_t>(width);
        pixels = gamecrt::operator_new_array<std::uint32_t>(count);
        std::memset(pixels, 0, count * 4);
        void* decoder = PixelDecoder();
        VirtualCall(decoder, 5, pixels, static_cast<const void*>(image + 1), count, image->format == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(TheDeviceLock());
    SurfacePixels target;
    long hr = LockTexture(holder, &target);
    if (hr != 0 && app::FatalErrorCode(hr, "Can't lock surface.\n") == 0) {   // "Can't lock surface"
        // The original leaves the device lock held here.
        if (pixels) gamecrt::operator_delete(pixels);
        texture->Release();
        return nullptr;
    }
    // Rows of 0x200 bytes, the image at the bottom.
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
    if (pixels) gamecrt::operator_delete(pixels);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    VirtualCall(manager, 22);
    return texture;
}
EI_REWRITTEN(0x005EC020, ItemFigureTexture);

// --- 005FFFB0: an image into part of a texture ------------------------------------------------------

int CUiWindow::CopyImage(CEngineObject* texture, const char* name, int imageWidth, int imageHeight, int width,
                         int height, int x, int y, int, int) {
    CDXSurface* holder = static_cast<CTexture*>(texture)->surface_;
    const MmpHeader* image;
    {
        mfc::CString base(name);
        mfc::CString file = base + ".mmp";
        image = static_cast<const MmpHeader*>(TheTextureManager()->GetFileData(file));
    }
    std::uint32_t* pixels = nullptr;
    if (image) {
        std::uint32_t count = static_cast<std::uint32_t>(imageWidth) * static_cast<std::uint32_t>(imageHeight);
        pixels = gamecrt::operator_new_array<std::uint32_t>(count);
        std::memset(pixels, 0, count * 4);
        // The device's decoder, slot 5: out, image data, count, PNT3.
        void* decoder = interface_->renderer_->decoder_;
        VirtualCall(decoder, 5, pixels, static_cast<const void*>(image + 1), count, image->format == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(TheDeviceLock());
    SurfacePixels target;
    long hr = LockTexture(holder, &target);
    if (hr != 0 && app::FatalErrorCode(hr, "Can't lock surface.\n") == 0) {   // "Can't lock surface"
        // The original leaves the device lock held here.
        if (pixels) gamecrt::operator_delete(pixels);
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
    if (pixels) gamecrt::operator_delete(pixels);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    VirtualCall(TheTextureManager(), 22);
    return image != nullptr;
}

EI_REWRITTEN(0x005FFFB0, CUiWindow::CopyImage);
