// The item strip's layout (00620380): the camp's and the hero screen's strips of items along the
// top or bottom edge, and the forwarding strip's draw (00621B50). See ui_screens.h
// (CUiItemStrip::Build, Draw) and docs/notes/ui.md ("Tenth pass", "Fourteenth pass").
#include "ui_item_info.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include "originals_ui.h"

namespace {

// Sprite builders through their original addresses (005E70E0 and 005E7680 are Rebirth-patched):
// 005E70E0 a textured rectangle (RET 0x28), 005E8450 a framed rectangle (RET 0x2C), 005E7680 a
// rectangle of `count` repeated cells (RET 0x30).
void __fastcall StripRectangle(CUiMesh* a0, void*, float a2, float a3, float a4, float a5, float a6, float a7, int a8, int a9, int a10, int a11) { UiMeshRectangleOriginal((void*)a0, nullptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11); }
void __fastcall StripFrame(CUiMesh* a0, void*, float a2, float a3, float a4, float a5, float a6, float a7, float a8, int a9, int a10, int a11, int a12) { UiMeshFramedOriginal((void*)a0, nullptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12); }
void __fastcall StripCells(CUiMesh* a0, void*, float a2, float a3, float a4, float a5, float a6, float a7, int a8, int a9, int a10, int a11, int a12, int a13) { UiMeshCellsOriginal((void*)a0, nullptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13); }

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The original keeps this value in a float local (FSTP dword) and reads it back.
float StoreFloat(float value) {
    volatile float stored = value;
    return stored;
}


// A surface's DC (005FDC00 / 005FDC60), its clear (005FE2F0) and a line of text (005FDE20,
// Rebirth-patched).
struct StripSurface {
    void* GetDC() { return SurfaceGetDC(this); }
    void Clear() { SurfaceClearOriginal(this); }
    int Text(const char* text, RECT* rect, int font, std::uint32_t colour, int format, int flag);
    void ReleaseDC() { SurfaceReleaseDC(this); }
};
int StripSurface::Text(const char* text, RECT* rect, int format, std::uint32_t colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

// The owner's virtual slots 43 (whether the strip's mode takes the item; AL) and 54 (a number
// shown under the item for mode `mode`).
bool OwnerTakes(CUiWindow* owner, char* item, int mode) {
    using Fn = bool(__thiscall*)(CUiWindow*, char*, int);
    return reinterpret_cast<Fn>(owner->vtable_[0xAC / 4])(owner, item, mode);
}
int OwnerNumber(CUiWindow* owner, char* item, int mode) {
    using Fn = int(__thiscall*)(CUiWindow*, char*, int);
    return reinterpret_cast<Fn>(owner->vtable_[0xD8 / 4])(owner, item, mode);
}

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    return region;
}

} // namespace

void CUiItemStrip::Build(char top) {
    if (top) {
        top_ = 0;
        std::uint32_t bits = 0x41000000;   // 8.0
        std::memcpy(&scale_, &bits, 4);
    } else {
        top_ = 500;
        std::uint32_t bits = 0x40800000;   // 4.0
        std::memcpy(&scale_, &bits, 4);
    }
    window.rect_ = RECT{0, top_, 800, top_ + 100};
    for (float& position : position_) std::memset(&position, 0, 4);
    std::memset(&pending_, 0, 4);
    std::memset(&velocity_, 0, 4);
    std::memset(&scrollTimer_, 0, 4);
    scrollDirection_ = 0;
    count_ = 0;
    selected_ = -1;
    tabsEnabled_ = true;

    // Regions 0 / 1 the scroll arrows, 2-4 and 5-7 the tab buttons at the left and right ends,
    // 8 the items.
    AddRect(&window, 30, top_ + 35, 50, top_ + 65);
    AddRect(&window, 750, top_ + 35, 770, top_ + 65);
    AddRect(&window, 0, top_ + 5, 30, top_ + 35);
    AddRect(&window, 0, top_ + 35, 30, top_ + 65);
    AddRect(&window, 0, top_ + 65, 30, top_ + 95);
    AddRect(&window, 770, top_ + 5, 800, top_ + 35);
    AddRect(&window, 770, top_ + 35, 800, top_ + 65);
    AddRect(&window, 770, top_ + 65, 800, top_ + 95);
    AddRect(&window, 50, top_, 750, top_ + 100);

    const float y = static_cast<float>(top_);
    const float hundred = GameFloat(0x0073BAB0);   // 100.0
    // The ends' faces.
    float depth = scale_ * GameFloat(0x0073CCA8);  // 0.8
    StripRectangle(window.AddSprite(0), nullptr, depth, 100.0f, 0.0f, y, 50.0f, y + hundred, 0xC0, 0xE, 0x8E, 0x72);
    depth = scale_ * GameFloat(0x0073CCA8);
    StripRectangle(window.AddSprite(0), nullptr, depth, 100.0f, 750.0f, y, 800.0f, y + hundred, 0x8E, 0xE, 0xC0, 0x72);
    // The tab buttons (y + 5, 35, 65, 95; depth scale * 0.6).
    const float y5 = GameFloat(0x0073BF34), y35 = GameFloat(0x007403E0), y65 = GameFloat(0x00741350),
                y95 = GameFloat(0x00741F08);
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
            depth = scale_ * GameFloat(0x0073CDE0);   // 0.6
            StripRectangle(window.AddSprite(0), nullptr, depth, 80.0f, tab.left, y + tops[i], tab.right, y + bottoms[i],
                           tab.u0, kV[i][0], tab.u1, kV[i][1]);
        }
    }
    // The highlight frame (plain, colour 0xFF8B8B37), moved around later.
    CUiMesh* frame = window.AddSprite(0);
    StripFrame(frame, nullptr, scale_ * GameFloat(0x0073CD70), 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100,
               0x100);
    auto* bytes = reinterpret_cast<std::uint8_t*>(frame);
    std::uint32_t flags;
    std::memcpy(&flags, bytes + 0x08, 4);
    flags |= 0x100;
    std::memcpy(bytes + 0x08, &flags, 4);
    const std::uint32_t colour = 0xFF8B8B37;
    const std::uint32_t zero = 0;
    std::memcpy(bytes + 0xD4, &colour, 4);
    std::memcpy(bytes + 0x280, &zero, 4);
    std::memcpy(bytes + 0x220, &zero, 4);
    // The background: eight cells between the ends (depth scale * 2), then a frame per 100 pixels.
    StripCells(window.AddSprite(0), nullptr, scale_ + scale_, 100.0f, 50.0f, y, 850.0f, y + hundred, 0xE, 0xE, 0x72, 0x72,
               8, 1);
    const float fifty = GameFloat(0x0074023C), hundredFifty = GameFloat(0x0073D618);
    for (int x = 0; x < 800; x += 100) {
        CUiMesh* cell = window.AddSprite(0);
        const float fx = static_cast<float>(x);
        StripFrame(cell, nullptr, scale_, 100.0f, fx + fifty, y, fx + hundredFifty, y + hundred, 15.0f, 0xE, 0xE, 0x72,
                   0x72);
    }
    tab_ = 5;
    SetTab(5);
    hovered_ = -1;
}
EI_REPLACE_METHOD(0x00620380, CUiItemStrip::Build);

// 00621B50 (RET 4). `first` is the current tab's position truncated, `frac` what is left of it
// (kept as a float); the cells sit 100 pixels apart from x 100 - frac * 100.
BOOL CUiItemStrip::Draw(int pass) {
    const float position = position_[tab_];
    const int first = static_cast<int>(position);
    const float frac = StoreFloat(position - static_cast<float>(first));
    const float hundred = GameFloat(0x0073BAB0), fifty = GameFloat(0x0074023C), one = GameFloat(0x0073B84C);
    CEngineObject** sprites = window.sceneObjects_.data;
    const std::uint32_t dim = 0x3E480000;   // 0.1953125
    auto* left = reinterpret_cast<std::uint8_t*>(sprites[0]);
    auto* right = reinterpret_cast<std::uint8_t*>(sprites[1]);
    *reinterpret_cast<std::uint32_t*>(left + 0xCC) = position_[tab_] > GameFloat(0x0073B858) ? dim : 0u;
    *reinterpret_cast<std::uint32_t*>(right + 0xCC) =
        static_cast<float>(list_.size - 7) > position_[tab_] ? dim : 0u;

    reinterpret_cast<CUiMesh*>(sprites[9])->Place(fifty - frac * hundred, static_cast<float>(top_), scale_ + scale_);
    for (int i = 0; i < 8; ++i) {
        reinterpret_cast<CUiMesh*>(window.sceneObjects_.data[10 + i])
            ->Place(((static_cast<float>(i) - frac) + one) * hundred, static_cast<float>(top_) + fifty, scale_);
    }
    window.DrawBase(pass);

    char* shown[8];
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= list_.size) {
            shown[i] = nullptr;
            continue;
        }
        ItemCell cell;
        cell.item = reinterpret_cast<CItem*>(list_.data[index]);
        shown[i] = list_.data[index];
        cell.size = 100.0f;
        cell.mode = actionMode_;
        cell.x = ((static_cast<float>(i) - frac) + one) * hundred;
        cell.y = static_cast<float>(top_) + fifty;
        cell.depth = scale_ * GameFloat(0x0073C51C);   // 1.5
        ItemScreenDrawItemOriginal(reinterpret_cast<CItemScreen*>(owner_), nullptr, pass, &cell);   // the owner screen's item cell draw
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
    const char* const kNumber = reinterpret_cast<const char*>(0x0078EEEC);   // "%d"
    mfc::CString text;
    auto* counts = reinterpret_cast<StripSurface*>(window.surfaces_.data[0]);
    counts->GetDC();
    counts->Clear();
    for (int i = 0; i < 8; ++i) {
        if (!shown[i]) continue;
        int count = *reinterpret_cast<const int*>(shown[i] + 0x48);
        if (count == 1) continue;
        text.Format(kNumber, count);
        RECT rect = cellRect(i);
        counts->Text(text, &rect, 1, 0xA7D7E4, 2, 1);
    }
    counts->ReleaseDC();
    auto* numbers = reinterpret_cast<StripSurface*>(window.surfaces_.data[1]);
    numbers->GetDC();
    numbers->Clear();
    for (int i = 0; i < 8; ++i) {
        char* item = shown[i];
        if (!item || slots_ == 9) continue;
        if (!OwnerTakes(owner_, item, actionMode_)) continue;
        text.Format(kNumber, OwnerNumber(owner_, item, slots_));
        RECT rect = cellRect(i);
        numbers->Text(text, &rect, 1, 0xA7D7E4, 1, 1);
    }
    numbers->ReleaseDC();
    return 1;
}
namespace {
BOOL __fastcall ItemStripDraw(CUiItemStrip* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x00621B50, ItemStripDraw);

// --- 00620A50: the strip's per-frame scroll ---------------------------------------------------

namespace {

// 00600270: the time since the last frame in seconds (ticks * 0.001f, unrounded in ST0).
inline double __fastcall StripFrameTime(void* w) { return WindowFrameTimeOriginal(w); }

// FCOMP; TEST AH,0x40: equal, or unordered.
bool EqualOrNaN(float a, float b) { return !(a < b) && !(a > b); }

} // namespace

void CUiItemStrip::Update() {
    const float zero = GameFloat(0x0073B858);
    const float repeat = GameFloat(0x00741EC0);      // 0.1 s per slot while an arrow is held
    const float nearDistance = GameFloat(0x00741EB4);  // 0.5 slot: closer than this the scroll eases out
    const float easeDivisor = GameFloat(0x00741EB8); // 0.25
    const float accel = GameFloat(0x00741EBC);       // 15 slots/s^2
    const float maxSpeed = GameFloat(0x00741EB0);    // 7 slots/s
    const float half = GameFloat(0x0073B86C);
    const float snapLow = GameFloat(0x0073C548);     // 0.01
    const float snapPending = GameFloat(0x0073CCD8); // 0.02
    const float snapHigh = GameFloat(0x00741F0C);    // 0.99
    const float one = GameFloat(0x0073B84C);
    const float dt = StoreFloat(static_cast<float>(StripFrameTime(this)));

    // An arrow held: one slot at once, then one more every `repeat` seconds.
    if (scrollDirection_ != 0) {
        int steps = 0;
        if (EqualOrNaN(scrollTimer_, zero)) {
            steps = 1;
            const std::uint32_t first = 0x3A83126F;   // 0.001
            std::memcpy(&scrollTimer_, &first, 4);
        } else {
            scrollTimer_ = StoreFloat(dt + scrollTimer_);
            while (scrollTimer_ > repeat) {
                scrollTimer_ = StoreFloat(scrollTimer_ - repeat);
                ++steps;
            }
        }
        pending_ = scrollDirection_ == 1 ? pending_ - static_cast<float>(steps) : static_cast<float>(steps) + pending_;
    }

    if (!EqualOrNaN(pending_, zero)) {
        float speed;
        if (std::fabs(pending_) > nearDistance) {
            // Far: accelerate towards the target up to the top speed, but no faster than reaches
            // the easing distance in one frame; the average of that and the new speed.
            const float gain = accel * dt;
            float limited;
            if (pending_ > zero) {
                velocity_ = StoreFloat(gain + velocity_);
                if (velocity_ > maxSpeed) velocity_ = maxSpeed;
                const float limit = StoreFloat((pending_ - nearDistance) / dt);
                const float current = velocity_;
                limited = !(current >= limit) ? current : limit;
            } else {
                velocity_ = StoreFloat(velocity_ - gain);
                const float minSpeed = -maxSpeed;
                if (!(velocity_ >= minSpeed)) velocity_ = minSpeed;
                const float limit = StoreFloat((nearDistance + pending_) / dt);
                const float current = velocity_;
                limited = !(limit >= current) ? current : limit;
            }
            speed = (limited + velocity_) * half;
        } else if (pending_ > zero) {
            // Near: the speed that eases out (sqrt(0.5 d) / 0.25).
            speed = static_cast<float>(std::sqrt(static_cast<double>(nearDistance * pending_)) / easeDivisor);
        } else {
            speed = -static_cast<float>(std::sqrt(static_cast<double>(-(nearDistance * pending_))) / easeDivisor);
        }
        velocity_ = StoreFloat(speed);
        float step = StoreFloat(speed * dt);
        if (!(std::fabs(pending_) >= std::fabs(step))) step = pending_;
        position_[tab_] = StoreFloat(step + position_[tab_]);
        pending_ = StoreFloat(pending_ - step);

        // Settle on a whole slot when the remaining scroll is tiny and the position is within
        // 0.01 above or 0.01 below one.
        const float whole = static_cast<float>(static_cast<int>(position_[tab_]));
        bool done = false;
        if (!(position_[tab_] - whole >= snapLow) && !(std::fabs(pending_) >= snapPending)) {
            position_[tab_] = whole;
            pending_ = 0.0f;
            done = true;
        }
        if (!done && position_[tab_] - whole > snapHigh && !(std::fabs(pending_) >= snapPending)) {
            position_[tab_] = StoreFloat(whole + one);
            pending_ = 0.0f;
        }
    }
    if (EqualOrNaN(pending_, zero)) velocity_ = 0.0f;

    CallRebuildList();
    // Kept within [0, size - 7].
    const float last = static_cast<float>(count_ - 7);
    if (!(last >= position_[tab_])) {
        position_[tab_] = last;
        pending_ = 0.0f;
        velocity_ = 0.0f;
    }
    if (!(position_[tab_] >= zero)) {
        position_[tab_] = 0.0f;
        pending_ = 0.0f;
        velocity_ = 0.0f;
    }
}
namespace {
void __fastcall ItemStripUpdate(CUiItemStrip* self) { self->Update(); }
} // namespace
EI_REPLACE(0x00620A50, ItemStripUpdate);
