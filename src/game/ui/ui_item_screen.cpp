// The item screens' common setup (the camp, the multiplayer hero screen and the exchange window):
// the caches' trimming, the base Create and the icon window's
// frame. See ui_item_info.h and docs/notes/ui.md ("Item descriptions").
#include "game/app/app.h"

#include "engine/core/core_streams.h"
#include "engine/core/engine_object.h"
#include "engine/models/figure_manager.h"
#include "engine/models/figure_mesh.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/players/party.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_screens.h"
#include "game/world/world.h"

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
        sprite()->SetRectangle(7.8f, kSpriteScale, left, 100.0f, middle, 130.0f, 0xFE, 2, 0x9A, 0x20);
        sprite()->SetRectangle(7.8f, kSpriteScale, left, 470.0f, middle, 500.0f, 0xFE, 0x20, 0x9A, 2);
        float half = static_cast<float>(x + 100);
        sprite()->SetRectangle(7.8f, kSpriteScale, half, 100.0f, right, 130.0f, 0x9A, 2, 0xFE, 0x20);
        sprite()->SetRectangle(7.8f, kSpriteScale, half, 470.0f, right, 500.0f, 0x9A, 0x20, 0xFE, 2);
    }
    leftBar = sprite();
    leftBar->SetRectangle(7.8f, kSpriteScale, 0.0f, 380.0f, 100.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    rightBar = sprite();
    rightBar->SetRectangle(7.8f, kSpriteScale, 100.0f, 380.0f, 200.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangle(7.8f, kSpriteScale, 600.0f, 380.0f, 700.0f, 420.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangle(7.8f, kSpriteScale, 700.0f, 380.0f, 800.0f, 420.0f, 0x9A, 0x22, 0xFE, 0x4A);
    sprite()->SetRectangle(7.8f, kSpriteScale, 200.0f, 280.0f, 300.0f, 320.0f, 0xFE, 0x22, 0x9A, 0x4A);
    sprite()->SetRectangle(7.8f, kSpriteScale, 300.0f, 280.0f, 400.0f, 320.0f, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The first two bars moved ------------------------------------------------------------

void ItemIconWindow::PlaceBars(int y) {
    float top = static_cast<float>(y - 20);
    float bottom = static_cast<float>(y + 20);
    leftBar->SetRectangle(7.8f, kSpriteScale, 0.0f, top, 100.0f, bottom, 0xFE, 0x22, 0x9A, 0x4A);
    rightBar->SetRectangle(7.8f, kSpriteScale, 100.0f, top, 200.0f, bottom, 0x9A, 0x22, 0xFE, 0x4A);
}

// --- The item screens' base Create ------------------------------------------------------

void ItemScreen::CreateBase() {
    ItemIconWindow* icons = AddChild(std::make_unique<ItemIconWindow>());
    iconWindow = icons;
    iconWindow->AddObject(objects[0]);
    iconWindow->Build();
    LoadTexture("modifiers2", TextureKind::Interface);
    LoadTexture("campslots", TextureKind::Interface);
    g_sceneRenderer.fogColour = 0;
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

Item* ItemScreen::MoveItem(Item* item, Party* to, Party* from, int limit, bool merge, bool turning) {
    bool whole = merge && g_interface.shiftDown != 0;
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
        if (!moved->turn) {   // a null item faults here, like the original
            moved->turn.Assign(new ItemTurn());
        }
        moved->turn->flag = true;
    }
    return moved;
}

Item* ItemScreen::ReturnAll(Party* to, Party* from) {
    Item* moved = nullptr;
    while (!from->items.empty()) {
        Item* first = from->items[0];
        moved = to->AddItem(first, true, true);
        from->RemoveItem(first, true);
    }
    return moved;
}

// --- Default answers of the item screen's slots ------------------------------------------------------

bool ItemScreen::CanEquip(Item*, int) { return false; }

// --- The item panels' owners ------------------------------------------------------------------------

UiItemPanel::UiItemPanel(CampPages* newCamp) : owner(newCamp), camp(newCamp) {}

UiItemPanel::UiItemPanel(MpHeroScreen* newHeroScreen)
    : owner(newHeroScreen), heroScreen(newHeroScreen) {}

// --- Ui item model -----------------------------------------------------------------------------------

// An item's model for the item screens' model view (ui_item_view.cpp caches it). See
// docs/notes/ui.md, section 9.

namespace {
// The stand-in for a model the manager does not have: a 100 x 100 sprite (layout 0..100 x 100..0
// at depth 10, scale 70) over texels 0..128.
constexpr float kDepth = 10.0f;
constexpr float kScale = 70.0f;
constexpr float kSize = 100.0f;
constexpr int kTexels = 0x80;
} // namespace

// The model manager's object `name`, else a new interface sprite standing in for it; one reference
// taken for the caller. A failed sprite is not checked (neither is it in the original).
UiMesh* ItemModel(std::string_view name) {
    FigureManager* manager = g_clientWorld.figureManager.get();
    // The interface's model manager makes class 0x43 objects: interface sprites.
    UiMesh* model = manager->NewFigure(name);
    if (!model) {
        model = manager->NewBlankFigure();
        model->SetRectangle(kDepth, kScale, 0.0f, kSize, kSize, 0.0f, 0, 0, kTexels, kTexels);
        model->FitVertexBounds();
    }
    model->AddRef();
    return model;
}

// --- Ui item panel input -----------------------------------------------------------------------------

// The item panels' shared left-button handler (of the description panel, the hero equipment, the quick
// strip, the trade and repair grids, both constructor pages and the blank panel). See docs/notes/ui.md,
// section 5.

void UiItemPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    default: break;
    }
}

void UiItemPanel::LeftDown(UiEvent& event) {
    const ItemUnder under = ItemAt(event.at);
    if (under.item) {
        owner->TakeItem(under.item, mode);
        return;
    }
    if (under.topic && camp) camp->TopicClicked(under.topic);
}

ItemUnder UiItemPanel::ItemAt(Point) { return {}; }
