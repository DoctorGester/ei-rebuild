// The item screens' common setup (the camp, the multiplayer hero screen and the exchange window):
// the caches' trimming (005F2F20 / 005F2F90), the base Create (005EDD80) and the icon window's
// frame (005F2A90, its slot 18). See ui_item_info.h and docs/notes/ui.md ("Item descriptions").
#include "model.h"
#include "ui_item_info.h"
#include "ui_camp.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_render.h"
#include "originals_gameplay.h"
#include "item.h"
#include "party.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }


// Sprite builders (CUiMesh, Rebirth-patched; called through their addresses): 005E70E0 a
// rectangle, 005E7680 a strip of `count` framed cells.
struct SpriteBuild {
    void Rectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                   int v1);   // 005E70E0
    void Cells(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
               int v1, int count, int flag);   // 005E7680
};
void SpriteBuild::Rectangle(float a0, float a1, float a2, float a3, float a4, float a5, int a6, int a7, int a8, int a9) { UiMeshRectangleOriginal((void*)this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); }
void SpriteBuild::Cells(float a0, float a1, float a2, float a3, float a4, float a5, int a6, int a7, int a8, int a9, int a10, int a11) { UiMeshCellsOriginal((void*)this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11); }

// 00500960: the display object; +0x14 a flag, +8 -> +0xC an object whose slot 5 takes two floats.
std::uint8_t* __cdecl Display() { return (std::uint8_t*)SceneRenderer(); }

} // namespace

void ItemObjectCache::Construct(std::uint8_t compare) {
    map.Header() = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    map.Count() = 0;
    map.Compare() = compare;
    map.EmptyInitialize();
    limit = 50;
}

// While the cache holds more than its limit: the least recently used entry (used before now) has
// its object released (slot 4) and is erased (005F3140 / 005F32A0). Spins while every entry was
// used at or after now.
void __fastcall TrimItemCache(ItemObjectCache* cache) {
    while (cache->map.size() > cache->limit) {
        std::uint32_t oldestTime = gameapi::timeGetTime();
        ItemObjectCache::Node* oldest = cache->map.end();
        for (ItemObjectCache::Node* node = cache->map.begin(); node != cache->map.end(); node = ItemObjectCache::Map::Next(node)) {
            if (node->second.lastUse < oldestTime) {
                oldestTime = node->second.lastUse;
                oldest = node;
            }
        }
        if (oldest != cache->map.end()) {
            static_cast<CEngineObject*>(oldest->second.object)->Release();
            cache->map.Erase(oldest);
        }
    }
}
EI_REPLACE(0x005F2F20, TrimItemCache);
EI_REPLACE(0x005F2F90, TrimItemCache);

// --- 005F2A90: the icon window's frame -------------------------------------------------------------

void CItemIconWindow::Build() {
    auto sprite = [this] { return reinterpret_cast<SpriteBuild*>(AddSprite(0)); };
    sprite()->Cells(8.0f, 100.0f, 0.0f, 100.0f, 800.0f, 500.0f, 0xE, 0xE, 0x72, 0x72, 8, 4);
    // Four 200-wide columns: two side strips, and corners at the top and bottom of each half.
    for (int x = 0; x < 800; x += 200) {
        float left = static_cast<float>(x);
        sprite()->Cells(7.9f, 100.0f, left, 100.0f, static_cast<float>(D(left) + D(21.0f /*0074054C*/)), 500.0f, 0xFE, 0x4C, 0xE9,
                        0xB0, 1, 4);
        float right = static_cast<float>(x + 200);
        sprite()->Cells(7.9f, 100.0f, static_cast<float>(D(left) + D(179.0f /*00740548*/)), 100.0f, right, 500.0f, 0xE9, 0x4C, 0xFE,
                        0xB0, 1, 4);
        float middle = static_cast<float>(D(left) + D(100.0f /*0073BAB0*/));   // + 100
        sprite()->Rectangle(7.8f, 100.0f, left, 100.0f, middle, 130.0f, 0xFE, 2, 0x9A, 0x20);
        sprite()->Rectangle(7.8f, 100.0f, left, 470.0f, middle, 500.0f, 0xFE, 0x20, 0x9A, 2);
        float half = static_cast<float>(x + 100);
        sprite()->Rectangle(7.8f, 100.0f, half, 100.0f, right, 130.0f, 0x9A, 2, 0xFE, 0x20);
        sprite()->Rectangle(7.8f, 100.0f, half, 470.0f, right, 500.0f, 0x9A, 0x20, 0xFE, 2);
    }
    sprite()->Rectangle(7.8f, 100.0f, 0.0f, 380.0f, 100.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->Rectangle(7.8f, 100.0f, 100.0f, 380.0f, 200.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->Rectangle(7.8f, 100.0f, 600.0f, 380.0f, 700.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->Rectangle(7.8f, 100.0f, 700.0f, 380.0f, 800.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->Rectangle(7.8f, 100.0f, 200.0f, 280.0f, 300.0f, 320.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->Rectangle(7.8f, 100.0f, 300.0f, 280.0f, 400.0f, 320.0f, 0x9A, 0x22, 0xFE, 0x4A);
}
EI_REPLACE_METHOD(0x005F2A90, CItemIconWindow::Build);

// --- 005F2E00: the first two bars moved ------------------------------------------------------------

void CItemIconWindow::PlaceBars(int y) {
    constexpr int kLeftBar = 25, kRightBar = 26;   // after the cell strip and the 4 x 6 column pieces
    float top = static_cast<float>(y - 20);
    float bottom = static_cast<float>(y + 20);
    reinterpret_cast<CUiMesh*>(sceneObjects_.data[kLeftBar])
        ->SetRectangle(7.8f, 100.0f, 0.0f, top, 100.0f, bottom, 0xFE, 0x22, 0x9A, 0x4A);
    reinterpret_cast<CUiMesh*>(sceneObjects_.data[kRightBar])
        ->SetRectangle(7.8f, 100.0f, 100.0f, top, 200.0f, bottom, 0x9A, 0x22, 0xFE, 0x4A);
}
EI_REPLACE_METHOD(0x005F2E00, CItemIconWindow::PlaceBars);

// --- 005EDD80: the item screens' base Create ------------------------------------------------------

namespace {
} // namespace

void CItemScreen::CreateBase() {
    void* memory = gamecrt::operator_new(0x148);
    CUiWindow* icons = nullptr;
    if (memory) {
        icons = static_cast<CUiWindow*>(memory)->Construct(interface_);
        icons->vtable_ = ei::GamePointer<void*>(0x00740444);
        icons->inputVtable_ = ei::GamePointer<void*>(0x00740410);
    }
    iconWindow_ = icons;
    reinterpret_cast<CUiContainer*>(this)->AddChild(icons);
    iconWindow_->AddObject(objects_.data[0]);
    using BuildFn = void(__thiscall*)(CUiWindow*);
    ei::VirtualSlot<BuildFn>(iconWindow_->vtable_, 18)(iconWindow_);
    LoadTexture("modifiers2", 3, 0);
    LoadTexture("campslots", 3, 0);
    std::uint32_t zero = 0;
    std::memcpy(Display() + 0x14, &zero, 4);
    {
        std::uint8_t* display = Display();
        std::uint8_t* inner;
        std::memcpy(&inner, display + 8, 4);
        void* target;
        std::memcpy(&target, inner + 0xC, 4);
        using SetFn = void(__thiscall*)(void*, float, float);
        ei::ObjectSlot<SetFn>(target, 5)(target, 99.0f, 100.0f);
    }
    // The caches keep 30 entries (20 with 00784754 set).
    if (ei::GameData<const volatile char>(0x00784754) != 0) {
        textures_.limit = 20;
        TrimItemCache(&textures_);
        models_.limit = 20;
        TrimItemCache(&models_);
    } else {
        textures_.limit = 30;
        TrimItemCache(&textures_);
        models_.limit = 30;
        TrimItemCache(&models_);
    }
}
EI_REPLACE_METHOD(0x005EDD80, CItemScreen::CreateBase);

// --- 005F6470 / 005F6620: the item screens' destructor bodies -----------------------------------

namespace {
// A cache's objects released (slot 4), in key order, then its map destroyed: the nodes erased
// (the original's inlined loop over the left spine, or 005FAFF0 / 005FB040 on the root), the
// header back to the pool (005FA920 / 005FA960, or 005FA060 / 005FA090).
void DestroyCache(ItemObjectCache& cache) {
    ReleaseCachedObjects(&cache);
    cache.map.Destroy();
}
} // namespace

// 00600BA0: the container base's destructor body. 006FE958: CStringArray's destructor.
void __fastcall ItemScreenDestroy(CItemScreen* self) {
    DestroyCache(self->textures_);
    DestroyCache(self->models_);
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F6470, ItemScreenDestroy);

void __fastcall CampScreenDestroy(CItemScreen* self) {
    auto* camp = static_cast<CCampPages*>(self);
    camp->pageTitles_.Destruct();
    // The heroes CArray (vtable 007402AC).
    camp->heroes_.vtable = ei::GamePointer<void>(0x007402AC);
    if (camp->heroes_.data) gamecrt::operator_delete(camp->heroes_.data);
    DestroyCache(self->textures_);
    DestroyCache(self->models_);
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F6620, CampScreenDestroy);

// --- 005F7010: screen 9's destructor body (vtable 00740D18) ---------------------------------------

namespace {
// STLport's node allocator for small blocks (free lists at 0079B224 by 8-byte size class; larger
// blocks go back to the heap), inlined by the original.
void DeallocateNodes(void* block, std::uint32_t bytes) {
    if (bytes > 0x80) {
        gamecrt::operator_delete(block);
        return;
    }
    void** lists = ei::GamePointer<void*>(0x0079B224);
    std::uint32_t index = (bytes - 1) >> 3;
    *static_cast<void**>(block) = lists[index];
    lists[index] = block;
}
} // namespace

void __fastcall Screen9Destroy(CItemScreen* self) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    // The vector of CStrings at +0x1AC (begin, end, capacity).
    std::uint8_t* begin;
    std::uint8_t* end;
    std::uint8_t* capacity;
    std::memcpy(&begin, bytes + 0x1AC, 4);
    std::memcpy(&end, bytes + 0x1B0, 4);
    for (std::uint8_t* at = begin; at != end; at += 4) CStringDestroy(at);
    std::memcpy(&begin, bytes + 0x1AC, 4);
    std::memcpy(&capacity, bytes + 0x1B4, 4);
    std::uint32_t entries = static_cast<std::uint32_t>(static_cast<int>(capacity - begin) >> 2);
    if (begin) DeallocateNodes(begin, entries * 4);
    DestroyCache(self->textures_);
    DestroyCache(self->models_);
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F7010, Screen9Destroy);

// --- 005EE1B0 / 005EE2C0: moving items between lists ----------------------------------------------

namespace {
// CParty::AddItem / RemoveItem (party_items.cpp) through their original addresses (verify wrappers).
CItem* AddItemAt(CParty* list, CItem* item, int keepCount, int merge) {
    return list->AddItem(item, keepCount, merge);
}
void RemoveItemAt(CParty* list, CItem* item, int all) { list->RemoveItem(item, all); }
void* TurnVtable() { return ei::GamePointer<void>(0x007403F8); }
} // namespace

// --- 005ED710 / 005ED780: the turning state's Clone and deleting destructor -----------------------

ItemTurn* ItemTurn::Clone() const {
    auto* copy = static_cast<ItemTurn*>(gamecrt::operator_new(sizeof(ItemTurn)));
    // The original writes the count before checking the allocation.
    reinterpret_cast<volatile int*>(copy)[1] = 0;
    if (!copy) return nullptr;
    copy->angle = angle;
    copy->speed = speed;
    copy->flag = flag;
    copy->glow = glow;
    copy->vtable = TurnVtable();
    return copy;
}
namespace {
ItemTurn* __fastcall ItemTurnClone(const ItemTurn* self) { return self->Clone(); }
} // namespace
EI_REPLACE(0x005ED710, ItemTurnClone);

ItemTurn* ItemTurn::DeletingDestruct(unsigned flags) {
    RefObjectDestructBase(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005ED780, ItemTurn::DeletingDestruct);

CItem* CItemScreen::MoveItem(CItem* item, CParty* to, CParty* from, int limit, int merge, int turning) {
    int whole = static_cast<std::uint8_t>(merge) != 0 && interface_->shiftDown_ != 0;
    if (limit > 0 && to->items_.size >= limit) {
        for (int i = 0; i < limit; ++i) {
            if (!item->IsSameAs(to->items_.data[i])) continue;
            CItem* moved = AddItemAt(to, item, whole, merge);
            RemoveItemAt(from, item, whole);
            return moved;
        }
        return nullptr;
    }
    CItem* moved = AddItemAt(to, item, whole, merge);
    RemoveItemAt(from, item, whole);
    if (static_cast<std::uint8_t>(turning)) {
        ItemTurn*& slot = ItemTurnOf(moved);   // a null result faults here, as in the original
        if (!slot) {
            auto* turn = static_cast<ItemTurn*>(gamecrt::operator_new(sizeof(ItemTurn)));
            // The original writes the count before checking the allocation (and the rest after).
            reinterpret_cast<volatile int*>(turn)[1] = 0;
            if (turn) turn->vtable = TurnVtable();
            turn->angle = 0.0f;
            turn->speed = 0.0f;
            slot = turn;
            ++turn->references;
        }
        slot->flag = true;
    }
    return moved;
}
EI_REPLACE_METHOD(0x005EE1B0, CItemScreen::MoveItem);

CItem* CItemScreen::ReturnAll(CParty* to, CParty* from) {
    CItem* moved = nullptr;
    while (from->items_.size != 0) {
        CItem* first = from->items_.data[0];
        moved = AddItemAt(to, first, 1, 1);
        RemoveItemAt(from, first, 1);
    }
    return moved;
}
EI_REPLACE_METHOD(0x005EE2C0, CItemScreen::ReturnAll);
