// The item strip's layout: the camp's and the hero screen's strips of items along the
// top or bottom edge, and the forwarding strip's draw. See ui_screens.h
// (CUiItemStrip::Build, Draw) and docs/notes/ui.md ("Camp, global map, hero screens").
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

#include <cmath>
#include "lib/nan_audit.h"
#include "lib/str.h"

void CUiItemStrip::Build(bool atTop) {
    top = atTop ? 0 : 500;
    scale = atTop ? 8.0f : 4.0f;
    rect = RECT{0, top, 800, top + 100};
    for (float& tabPosition : position) tabPosition = 0.0f;
    pending = 0.0f;
    velocity = 0.0f;
    scrollTimer = 0.0f;
    scrollDirection = 0;
    count = 0;
    selected = -1;
    tabsEnabled = true;

    // Regions 0 / 1 the scroll arrows, 2-4 and 5-7 the tab buttons at the left and right ends,
    // 8 the items.
    auto region = [this](int left, int atTop, int right, int bottom) { AddRegion()->SetRect(left, atTop, right, bottom); };
    region(30, top + 35, 50, top + 65);
    region(750, top + 35, 770, top + 65);
    region(0, top + 5, 30, top + 35);
    region(0, top + 35, 30, top + 65);
    region(0, top + 65, 30, top + 95);
    region(770, top + 5, 800, top + 35);
    region(770, top + 35, 800, top + 65);
    region(770, top + 65, 800, top + 95);
    region(50, top, 750, top + 100);

    const float y = static_cast<float>(top);
    const float hundred = 100.0f;
    // The ends' faces.
    float depth = scale * 0.8f;
    AddBlankSprite()->SetRectangle(depth, 100.0f, 0.0f, y, 50.0f, y + hundred, 0xC0, 0xE, 0x8E, 0x72);
    depth = scale * 0.8f;
    AddBlankSprite()->SetRectangle(depth, 100.0f, 750.0f, y, 800.0f, y + hundred, 0x8E, 0xE, 0xC0, 0x72);
    // The tab buttons (y + 5, 35, 65, 95; depth scale * 0.6).
    const float y5 = 5.0f, y35 = 35.0f, y65 = 65.0f,
                y95 = 95.0f;
    struct Tab {
        float left, right;
        int u0, u1;
    };
    static constexpr Tab kTabs[2] = {{0.0f, 30.0f, 2, 0x2A}, {770.0f, 800.0f, 0x2C, 0x54}};
    for (const Tab& entry : kTabs) {
        const float tops[3] = {y5, y35, y65};
        const float bottoms[3] = {y35, y65, y95};
        static constexpr int kV[3][2] = {{0x82, 0xAA}, {0xAC, 0xD4}, {0xD6, 0xFE}};
        for (int i = 0; i < 3; ++i) {
            depth = scale * 0.6f;
            AddBlankSprite()->SetRectangle(depth, 80.0f, entry.left, y + tops[i], entry.right, y + bottoms[i], entry.u0, kV[i][0], entry.u1, kV[i][1]);
        }
    }
    // The highlight frame (plain, colour 0xFF8B8B37), moved around later.
    CUiMesh* frame = AddBlankSprite();
    frame->Framed(scale * 0.9f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flags |= FigureMeshFlag::Flat;
    frame->flatColour = 0xFF8B8B37;
    frame->texture = nullptr;
    frame->visible = false;
    // The background: eight cells between the ends (depth scale * 2), then a frame per 100 pixels.
    AddBlankSprite()->SetGrid(scale + scale, 100.0f, 50.0f, y, 850.0f, y + hundred, 0xE, 0xE, 0x72, 0x72, 8, 1);
    const float fifty = 50.0f, hundredFifty = 150.0f;
    for (int x = 0; x < 800; x += 100) {
        CUiMesh* cell = AddBlankSprite();
        const float fx = static_cast<float>(x);
        cell->Framed(scale, 100.0f, fx + fifty, y, fx + hundredFifty, y + hundred, 15.0f, 0xE, 0xE, 0x72, 0x72);
    }
    tab = 5;
    SetTab(5);
    hovered = -1;
}

// `first` is the current tab's position truncated, `frac` what is left of it
// (kept as a float); the cells sit 100 pixels apart from x 100 - frac * 100.
void CForwardingStrip::Draw(CSceneRenderer* pass) {
    const float scrollPosition = position[tab];
    const int first = static_cast<int>(scrollPosition);
    const float frac = static_cast<float>(scrollPosition - static_cast<float>(first));
    const float hundred = 100.0f, fifty = 50.0f, one = 1.0f;
    // The scroll arrows (sprites 0 and 1) show their dim frame while the strip can move that way.
    const float dim = 0.1953125f;
    SceneMesh(0)->textureOffsetU = position[tab] > 0.0f ? dim : 0.0f;
    SceneMesh(1)->textureOffsetU = static_cast<float>(std::ssize(list) - 7) > position[tab] ? dim : 0.0f;

    SceneMesh(9)->Place(fifty - frac * hundred, static_cast<float>(top), scale + scale);
    for (int i = 0; i < 8; ++i) {
        SceneMesh(10 + i)
            ->Place(((static_cast<float>(i) - frac) + one) * hundred, static_cast<float>(top) + fifty, scale);
    }
    DrawBase(pass);

    CItem* shown[8];
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= std::ssize(list)) {
            shown[i] = nullptr;
            continue;
        }
        shown[i] = list[index];
        const ItemCell cell{list[index], ((static_cast<float>(i) - frac) + one) * hundred, static_cast<float>(top) + fifty,
                            scale * 1.5f, 100.0f, mode};
        owner->DrawItem(pass, cell);
    }

    // The text rectangle of cell i: 70 x 20 from 15 pixels into the cell.
    auto cellRect = [&](int i) {
        int x = static_cast<int>((static_cast<float>(i) - frac) * hundred);
        RECT rect;
        rect.left = x + 0xF;
        rect.top = 0;
        rect.right = x + 0x55;
        rect.bottom = 0x14;
        return rect;
    };
    const char* const kNumber = "%d";
    std::string text;
    CUiSurface* counts = surfaces[0];
    counts->Clear();
    for (int i = 0; i < 8; ++i) {
        if (!shown[i]) continue;
        int stackCount = static_cast<int>(shown[i]->count);
        if (stackCount == 1) continue;
        text = str::Format(kNumber, stackCount);
        RECT area = cellRect(i);
        counts->DrawTextIn(text, &area, 1, 0xA7D7E4, 2, 1);
    }
    CUiSurface* numbers = surfaces[1];
    numbers->Clear();
    for (int i = 0; i < 8; ++i) {
        CItem* item = shown[i];
        if (!item || slots == 9) continue;
        if (!owner->CanEquip(item, mode)) continue;       // whether the strip's mode takes the item
        text = str::Format(kNumber, owner->Price(item, slots));     // the number shown under it
        RECT area = cellRect(i);
        numbers->DrawTextIn(text, &area, 1, 0xA7D7E4, 1, 1);
    }
}

// --- The strip's per-frame scroll ---------------------------------------------------

void CUiItemStrip::Update() {
    const float zero = 0.0f;
    const float repeat = 0.1f;      // seconds per slot while an arrow is held
    const float nearDistance = 0.5f;  // slots: closer than this the scroll eases out
    const float easeDivisor = 0.25f;
    const float accel = 15.0f;       // slots/s^2
    const float maxSpeed = 7.0f;    // slots/s
    const float half = 0.5f;
    const float snapLow = 0.01f;
    const float snapPending = 0.02f;
    const float snapHigh = 0.99f;
    const float one = 1.0f;
    const float dt = static_cast<float>(CUiContainer::FrameTime());

    // An arrow held: one slot at once, then one more every `repeat` seconds.
    if (scrollDirection != 0) {
        int steps = 0;
        EI_NAN_AUDIT(scrollTimer);
        if (scrollTimer == zero) {
            steps = 1;
            scrollTimer = 0.001f;
        } else {
            scrollTimer = static_cast<float>(dt + scrollTimer);
            while (scrollTimer > repeat) {
                scrollTimer = static_cast<float>(scrollTimer - repeat);
                ++steps;
            }
        }
        pending = scrollDirection == 1 ? pending - static_cast<float>(steps) : static_cast<float>(steps) + pending;
    }

    EI_NAN_AUDIT(pending);
    if (pending != zero) {
        float speed;
        if (std::fabs(pending) > nearDistance) {
            // Far: accelerate towards the target up to the top speed, but no faster than reaches
            // the easing distance in one frame; the average of that and the new speed.
            const float gain = accel * dt;
            float limited;
            if (pending > zero) {
                velocity = static_cast<float>(gain + velocity);
                if (velocity > maxSpeed) velocity = maxSpeed;
                const float limit = static_cast<float>((pending - nearDistance) / dt);
                const float current = velocity;
                limited = current >= limit ? limit : current;
            } else {
                velocity = static_cast<float>(velocity - gain);
                const float minSpeed = -maxSpeed;
                velocity = velocity >= minSpeed ? velocity : minSpeed;
                const float limit = static_cast<float>((nearDistance + pending) / dt);
                const float current = velocity;
                limited = limit >= current ? limit : current;
            }
            speed = (limited + velocity) * half;
        } else if (pending > zero) {
            // Near: the speed that eases out (sqrt(0.5 d) / 0.25).
            speed = static_cast<float>(std::sqrt(static_cast<double>(nearDistance * pending)) / easeDivisor);
        } else {
            speed = -static_cast<float>(std::sqrt(static_cast<double>(-(nearDistance * pending))) / easeDivisor);
        }
        velocity = static_cast<float>(speed);
        float step = static_cast<float>(speed * dt);
        step = std::fabs(pending) >= std::fabs(step) ? step : pending;
        position[tab] = static_cast<float>(step + position[tab]);
        pending = static_cast<float>(pending - step);

        // Settle on a whole slot when the remaining scroll is tiny and the position is within
        // 0.01 above or 0.01 below one.
        const float whole = static_cast<float>(static_cast<int>(position[tab]));
        bool done = false;
        EI_NAN_AUDIT(position[tab], pending);
        if (position[tab] - whole < snapLow && std::fabs(pending) < snapPending) {
            position[tab] = whole;
            pending = 0.0f;
            done = true;
        }
        if (!done && position[tab] - whole > snapHigh && std::fabs(pending) < snapPending) {
            position[tab] = static_cast<float>(whole + one);
            pending = 0.0f;
        }
    }
    EI_NAN_AUDIT(pending);
    if (pending == zero) velocity = 0.0f;

    RebuildList();
    // Kept within [0, size - 7].
    const float last = static_cast<float>(count - 7);
    EI_NAN_AUDIT(position[tab]);
    if (last < position[tab]) {
        position[tab] = last;
        pending = 0.0f;
        velocity = 0.0f;
    }
    if (position[tab] < zero) {
        position[tab] = 0.0f;
        pending = 0.0f;
        velocity = 0.0f;
    }
}
