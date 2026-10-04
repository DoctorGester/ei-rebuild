// The item screens' model view (005ECA10) and the description panel's drawing (005EE5C0). See
// ui_item_info.h and docs/notes/ui.md ("Item descriptions").
//
// Floating point follows the original's x87 code: values the original keeps on the FPU stack stay
// there (inline assembly where a value is stored and still used unrounded, or truncated with the
// game's _ftol), values it stores as float go through Store, products keep their operand order
// (precise floating point), constants are read from the exe's memory.
#include "ui_item_info.h"

#include "item.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

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

// The game's _ftol of a float times the float at `scale`, on the FPU.
int ScaledFtol(float value, std::uintptr_t scale) {
    int result;
    __asm {
        fld value
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}

// A colour 0xFFrrggbb from three floats in 0..1 (each times 255, truncated).
std::uint32_t PackColour(const float* rgb) {
    std::uint32_t r = static_cast<std::uint32_t>(ScaledFtol(rgb[0], 0x0073C370));
    std::uint32_t g = static_cast<std::uint32_t>(ScaledFtol(rgb[1], 0x0073C370));
    std::uint32_t b = static_cast<std::uint32_t>(ScaledFtol(rgb[2], 0x0073C370));
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// A sprite of the interface (CUiMesh) as the model view uses it (thiscall).
struct Sprite {
    void Place(float x, float y, float depth);   // 005E6FA0 (Rebirth-patched)
    void SetScale(float scale);                  // 005E7060 (+0x23C, +0x250, +0x264)
    double Scale();                              // 005E70B0 (+0x23C, in ST0)
    void Position(float* x, float* y, float* depth);   // 005E7010 (layout point and depth)
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                      int u1, int v1);           // 005E70E0 (Rebirth-patched)
    void Draw(int pass);                         // 005E6D90
};
__declspec(naked) void Sprite::Place(float, float, float) EI_ORIGINAL(0x005E6FA0)
__declspec(naked) void Sprite::SetScale(float) EI_ORIGINAL(0x005E7060)
__declspec(naked) double Sprite::Scale() EI_ORIGINAL(0x005E70B0)
__declspec(naked) void Sprite::Position(float*, float*, float*) EI_ORIGINAL(0x005E7010)
__declspec(naked) void Sprite::SetRectangle(float, float, float, float, float, float, int, int, int, int)
    EI_ORIGINAL(0x005E70E0)
__declspec(naked) void Sprite::Draw(int) EI_ORIGINAL(0x005E6D90)
Sprite* AsSprite(void* p) { return static_cast<Sprite*>(p); }

// The caches' STLport map (thiscall; library code) and their trimming (game code, not rewritten).
struct CacheMap {
    ItemObjectCache::Node** Find(ItemObjectCache::Node** out, const stlp::string* key);
    ItemObjectCache::Node** FindTexture(ItemObjectCache::Node** out, const stlp::string* key);
    void** Slot(const stlp::string* key);          // operator[]: the value {object, time}
    void** TextureSlot(const stlp::string* key);
};
__declspec(naked) ItemObjectCache::Node** CacheMap::Find(ItemObjectCache::Node**, const stlp::string*)
    EI_ORIGINAL(0x005F3730)
__declspec(naked) ItemObjectCache::Node** CacheMap::FindTexture(ItemObjectCache::Node**, const stlp::string*)
    EI_ORIGINAL(0x005F3B50)
__declspec(naked) void** CacheMap::Slot(const stlp::string*) EI_ORIGINAL(0x005F3000)
__declspec(naked) void** CacheMap::TextureSlot(const stlp::string*) EI_ORIGINAL(0x005F3170)
__declspec(naked) void __fastcall TrimModels(ItemObjectCache*) EI_ORIGINAL(0x005F2F20)
__declspec(naked) void __fastcall TrimTextures(ItemObjectCache*) EI_ORIGINAL(0x005F2F90)
CacheMap* MapOf(ItemObjectCache* cache) { return reinterpret_cast<CacheMap*>(cache); }

// 005EBFB0 / 005EC020 (__fastcall): an item's model (counted) and its figure texture, or null.
__declspec(naked) void* __fastcall CreateModel(const char*) EI_ORIGINAL(0x005EBFB0)
__declspec(naked) void* __fastcall CreateTexture(const char*) EI_ORIGINAL(0x005EC020)

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
            MapOf(cache)->FindTexture(&node, &key);
        else
            MapOf(cache)->Find(&node, &key);
    }
    if (node != cache->header) {
        node->lastUse = gameapi::timeGetTime();
        return node->object;
    }
    void* object = textures ? CreateTexture(name) : CreateModel(name);
    if (!object) return nullptr;
    std::uint32_t time = gameapi::timeGetTime();
    {
        stlp::string key(name);
        void** value = textures ? MapOf(cache)->TextureSlot(&key) : MapOf(cache)->Slot(&key);
        value[0] = object;
        value[1] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(time));
    }
    if (textures)
        TrimTextures(cache);
    else
        TrimModels(cache);
    return object;
}

// An item's turning state (0x18 bytes, vtable 007403F8, a counted object at the item's +0x1C).
struct ItemTurn {
    void* vtable;
    int references;          // +0x04
    float angle;             // +0x08 radians, wrapped at 007C7D20 (2 pi)
    float speed;             // +0x0C
    bool flag;               // +0x10
    std::uint8_t unknown_11[3];
    float glow;              // +0x14 phase of the spell glow
};
static_assert(sizeof(ItemTurn) == 0x18);

// 00419CD0: RefPtr assignment (thiscall on the item's +0x1C; counts the new object).
struct RefSlot {
    void Assign(void* object);
};
__declspec(naked) void RefSlot::Assign(void*) EI_ORIGINAL(0x00419CD0)

// 004667F0: the game's ISAAC generator (007A9D40), the next word.
struct Isaac {
    std::uint32_t Next();
};
__declspec(naked) std::uint32_t Isaac::Next() EI_ORIGINAL(0x004667F0)

// A new turning state with a random glow phase: word * 2 pi (as float) * 2^-32 (FILD of the
// unsigned word; FMUL float; FMUL double; FSTP float).
float RandomPhase(std::uint32_t random) {
    float result;
    __asm {
        push 0
        push random
        fild qword ptr [esp]
        add esp, 8
        fmul dword ptr ds:[0x007C7D20]
        fmul qword ptr ds:[0x0073C468]
        fstp result
    }
    return result;
}

// The spell glow: the phase advances by dt * 5 and wraps at 2 pi (_CIfmod); its sine scales the
// school's colour (00740358, 16 bytes per school) into 0xFFrrggbb. The phase is stored as float
// but the sine and the colour use the unrounded value, as the original.
std::uint32_t Glow(float dt, ItemTurn* turn, const float* colour) {
    std::uint32_t r, g, b;
    __asm {
        mov ecx, turn
        fld dt
        fmul qword ptr ds:[0x007403F0]
        fadd dword ptr [ecx + 0x14]
        fld qword ptr ds:[0x007402B8]
        fadd st(0), st(0)
        mov eax, 0x006E95AA
        call eax
        mov ecx, turn
        fst dword ptr [ecx + 0x14]
        fsin
        fadd qword ptr ds:[0x0073BAC8]
        fmul qword ptr ds:[0x007403E8]
        mov edx, colour
        fld st(0)
        fmul dword ptr [edx]
        fmul dword ptr ds:[0x0073C370]
        mov eax, 0x006E853C
        call eax
        mov r, eax
        mov edx, colour
        fld st(0)
        fmul dword ptr [edx + 4]
        fmul dword ptr ds:[0x0073C370]
        mov eax, 0x006E853C
        call eax
        mov g, eax
        mov edx, colour
        fmul dword ptr [edx + 8]
        fmul dword ptr ds:[0x0073C370]
        mov eax, 0x006E853C
        call eax
        mov b, eax
    }
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The turn: on the hovered item the speed grows by 8 per second; elsewhere it brakes towards the
// next full turn (4 * (2 pi - angle)), or stops below 0.1. At most 5. The angle advances by
// dt * speed and wraps at 2 pi (compared unrounded, stored as float).
void Turn(ItemTurn* turn, float dt, bool hovered) {
    if (hovered) {
        turn->speed = Store(D(GameFloat(0x007402F4)) * D(dt) + D(turn->speed));
    } else if (!(turn->speed >= GameFloat(0x007402F0))) {   // FCOMP; TEST AH,1: below or unordered
        turn->angle = 0.0f;
        turn->speed = 0.0f;
    } else {
        double pi = D(GameFloat(0x007402C0));
        turn->speed = Store((pi + pi - D(turn->angle)) * D(GameFloat(0x0073BAA0)));
    }
    if (turn->speed > GameFloat(0x007402EC)) std::memcpy(&turn->speed, reinterpret_cast<const void*>(0x007402EC), 4);
    // The angle is stored (FST) and compared unrounded with 2 pi.
    __asm {
        mov ecx, turn
        fld dt
        fmul dword ptr [ecx + 0x0C]
        fadd dword ptr [ecx + 0x08]
        fst dword ptr [ecx + 0x08]
        fcomp dword ptr ds:[0x007C7D20]
        fnstsw ax
        test ah, 0x41
        jnz done
    again:
        fld dword ptr [ecx + 0x08]
        fsub dword ptr ds:[0x007C7D20]
        fst dword ptr [ecx + 0x08]
        fcomp dword ptr ds:[0x007C7D20]
        fnstsw ax
        test ah, 0x41
        jz again
    done:
    }
}

// 0049E800 (thiscall on the result, RET 8): the rotation by `angle` about `axis` (normalised).
struct Quaternion4 {
    float w, x, y, z;
    Quaternion4* FromAxisAngle(const float* axis, float angle);
};
__declspec(naked) Quaternion4* Quaternion4::FromAxisAngle(const float*, float) EI_ORIGINAL(0x0049E800)

// The rotation about `axis` by pi (the float at 007402C0).
Quaternion4 HalfTurn(float ax, float ay, float az) {
    const float axis[3] = {ax, ay, az};
    Quaternion4 q;
    float pi;
    std::memcpy(&pi, reinterpret_cast<const void*>(0x007402C0), 4);
    q.FromAxisAngle(axis, pi);
    return q;
}

// The two products the original inlines (a * b), each with its own order of the w sum.
Quaternion4 ProductA(const Quaternion4& a, const Quaternion4& b) {   // weapons
    Quaternion4 r;
    r.w = Store(D(a.w) * D(b.w) - ((D(a.y) * D(b.y) + D(a.x) * D(b.x)) + D(b.z) * D(a.z)));
    r.x = Store(((D(b.y) * D(a.z) - D(a.y) * D(b.z)) + D(a.w) * D(b.x)) + D(a.x) * D(b.w));
    r.y = Store(((D(a.x) * D(b.z) - D(b.x) * D(a.z)) + D(a.w) * D(b.y)) + D(a.y) * D(b.w));
    r.z = Store(((D(a.y) * D(b.x) - D(a.x) * D(b.y)) + D(b.w) * D(a.z)) + D(a.w) * D(b.z));
    return r;
}
Quaternion4 ProductB(const Quaternion4& a, const Quaternion4& b) {   // other items
    Quaternion4 r;
    r.w = Store(D(a.w) * D(b.w) - ((D(b.z) * D(a.z) + D(b.y) * D(a.y)) + D(b.x) * D(a.x)));
    r.x = Store(((D(b.y) * D(a.z) - D(b.z) * D(a.y)) + D(a.w) * D(b.x)) + D(b.w) * D(a.x));
    r.y = Store(((D(b.z) * D(a.x) - D(b.x) * D(a.z)) + D(a.w) * D(b.y)) + D(b.w) * D(a.y));
    r.z = Store(((D(b.x) * D(a.y) - D(b.y) * D(a.x)) + D(a.z) * D(b.w)) + D(a.w) * D(b.z));
    return r;
}

void SetOrientation(std::uint8_t* model, const Quaternion4& q) { std::memcpy(model + 0x34, &q, sizeof(q)); }

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
}

} // namespace

// --- 005ECA10 -----------------------------------------------------------------------------------------

std::uint8_t* CItemScreen::CachedModel(CItem* item) {
    mfc::CString name;
    ItemName(item, 20, &name);
    ItemObjectCache::Node* node;
    stlp::string key(static_cast<const char*>(name));
    MapOf(&models_)->Find(&node, &key);
    return node != models_.header ? static_cast<std::uint8_t*>(node->object) : nullptr;
}

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
    AsSprite(model)->SetScale(Store(D(GameFloat(0x007402C4)) * D(cell->size) * D(cell->depth) * D(GameFloat(0x00740408))));
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
        std::uint32_t word = reinterpret_cast<Isaac*>(0x007A9D40)->Next();
        turn->glow = RandomPhase(word);
        reinterpret_cast<RefSlot*>(itemBytes + 0x1C)->Assign(turn);
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
        float y = Store(D(Get<float>(model, 0x190)) * D(cell->size) * half + D(cell->y));
        float x = Store(D(cell->x) - D(Get<float>(model, 0x18C)) * D(cell->size) * half);
        AsSprite(model)->Place(x, y, cell->depth);
        AsSprite(model)->SetScale(Store(D(GameFloat(0x007402C4)) * D(cell->size) * D(cell->depth) *
                                        D(GameFloat(0x00740164)) / D(extent)));
    } else {
        SetShade(model, 0x3F800000);
        float dt = Store(reinterpret_cast<CUiContainer*>(this)->FrameTime());
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
            Quaternion4 a = HalfTurn(1.0f, 0.0f, 0.0f);
            const float axis[3] = {1.0f, 1.0f, 0.0f};
            Quaternion4 b;
            b.FromAxisAngle(axis, turn->angle);
            SetOrientation(model, ProductA(a, b));
        } else if (Get<int>(itemBytes, 0x10) == 0x3007 && Get<int>(itemBytes, 0x4C) == 2 &&
                   (Get<int>(itemBytes, 0x50) == 0 || Get<int>(itemBytes, 0x50) == 1 || Get<int>(itemBytes, 0x50) == 2)) {
            SetOrientation(model, HalfTurn(1.0f, 0.0f, 0.0f));   // blueprints lie still
        } else {
            // Others turn about y after a half turn about x.
            Quaternion4 a = HalfTurn(1.0f, 0.0f, 0.0f);
            const float axis[3] = {0.0f, 1.0f, 0.0f};
            Quaternion4 b;
            b.FromAxisAngle(axis, turn->angle);
            SetOrientation(model, ProductB(a, b));
        }

        // The frame behind the item (texture 2 of the screen), drawn once and removed.
        float frameDepth = Store(D(cell->depth) * D(GameFloat(0x0073FF20)));
        CUiMesh* frame = AddSprite(0);
        double half = D(GameFloat(0x007403E0));
        AsSprite(frame)->SetRectangle(frameDepth, 100.0f, Store(D(cell->x) - half), Store(D(cell->y) - half),
                                      Store(D(cell->x) + half), Store(D(cell->y) + half), 0x9D, 0x9D, 0xE3, 0xE3);
        Set(frame, 0x280, objects_.data_[2]);
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
        float scale = Store(AsSprite(model)->Scale());
        CUiMesh* mark = AddSprite(0);
        AsSprite(mark)->SetRectangle(Store(D(pz) * D(GameFloat(0x0073CD70))), 100.0f, 0.0f, 0.0f, 35.0f, 35.0f, 0, 0,
                                     0x20, 0x20);
        Set(mark, 0x280, objects_.data_[1]);
        AsSprite(mark)->SetScale(scale);
        SetShade(reinterpret_cast<std::uint8_t*>(mark), cell->mode == 9 ? 0x3F000000 : 0x3F800000);
        float step = Store(D(scale) * D(GameFloat(0x007403DC)));
        for (int i = 0; i < reinterpret_cast<CSpell*>(spell)->GetModifierCount(); ++i) {
            int code = static_cast<signed char>(Get<std::uint8_t>(spell, 0x54 + i)) - 1;
            double cellSize = D(GameFloat(0x0073C520));
            Set(mark, 0xCC, Store(static_cast<double>(code & 7) * cellSize));
            Set(mark, 0xD0, Store(static_cast<double>(-(code >> 3)) * cellSize));
            double d = D(step) / D(pz);
            float dx;
            if (i == 0 || i == 4)
                dx = 0.0f;
            else if (i == 5 || i == 6 || i == 7)
                dx = -Store(d);
            else
                dx = Store(d);
            double dy;
            if (i == 2 || i == 6)
                dy = D(GameFloat(0x0073B858));
            else if (i == 0 || i == 1 || i == 7)
                dy = -d;
            else
                dy = d;
            float depth = Store(D(pz) * D(GameFloat(0x0073CD70)));
            AsSprite(mark)->Place(Store(D(dx) + D(px)), Store(dy + D(py)), depth);
            AsSprite(mark)->Draw(pass);
        }
        RemoveSceneObject(reinterpret_cast<CEngineObject*>(mark));
    }

    // Items the strip's mode cannot take get a dark square in front.
    using CanEquipFn = bool(__thiscall*)(CItemScreen*, CItem*, int);
    if (cell->mode != 9 && !reinterpret_cast<CanEquipFn>(vtable_[43])(this, item, cell->mode)) {
        float depth = Store(D(cell->depth) * D(GameFloat(0x0073CF50)));
        if (LiesFlat(item)) depth = Store(D(cell->depth) * D(GameFloat(0x0073CCA8)));
        CUiMesh* shade = AddSprite(0);
        double half = D(GameFloat(0x007403E0));
        AsSprite(shade)->SetRectangle(depth, 100.0f, Store(D(cell->x) - half), Store(D(cell->y) - half),
                                      Store(D(cell->x) + half), Store(D(cell->y) + half), 0, 0, 0x100, 0x100);
        Set<std::uint32_t>(shade, 0x280, 0);
        Set(shade, 0x08, Get<std::uint32_t>(shade, 0x08) | 0x100);
        Set<std::uint32_t>(shade, 0xD4, 0x82804040);
        AsSprite(shade)->Draw(pass);
        RemoveSceneObject(reinterpret_cast<CEngineObject*>(shade));
    }
}
EI_REPLACE_METHOD(0x005ECA10, CItemScreen::DrawItem);

// --- 005EE5C0: the description panel's slot 20 --------------------------------------------------------

namespace {
// The owner's DrawItem through its original address (so that its hook, and in the verify build its
// wrapper, sees the call).
struct DrawItemAt {
    void DrawItem(int pass, ItemCell* cell);
};
__declspec(naked) void DrawItemAt::DrawItem(int, ItemCell*) EI_ORIGINAL(0x005ECA10)
} // namespace

BOOL CItemInfoPanel::Draw(int pass) {
    if (itemCount_ && items_[0]) {
        ItemCell cell;
        cell.item = items_[0];
        cell.x = Store(static_cast<double>(x_) + D(GameFloat(0x0073BAB0)));   // + 100
        cell.depth = 3.0f;
        cell.size = 120.0f;
        cell.mode = 9;
        cell.y = Store(static_cast<double>(y_) + D(GameFloat(0x0073CF58)));   // + 200
        reinterpret_cast<DrawItemAt*>(owner_)->DrawItem(pass, &cell);
    }
    return 1;
}

namespace {
BOOL __fastcall InfoPanelDraw(CItemInfoPanel* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x005EE5C0, InfoPanelDraw);
