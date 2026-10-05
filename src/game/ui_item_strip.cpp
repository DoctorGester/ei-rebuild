// The item strip's layout (00620380): the camp's and the hero screen's strips of items along the
// top or bottom edge. See ui_screens.h (CUiItemStrip::Build) and docs/notes/ui.md ("Tenth pass").
#include "ui_screens.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

// Sprite builders through their original addresses (005E70E0 and 005E7680 are Rebirth-patched):
// 005E70E0 a textured rectangle (RET 0x28), 005E8450 a framed rectangle (RET 0x2C), 005E7680 a
// rectangle of `count` repeated cells (RET 0x30).
__declspec(naked) void __fastcall StripRectangle(CUiMesh*, void*, float /*depth*/, float /*scale*/, float, float, float,
                                                 float, int, int, int, int) EI_ORIGINAL(0x005E70E0)
__declspec(naked) void __fastcall StripFrame(CUiMesh*, void*, float /*depth*/, float /*scale*/, float, float, float, float,
                                             float /*border*/, int, int, int, int) EI_ORIGINAL(0x005E8450)
__declspec(naked) void __fastcall StripCells(CUiMesh*, void*, float /*depth*/, float /*scale*/, float, float, float, float,
                                             int, int, int, int, int /*count*/, int) EI_ORIGINAL(0x005E7680)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

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
    unknown_17C = -1;
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
