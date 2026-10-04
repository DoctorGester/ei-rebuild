// The camp trade grid's draw (00627390, CTradeGrid slot 20, vtable 0074170C; thiscall, RET 4).
// The depth buffer is cleared to 0 over the screen and to 1 over the grid (200..600 x 100..500 in
// layout coordinates), so the cells' item models draw only there; then the window's sprites and
// models (005FEB90), and on its surface: every bought item (+0x150) and sold item (+0x154) in its
// cell of a 4 x 4 layout (100 pixels apart; 005ECA10 draws the item's model), its count when more
// than one, and unless +0x160 its price (+0x14C slot 54 with the kind +0x158 / +0x15C). Last the
// depth buffer is cleared to 1 again. The new renderer (src/render) gets the depth clears. See
// docs/notes/ui.md ("Camp") and docs/notes/renderer.md.
#include "ui_camp.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

constexpr int kD3DClear = 10;               // IDirect3DDevice7::Clear
constexpr std::uint32_t kTextColour = 0xA7D7E4;
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// The cell an item's model is drawn in (005ECA10's second argument).
struct ItemCell {
    void* item;
    float x, y;
    float size;
    float depth;      // 100
    int scale;        // the grid's +0x148
};

// 005ECA10 (thiscall on the grid's +0x14C object): draws an item's model in a cell.
__declspec(naked) void __fastcall DrawItemModel(void*, void*, int /*pass*/, ItemCell*) EI_ORIGINAL(0x005ECA10)
// The window's surface text helpers (Rebirth patches 005FDE20).
struct SurfaceText {
    void Begin();
    void Clear();
    void Draw(const char* text, RECT* rect, int a, std::uint32_t colour, int format, int b);
    void End();
};
__declspec(naked) void SurfaceText::Begin() EI_ORIGINAL(0x005FDC00)
__declspec(naked) void SurfaceText::Clear() EI_ORIGINAL(0x005FE2F0)
__declspec(naked) void SurfaceText::Draw(const char*, RECT*, int, std::uint32_t, int, int) EI_ORIGINAL(0x005FDE20)
__declspec(naked) void SurfaceText::End() EI_ORIGINAL(0x005FDC60)

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

// IDirect3DDevice7::Clear of the depth buffer (and the stencil when the device has one) over
// `rect` to `z`, result in the renderer's +8; handed to the new renderer too.
void ClearDepth(UiRenderer* renderer, RECT* rect, float z) {
    auto* r = reinterpret_cast<std::uint8_t*>(renderer);
    unsigned long flags = (At<std::uint32_t>(r, 0x90) & 0x4000) && At<std::uint32_t>(r, 0x9C) ? 6ul : 2ul;
    gameapi::EnterCriticalSection(DeviceLock());
    At<long>(r, 8) = ComCall(At<void*>(r, 0x54), kD3DClear, 1ul, rect, flags, 0ul, z, 0ul);
    gameapi::LeaveCriticalSection(DeviceLock());
    render::Rect nr{rect->left, rect->top, rect->right, rect->bottom};
    render::ClearDepth(z, &nr, 1);
}

// One list's items: cell i at column i % 4, row i / 4; `y0` and the sizes differ between the
// bought (upper) and sold (lower) lists, as do the text rows.
void DrawItems(CTradeGrid* grid, int pass, void* list, SurfaceText* surface, mfc::CString& text, float yOffset,
               float bigSize, float smallSize, int countTop, int priceTop, int kind) {
    void** items = *static_cast<void***>(list);
    int count = At<int>(list, 4);
    for (int i = 0; i < count; ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        ItemCell cell{items[i], static_cast<float>(x) + GameFloat(0x0073BF48), static_cast<float>(y) + yOffset,
                      i <= 3 ? bigSize : smallSize, 100.0f, At<int>(grid, 0x148)};
        DrawItemModel(At<void*>(grid, 0x14C), nullptr, pass, &cell);
        int pieces = At<int>(cell.item, 0x48);
        if (pieces > 1) {
            text.Format(Text(0x0078EEEC), pieces);
            RECT r{x + 15, y + countTop, x + 0x55, y + countTop + 20};
            surface->Draw(text, &r, 1, kTextColour, 2, 1);
        }
        if (!At<char>(grid, 0x160)) {
            int price = ei::VCall<int>(At<void*>(grid, 0x14C), 0xD8 / 4, cell.item, kind);
            text.Format(Text(0x0078EEEC), price);
            RECT r{x + 15, y + priceTop, x + 0x55, y + priceTop + 20};
            surface->Draw(text, &r, 1, kTextColour, 1, 1);
        }
    }
}

} // namespace

// 00627390. Returns 1.
int __fastcall TradeGridDraw(CTradeGrid* self, void*, int pass) {
    CInterface3D* ui = self->interface_;
    ui->renderer_->FlushBatchA();
    ui->renderer_->FlushBatchB();
    RECT grid{200, 100, 600, 500};
    ui->CallScaleRectToScreen(&grid);
    RECT screen{0, 0, At<int>(ui, 0), At<int>(ui, 4)};
    ClearDepth(ui->renderer_, &screen, 0.0f);
    RECT inside = grid;
    ClearDepth(ui->renderer_, &inside, 1.0f);
    ui->renderer_->FlushBatchA();
    ui->renderer_->FlushBatchB();
    self->DrawBase(pass);

    mfc::CString text;
    auto* surface = reinterpret_cast<SurfaceText*>(self->surfaces_.data_[0]);
    surface->Begin();
    surface->Clear();
    DrawItems(self, pass, At<void*>(self, 0x150), surface, text, GameFloat(0x0073D618), 48.0f, 24.0f, 0x0F, 0x41,
              At<int>(self, 0x158));
    DrawItems(self, pass, At<void*>(self, 0x154), surface, text, GameFloat(0x00741F18), 12.0f, 6.0f, 0xD7, 0x109,
              At<int>(self, 0x15C));
    surface->End();
    ui = self->interface_;
    ui->renderer_->FlushBatchA();
    ui->renderer_->FlushBatchB();
    RECT whole{0, 0, At<int>(ui, 0), At<int>(ui, 4)};
    ClearDepth(ui->renderer_, &whole, 1.0f);
    return 1;
}
EI_REPLACE(0x00627390, TradeGridDraw);
