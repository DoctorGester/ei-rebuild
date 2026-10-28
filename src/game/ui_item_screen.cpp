// The item screens' common setup (the camp, the multiplayer hero screen and the exchange window):
// the caches' trimming, the base Create and the icon window's
// frame. See ui_item_info.h and docs/notes/ui.md ("Item descriptions").
#include "app.h"
#include "model.h"
#include "texture.h"
#include "ui.h"
#include "ui_item_info.h"
#include "ui_camp.h"
#include "ui_screens.h"
#include "core_streams.h"

#include "lib/gameapi.h"
#include "item.h"
#include "party.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

inline double D(float f) { return f; }

// A cache's objects released (Release), in key order; the map goes with the screen.
template <typename Object>
void DestroyCache(ItemObjectCache<Object>& cache) {
    ReleaseCachedObjects(&cache);
}

CItem* AddItemAt(CParty* list, CItem* item, int keepCount, int merge) {
    return list->AddItem(item, keepCount, merge);
}
void RemoveItemAt(CParty* list, CItem* item, int all) { list->RemoveItem(item, all); }

} // namespace

// While the cache holds more than its limit: the least recently used entry (used before now) has
// its object released and is erased. Spins while every entry was
// used at or after now.
template <typename Object>
void TrimItemCache(ItemObjectCache<Object>* cache) {
    while (cache->map.size() > cache->limit) {
        std::uint32_t oldestTime = gameapi::timeGetTime();
        auto oldest = cache->map.end();
        for (auto node = cache->map.begin(); node != cache->map.end(); ++node) {
            if (node->second.lastUse < oldestTime) {
                oldestTime = node->second.lastUse;
                oldest = node;
            }
        }
        if (oldest != cache->map.end()) {
            oldest->second.object->Release();
            cache->map.erase(oldest);
        }
    }
}

// --- The icon window's frame -------------------------------------------------------------

void CItemIconWindow::Build() {
    auto sprite = [this] { return AddBlankSprite(); };
    sprite()->SetGridVanilla(8.0f, 100.0f, 0.0f, 100.0f, 800.0f, 500.0f, 0xE, 0xE, 0x72, 0x72, 8, 4);
    // Four 200-wide columns: two side strips, and corners at the top and bottom of each half.
    for (int x = 0; x < 800; x += 200) {
        float left = static_cast<float>(x);
        sprite()->SetGridVanilla(7.9f, 100.0f, left, 100.0f, static_cast<float>(D(left) + D(21.0f)), 500.0f, 0xFE, 0x4C, 0xE9,
                        0xB0, 1, 4);
        float right = static_cast<float>(x + 200);
        sprite()->SetGridVanilla(7.9f, 100.0f, static_cast<float>(D(left) + D(179.0f)), 100.0f, right, 500.0f, 0xE9, 0x4C, 0xFE,
                        0xB0, 1, 4);
        float middle = static_cast<float>(D(left) + D(100.0f));
        sprite()->SetRectangleVanilla(7.8f, 100.0f, left, 100.0f, middle, 130.0f, 0xFE, 2, 0x9A, 0x20);
        sprite()->SetRectangleVanilla(7.8f, 100.0f, left, 470.0f, middle, 500.0f, 0xFE, 0x20, 0x9A, 2);
        float half = static_cast<float>(x + 100);
        sprite()->SetRectangleVanilla(7.8f, 100.0f, half, 100.0f, right, 130.0f, 0x9A, 2, 0xFE, 0x20);
        sprite()->SetRectangleVanilla(7.8f, 100.0f, half, 470.0f, right, 500.0f, 0x9A, 0x20, 0xFE, 2);
    }
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 0.0f, 380.0f, 100.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 100.0f, 380.0f, 200.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 600.0f, 380.0f, 700.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 700.0f, 380.0f, 800.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 200.0f, 280.0f, 300.0f, 320.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangleVanilla(7.8f, 100.0f, 300.0f, 280.0f, 400.0f, 320.0f, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The first two bars moved ------------------------------------------------------------

void CItemIconWindow::PlaceBars(int y) {
    constexpr int kLeftBar = 25, kRightBar = 26;   // after the cell strip and the 4 x 6 column pieces
    float top = static_cast<float>(y - 20);
    float bottom = static_cast<float>(y + 20);
    SceneMesh(kLeftBar)
        ->SetRectangle(7.8f, 100.0f, 0.0f, top, 100.0f, bottom, 0xFE, 0x22, 0x9A, 0x4A);
    SceneMesh(kRightBar)
        ->SetRectangle(7.8f, 100.0f, 100.0f, top, 200.0f, bottom, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The item screens' base Create ------------------------------------------------------

void CItemScreen::CreateBase() {
    CItemIconWindow* icons = new CItemIconWindow(interface_);
    iconWindow_ = icons;
    AddChild(icons);
    iconWindow_->AddObject(objects_[0]);
    iconWindow_->Build();
    LoadTexture("modifiers2", 3);
    LoadTexture("campslots", 3);
    g_sceneRenderer->fogColour_ = 0;
    g_sceneRenderer->pipeline_->transform_->SetDepthRange(99.0f, 100.0f);
    // The caches keep 30 entries (20 with little texture memory).
    if (app::g_littleTextureMemory != 0) {
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

// --- The item screens' destructor bodies -----------------------------------

CItemScreen::~CItemScreen() {
    DestroyCache(textures_);
    DestroyCache(models_);
}

// The page titles, the heroes array, then the item screen's.
CCampPages::~CCampPages() = default;

// --- Screen 9's destructor ------------------------------------------

CExchangeScreen::~CExchangeScreen() {}

// --- Moving items between lists ----------------------------------------------

// --- The turning state's Clone and deleting destructor -----------------------

ItemTurn* ItemTurn::Clone() {
    auto* copy = new ItemTurn();
    if (!copy) return nullptr;
    copy->angle = angle;
    copy->speed = speed;
    copy->flag = flag;
    copy->glow = glow;
    return copy;
}

ItemTurn::~ItemTurn() {}

CItem* CItemScreen::MoveItem(CItem* item, CParty* to, CParty* from, int limit, int merge, int turning) {
    int whole = static_cast<std::uint8_t>(merge) != 0 && interface_->shiftDown_ != 0;
    if (limit > 0 && std::ssize(to->items_) >= limit) {
        for (int i = 0; i < limit; ++i) {
            if (!item->IsSameAs(to->items_[i])) continue;
            CItem* moved = AddItemAt(to, item, whole, merge);
            RemoveItemAt(from, item, whole);
            return moved;
        }
        return nullptr;
    }
    CItem* moved = AddItemAt(to, item, whole, merge);
    RemoveItemAt(from, item, whole);
    if (static_cast<std::uint8_t>(turning)) {
        if (!ItemTurnOf(moved)) {   // a null item faults here, like the original
            auto* turn = new ItemTurn();
            turn->angle = 0.0f;
            turn->speed = 0.0f;
            moved->link_ = turn;
            ++turn->refs_;
        }
        ItemTurnOf(moved)->flag = true;
    }
    return moved;
}

CItem* CItemScreen::ReturnAll(CParty* to, CParty* from) {
    CItem* moved = nullptr;
    while (!from->items_.empty()) {
        CItem* first = from->items_[0];
        moved = AddItemAt(to, first, 1, 1);
        RemoveItemAt(from, first, 1);
    }
    return moved;
}
