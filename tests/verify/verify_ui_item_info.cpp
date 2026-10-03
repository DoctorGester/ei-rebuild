// Checks for the item description panel (ui_item_info.cpp).
//
// The descriptions only write text into the panel's surface 0 (and DescribeStats / ShowHelp empty
// the panel's item list first, which leaves the same state however often it runs). They run side
// by side on the surface's pixels: the pixels are read (DirectDraw lock, outside the functions'
// own GetDC/ReleaseDC), the original draws, its pixels are read and the old ones put back, ours
// draws, and both pixel sets must be equal. A description that reaches a sub-description runs the
// sub-description's own wrapper inside the original (nested side by side).
#include "verify.h"

#include "../../src/game/ui_item_info.h"
#include "../../src/lib/gameapi.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace {

void* s_unused;

// ---- Surface pixels (as verify_ui_base.cpp) ------------------------------------------------------

IDirectDrawSurface7* DirectDrawSurface(void* surface) {
    std::uint8_t* inner = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(surface) + 0x10);
    return inner ? *reinterpret_cast<IDirectDrawSurface7**>(inner + 0x10) : nullptr;
}

struct Pixels {
    int height = 0;
    int rowBytes = 0;
    std::vector<std::uint8_t> bytes;
};

CRITICAL_SECTION* SurfaceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// Reads the surface into `pixels`, or with `write` stores them back. False when it cannot be locked.
bool AccessPixels(void* surface, Pixels& pixels, bool write) {
    IDirectDrawSurface7* dd = surface ? DirectDrawSurface(surface) : nullptr;
    if (!dd) return false;
    gameapi::EnterCriticalSection(SurfaceLock());
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    bool ok = dd->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK;
    if (ok) {
        auto* base = static_cast<std::uint8_t*>(desc.lpSurface);
        int rowBytes = static_cast<int>(desc.dwWidth * (desc.ddpfPixelFormat.dwRGBBitCount / 8));
        if (!write) {
            pixels.height = static_cast<int>(desc.dwHeight);
            pixels.rowBytes = rowBytes;
            pixels.bytes.resize(static_cast<std::size_t>(rowBytes) * desc.dwHeight);
            for (DWORD y = 0; y < desc.dwHeight; ++y)
                std::memcpy(&pixels.bytes[y * rowBytes], base + y * desc.lPitch, rowBytes);
        } else if (pixels.rowBytes == rowBytes && pixels.height == static_cast<int>(desc.dwHeight)) {
            for (int y = 0; y < pixels.height; ++y)
                std::memcpy(base + y * desc.lPitch, &pixels.bytes[y * rowBytes], rowBytes);
        }
        dd->Unlock(nullptr);
    }
    gameapi::LeaveCriticalSection(SurfaceLock());
    return ok;
}

int FirstDifferentRow(const Pixels& a, const Pixels& b) {
    if (a.bytes.size() != b.bytes.size() || a.rowBytes != b.rowBytes) return -2;
    for (int y = 0; y < a.height; ++y)
        if (std::memcmp(&a.bytes[y * a.rowBytes], &b.bytes[y * b.rowBytes], a.rowBytes) != 0) return y;
    return -1;
}

void* PanelSurface(CItemInfoPanel* panel) {
    return panel->surfaces_.size_ > 0 ? panel->surfaces_.data_[0] : nullptr;
}

// The original draws, its pixels are recorded and the old ones put back, ours draws; the pixels
// and the item list (count, first item) must agree.
void CompareDrawing(const char* name, CItemInfoPanel* panel, const std::function<void()>& original,
                    const std::function<void()>& ours, int detail) {
    void* surface = PanelSurface(panel);
    Pixels before;
    bool pixels = AccessPixels(surface, before, false);
    original();
    int expectedCount = panel->itemCount_;
    CItem* expectedItem = panel->Item();
    Pixels expected;
    if (pixels) pixels = AccessPixels(surface, expected, false) && AccessPixels(surface, before, true);
    ours();
    int row = -1;
    if (pixels) {
        Pixels actual;
        if (AccessPixels(surface, actual, false)) row = FirstDifferentRow(expected, actual);
    }
    bool same = row == -1 && panel->itemCount_ == expectedCount && panel->Item() == expectedItem;
    ei::verify::Check(name, true, same, "detail %d: pixel row %d, items %d/%d (pixels compared: %d)", detail, row,
                      expectedCount, panel->itemCount_, pixels);
}

int KindOf(CItem* item) { return item ? *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(item) + 0x10) : -1; }

} // namespace

// 005EE760: PUSH -1; PUSH imm32 (7).
static void(__thiscall* s_describeItem)(CItemInfoPanel*);
static void __fastcall DescribeItemVerify(CItemInfoPanel* self, void*) {
    CompareDrawing("CItemInfoPanel::DescribeItem", self, [&] { s_describeItem(self); }, [&] { self->DescribeItem(); },
                   KindOf(self->Item()));
}
EI_VERIFY(0x005EE760, DescribeItemVerify, 7, s_describeItem);

// The sub-descriptions: MOV EAX,FS:[0]; PUSH -1 (8).
namespace {
using Describer = void(__thiscall*)(CItemInfoPanel*);
void CompareDescription(const char* name, CItemInfoPanel* self, Describer original, void (CItemInfoPanel::*ours)()) {
    CompareDrawing(name, self, [&] { original(self); }, [&] { (self->*ours)(); }, KindOf(self->Item()));
}
} // namespace

static Describer s_describeMaterial;
static void __fastcall DescribeMaterialVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeMaterial", self, s_describeMaterial, &CItemInfoPanel::DescribeMaterial);
}
EI_VERIFY(0x005EFD90, DescribeMaterialVerify, 8, s_describeMaterial);

static Describer s_describeBlueprint;
static void __fastcall DescribeBlueprintVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeBlueprint", self, s_describeBlueprint, &CItemInfoPanel::DescribeBlueprint);
}
EI_VERIFY(0x005F0370, DescribeBlueprintVerify, 8, s_describeBlueprint);

static Describer s_describeSpellRecord;
static void __fastcall DescribeSpellRecordVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeSpellRecord", self, s_describeSpellRecord,
                       &CItemInfoPanel::DescribeSpellRecord);
}
EI_VERIFY(0x005F0B90, DescribeSpellRecordVerify, 8, s_describeSpellRecord);

static Describer s_describeModifierRecord;
static void __fastcall DescribeModifierRecordVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeModifierRecord", self, s_describeModifierRecord,
                       &CItemInfoPanel::DescribeModifierRecord);
}
EI_VERIFY(0x005F1130, DescribeModifierRecordVerify, 8, s_describeModifierRecord);

static Describer s_describeQuickItem;
static void __fastcall DescribeQuickItemVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeQuickItem", self, s_describeQuickItem, &CItemInfoPanel::DescribeQuickItem);
}
EI_VERIFY(0x005EF300, DescribeQuickItemVerify, 8, s_describeQuickItem);

static Describer s_describeCarriedSpell;
static void __fastcall DescribeCarriedSpellVerify(CItemInfoPanel* self, void*) {
    CompareDescription("CItemInfoPanel::DescribeCarriedSpell", self, s_describeCarriedSpell,
                       &CItemInfoPanel::DescribeCarriedSpell);
}
EI_VERIFY(0x005EF5D0, DescribeCarriedSpellVerify, 8, s_describeCarriedSpell);

// 005F1590: PUSH -1; PUSH imm32 (7). The detail is the statistic's kind and index.
static void(__thiscall* s_describeStats)(CItemInfoPanel*, ItemHoverInfo*);
static void __fastcall DescribeStatsVerify(CItemInfoPanel* self, void*, ItemHoverInfo* info) {
    CompareDrawing("CItemInfoPanel::DescribeStats", self, [&] { s_describeStats(self, info); },
                   [&] { self->DescribeStats(info); }, info->kind * 100 + info->index);
}
EI_VERIFY(0x005F1590, DescribeStatsVerify, 7, s_describeStats);

// 005F2910: PUSH -1; PUSH imm32 (7).
static void(__thiscall* s_showHelp)(CItemInfoPanel*, int);
static void __fastcall ShowHelpVerify(CItemInfoPanel* self, void*, int topic) {
    CompareDrawing("CItemInfoPanel::ShowHelp", self, [&] { s_showHelp(self, topic); }, [&] { self->ShowHelp(topic); },
                   topic);
}
EI_VERIFY(0x005F2910, ShowHelpVerify, 7, s_showHelp);

// 005EE6C0: ours only (the list keeps a copy of the item, and a second run would find it there).
// Afterwards: nothing changed when the item was already the described one (its slot 14 says so),
// else the list holds one item (a null item: none).
static void __fastcall DescribeVerify(CItemInfoPanel* self, void*, CItem* item) {
    int count = self->itemCount_;
    CItem* shown = self->Item();
    using SameFn = BOOL(__thiscall*)(CItem*, CItem*);
    bool same = shown && item && reinterpret_cast<SameFn>((*reinterpret_cast<void***>(shown))[14])(shown, item);
    self->Describe(item);
    bool ok = same ? self->itemCount_ == count && self->Item() == shown
                   : self->itemCount_ == (item ? 1 : 0) && (!item || self->Item() != nullptr);
    ei::verify::Check("CItemInfoPanel::Describe", true, ok, "item %p same %d: items %d -> %d", item, same, count,
                      self->itemCount_);
}
EI_VERIFY(0x005EE6C0, DescribeVerify, 0, s_unused);

// The input side (005EE6B0 slot 1, 005EE640 key down, 005EE680 key up): ours only. They redraw
// through DescribeItem (whose own wrapper compares the drawing) and leave the item list alone.
namespace {
CItemInfoPanel* PanelOf(void* input) { return reinterpret_cast<CItemInfoPanel*>(static_cast<std::uint8_t*>(input) - 0x94); }
struct ListState {
    int count;
    CItem* item;
    explicit ListState(CItemInfoPanel* panel) : count(panel->itemCount_), item(panel->Item()) {}
    void Check(const char* name, CItemInfoPanel* panel, int key) const {
        ei::verify::Check(name, true, panel->itemCount_ == count && panel->Item() == item, "key %x: items %d -> %d", key,
                          count, panel->itemCount_);
    }
};
} // namespace

static void __fastcall InfoPanelRefreshVerify(void* input) {
    CItemInfoPanel* panel = PanelOf(input);
    ListState before(panel);
    panel->DescribeItem();
    before.Check("CItemInfoPanel::Refresh", panel, 0);
}
EI_VERIFY(0x005EE6B0, InfoPanelRefreshVerify, 0, s_unused);

static void __fastcall InfoPanelKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    CItemInfoPanel* panel = PanelOf(input);
    ListState before(panel);
    panel->OnKeyDown(key, repeat, flags);
    before.Check("CItemInfoPanel::OnKeyDown", panel, key);
}
EI_VERIFY(0x005EE640, InfoPanelKeyDownVerify, 0, s_unused);

static void __fastcall InfoPanelKeyUpVerify(void* input, void*, int key, int repeat, int flags) {
    CItemInfoPanel* panel = PanelOf(input);
    ListState before(panel);
    panel->OnKeyUp(key, repeat, flags);
    before.Check("CItemInfoPanel::OnKeyUp", panel, key);
}
EI_VERIFY(0x005EE680, InfoPanelKeyUpVerify, 0, s_unused);
