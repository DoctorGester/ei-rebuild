// Checks for the item description panel (ui_item_info.cpp).
//
// The descriptions only write text into the panel's surface 0 (and DescribeStats / ShowHelp empty
// the panel's item list first, which leaves the same state however often it runs). They run side
// by side on the surface's pixels: the pixels are read (DirectDraw lock, outside the functions'
// own GetDC/ReleaseDC), the original draws, its pixels are read and the old ones put back, ours
// draws, and both pixel sets must be equal. A description that reaches a sub-description runs the
// sub-description's own wrapper inside the original (nested side by side).
#include "verify.h"
#include "pixel_settle.h"

#include "../../src/game/console.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/lib/gameapi.h"
#include "../../src/hook/log.h"

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
    bool unstable = false;
    if (pixels) {
        Pixels actual;
        if (AccessPixels(surface, actual, false)) row = FirstDifferentRow(expected, actual);
        if (row != -1) {
            // Outside disturbance (ticket X8, as X5): redraw the original from the saved pixels until
            // two redraws agree, then ours from the saved pixels against that settled draw.
            auto redraw = [&](Pixels* out) {
                return AccessPixels(surface, before, true) && (original(), true) && AccessPixels(surface, *out, false);
            };
            ei::verify::SettleOutcome settle = ei::verify::SettleOriginal(
                expected, redraw, [](const Pixels& a, const Pixels& b) { return FirstDifferentRow(a, b) == -1 ? 0 : 1; });
            Pixels again;
            bool ok = AccessPixels(surface, before, true) && (ours(), true) && AccessPixels(surface, again, false);
            row = !ok ? -2 : settle.settled ? FirstDifferentRow(expected, again) : -1;
            unstable = ok && !settle.settled;
            ei::Log("verify %s: pixels differed (detail %d); the original's redraws %s after %d tries; rerun row %d", name,
                    detail, settle.settled ? "settled" : "did not settle", settle.tries, row);
        }
    }
    bool same = row == -1 && panel->itemCount_ == expectedCount && panel->Item() == expectedItem;
    ei::verify::Check(name, true, same, "detail %d: pixel row %d, items %d/%d (pixels compared: %d%s)", detail, row,
                      expectedCount, panel->itemCount_, pixels && !unstable, unstable ? ", original unstable" : "");
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

// 005F13A0 (PUSH -1; PUSH imm32: 7). Side by side: the original fills a scratch array, ours the
// caller's; the strings must match.
static void(__fastcall* s_damageLists)(const float*, console::CStringArray*, char);
static void __fastcall DamageListsVerify(const float* values, console::CStringArray* out, char integer) {
    console::CStringArray expected;
    s_damageLists(values, &expected, integer);
    ItemDamageLists(values, out, integer);
    bool same = expected.GetSize() == out->GetSize();
    for (int i = 0; same && i < out->GetSize(); ++i)
        same = std::strcmp(static_cast<const char*>(expected[i]), static_cast<const char*>((*out)[i])) == 0;
    ei::verify::Check("ItemDamageLists", true, same, "integer %d: '%s' / '%s'", integer,
                      expected.GetSize() > 1 ? static_cast<const char*>(expected[1]) : "",
                      out->GetSize() > 1 ? static_cast<const char*>((*out)[1]) : "");
}
EI_VERIFY(0x005F13A0, DamageListsVerify, 7, s_damageLists);

// --- The model view (ui_item_view.cpp)---------------------------------------------------------------

namespace {

// What DrawItem leaves on the item's model: flags (+0x08), position (+0x1C..+0x27), orientation
// (+0x34..+0x43), colours (+0xD4, +0xD8, +0x224..+0x22F), scale (+0x23C, +0x250, +0x264), +0x27C
// and the texture (+0x280); and the item's turning state (angle, speed, flag, glow).
struct ModelState {
    std::uint8_t bytes[4 + 12 + 16 + 8 + 12 + 12 + 8] = {};
    ModelState() = default;
    explicit ModelState(const std::uint8_t* m) {
        std::uint8_t* out = bytes;
        auto take = [&](int offset, int size) {
            std::memcpy(out, m + offset, size);
            out += size;
        };
        take(0x08, 4);
        take(0x1C, 12);
        take(0x34, 16);
        take(0xD4, 8);
        take(0x224, 12);
        take(0x23C, 4);
        take(0x250, 4);
        take(0x264, 4);
        take(0x27C, 8);
    }
    void Restore(std::uint8_t* m) const {
        const std::uint8_t* in = bytes;
        auto put = [&](int offset, int size) {
            std::memcpy(m + offset, in, size);
            in += size;
        };
        put(0x08, 4);
        put(0x1C, 12);
        put(0x34, 16);
        put(0xD4, 8);
        put(0x224, 12);
        put(0x23C, 4);
        put(0x250, 4);
        put(0x264, 4);
        put(0x27C, 8);
    }
    bool operator==(const ModelState& o) const { return std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0; }
};

struct TurnState {
    std::uint8_t bytes[0x10];   // +0x08 .. +0x17 of the turning state
    explicit TurnState(const std::uint8_t* t) { std::memcpy(bytes, t + 8, sizeof(bytes)); }
    void Restore(std::uint8_t* t) const { std::memcpy(t + 8, bytes, sizeof(bytes)); }
    bool operator==(const TurnState& o) const { return std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0; }
};

std::uint8_t* TurnOf(CItem* item) { return *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(item) + 0x1C); }

} // namespace

// 005ECA10 (PUSH EBP; MOV EBP,ESP; PUSH -1: 5). Side by side when the item already has its turning
// state: the original draws, the model's fields and the turning state are recorded and put back,
// ours draws (it finds the model and texture the original cached), and both must agree; the
// scene objects added for the frame, the marks and the shade must be gone again after each run.
// Without a turning state ours runs alone (the original would make one from the game's random
// generator) and must leave one (vtable 007403F8). Both runs draw: the item is drawn twice.
static void(__thiscall* s_drawItem)(CItemScreen*, int, ItemCell*);
static void __fastcall DrawItemVerify(CItemScreen* self, void*, int pass, ItemCell* cell) {
    CItem* item = cell->item;
    int sceneObjects = self->sceneObjects_.size_;
    std::uint8_t* turn = TurnOf(item);
    if (!turn) {
        self->DrawItem(pass, cell);
        turn = TurnOf(item);
        bool ok = turn && *reinterpret_cast<std::uintptr_t*>(turn) == 0x007403F8 && self->sceneObjects_.size_ == sceneObjects;
        ei::verify::Check("CItemScreen::DrawItem", true, ok, "new turning state %p, scene objects %d -> %d", turn,
                          sceneObjects, self->sceneObjects_.size_);
        return;
    }
    TurnState turnBefore(turn);
    std::uint8_t* model = self->CachedModel(item);
    bool hadModel = model != nullptr;
    ModelState modelBefore;
    if (hadModel) modelBefore = ModelState(model);
    s_drawItem(self, pass, cell);
    int sceneAfterOriginal = self->sceneObjects_.size_;
    model = self->CachedModel(item);
    if (!model) {
        // No model could be made: both return before drawing.
        self->DrawItem(pass, cell);
        ei::verify::Check("CItemScreen::DrawItem", true, TurnState(turn) == turnBefore, "no model, turning state changed");
        return;
    }
    ModelState expected(model);
    TurnState expectedTurn(turn);
    turnBefore.Restore(turn);
    if (hadModel) modelBefore.Restore(model);
    self->DrawItem(pass, cell);
    ModelState actual(model);
    TurnState actualTurn(turn);
    bool ok = expected == actual && expectedTurn == actualTurn && sceneAfterOriginal == sceneObjects &&
              self->sceneObjects_.size_ == sceneObjects;
    ei::verify::Check("CItemScreen::DrawItem", true, ok, "kind %x mode %d: model %d turn %d scene objects %d/%d/%d",
                      KindOf(item), cell->mode, expected == actual, expectedTurn == actualTurn, sceneObjects,
                      sceneAfterOriginal, self->sceneObjects_.size_);
}
EI_VERIFY(0x005ECA10, DrawItemVerify, 5, s_drawItem);

// 005ED7B0: PUSH -1; PUSH imm32 (7). Side by side as DrawItem: the face model's state is recorded
// after the original, put back, and ours must leave the same (both draw, the face is drawn twice).
static void(__thiscall* s_drawHeroFace)(CItemScreen*, int, HeroFaceCell*);
static void __fastcall DrawHeroFaceVerify(CItemScreen* self, void*, int pass, HeroFaceCell* cell) {
    int sceneObjects = self->sceneObjects_.size_;
    mfc::CString modelName, textureName;
    CItemScreen::FaceNames(cell->prototype, &modelName, &textureName);
    std::uint8_t* model = self->CachedModelNamed(modelName);
    bool hadModel = model != nullptr;
    ModelState modelBefore;
    if (hadModel) modelBefore = ModelState(model);
    s_drawHeroFace(self, pass, cell);
    int sceneAfterOriginal = self->sceneObjects_.size_;
    model = self->CachedModelNamed(modelName);
    if (!model) {
        self->DrawHeroFace(pass, cell);
        ei::verify::Check("CItemScreen::DrawHeroFace", true, self->CachedModelNamed(modelName) == nullptr,
                          "no model for %s", static_cast<const char*>(modelName));
        return;
    }
    ModelState expected(model);
    if (hadModel) modelBefore.Restore(model);
    self->DrawHeroFace(pass, cell);
    ModelState actual(model);
    bool ok = expected == actual && sceneAfterOriginal == sceneObjects && self->sceneObjects_.size_ == sceneObjects;
    ei::verify::Check("CItemScreen::DrawHeroFace", true, ok, "%s / %s: model %d scene objects %d/%d/%d",
                      static_cast<const char*>(modelName), static_cast<const char*>(textureName), expected == actual,
                      sceneObjects, sceneAfterOriginal, self->sceneObjects_.size_);
}
EI_VERIFY(0x005ED7B0, DrawHeroFaceVerify, 7, s_drawHeroFace);

// 005EE5C0 (slot 20): ours only (it draws through DrawItem, whose wrapper compares); returns 1.
static BOOL __fastcall InfoPanelDrawVerify(CItemInfoPanel* self, void*, int pass) {
    BOOL result = self->Draw(pass);
    ei::verify::Check("CItemInfoPanel::Draw", 1, result, "result %d", result);
    return result;
}
EI_VERIFY(0x005EE5C0, InfoPanelDrawVerify, 0, s_unused);

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

// 005F2F20 / 005F2F90: ours only (erasing cannot run twice). The cache must end within its limit.
static void __fastcall TrimModelsVerify(ItemObjectCache* cache) {
    TrimItemModels(cache);
    ei::verify::Check("TrimItemModels", true, cache->count <= cache->limit, "%u entries, limit %u", cache->count,
                      cache->limit);
}
EI_VERIFY(0x005F2F20, TrimModelsVerify, 0, s_unused);
static void __fastcall TrimTexturesVerify(ItemObjectCache* cache) {
    TrimItemTextures(cache);
    ei::verify::Check("TrimItemTextures", true, cache->count <= cache->limit, "%u entries, limit %u", cache->count,
                      cache->limit);
}
EI_VERIFY(0x005F2F90, TrimTexturesVerify, 0, s_unused);

// 005F2A90: ours only (it adds sprites). 31 scene objects must be added (the strip, 4 x 6 column
// pieces, 6 bars).
static void __fastcall IconBuildVerify(CItemIconWindow* self, void*) {
    int before = self->sceneObjects_.size_;
    self->Build();
    ei::verify::Check("CItemIconWindow::Build", true, self->sceneObjects_.size_ == before + 31, "scene objects %d -> %d",
                      before, self->sceneObjects_.size_);
}
EI_VERIFY(0x005F2A90, IconBuildVerify, 0, s_unused);

// 005EDD80: ours only (it makes the icon window). The window and the caches' limits.
static void __fastcall CreateBaseVerify(CItemScreen* self, void*) {
    self->CreateBase();
    bool ok = self->iconWindow_ && *reinterpret_cast<std::uintptr_t*>(self->iconWindow_) == 0x00740444 &&
              self->models_.count <= self->models_.limit && self->textures_.count <= self->textures_.limit;
    ei::verify::Check("CItemScreen::CreateBase", true, ok, "icons %p, models %u/%u, textures %u/%u", self->iconWindow_,
                      self->models_.count, self->models_.limit, self->textures_.count, self->textures_.limit);
}
EI_VERIFY(0x005EDD80, CreateBaseVerify, 0, s_unused);

// 005F6470 / 005F6620: ours only (destructor bodies). Both caches emptied, the window base too.
static void __fastcall ItemScreenDestroyVerify(CItemScreen* self) {
    ItemScreenDestroy(self);
    ei::verify::Check("ItemScreenDestroy", true,
                      self->models_.count == 0 && self->textures_.count == 0 && !self->sceneObjects_.data_,
                      "models %u textures %u", self->models_.count, self->textures_.count);
}
EI_VERIFY(0x005F6470, ItemScreenDestroyVerify, 0, s_unused);
static void __fastcall CampScreenDestroyVerify(CItemScreen* self) {
    CampScreenDestroy(self);
    ei::verify::Check("CampScreenDestroy", true,
                      self->models_.count == 0 && self->textures_.count == 0 && !self->sceneObjects_.data_,
                      "models %u textures %u", self->models_.count, self->textures_.count);
}
EI_VERIFY(0x005F6620, CampScreenDestroyVerify, 0, s_unused);
static void __fastcall Screen9DestroyVerify(CItemScreen* self) {
    Screen9Destroy(self);
    ei::verify::Check("Screen9Destroy", true,
                      self->models_.count == 0 && self->textures_.count == 0 && !self->sceneObjects_.data_,
                      "models %u textures %u", self->models_.count, self->textures_.count);
}
EI_VERIFY(0x005F7010, Screen9DestroyVerify, 0, s_unused);

// 005EE440: ours only (a builder). One surface more, 45 item texts and 25 skill texts more.
static void __fastcall InfoPanelBuildVerify(CItemInfoPanel* self, void*, int x, int y) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    int surfaces = self->surfaces_.size_;
    int items, skills;
    std::memcpy(&items, bytes + 0x1C8 + 8, 4);
    std::memcpy(&skills, bytes + 0x1DC + 8, 4);
    self->Build(x, y);
    int itemsAfter, skillsAfter;
    std::memcpy(&itemsAfter, bytes + 0x1C8 + 8, 4);
    std::memcpy(&skillsAfter, bytes + 0x1DC + 8, 4);
    bool ok = self->surfaces_.size_ == surfaces + 1 && itemsAfter == items + 45 && skillsAfter == skills + 25 &&
              self->x_ == x && self->y_ == y;
    ei::verify::Check("CItemInfoPanel::Build", true, ok, "surfaces %d -> %d, items %d -> %d, skills %d -> %d",
                      surfaces, self->surfaces_.size_, items, itemsAfter, skills, skillsAfter);
}
EI_VERIFY(0x005EE440, InfoPanelBuildVerify, 0, s_unused);
