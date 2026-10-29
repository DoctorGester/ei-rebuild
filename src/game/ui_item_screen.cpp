// The item screens' common setup (the camp, the multiplayer hero screen and the exchange window):
// the caches' trimming, the base Create and the icon window's
// frame. See ui_item_info.h and docs/notes/ui.md ("Item descriptions").
#include "app.h"
#include "core_streams.h"
#include "item.h"
#include "model.h"
#include "party.h"
#include "texture.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

// Every cached object released, in key order.
template <typename Object>
void ReleaseCachedObjects(ItemObjectCache<Object>* cache) {
    for (auto& entry : cache->map) entry.second.object->Release();
}

} // namespace

// --- The icon window's frame -------------------------------------------------------------

void ItemIconWindow::Build() {
    auto sprite = [this] { return AddBlankSprite(); };
    sprite()->SetGrid(8.0f, 100.0f, 0.0f, 100.0f, 800.0f, 500.0f, 0xE, 0xE, 0x72, 0x72, 8, 4);
    // Four 200-wide columns: two side strips, and corners at the top and bottom of each half.
    for (int x = 0; x < 800; x += 200) {
        float left = static_cast<float>(x);
        sprite()->SetGrid(7.9f, 100.0f, left, 100.0f, left + 21.0f, 500.0f, 0xFE, 0x4C, 0xE9,
                        0xB0, 1, 4);
        float right = static_cast<float>(x + 200);
        sprite()->SetGrid(7.9f, 100.0f, left + 179.0f, 100.0f, right, 500.0f, 0xE9, 0x4C, 0xFE,
                        0xB0, 1, 4);
        float middle = left + 100.0f;
        sprite()->SetRectangle(7.8f, 100.0f, left, 100.0f, middle, 130.0f, 0xFE, 2, 0x9A, 0x20);
        sprite()->SetRectangle(7.8f, 100.0f, left, 470.0f, middle, 500.0f, 0xFE, 0x20, 0x9A, 2);
        float half = static_cast<float>(x + 100);
        sprite()->SetRectangle(7.8f, 100.0f, half, 100.0f, right, 130.0f, 0x9A, 2, 0xFE, 0x20);
        sprite()->SetRectangle(7.8f, 100.0f, half, 470.0f, right, 500.0f, 0x9A, 0x20, 0xFE, 2);
    }
    sprite()->SetRectangle(7.8f, 100.0f, 0.0f, 380.0f, 100.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangle(7.8f, 100.0f, 100.0f, 380.0f, 200.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangle(7.8f, 100.0f, 600.0f, 380.0f, 700.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangle(7.8f, 100.0f, 700.0f, 380.0f, 800.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangle(7.8f, 100.0f, 200.0f, 280.0f, 300.0f, 320.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangle(7.8f, 100.0f, 300.0f, 280.0f, 400.0f, 320.0f, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The first two bars moved ------------------------------------------------------------

void ItemIconWindow::PlaceBars(int y) {
    constexpr int kLeftBar = 25, kRightBar = 26;   // after the cell strip and the 4 x 6 column pieces
    float top = static_cast<float>(y - 20);
    float bottom = static_cast<float>(y + 20);
    SceneMesh(kLeftBar)
        ->SetRectangle(7.8f, 100.0f, 0.0f, top, 100.0f, bottom, 0xFE, 0x22, 0x9A, 0x4A);
    SceneMesh(kRightBar)
        ->SetRectangle(7.8f, 100.0f, 100.0f, top, 200.0f, bottom, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The item screens' base Create ------------------------------------------------------

void ItemScreen::CreateBase() {
    ItemIconWindow* icons = new ItemIconWindow(ui);
    iconWindow = icons;
    AddChild(icons);
    iconWindow->AddObject(objects[0]);
    iconWindow->Build();
    LoadTexture("modifiers2", TextureKind::Interface);
    LoadTexture("campslots", TextureKind::Interface);
    g_sceneRenderer.fogColour = 0;
    g_sceneRenderer.pipeline->SetDepthRange(99.0f, 100.0f);
    // The caches keep 30 entries (the game's 20 were for a card with little texture memory).
    textures.limit = 30;
    TrimItemCache(&textures);
    models.limit = 30;
    TrimItemCache(&models);
}

// --- The item screens' destructor ---------------------------------------------------

// The caches' objects released, in key order; the maps go with the screen.
ItemScreen::~ItemScreen() {
    ReleaseCachedObjects(&textures);
    ReleaseCachedObjects(&models);
}

// --- The turning state's Clone -----------------------------------------------------

ItemTurn* ItemTurn::Clone() {
    auto* copy = new ItemTurn();
    copy->angle = angle;
    copy->speed = speed;
    copy->flag = flag;
    copy->glow = glow;
    return copy;
}

// --- Moving items between lists ----------------------------------------------------

Item* ItemScreen::MoveItem(Item* item, CParty* to, CParty* from, int limit, bool merge, bool turning) {
    bool whole = merge && ui->shiftDown != 0;
    if (limit > 0 && std::ssize(to->items) >= limit) {
        for (int i = 0; i < limit; ++i) {
            if (!item->IsSameAs(to->items[i])) continue;
            Item* moved = to->AddItem(item, whole, merge);
            from->RemoveItem(item, whole);
            return moved;
        }
        return nullptr;
    }
    Item* moved = to->AddItem(item, whole, merge);
    from->RemoveItem(item, whole);
    if (turning) {
        if (!ItemTurnOf(moved)) {   // a null item faults here, like the original
            moved->link.Assign(new ItemTurn());
        }
        ItemTurnOf(moved)->flag = true;
    }
    return moved;
}

Item* ItemScreen::ReturnAll(CParty* to, CParty* from) {
    Item* moved = nullptr;
    while (!from->items.empty()) {
        Item* first = from->items[0];
        moved = to->AddItem(first, 1, 1);
        from->RemoveItem(first, 1);
    }
    return moved;
}

// --- Default answers of the item screen's slots ------------------------------------------------------

bool ItemScreen::CanEquip(Item*, int) { return false; }

PartySkill ItemScreen::BestPartySkill(int) { return {}; }

CUnit* ItemScreen::HeroUnit(int) { return nullptr; }

float ItemScreen::Weight() { return 0.0f; }

int ItemScreen::Price(Item*, int) { return 0; }

void ItemTurn::OnShutdown() {}
