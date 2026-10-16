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

#include "dx_surface.h"
#include "item.h"
#include "random.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/vecmath.h"
#include "render/render_state.h"
#include "ei/x87.h"
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

#include <cstdint>
#include <cstring>
#include <bit>

namespace {

inline double D(float f) { return f; }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
template <typename T>
void Set(void* p, int offset, T v) {
    std::memcpy(static_cast<std::uint8_t*>(p) + offset, &v, sizeof(T));
}
void CopyDword(void* p, int offset, std::uintptr_t source) {
    std::memcpy(static_cast<std::uint8_t*>(p) + offset, reinterpret_cast<const void*>(source), 4);
}

// The game's _ftol of a float times the float at `scale`, the product unrounded.
int ScaledFtol(float value, std::uintptr_t scale) { return static_cast<int>(gamecrt::Ftol(static_cast<double>(value) * GameFloat(scale))); }

// A colour 0xFFrrggbb from three floats in 0..1 (each times 255, truncated).
std::uint32_t PackColour(const float* rgb) {
    std::uint32_t r = static_cast<std::uint32_t>(ScaledFtol(rgb[0], 0x0073C370));
    std::uint32_t g = static_cast<std::uint32_t>(ScaledFtol(rgb[1], 0x0073C370));
    std::uint32_t b = static_cast<std::uint32_t>(ScaledFtol(rgb[2], 0x0073C370));
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// A sprite of the interface (CUiMesh) as the model view uses it (thiscall).
struct Sprite : SpritePositionOriginal {
    void Place(float x, float y, float depth);   // 005E6FA0 (Rebirth-patched)
    void SetScale(float scale);                  // 005E7060 (+0x23C, +0x250, +0x264)
    double Scale();                              // 005E70B0 (+0x23C, in ST0)
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                      int u1, int v1);           // 005E70E0 (Rebirth-patched)
    void Draw(int pass);                         // 005E6D90
};
void Sprite::Place(float a0, float a1, float a2) { SpritePlaceOriginal((void*)this, nullptr, a0, a1, a2); }
void Sprite::SetScale(float a0) { SpriteSetScaleOriginal((void*)this, nullptr, std::bit_cast<std::uint32_t>(a0)); }
double Sprite::Scale() { return SpriteScaleOriginal((void*)this); }
void Sprite::SetRectangle(float a0, float a1, float a2, float a3, float a4, float a5, int a6, int a7, int a8, int a9) { UiMeshRectangleOriginal((void*)this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); }
void Sprite::Draw(int pass) { SpriteDrawOriginal(this, nullptr, pass); }
Sprite* AsSprite(void* p) { return static_cast<Sprite*>(p); }

// The caches' STLport map (thiscall on the cache; library code): find into `out` (RET 8; the
// model and the texture cache's own instantiations) and operator[] (RET 4; the value {object,
// time}). Their trimming is game code, not rewritten.
__declspec(naked) ItemObjectCache::Node** __fastcall CacheFind(ItemObjectCache*, void*, ItemObjectCache::Node** /*out*/,
                                                               const stlp::string* /*key*/) EI_ORIGINAL(0x005F3730)
__declspec(naked) ItemObjectCache::Node** __fastcall CacheFindTexture(ItemObjectCache*, void*,
                                                                      ItemObjectCache::Node** /*out*/,
                                                                      const stlp::string* /*key*/) EI_ORIGINAL(0x005F3B50)
__declspec(naked) void** __fastcall CacheSlot(ItemObjectCache*, void*, const stlp::string* /*key*/) EI_ORIGINAL(0x005F3000)
__declspec(naked) void** __fastcall CacheTextureSlot(ItemObjectCache*, void*, const stlp::string* /*key*/)
    EI_ORIGINAL(0x005F3170)

// 005EBFB0 / 005EC020 (__fastcall): an item's model (counted) and its figure texture, or null.

// The item's names (virtual slots 19 and 20; return a CString through the hidden pointer).
void ItemName(CItem* item, int slot, mfc::CString* out) {
    using NameFn = mfc::CString*(__thiscall*)(CItem*, mfc::CString*);
    reinterpret_cast<NameFn>((*reinterpret_cast<void***>(item))[slot])(item, out);
}

// The cached object for `name` (its use time refreshed), or a new one put in the cache, which
// then drops its least recently used entries beyond its limit. Null when it cannot be made.
// `textures` picks the texture cache's own instantiations (they behave the same).
void* CachedObject(ItemObjectCache* cache, const char* name, bool textures) {
    ItemObjectCache::Node* node;
    {
        stlp::string key(name);
        if (textures)
            CacheFindTexture(cache, nullptr, &node, &key);
        else
            CacheFind(cache, nullptr, &node, &key);
    }
    if (node != cache->header) {
        node->lastUse = gameapi::timeGetTime();
        return node->object;
    }
    void* object = textures ? ItemFigureTexture(name) : ItemModel(name);
    if (!object) return nullptr;
    std::uint32_t time = gameapi::timeGetTime();
    {
        stlp::string key(name);
        void** value = textures ? CacheTextureSlot(cache, nullptr, &key) : CacheSlot(cache, nullptr, &key);
        value[0] = object;
        value[1] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(time));
    }
    if (textures)
        TrimItemTextures(cache);
    else
        TrimItemModels(cache);
    return object;
}

// A new turning state with a random glow phase: word * 2 pi (as float) * 2^-32 (FILD of the
// unsigned word; FMUL float; FMUL double; FSTP float).
float RandomPhase(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * GameFloat(0x007C7D20) * GameDouble(0x0073C468));
}

// The spell glow: the phase advances by dt * 5 and wraps at 2 pi (_CIfmod); its sine scales the
// school's colour (00740358, 16 bytes per school) into 0xFFrrggbb. The phase is stored as float
// but the sine and the colour use the unrounded value, as the original.
std::uint32_t Glow(float dt, ItemTurn* turn, const float* colour) {
    const double advanced = static_cast<double>(dt) * GameDouble(0x007403F0) + turn->glow;
    const double pi = GameDouble(0x007402B8);
    const double phase = libcrt::CiFmod(advanced, pi + pi);
    turn->glow = static_cast<float>(phase);
    const double strength = (x87::fsin(phase) + GameDouble(0x0073BAC8)) * GameDouble(0x007403E8);
    const auto channel = [&](int i) { return gamecrt::Ftol(strength * colour[i] * GameFloat(0x0073C370)); };
    const std::uint32_t r = channel(0);
    const std::uint32_t g = channel(1);
    const std::uint32_t b = channel(2);
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The turn: on the hovered item the speed grows by 8 per second; elsewhere it brakes towards the
// next full turn (4 * (2 pi - angle)), or stops below 0.1. At most 5. The angle advances by
// dt * speed and wraps at 2 pi (compared unrounded, stored as float).
void Turn(ItemTurn* turn, float dt, bool hovered) {
    if (hovered) {
        turn->speed = static_cast<float>(D(GameFloat(0x007402F4)) * D(dt) + D(turn->speed));
    } else if (!(turn->speed >= GameFloat(0x007402F0))) {   // FCOMP; TEST AH,1: below or unordered
        turn->angle = 0.0f;
        turn->speed = 0.0f;
    } else {
        double pi = D(GameFloat(0x007402C0));
        turn->speed = static_cast<float>((pi + pi - D(turn->angle)) * D(GameFloat(0x0073BAA0)));
    }
    if (turn->speed > GameFloat(0x007402EC)) std::memcpy(&turn->speed, reinterpret_cast<const void*>(0x007402EC), 4);
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
    float pi;
    std::memcpy(&pi, reinterpret_cast<const void*>(0x007402C0), 4);
    q.SetAxisAngle(axis, pi);
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

void SetOrientation(std::uint8_t* model, const Quaternion& q) { std::memcpy(model + 0x34, &q, sizeof(q)); }

// Spell records, scrolls and spell containers lie flat (turned about z); kinds by +0x10, +0x4C, +0x50.
bool LiesFlat(CItem* item) {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(item);
    int kind = Get<int>(p, 0x10);
    if (kind == 0x3008) return true;
    if (kind == 0x3007 && Get<int>(p, 0x4C) == 2 && Get<int>(p, 0x50) == 3) return true;
    return kind == 0x3007 && Get<int>(p, 0x4C) == 2 && Get<int>(p, 0x50) == 4;
}

void SetShade(std::uint8_t* sprite, std::uint32_t grey) {
    Set(sprite, 0x224, grey);
    Set(sprite, 0x228, grey);
    Set(sprite, 0x22C, grey);
}

// The renderer's Direct3D device (UiRenderer 007AF080 +0x54): SetRenderState (slot 20).
void SetSpecular(int on) {
    auto* renderer = *reinterpret_cast<std::uint8_t**>(0x007AF080);
    void* device = Get<void*>(renderer, 0x54);
    using SetStateFn = long(__stdcall*)(void*, int, int);
    reinterpret_cast<SetStateFn>((*static_cast<void***>(device))[20])(device, 0x1D, on);
    render::state::Render(0x1D, static_cast<std::uint32_t>(on));
}

} // namespace

// --- 005ECA10 -----------------------------------------------------------------------------------------

void CItemScreen::DrawItem(int pass, ItemCell* cell) {
    CItem* item = cell->item;
    std::uint8_t* model;
    {
        mfc::CString name;
        ItemName(item, 20, &name);
        model = static_cast<std::uint8_t*>(CachedObject(&models_, name, false));
    }
    void* texture;
    {
        mfc::CString name;
        ItemName(item, 19, &name);
        texture = CachedObject(&textures_, name, true);
    }
    if (!model || !texture) return;

    Set(model, 0x280, texture);
    AsSprite(model)->Place(cell->x, cell->y, cell->depth);
    AsSprite(model)->SetScale(static_cast<float>(D(GameFloat(0x007402C4)) * D(cell->size) * D(cell->depth) * D(GameFloat(0x00740408))));
    Set(model, 0x27C, Get<std::uint32_t>(this, 0xEC));

    std::uint8_t* itemBytes = reinterpret_cast<std::uint8_t*>(item);
    if (Get<void*>(itemBytes, 0x1C) == nullptr) {
        // A new turning state with a random glow phase. The original writes the count before
        // checking the allocation.
        auto* turn = static_cast<ItemTurn*>(gamecrt::operator_new(sizeof(ItemTurn)));
        reinterpret_cast<volatile int*>(turn)[1] = 0;
        if (turn) turn->vtable = reinterpret_cast<void*>(0x007403F8);
        turn->angle = 0.0f;
        turn->speed = 0.0f;
        turn->flag = false;
        std::uint32_t word = RandomNext(&IsaacRandom::Global());
        turn->glow = RandomPhase(word);
        RefPtrAssign(itemBytes + 0x1C, nullptr, turn);   // the item's RefPtr slot
    }

    if (cell->mode == 9) {
        // Greyed: half bright, lying still, scaled to fit the cell by the model's larger extent.
        SetShade(model, 0x3F000000);
        SetOrientation(model, LiesFlat(item) ? HalfTurn(0.0f, 0.0f, 1.0f) : HalfTurn(1.0f, 0.0f, 0.0f));
        // The larger of +0x1A8 and +0x1AC: FCOMP of +0x1A8 with +0x1AC; C0 (below or unordered)
        // picks +0x1AC (a branch-free select of the dwords).
        float a = Get<float>(model, 0x1AC);
        float b = Get<float>(model, 0x1A8);
        std::uint32_t bits = (b < a || x87::IsNaN(a) || x87::IsNaN(b)) ? Get<std::uint32_t>(model, 0x1AC)
                                                                        : Get<std::uint32_t>(model, 0x1A8);
        float extent;
        std::memcpy(&extent, &bits, sizeof(extent));
        double half = D(GameFloat(0x0073B86C));
        float y = static_cast<float>(D(Get<float>(model, 0x190)) * D(cell->size) * half + D(cell->y));
        float x = static_cast<float>(D(cell->x) - D(Get<float>(model, 0x18C)) * D(cell->size) * half);
        AsSprite(model)->Place(x, y, cell->depth);
        AsSprite(model)->SetScale(static_cast<float>(D(GameFloat(0x007402C4)) * D(cell->size) * D(cell->depth) *
                                        D(GameFloat(0x00740164)) / D(extent)));
    } else {
        SetShade(model, 0x3F800000);
        float dt = static_cast<float>(reinterpret_cast<CUiContainer*>(this)->FrameTime());
        auto* turn = Get<ItemTurn*>(itemBytes, 0x1C);
        int kind = Get<int>(itemBytes, 0x10);
        void* spell = Get<void*>(itemBytes, 0x44);
        if (spell && (kind == 0x3004 || kind == 0x3005)) {
            int school = Get<int>(reinterpret_cast<CSpell*>(spell)->GetPrototype(), 0x14);
            if (school < 8)
                Set(model, 0xD8, Glow(dt, turn, reinterpret_cast<const float*>(0x00740358 + school * 0x10)));
            else
                turn->glow = 0.0f;
        }
        Turn(turn, dt, item == hoveredItem_);

        if (LiesFlat(item)) {
            SetOrientation(model, HalfTurn(0.0f, 0.0f, 1.0f));
        } else if (kind == 0x3004) {
            // Weapons turn about (1, 1, 0) after a half turn about x.
            Quaternion a = HalfTurn(1.0f, 0.0f, 0.0f);
            const Vector3 axis = {1.0f, 1.0f, 0.0f};
            Quaternion b;
            b.SetAxisAngle(axis, turn->angle);
            SetOrientation(model, ProductA(a, b));
        } else if (Get<int>(itemBytes, 0x10) == 0x3007 && Get<int>(itemBytes, 0x4C) == 2 &&
                   (Get<int>(itemBytes, 0x50) == 0 || Get<int>(itemBytes, 0x50) == 1 || Get<int>(itemBytes, 0x50) == 2)) {
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
        float frameDepth = static_cast<float>(D(cell->depth) * D(GameFloat(0x0073FF20)));
        CUiMesh* frame = AddSprite(0);
        double half = D(GameFloat(0x007403E0));
        AsSprite(frame)->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                                      static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
        Set(frame, 0x280, objects_.data[2]);
        AsSprite(frame)->Draw(pass);
        RemoveSceneObject(reinterpret_cast<CEngineObject*>(frame));
    }

    Set(model, 0xD4, PackColour(reinterpret_cast<const float*>(model + 0x224)));
    Set(model, 0x08, Get<std::uint32_t>(model, 0x08) | 0x100);
    if (Get<void*>(itemBytes, 0x44)) SetSpecular(1);
    AsSprite(model)->Draw(pass);
    if (Get<void*>(itemBytes, 0x44)) SetSpecular(0);

    // A spell container's modifiers: a mark per modifier around the model (eight places).
    void* spell = Get<void*>(itemBytes, 0x44);
    if (Get<int>(itemBytes, 0x10) == 0x3008 && spell && reinterpret_cast<CSpell*>(spell)->GetModifierCount() > 0) {
        float px, py, pz;
        AsSprite(model)->Position(&px, &py, &pz);
        float scale = static_cast<float>(AsSprite(model)->Scale());
        CUiMesh* mark = AddSprite(0);
        AsSprite(mark)->SetRectangle(static_cast<float>(D(pz) * D(GameFloat(0x0073CD70))), 100.0f, 0.0f, 0.0f, 35.0f, 35.0f, 0, 0,
                                     0x20, 0x20);
        Set(mark, 0x280, objects_.data[1]);
        AsSprite(mark)->SetScale(scale);
        SetShade(reinterpret_cast<std::uint8_t*>(mark), cell->mode == 9 ? 0x3F000000 : 0x3F800000);
        float step = static_cast<float>(D(scale) * D(GameFloat(0x007403DC)));
        for (int i = 0; i < reinterpret_cast<CSpell*>(spell)->GetModifierCount(); ++i) {
            int code = static_cast<signed char>(Get<std::uint8_t>(spell, 0x54 + i)) - 1;
            double cellSize = D(GameFloat(0x0073C520));
            Set(mark, 0xCC, static_cast<float>(static_cast<double>(code & 7) * cellSize));
            Set(mark, 0xD0, static_cast<float>(static_cast<double>(-(code >> 3)) * cellSize));
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
                dy = D(GameFloat(0x0073B858));
            else if (i == 0 || i == 1 || i == 7)
                dy = -d;
            else
                dy = d;
            float depth = static_cast<float>(D(pz) * D(GameFloat(0x0073CD70)));
            AsSprite(mark)->Place(static_cast<float>(D(dx) + D(px)), static_cast<float>(dy + D(py)), depth);
            AsSprite(mark)->Draw(pass);
        }
        RemoveSceneObject(reinterpret_cast<CEngineObject*>(mark));
    }

    // Items the strip's mode cannot take get a dark square in front.
    using CanEquipFn = bool(__thiscall*)(CItemScreen*, CItem*, int);
    if (cell->mode != 9 && !reinterpret_cast<CanEquipFn>(vtable_[43])(this, item, cell->mode)) {
        float depth = static_cast<float>(D(cell->depth) * D(GameFloat(0x0073CF50)));
        if (LiesFlat(item)) depth = static_cast<float>(D(cell->depth) * D(GameFloat(0x0073CCA8)));
        CUiMesh* shade = AddSprite(0);
        double half = D(GameFloat(0x007403E0));
        AsSprite(shade)->SetRectangle(depth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                                      static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0, 0, 0x100, 0x100);
        Set<std::uint32_t>(shade, 0x280, 0);
        Set(shade, 0x08, Get<std::uint32_t>(shade, 0x08) | 0x100);
        Set<std::uint32_t>(shade, 0xD4, 0x82804040);
        AsSprite(shade)->Draw(pass);
        RemoveSceneObject(reinterpret_cast<CEngineObject*>(shade));
    }
}
EI_REPLACE_METHOD(0x005ECA10, CItemScreen::DrawItem);

// --- 005EE5C0: the description panel's slot 20 --------------------------------------------------------

BOOL CItemInfoPanel::Draw(int pass) {
    if (itemCount_ && items_[0]) {
        ItemCell cell;
        cell.item = items_[0];
        cell.x = static_cast<float>(static_cast<double>(x_) + D(GameFloat(0x0073BAB0)));   // + 100
        cell.depth = 3.0f;
        cell.size = 120.0f;
        cell.mode = 9;
        cell.y = static_cast<float>(static_cast<double>(y_) + D(GameFloat(0x0073CF58)));   // + 200
        reinterpret_cast<CItemScreen*>(owner_)->DrawItem(pass, &cell);   // through its address, so the wrapper sees the call
    }
    return 1;
}

EI_REPLACE_METHOD(0x005EE5C0, CItemInfoPanel::Draw);

// --- 005ED7B0: a hero's face ------------------------------------------------------------------------

Quaternion* __fastcall MultiplyQuaternions(Quaternion*, const Quaternion*, const Quaternion*);
namespace {

// hash_map<string, record*>::operator[] of the unit prototypes (007AF364, 005335E0) and of the
// figure prototypes (007AF3A4, 0053C780); both insert a null entry for an unknown name.
char** FigurePrototypeAt(void* map, void*, const stlp::string* key) { return FigureMapAtOriginal(map, nullptr, key); }


// The inlined strcmp(a, b) == 0.
bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

} // namespace

void CItemScreen::FaceNames(const char* prototype, mfc::CString* model, mfc::CString* texture) {
    char* unit;
    {
        stlp::string key(prototype);
        unit = static_cast<char*>(*PrototypeMapAtOriginal(reinterpret_cast<void*>(0x007AF364), nullptr, &key));
    }
    char* figure;
    {
        stlp::string key(Get<const char*>(unit, 4));
        figure = *FigurePrototypeAt(reinterpret_cast<void*>(0x007AF3A4), nullptr, &key);
    }
    mfc::CString sex;
    const char* figureModel = Get<const char*>(figure, 0xE8);
    const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes (005ED889): the model's fifth character
    if (fixes ? !pluginfixes::ModelIsFemale(figureModel) : SameText(figureModel, game::Text(0x007908C8)))   // "unhuma"
        sex = game::Text(0x00792EAC);                                   // "m"
    else if (fixes || SameText(figureModel, game::Text(0x00792EA4)))     // "unhufe"
        sex = game::Text(0x0078BBD8);                                   // "f"
    model->Format(game::Text(0x00792E94), figureModel, Get<int>(unit, 0x10) + 1);         // "infa%sme%dface"
    texture->Format(game::Text(0x00792E88), static_cast<const char*>(sex), Get<int>(unit, 0x0C));   // "face%s%02d"
}

void CItemScreen::DrawHeroFace(int pass, HeroFaceCell* cell) {
    mfc::CString modelName, textureName;
    FaceNames(cell->prototype, &modelName, &textureName);
    auto* model = static_cast<std::uint8_t*>(CachedObject(&models_, modelName, false));
    void* texture = CachedObject(&textures_, textureName, true);
    if (!model || !texture) return;

    Set(model, 0x280, texture);
    AsSprite(model)->Place(cell->x, cell->y, cell->depth);
    AsSprite(model)->SetScale(static_cast<float>(D(cell->size) * D(cell->depth) * D(GameFloat(0x007402C4)) * D(GameFloat(0x00740408))));
    Set(model, 0x27C, Get<std::uint32_t>(this, 0xEC));
    Set(model, 0x224, 1.0f);
    Set(model, 0x228, 1.0f);
    Set(model, 0x22C, 1.0f);
    // A half turn about x, then tilted about -x by (y - 300) / 1000.
    Quaternion turn = HalfTurn(1.0f, 0.0f, 0.0f);
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    tilt.SetAxisAngle(axis, static_cast<float>((D(cell->y) - D(GameFloat(0x0073FA98))) * D(GameFloat(0x0074040C))));
    Quaternion orientation;
    SetOrientation(model, *MultiplyQuaternions(&orientation, &tilt, &turn));

    // The frame behind the face (texture 2 of the screen), drawn once and removed, then the face.
    float frameDepth = static_cast<float>(D(cell->depth) * D(GameFloat(0x0073FF20)));
    CUiMesh* frame = AddSprite(0);
    double half = D(GameFloat(0x007403E0));
    AsSprite(frame)->SetRectangle(frameDepth, 100.0f, static_cast<float>(D(cell->x) - half), static_cast<float>(D(cell->y) - half),
                                  static_cast<float>(D(cell->x) + half), static_cast<float>(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
    Set(frame, 0x280, objects_.data[2]);
    AsSprite(frame)->Draw(pass);
    RemoveSceneObject(reinterpret_cast<CEngineObject*>(frame));
    AsSprite(model)->Draw(pass);
}
EI_REPLACE_METHOD(0x005ED7B0, CItemScreen::DrawHeroFace);
// EI_Plugin.asi patches the sex test of the face's picture (005ED889) in a Rebirth launch.

// --- 005EC020: an item's figure texture -------------------------------------------------------------

namespace {

CTextureManager* TextureManager() { return *reinterpret_cast<CTextureManager* const*>(0x007AF744); }
void* PixelDecoder() { return *reinterpret_cast<void* const*>(0x007AF06C); }

// The texture manager (thiscall): 00691D80 makes a texture (width, height, format), 00691CF0 finds
// a loaded image by file name.
std::uint8_t* NewManagedTexture(void* manager, void*, int width, int height, int kind) {
    return NewManagedTextureOriginal(manager, nullptr, width, height, kind);
}

// 004800E0 (cdecl): a fatal DirectX error; what it returns decides whether the caller goes on.

template <typename... Args>
void VirtualCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    reinterpret_cast<void(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// What a texture's +0x5C points at: the counted holder of its DirectDraw surface.
struct TextureSurface {
    void* vtable;
    int references;
    long result;                 // +0x08
    std::uint8_t unused_0C[4];
    void** surface;              // +0x10 IDirectDrawSurface7
};

constexpr std::uint32_t kPnt3 = 0x33544E50;   // "PNT3"

// The texture's pixels for one fill into `desc` (a DDSURFACEDESC2 as dwords): in the switch-over
// the texture manager's textures have only the new renderer's memory (00691D80, ticket R8), which
// is written directly (DirectPixels, `direct` set: no Lock / Unlock, no result in the holder);
// otherwise a Lock with flags 0 as the original, its result in the holder's +8.
long LockTexture(TextureSurface* holder, std::uint32_t* desc, bool* direct) {
    *direct = DirectPixels(holder, desc);
    if (*direct) return 0;
    using LockFn = long(__stdcall*)(void**, RECT*, void*, unsigned long, void*);
    long hr = reinterpret_cast<LockFn>((*reinterpret_cast<void***>(holder->surface))[25])(holder->surface, nullptr, desc,
                                                                                           0, nullptr);
    holder->result = hr;
    return hr;
}
long UnlockTexture(TextureSurface* holder, bool direct) {
    if (direct) return 0;
    using UnlockFn = long(__stdcall*)(void**, RECT*);
    long hr = reinterpret_cast<UnlockFn>((*reinterpret_cast<void***>(holder->surface))[32])(holder->surface, nullptr);
    holder->result = hr;
    return hr;
}

} // namespace

void* __fastcall ItemFigureTexture(const char* name) {
    void* manager = TextureManager();
    std::uint8_t* texture = NewManagedTexture(manager, nullptr, 0x100, 0x100, 0x20);
    if (!texture) return nullptr;
    Set(texture, 4, Get<int>(texture, 4) + 1);
    auto* holder = Get<TextureSurface*>(texture, 0x5C);
    std::uint8_t* image;
    {
        mfc::CString base(name);
        mfc::CString file = base + game::Text(0x0078FB4C);   // ".mmp"
        image = static_cast<unsigned char*>(static_cast<CTextureManager*>(manager)->GetFileData(file));
    }
    int width, height;
    if (!image) {
        height = 0x100;
        width = 0x100;
    } else {
        width = Get<int>(image, 4);
        height = Get<int>(image, 8);
    }
    std::uint32_t* pixels = nullptr;
    if (image) {
        // The image decoded to 32-bit pixels (decoder slot 5: out, image data, count, PNT3).
        std::uint32_t count = static_cast<std::uint32_t>(height) * static_cast<std::uint32_t>(width);
        pixels = static_cast<std::uint32_t*>(gamecrt::operator_new(count * 4));
        std::memset(pixels, 0, count * 4);
        void* decoder = PixelDecoder();
        VirtualCall(decoder, 5, pixels, image + 0x4C, count, Get<std::uint32_t>(image, 0x10) == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78));
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    bool direct;
    long hr = LockTexture(holder, desc, &direct);
    if (hr != 0 && app::FatalErrorCode(hr, game::Text(0x0078E30C)) == 0) {   // "Can't lock surface"
        // The original leaves the device lock held here.
        if (pixels) gamecrt::operator_delete(pixels);
        VirtualCall(texture, 4);
        return nullptr;
    }
    // Rows of 0x200 bytes, the image at the bottom.
    std::uint32_t at = desc[9] + (static_cast<std::uint32_t>(0x100 - height) << 9);
    if (!image) {
        for (int row = height; row > 0; --row) {
            std::memset(reinterpret_cast<void*>(at), 0xFF, static_cast<std::uint32_t>(width) * 2);
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
            at = reinterpret_cast<std::uint32_t>(to) + static_cast<std::uint32_t>(0x100 - width) * 2;
        }
    }
    if (pixels) gamecrt::operator_delete(pixels);
    gameapi::LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78));
    hr = UnlockTexture(holder, direct);
    if (hr != 0 && app::FatalErrorCode(hr, game::Text(0x0078E2F4)) == 0) {   // "Can't unlock surface"
        VirtualCall(texture, 4);
        return nullptr;
    }
    VirtualCall(manager, 22);
    return texture;
}
EI_REPLACE(0x005EC020, ItemFigureTexture);

// --- 005FFFB0: an image into part of a texture ------------------------------------------------------

int CUiWindow::CopyImage(CEngineObject* texture, const char* name, int imageWidth, int imageHeight, int width,
                         int height, int x, int y, int, int) {
    auto* holder = Get<TextureSurface*>(texture, 0x5C);
    std::uint8_t* image;
    {
        mfc::CString base(name);
        mfc::CString file = base + game::Text(0x0078FB4C);   // ".mmp"
        image = static_cast<unsigned char*>(TextureManager()->GetFileData(file));
    }
    std::uint32_t* pixels = nullptr;
    if (image) {
        std::uint32_t count = static_cast<std::uint32_t>(imageWidth) * static_cast<std::uint32_t>(imageHeight);
        pixels = static_cast<std::uint32_t*>(gamecrt::operator_new(count * 4));
        std::memset(pixels, 0, count * 4);
        // The device's decoder (interface +8 -> +0x4C), slot 5: out, image data, count, PNT3.
        std::uint8_t* device = Get<std::uint8_t*>(interface_, 8);
        void* decoder = Get<void*>(device, 0x4C);
        VirtualCall(decoder, 5, pixels, image + 0x4C, count, Get<std::uint32_t>(image, 0x10) == kPnt3 ? 1 : 0);
    }
    gameapi::EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78));
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    bool direct;
    long hr = LockTexture(holder, desc, &direct);
    if (hr != 0 && app::FatalErrorCode(hr, game::Text(0x0078E30C)) == 0) {   // "Can't lock surface"
        // The original leaves the device lock held here.
        if (pixels) gamecrt::operator_delete(pixels);
        return 0;
    }
    std::uint32_t at = desc[9] + (static_cast<std::uint32_t>(y) * 0x100 + static_cast<std::uint32_t>(x)) * 2;
    if (!image) {
        for (int row = height; row > 0; --row) {
            std::memset(reinterpret_cast<void*>(at), 0xFF, static_cast<std::uint32_t>(width) * 2);
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
            at = reinterpret_cast<std::uint32_t>(to) + static_cast<std::uint32_t>(0x100 - width) * 2;
            from += imageWidth - width;
        }
    }
    if (pixels) gamecrt::operator_delete(pixels);
    gameapi::LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78));
    hr = UnlockTexture(holder, direct);
    if (hr != 0) app::FatalErrorCode(hr, game::Text(0x0078E2F4));   // "Can't unlock surface"
    VirtualCall(TextureManager(), 22);
    return image != nullptr;
}

EI_REPLACE_METHOD(0x005FFFB0, CUiWindow::CopyImage);
