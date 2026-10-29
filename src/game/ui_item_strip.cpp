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

namespace {

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

} // namespace

void CUiItemStrip::Build(char top) {
    if (top) {
        top_ = 0;
        std::uint32_t bits = 0x41000000;   // 8.0
        scale_ = std::bit_cast<float>(bits);
    } else {
        top_ = 500;
        std::uint32_t bits = 0x40800000;   // 4.0
        scale_ = std::bit_cast<float>(bits);
    }
    rect_ = RECT{0, top_, 800, top_ + 100};
    for (float& position : position_) position = 0.0f;
    pending_ = 0.0f;
    velocity_ = 0.0f;
    scrollTimer_ = 0.0f;
    scrollDirection_ = 0;
    count_ = 0;
    selected_ = -1;
    tabsEnabled_ = true;

    // Regions 0 / 1 the scroll arrows, 2-4 and 5-7 the tab buttons at the left and right ends,
    // 8 the items.
    AddRect(this, 30, top_ + 35, 50, top_ + 65);
    AddRect(this, 750, top_ + 35, 770, top_ + 65);
    AddRect(this, 0, top_ + 5, 30, top_ + 35);
    AddRect(this, 0, top_ + 35, 30, top_ + 65);
    AddRect(this, 0, top_ + 65, 30, top_ + 95);
    AddRect(this, 770, top_ + 5, 800, top_ + 35);
    AddRect(this, 770, top_ + 35, 800, top_ + 65);
    AddRect(this, 770, top_ + 65, 800, top_ + 95);
    AddRect(this, 50, top_, 750, top_ + 100);

    const float y = static_cast<float>(top_);
    const float hundred = 100.0f;
    // The ends' faces.
    float depth = scale_ * 0.8f;
    AddBlankSprite()->SetRectangleVanilla(depth, 100.0f, 0.0f, y, 50.0f, y + hundred, 0xC0, 0xE, 0x8E, 0x72);
    depth = scale_ * 0.8f;
    AddBlankSprite()->SetRectangleVanilla(depth, 100.0f, 750.0f, y, 800.0f, y + hundred, 0x8E, 0xE, 0xC0, 0x72);
    // The tab buttons (y + 5, 35, 65, 95; depth scale * 0.6).
    const float y5 = 5.0f, y35 = 35.0f, y65 = 65.0f,
                y95 = 95.0f;
    struct Tab {
        float left, right;
        int u0, u1;
    };
    static constexpr Tab kTabs[2] = {{0.0f, 30.0f, 2, 0x2A}, {770.0f, 800.0f, 0x2C, 0x54}};
    for (const Tab& tab : kTabs) {
        const float tops[3] = {y5, y35, y65};
        const float bottoms[3] = {y35, y65, y95};
        static constexpr int kV[3][2] = {{0x82, 0xAA}, {0xAC, 0xD4}, {0xD6, 0xFE}};
        for (int i = 0; i < 3; ++i) {
            depth = scale_ * 0.6f;
            AddBlankSprite()->SetRectangleVanilla(depth, 80.0f, tab.left, y + tops[i], tab.right, y + bottoms[i], tab.u0, kV[i][0], tab.u1, kV[i][1]);
        }
    }
    // The highlight frame (plain, colour 0xFF8B8B37), moved around later.
    CUiMesh* frame = AddBlankSprite();
    frame->Framed(scale_ * 0.9f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flags_ |= 0x100;
    frame->flatColour_ = 0xFF8B8B37;
    frame->texture_ = nullptr;
    frame->visible_ = 0;
    // The background: eight cells between the ends (depth scale * 2), then a frame per 100 pixels.
    AddBlankSprite()->SetGridVanilla(scale_ + scale_, 100.0f, 50.0f, y, 850.0f, y + hundred, 0xE, 0xE, 0x72, 0x72, 8, 1);
    const float fifty = 50.0f, hundredFifty = 150.0f;
    for (int x = 0; x < 800; x += 100) {
        CUiMesh* cell = AddBlankSprite();
        const float fx = static_cast<float>(x);
        cell->Framed(scale_, 100.0f, fx + fifty, y, fx + hundredFifty, y + hundred, 15.0f, 0xE, 0xE, 0x72, 0x72);
    }
    tab_ = 5;
    SetTab(5);
    hovered_ = -1;
}

// `first` is the current tab's position truncated, `frac` what is left of it
// (kept as a float); the cells sit 100 pixels apart from x 100 - frac * 100.
void CForwardingStrip::Draw(CSceneRenderer* pass) {
    const float position = position_[tab_];
    const int first = static_cast<int>(position);
    const float frac = static_cast<float>(position - static_cast<float>(first));
    const float hundred = 100.0f, fifty = 50.0f, one = 1.0f;
    // The scroll arrows (sprites 0 and 1) show their dim frame while the strip can move that way.
    const float dim = 0.1953125f;
    SceneMesh(0)->textureOffsetU_ = position_[tab_] > 0.0f ? dim : 0.0f;
    SceneMesh(1)->textureOffsetU_ = static_cast<float>(std::ssize(list_) - 7) > position_[tab_] ? dim : 0.0f;

    SceneMesh(9)->Place(fifty - frac * hundred, static_cast<float>(top_), scale_ + scale_);
    for (int i = 0; i < 8; ++i) {
        SceneMesh(10 + i)
            ->Place(((static_cast<float>(i) - frac) + one) * hundred, static_cast<float>(top_) + fifty, scale_);
    }
    DrawBase(pass);

    CItem* shown[8];
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= std::ssize(list_)) {
            shown[i] = nullptr;
            continue;
        }
        ItemCell cell;
        cell.item = list_[index];
        shown[i] = list_[index];
        cell.size = 100.0f;
        cell.mode = mode_;
        cell.x = ((static_cast<float>(i) - frac) + one) * hundred;
        cell.y = static_cast<float>(top_) + fifty;
        cell.depth = scale_ * 1.5f;
        owner_->DrawItem(pass, &cell);   // the owner screen's item cell draw
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
    CUiSurface* counts = surfaces_[0];
    counts->Clear();
    for (int i = 0; i < 8; ++i) {
        if (!shown[i]) continue;
        int count = static_cast<int>(shown[i]->count_);
        if (count == 1) continue;
        text = str::Format(kNumber, count);
        RECT rect = cellRect(i);
        counts->DrawLine(text, &rect, 1, 0xA7D7E4, 2, 1);
    }
    CUiSurface* numbers = surfaces_[1];
    numbers->Clear();
    for (int i = 0; i < 8; ++i) {
        CItem* item = shown[i];
        if (!item || slots_ == 9) continue;
        if (!owner_->CanEquip(item, mode_)) continue;     // whether the strip's mode takes the item
        text = str::Format(kNumber, owner_->Price(item, slots_));   // the number shown under it
        RECT rect = cellRect(i);
        numbers->DrawLine(text, &rect, 1, 0xA7D7E4, 1, 1);
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
    if (scrollDirection_ != 0) {
        int steps = 0;
        EI_NAN_AUDIT(scrollTimer_);
        if (scrollTimer_ == zero) {
            steps = 1;
            const std::uint32_t first = 0x3A83126F;   // 0.001
            scrollTimer_ = std::bit_cast<float>(first);
        } else {
            scrollTimer_ = static_cast<float>(dt + scrollTimer_);
            while (scrollTimer_ > repeat) {
                scrollTimer_ = static_cast<float>(scrollTimer_ - repeat);
                ++steps;
            }
        }
        pending_ = scrollDirection_ == 1 ? pending_ - static_cast<float>(steps) : static_cast<float>(steps) + pending_;
    }

    EI_NAN_AUDIT(pending_);
    if (pending_ != zero) {
        float speed;
        if (std::fabs(pending_) > nearDistance) {
            // Far: accelerate towards the target up to the top speed, but no faster than reaches
            // the easing distance in one frame; the average of that and the new speed.
            const float gain = accel * dt;
            float limited;
            if (pending_ > zero) {
                velocity_ = static_cast<float>(gain + velocity_);
                if (velocity_ > maxSpeed) velocity_ = maxSpeed;
                const float limit = static_cast<float>((pending_ - nearDistance) / dt);
                const float current = velocity_;
                limited = current >= limit ? limit : current;
            } else {
                velocity_ = static_cast<float>(velocity_ - gain);
                const float minSpeed = -maxSpeed;
                velocity_ = velocity_ >= minSpeed ? velocity_ : minSpeed;
                const float limit = static_cast<float>((nearDistance + pending_) / dt);
                const float current = velocity_;
                limited = limit >= current ? limit : current;
            }
            speed = (limited + velocity_) * half;
        } else if (pending_ > zero) {
            // Near: the speed that eases out (sqrt(0.5 d) / 0.25).
            speed = static_cast<float>(std::sqrt(static_cast<double>(nearDistance * pending_)) / easeDivisor);
        } else {
            speed = -static_cast<float>(std::sqrt(static_cast<double>(-(nearDistance * pending_))) / easeDivisor);
        }
        velocity_ = static_cast<float>(speed);
        float step = static_cast<float>(speed * dt);
        step = std::fabs(pending_) >= std::fabs(step) ? step : pending_;
        position_[tab_] = static_cast<float>(step + position_[tab_]);
        pending_ = static_cast<float>(pending_ - step);

        // Settle on a whole slot when the remaining scroll is tiny and the position is within
        // 0.01 above or 0.01 below one.
        const float whole = static_cast<float>(static_cast<int>(position_[tab_]));
        bool done = false;
        EI_NAN_AUDIT(position_[tab_], pending_);
        if (position_[tab_] - whole < snapLow && std::fabs(pending_) < snapPending) {
            position_[tab_] = whole;
            pending_ = 0.0f;
            done = true;
        }
        if (!done && position_[tab_] - whole > snapHigh && std::fabs(pending_) < snapPending) {
            position_[tab_] = static_cast<float>(whole + one);
            pending_ = 0.0f;
        }
    }
    EI_NAN_AUDIT(pending_);
    if (pending_ == zero) velocity_ = 0.0f;

    RebuildList();
    // Kept within [0, size - 7].
    const float last = static_cast<float>(count_ - 7);
    EI_NAN_AUDIT(position_[tab_]);
    if (last < position_[tab_]) {
        position_[tab_] = last;
        pending_ = 0.0f;
        velocity_ = 0.0f;
    }
    if (position_[tab_] < zero) {
        position_[tab_] = 0.0f;
        pending_ = 0.0f;
        velocity_ = 0.0f;
    }
}
