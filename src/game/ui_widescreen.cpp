// Rebirth's WideScreen patch: the layout addon.dll computes, the layout widths it writes and its
// scaling. See ui_widescreen.h.
#include "ui_widescreen.h"

#include "ui.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "hook/log.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace widescreen {
namespace {

// The layout widths addon rewrites (rewrite-guide rule 9: every reader reads them from memory).
constexpr std::uintptr_t kLayoutWidthAddress = std::uintptr_t(0x00742B08);   // 800.0: the layout's right edge
constexpr std::uintptr_t kPanelRightAddress = std::uintptr_t(0x007428AC);    // 695.0: the bottom panels' right column

constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;

// addon reads its settings with RegQueryValueExW under HKEY_CURRENT_USER; these go through the game's
// own import of RegQueryValueExA (the strings are the same in both), so harness runs can answer them.
constexpr const char* kStarterSettings = "Software\\Gipat.Ru\\EI_Starter\\Settings";
constexpr const char* kGeneralSettings = "Software\\Gipat.ru\\EI_Starter\\EvilIslands\\General Settings";

// Settings\WideScreen: a DWORD, 1 when the key or the value is missing (addon's default).
bool WideScreenSetting() {
    HKEY key;
    DWORD value = 1;
    if (gameapi::RegOpenKeyExA(HKEY_CURRENT_USER, kStarterSettings, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD read = 0, size = sizeof(read), type = 0;
        if (gameapi::RegQueryValueExA(key, "WideScreen", nullptr, &type, reinterpret_cast<BYTE*>(&read), &size) ==
                ERROR_SUCCESS &&
            type == REG_DWORD)
            value = read;
        gameapi::RegCloseKey(key);
    }
    return value != 0;
}

// General Settings\VIDEORESOLUTION parsed as addon does ("%dx%d"; what does not parse stays 0).
void Resolution(int* width, int* height) {
    *width = 0;
    *height = 0;
    HKEY key;
    if (gameapi::RegOpenKeyExA(HKEY_CURRENT_USER, kGeneralSettings, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    char text[64] = {};
    DWORD size = sizeof(text) - 1, type = 0;
    if (gameapi::RegQueryValueExA(key, "VIDEORESOLUTION", nullptr, &type, reinterpret_cast<BYTE*>(text), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ)
        gamecrt::sscanf(text, "%dx%d", width, height);   // addon: its own CRT's sscanf
    gameapi::RegCloseKey(key);
}

// addon's apply function (10023AFC): the checks that refuse a mode (logged as failed or disabled),
// then the values. The layout units are computed at the FPU precision of WinMain's entry (53 bits,
// as when addon computes them), the quotient rounded to a float before the ceiling.
Layout Compute() {
    Layout layout{};
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::WideScreen) || !WideScreenSetting()) return layout;
    Resolution(&layout.width, &layout.height);
    const int w = layout.width, h = layout.height;
    if (static_cast<unsigned>(w - 640) > 0xFD7Fu || static_cast<unsigned>(h - 480) > 0xFE1Fu || w / h < 1) return layout;
    if (w < kLayoutWidth || h < kLayoutHeight || w * 3 <= h * 4) return layout;
    layout.width43 = h * kLayoutWidth / kLayoutHeight;
    layout.margin = (w - layout.width43) / 2;
    volatile float units = static_cast<float>(layout.margin * 800.0 / layout.width43);
    layout.extra = static_cast<int>(std::ceil(static_cast<double>(units)));
    // FILD width43; FIDIV width; FSTP dword (10024954) and FILD width; FMUL 0.0075; FIDIV height;
    // FSTP dword (10024902).
    volatile double ratio = static_cast<double>(layout.width43) / static_cast<double>(w);
    layout.axisScale = static_cast<float>(ratio);
    volatile double perHeight = static_cast<double>(w) * 0.0075;
    perHeight = perHeight / static_cast<double>(h);
    layout.fontScale = static_cast<float>(perHeight);
    layout.active = true;
    return layout;
}

Layout g_layout;
bool g_computed;

int g_panelOffset;                 // ours (addon's 100BD0A0)
int* g_panelOffsetAt = &g_panelOffset;

float g_foundLayoutWidth, g_foundPanelRight;

float ReadFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

void WriteFloat(std::uintptr_t address, float value) {
    auto* p = reinterpret_cast<float*>(address);
    DWORD old;
    VirtualProtect(p, sizeof(float), PAGE_READWRITE, &old);
    *p = value;
    VirtualProtect(p, sizeof(float), old, &old);
}

// addon's CUiRegion::SetRect (the target of its JMP at 005FC9E0) adds its panel offset with
// ADD ECX,[offset] ... ADD EAX,[offset]: the variable's address is that operand. Only while addon's
// code is there (a Rebirth launch, before our hooks): our builders then write that variable, and
// addon's builders run by the verify build's side-by-side wrappers set the one our callees read.
int* AddonPanelOffset() {
    constexpr std::uintptr_t kSetRect = std::uintptr_t(0x005FC9E0);
    const auto* start = reinterpret_cast<const std::uint8_t*>(kSetRect);
    if (start[0] != 0xE9) return nullptr;
    std::int32_t rel;
    std::memcpy(&rel, start + 1, sizeof(rel));
    const auto* handler = reinterpret_cast<const std::uint8_t*>(kSetRect + 5 + rel);
    HMODULE owner = nullptr;
    char name[MAX_PATH] = {};
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(handler), &owner) ||
        !GetModuleFileNameA(owner, name, MAX_PATH))
        return nullptr;
    const char* file = std::strrchr(name, '\\');
    if (_stricmp(file ? file + 1 : name, "addon.dll") != 0) return nullptr;
    for (int i = 0; i + 12 < 0x60; ++i) {
        if (handler[i] != 0x03 || handler[i + 1] != 0x0D) continue;   // ADD ECX,[m32]
        std::uint32_t first;
        std::memcpy(&first, handler + i + 2, sizeof(first));
        for (int j = i + 6; j + 6 < 0x60; ++j) {
            std::uint32_t second;
            std::memcpy(&second, handler + j + 2, sizeof(second));
            if (handler[j] == 0x03 && handler[j + 1] == 0x05 && second == first)   // ADD EAX,[same]
                return reinterpret_cast<int*>(static_cast<std::uintptr_t>(first));
        }
        return nullptr;
    }
    return nullptr;
}

// Before the hooks are installed: the layout, the two widths written as addon writes them, and in a
// Rebirth launch addon's panel offset shared.
void Startup() {
    g_layout = Compute();
    g_computed = true;
    g_foundLayoutWidth = ReadFloat(kLayoutWidthAddress);
    g_foundPanelRight = ReadFloat(kPanelRightAddress);
    if (!g_layout.active) return;
    // addon: FILD extra; FADD 800.0 / 695.0 (doubles); FSTP dword.
    WriteFloat(kLayoutWidthAddress, static_cast<float>(g_layout.extra + 800.0));
    WriteFloat(kPanelRightAddress, static_cast<float>(g_layout.extra + 695.0));
    ei::Log("WideScreen: %dx%d, 4:3 part %d, margin %d pixels = %d layout units; layout width %g, panel right %g",
            g_layout.width, g_layout.height, g_layout.width43, g_layout.margin, g_layout.extra,
            ReadFloat(kLayoutWidthAddress), ReadFloat(kPanelRightAddress));
    if (int* addon = AddonPanelOffset()) {
        g_panelOffsetAt = addon;
        ei::Log("WideScreen: addon.dll's panel offset at %p: shared", static_cast<void*>(addon));
    }
}
EI_STARTUP_ACTION(Startup);

// Division of the 64-bit product as addon's IMUL r/m32; IDIV r/m32 do (no 32-bit wrap).
int MulDiv64(int a, int b, int divisor) {
    return static_cast<int>(static_cast<std::int64_t>(a) * b / divisor);
}

} // namespace

const Layout& Get() {
    if (!g_computed) {
        g_layout = Compute();
        g_computed = true;
    }
    return g_layout;
}

int PanelOffset() { return *g_panelOffsetAt; }
void SetPanelOffset(int offset) { *g_panelOffsetAt = offset; }

int EdgeShift() {
    const Layout& l = Get();
    return l.active ? l.extra : 0;
}

// addon's entry handlers (e.g. 10023344 at 00641F20: MOV EAX,[extra]; MOV [offset],EAX; 100232AF at
// 00645F80: NEG EAX first) and exit handlers (10023359: MOV [offset],0).
void EnterLeftPanel() {
    if (Active()) SetPanelOffset(-Get().extra);
}
void EnterRightPanel() {
    if (Active()) SetPanelOffset(Get().extra);
}
void LeavePanel() {
    if (Active()) SetPanelOffset(0);
}

// 10023213 (addon's 005F7CA0): x scaled by the 4:3 part's width plus the margin (when ECX is
// nonzero), y by the mode's height.
int RectToScreen(RECT* rect, bool margin) {
    const Layout& l = Get();
    const int shift = margin ? l.margin : 0;
    rect->left = MulDiv64(l.width43, rect->left, kLayoutWidth) + shift;
    rect->top = MulDiv64(l.height, rect->top, kLayoutHeight);
    rect->right = MulDiv64(l.width43, rect->right, kLayoutWidth) + shift;
    rect->bottom = MulDiv64(l.height, rect->bottom, kLayoutHeight);
    return rect->bottom;
}

// 10023278 (addon's 005F7D20 and 005F7D60).
int PointToScreen(POINT* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(l.width43, point->x, kLayoutWidth) + (margin ? l.margin : 0);
    point->y = MulDiv64(l.height, point->y, kLayoutHeight);
    return point->y;
}

// addon's code at 005F7D9E (98 bytes over 005F7DA0): left, right, top, bottom in that order.
int RectFromScreen(RECT* rect, bool margin) {
    const Layout& l = Get();
    const int shift = margin ? l.margin : 0;
    rect->left = MulDiv64(rect->left - shift, kLayoutWidth, l.width43);
    rect->right = MulDiv64(rect->right - shift, kLayoutWidth, l.width43);
    rect->top = MulDiv64(rect->top, kLayoutHeight, l.height);
    rect->bottom = MulDiv64(rect->bottom, kLayoutHeight, l.height);
    return rect->bottom;
}

// addon's code at 005F7DFE (96 bytes over 005F7E00 and 005F7E30, which jumps to 005F7E00).
int PointFromScreen(POINT* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(point->x - (margin ? l.margin : 0), kLayoutWidth, l.width43);
    point->y = MulDiv64(kLayoutHeight, point->y, l.height);
    return point->y;
}

// addon's code at 005F83CF: MOV EAX,800; IMUL EBX; MOV ECX,width43; IDIV ECX, then the same with 600
// and the height.
SIZE TextExtentToLayout(SIZE pixels) {
    const Layout& l = Get();
    return SIZE{MulDiv64(pixels.cx, kLayoutWidth, l.width43), MulDiv64(pixels.cy, kLayoutHeight, l.height)};
}

float AxisScale() { return Active() ? Get().axisScale : 1.0f; }

float FontScale() { return Active() ? Get().fontScale : *reinterpret_cast<const volatile float*>(std::uintptr_t(0x0073D09C)); }

int PickShift() { return Active() ? Get().margin / 2 : 0; }

namespace {
constexpr int kHeroScreenSprites = 21;   // the hero screen's 19 and the two curtains
constexpr int kHeroListMode = 9;

void SetCurtainLook(CUiMesh* curtain, int visible) {
    curtain->tint_[0] = curtain->tint_[1] = curtain->tint_[2] = 0.3f;
    curtain->visible_ = visible;
}
} // namespace

// 100234AB / 100235BA: AddSprite(0) (005FF3C0) and SetRectangle (005E70E0) through their addresses;
// FILD margin; FCHS for the left edge, FILD margin; FADD 800.0 (a double) for the right one.
void AddCurtains(CUiWindow* window, int visible) {
    if (!Active()) return;
    const int margin = Get().margin;
    CUiMesh* left = window->AddSprite(nullptr);
    left->SetRectangle(2.0f, 100.0f, static_cast<float>(-margin), 0.0f, 0.0f, 600.0f, 0xE3, 0x3E, 0x100, 0xB0);
    SetCurtainLook(left, visible);
    CUiMesh* right = window->AddSprite(nullptr);
    right->SetRectangle(2.0f, 100.0f, 800.0f, 0.0f, static_cast<float>(margin + 800.0), 600.0f, 0x100, 0xB0, 0xE3, 0x3E);
    SetCurtainLook(right, visible);
}

void ShowHeroCurtains(CUiWindow* window, int mode) {
    if (!Active() || window->sceneObjects_.size != kHeroScreenSprites) return;
    const int shown = mode != kHeroListMode ? 1 : 0;
    window->SceneMesh(kHeroScreenSprites - 2)->visible_ = shown;
    window->SceneMesh(kHeroScreenSprites - 1)->visible_ = shown;
}

// addon's apply function: 0x320 + extra written at 0065E7C3, -0x320 - extra at 0065E7CA.
int LayoutRight() { return kLayoutWidth + Get().extra; }

float FoundLayoutWidth() { return g_foundLayoutWidth; }
float FoundPanelRight() { return g_foundPanelRight; }

} // namespace widescreen
