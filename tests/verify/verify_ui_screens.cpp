// Checks for the interface screens (src/game/ui_screens.cpp).
//
// Pure functions run side by side with the original. Functions that only change a few fields run
// the original first, record the fields, restore them and run ours. Handlers that play sounds,
// open windows or call into other screens run only ours; the wrapper checks the state they leave.
#include "verify.h"

#include "../../src/game/ui_screens.h"
#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

// The fields of an item strip that its scrolling and tab code write, plus the tab buttons' shade.
struct StripState {
    std::uint8_t fields[0x180 - 0x150];   // tab, direction, timer, positions, pending, velocity
    float shades[6][3];
    void* capture;

    void Take(CUiItemStrip* s, bool withShades) {
        std::memcpy(fields, reinterpret_cast<char*>(s) + 0x150, sizeof(fields));
        std::memset(shades, 0, sizeof(shades));
        if (withShades)
            for (int i = 0; i < 6; ++i)
                std::memcpy(shades[i], reinterpret_cast<char*>(s->window.sceneObjects_.data[i + 2]) + 0x224, 12);
        capture = UiCapture(s->window.interface_);
    }
    void Restore(CUiItemStrip* s, bool withShades) const {
        std::memcpy(reinterpret_cast<char*>(s) + 0x150, fields, sizeof(fields));
        if (withShades)
            for (int i = 0; i < 6; ++i)
                std::memcpy(reinterpret_cast<char*>(s->window.sceneObjects_.data[i + 2]) + 0x224, shades[i], 12);
        UiCapture(s->window.interface_) = capture;
    }
    bool operator==(const StripState& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

bool HasTabObjects(const CUiItemStrip* s) {
    if (s->window.sceneObjects_.size < 8 || !s->window.sceneObjects_.data) return false;
    for (int i = 2; i < 8; ++i)
        if (!s->window.sceneObjects_.data[i]) return false;
    return true;
}
} // namespace

// --- Item strip: the item classifiers (pure) -------------------------------------------------

// 006218F0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static int(__fastcall* s_itemTab)(CUiItemStrip*, void*, const char*);
static int __fastcall ItemTabVerify(CUiItemStrip* self, void*, const char* item) {
    int expected = s_itemTab(self, nullptr, item);
    ei::verify::Check("CUiItemStrip::ItemTab", expected, self->CUiItemStrip::ItemTab(item), "type %X",
                      *reinterpret_cast<const int*>(item + 0x10));
    return expected;
}
EI_VERIFY(0x006218F0, ItemTabVerify, 6, s_itemTab);

// 006219A0 / 00621A80: PUSH ESI; MOV ESI,[ESP+8]; PUSH EDI (6 bytes).
static int(__fastcall* s_spellTab)(CUiItemStrip*, void*, const char*);
static int __fastcall SpellTabVerify(CUiItemStrip* self, void*, const char* item) {
    int expected = s_spellTab(self, nullptr, item);
    ei::verify::Check("CUiItemStrip::SpellTab", expected, self->CUiItemStrip::SpellTab(item), "type %X record %u",
                      *reinterpret_cast<const int*>(item + 0x10), *reinterpret_cast<const unsigned*>(item + 0x54));
    return expected;
}
EI_VERIFY(0x006219A0, SpellTabVerify, 6, s_spellTab);

static int(__fastcall* s_armorTab)(CUiItemStrip*, void*, const char*);
static int __fastcall ArmorTabVerify(CUiItemStrip* self, void*, const char* item) {
    int expected = s_armorTab(self, nullptr, item);
    ei::verify::Check("CUiItemStrip::ArmorTab", expected, self->CUiItemStrip::ArmorTab(item), "type %X record %u",
                      *reinterpret_cast<const int*>(item + 0x10), *reinterpret_cast<const unsigned*>(item + 0x54));
    return expected;
}
EI_VERIFY(0x00621A80, ArmorTabVerify, 6, s_armorTab);

// --- Item strip: tab and scrolling (original, restore, ours) -----------------------------------

// 00620960: SUB ESP,0x1C; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_setTab)(CUiItemStrip*, void*, int);
static void __fastcall SetTabVerify(CUiItemStrip* self, void*, int tab) {
    bool shades = HasTabObjects(self);
    if (!shades) {
        self->CUiItemStrip::SetTab(tab);
        return;
    }
    StripState before, expected, actual;
    before.Take(self, true);
    s_setTab(self, nullptr, tab);
    expected.Take(self, true);
    before.Restore(self, true);
    self->CUiItemStrip::SetTab(tab);
    actual.Take(self, true);
    ei::verify::Check("CUiItemStrip::SetTab", true, expected == actual, "tab %d -> %d position %08X pending %08X",
                      *reinterpret_cast<int*>(before.fields), tab, Bits(*reinterpret_cast<float*>(before.fields + 0x0C)),
                      Bits(*reinterpret_cast<float*>(before.fields + 0x24)));
}
EI_VERIFY(0x00620960, SetTabVerify, 5, s_setTab);

// 00620F10: MOV EAX,[ESP+0xC]; PUSH ESI (5 bytes).
static void(__fastcall* s_mouseMove)(void*, void*, int, int, int);
static void __fastcall StripMouseMoveVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CUiItemStrip>(input);
    StripState before, expected, actual;
    before.Take(self, false);
    s_mouseMove(input, nullptr, flags, x, y);
    expected.Take(self, false);
    before.Restore(self, false);
    self->OnMouseMove(flags, x, y);
    actual.Take(self, false);
    ei::verify::Check("CUiItemStrip::OnMouseMove", true, expected == actual, "(%d,%d) direction %d pending %08X", x, y,
                      self->scrollDirection_, Bits(*reinterpret_cast<float*>(before.fields + 0x24)));
}
EI_VERIFY(0x00620F10, StripMouseMoveVerify, 5, s_mouseMove);

// 00621010: PUSH ECX; PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0xC0] (10 bytes).
static void(__fastcall* s_buttonUp)(void*, void*, int, int, int);
static void __fastcall StripButtonUpVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CUiItemStrip>(input);
    StripState before, expected, actual;
    before.Take(self, false);
    s_buttonUp(input, nullptr, flags, x, y);
    expected.Take(self, false);
    before.Restore(self, false);
    self->OnLButtonUp(flags, x, y);
    actual.Take(self, false);
    ei::verify::Check("CUiItemStrip::OnLButtonUp", true, expected == actual, "direction %d pending %08X",
                      *reinterpret_cast<int*>(before.fields + 4), Bits(*reinterpret_cast<float*>(before.fields + 0x24)));
}
EI_VERIFY(0x00621010, StripButtonUpVerify, 10, s_buttonUp);

// --- Item strip: ours only ----------------------------------------------------------------------

static BOOL __fastcall StripWheelVerify(void* input, void*, int keys, int delta, int x, int y) {
    auto* self = WindowFromInput<CUiItemStrip>(input);
    float pending = self->pending_;
    bool inStrip = self->window.RegionAt(x, y) == 8;
    BOOL result = self->OnWheel(keys, static_cast<short>(delta), x, y);
    float expected = inStrip ? static_cast<float>(static_cast<short>(delta) / 120) + pending : pending;
    ei::verify::Check("CUiItemStrip::OnWheel", Bits(expected), Bits(self->pending_), "delta %d (%d,%d)", delta, x, y);
    ei::verify::Check("CUiItemStrip::OnWheel result", 1, result, "");
    return result;
}
EI_VERIFY(0x006210F0, StripWheelVerify, 0, s_unused);

static void __fastcall StripButtonDownVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CUiItemStrip>(input);
    int region = self->window.RegionAt(x, y);
    bool shift = UiShiftDown(self->window.interface_);
    self->OnLButtonDown(flags, x, y);
    bool ok = region < 0 || UiHandled(self);
    if ((region == 0 || region == 1) && !shift)
        ok = ok && self->scrollDirection_ == region + 1 && UiCapture(self->window.interface_) == input;
    if ((region == 0 || region == 1) && shift)
        ok = ok && self->position_[self->tab_] == (region == 0 ? 0.0f : 1000000.0f) && self->pending_ == 0.0f;
    if (self->tabsEnabled_ && region >= 2 && region < 8) ok = ok && self->tab_ == region - 2;
    ei::verify::Check("CUiItemStrip::OnLButtonDown", true, ok, "region %d shift %d", region, shift);
}
EI_VERIFY(0x00620E10, StripButtonDownVerify, 0, s_unused);

static void __fastcall StripButtonDownForwardVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CUiItemStrip>(input);
    int region = self->window.RegionAt(x, y);
    self->OnLButtonDownForward(flags, x, y);
    ei::verify::Check("CUiItemStrip::OnLButtonDownForward", true, region < 0 || UiHandled(self), "region %d", region);
}
EI_VERIFY(0x006212A0, StripButtonDownForwardVerify, 0, s_unused);

static void __fastcall SetFilterVerify(CUiItemStrip* self, void*, int filter) {
    self->CUiItemStrip::SetFilter(filter);
    bool ok = self->filter_ == filter && self->pending_ == 0.0f && self->velocity_ == 0.0f;
    for (float position : self->position_) ok = ok && position == 0.0f;
    ei::verify::Check("CUiItemStrip::SetFilter", true, ok, "filter %d", filter);
}
EI_VERIFY(0x00621420, SetFilterVerify, 0, s_unused);

static bool TabKeys() { return *reinterpret_cast<const int*>(0x0079BE84) != 0; }

static void __fastcall SelectTabVerify(CUiItemStrip* self, void*, int tab) {
    int expected = TabKeys() ? tab : self->tab_;
    self->CUiItemStrip::SelectTab(tab);
    ei::verify::Check("CUiItemStrip::SelectTab", expected, self->tab_, "tab %d", tab);
}
EI_VERIFY(0x00621510, SelectTabVerify, 0, s_unused);

static void __fastcall CycleTabVerify(CUiItemStrip* self, void*, int first, int count) {
    int tab = self->tab_;
    int expected = tab;
    if (TabKeys()) expected = first <= tab && tab < first + count ? (tab + 1 == first + count ? first : tab + 1) : first;
    self->CUiItemStrip::CycleTab(first, count);
    ei::verify::Check("CUiItemStrip::CycleTab", expected, self->tab_, "tab %d in [%d,+%d)", tab, first, count);
}
EI_VERIFY(0x00621530, CycleTabVerify, 0, s_unused);

static void __fastcall RebuildListVerify(CUiItemStrip* self, void*) {
    self->CUiItemStrip::RebuildList();
    // The list must be the source items whose tab (by the original classifiers) is the current one.
    bool ok = self->count_ == self->list_.size;
    int n = 0;
    for (int i = 0; ok && i < self->source_->size; ++i) {
        char* item = self->source_->data[i];
        bool keep;
        if (self->filter_ == 0 && self->tab_ == 5) {
            keep = true;
        } else {
            int tab = 0;
            switch (self->filter_) {
            case 0: tab = s_itemTab ? s_itemTab(self, nullptr, item) : self->ItemTab(item); break;
            case 1: tab = s_spellTab ? s_spellTab(self, nullptr, item) : self->SpellTab(item); break;
            case 2: tab = s_armorTab ? s_armorTab(self, nullptr, item) : self->ArmorTab(item); break;
            }
            keep = tab == self->tab_;
        }
        if (keep) ok = n < self->list_.size && self->list_.data[n++] == item;
    }
    ok = ok && n == self->list_.size;
    ei::verify::Check("CUiItemStrip::RebuildList", true, ok, "filter %d tab %d source %d list %d", self->filter_,
                      self->tab_, self->source_->size, self->list_.size);
}
EI_VERIFY(0x006212F0, RebuildListVerify, 0, s_unused);

// 00621F30: PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes). Both rebuild the list.
static char*(__fastcall* s_itemAt)(CUiItemStrip*, void*, int, int, int*);
static char* __fastcall ItemAtVerify(CUiItemStrip* self, void*, int x, int y, int* cursor) {
    int expectedCursor = *cursor;
    char* expected = s_itemAt(self, nullptr, x, y, &expectedCursor);
    char* actual = self->CUiItemStrip::ItemAt(x, y, cursor);
    ei::verify::Check("CUiItemStrip::ItemAt", true, expected == actual && expectedCursor == *cursor,
                      "(%d,%d) tab %d position %08X -> %p/%p", x, y, self->tab_, Bits(self->position_[self->tab_]),
                      expected, actual);
    return expected;
}
EI_VERIFY(0x00621F30, ItemAtVerify, 5, s_itemAt);

// --- Multiplayer hero screen ------------------------------------------------------------------

// 00616900: MOV ECX,0x7AFEA5 (5 bytes).
static char*(__fastcall* s_getHero)(CMpHeroScreen*);
static char* __fastcall GetHeroVerify(CMpHeroScreen* self) {
    char* expected = s_getHero(self);
    ei::verify::Check("CMpHeroScreen::GetHero", expected, self->CMpHeroScreen::GetHero(), "");
    return expected;
}
EI_VERIFY(0x00616900, GetHeroVerify, 5, s_getHero);

// 006193C0: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI] (5 bytes). Returned in ST0.
static double(__fastcall* s_weight)(CMpHeroScreen*);
static double __fastcall WeightVerify(CMpHeroScreen* self) {
    double expected = s_weight(self);
    double actual = self->CMpHeroScreen::EquipmentWeight();
    ei::verify::Check("CMpHeroScreen::EquipmentWeight", Bits(expected), Bits(actual), "%p %p %p", self->weapon_,
                      self->armor_, self->spellContainer_);
    return expected;
}
EI_VERIFY(0x006193C0, WeightVerify, 5, s_weight);

static void __fastcall ClampHeroVerify(CMpHeroScreen* self, void*) {
    self->CMpHeroScreen::ClampHeroSelection();
    const RecordTable& heroes = MpHeroes();
    int count = static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize;
    int index = self->heroIndex_;
    bool ok = count == 0 ? index == -1 : index >= 0 && index < count;
    ok = ok && (count == 0 || (self->heroTop_ <= index && index < self->heroTop_ + 10));
    ei::verify::Check("CMpHeroScreen::ClampHeroSelection", true, ok, "index %d top %d count %d", index,
                      self->heroTop_, count);
}
EI_VERIFY(0x00618FF0, ClampHeroVerify, 0, s_unused);

static void __fastcall ClampVoiceVerify(CMpHeroScreen* self, void*) {
    self->CMpHeroScreen::ClampVoiceSelection();
    int count = static_cast<int>(self->voices_[1] - self->voices_[0]) >> 3;
    int index = self->voiceIndex_;
    bool ok = count == 0 || (index >= 0 && index < count && self->voiceTop_ <= index && index < self->voiceTop_ + 11);
    if (ok && count > 0) {
        const char* chosen = *reinterpret_cast<const char* const*>(self->voices_[0] + index * 8);
        const char* stored = *reinterpret_cast<const char* const*>(self->Hero() + 0x18);
        ok = std::strcmp(chosen, stored) == 0;
    }
    ei::verify::Check("CMpHeroScreen::ClampVoiceSelection", true, ok, "index %d top %d count %d", index,
                      self->voiceTop_, count);
}
EI_VERIFY(0x00619090, ClampVoiceVerify, 0, s_unused);

static void __fastcall ItemChosenVerify(CMpHeroScreen* self, void*, char* item, int mode) {
    int type = *reinterpret_cast<const int*>(item + 0x10);
    self->CMpHeroScreen::OnItemChosen(item, mode);
    bool ok = true;
    if (mode == 0) {
        if (type == 0x3004) ok = self->weapon_ == item;
        else if (type == 0x3006) ok = self->armor_ == item;
        else if (type == 0x3008) ok = self->spellContainer_ == item;
    }
    ei::verify::Check("CMpHeroScreen::OnItemChosen", true, ok, "type %X mode %d", type, mode);
}
EI_VERIFY(0x00618980, ItemChosenVerify, 0, s_unused);

static void __fastcall HeroButtonUpVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CMpHeroScreen>(input);
    self->OnLButtonUp(flags, x, y);
    ei::verify::Check("CMpHeroScreen::OnLButtonUp", true,
                      self->rotating_ == 0 && UiCapture(self->window.interface_) != input, "(%d,%d)", x, y);
}
EI_VERIFY(0x006177D0, HeroButtonUpVerify, 0, s_unused);

// --- Escape menu, credits, exchange -------------------------------------------------------------

static void __fastcall EscMouseMoveVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CEscMenu>(input);
    int region = self->window.RegionAt(x, y);
    self->OnMouseMove(flags, x, y);
    int expected = region >= 0 && region <= 4 ? region : -1;
    ei::verify::Check("CEscMenu::OnMouseMove", expected, self->hovered_, "(%d,%d)", x, y);
}
EI_VERIFY(0x0062AD10, EscMouseMoveVerify, 0, s_unused);

static void __fastcall EscKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CEscMenu>(input);
    CInterface3D* ui = self->window.interface_;
    self->OnKeyDown(key, repeat, flags);
    if (key == VK_ESCAPE)
        ei::verify::Check("CEscMenu::OnKeyDown", true, UiHandled(self) && ui->pendingFlag_ && ui->pendingValue_ == 0,
                          "key %X", key);
    else
        ei::verify::Check("CEscMenu::OnKeyDown", true, true, "");
}
EI_VERIFY(0x0062B300, EscKeyDownVerify, 0, s_unused);

// The menu's Create (ui_layouts.cpp), ours only: one model and the five regions are added, the
// window covers the layout.
static void __fastcall EscBuildVerify(CEscMenu* self, void*) {
    int models = self->window.models_.size;
    int regions = self->window.regions_.size;
    self->CEscMenu::Build();
    bool ok = self->window.models_.size == models + 1 && self->window.regions_.size == regions + 5 &&
              self->window.rect_.right == 800 && self->window.rect_.bottom == 600;
    ei::verify::Check("CEscMenu::Build", true, ok, "models %d->%d regions %d->%d", models,
                      self->window.models_.size, regions, self->window.regions_.size);
}
EI_VERIFY(0x0062AB20, EscBuildVerify, 0, s_unused);

// The menu's clicks (ui_layouts.cpp), ours only: the window the region opens is on top of the
// interface afterwards (save/load 00741388 with the save flag, options 00741FAC, the quit box
// 00742348), resume leaves pending value 0, other points open nothing.
static void __fastcall EscLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CEscMenu>(input);
    CInterface3D* ui = self->window.interface_;
    int region = self->window.RegionAt(x, y);
    bool multiplayer = IsMultiplayerClient() != 0;
    CUiWindow* top = ui->GetActiveWindow();
    self->CEscMenu::OnLButtonDown(buttons, x, y);
    CUiWindow* now = ui->GetActiveWindow();
    auto vtableOf = [](CUiWindow* w) { return w ? reinterpret_cast<std::uintptr_t>(w->vtable_) : 0u; };
    bool ok;
    if ((region == 0 || region == 1) && !multiplayer)
        ok = now != top && vtableOf(now) == 0x00741388 &&
             reinterpret_cast<CSaveLoadWindow*>(now)->isSave_ == (region == 0);
    else if (region == 2)
        ok = ui->pendingFlag_ && ui->pendingValue_ == 0;
    else if (region == 3)
        ok = now != top && vtableOf(now) == 0x00742348;
    else if (region == 4)
        ok = now != top && vtableOf(now) == 0x00741FAC;
    else
        ok = now == top;
    ei::verify::Check("CEscMenu::OnLButtonDown", true, ok, "(%d,%d) region %d top %p->%p", x, y, region, top, now);
}
EI_VERIFY(0x0062AE90, EscLButtonDownVerify, 0, s_unused);

// The main menu's clicks (ui_layouts.cpp), ours only: the window the region opens is on top of
// the interface afterwards with its vtable (difficulty box 0074225C, save/load 00741388 as a load
// window, options 00741FAC, titles 00740C3C, hero screen 0074214C); other points open nothing.
// Region 2 quits (no scenario clicks it).
static void __fastcall MainMenuLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CMainMenu>(input);
    CInterface3D* ui = self->interface_;
    int region = self->RegionAt(x, y);
    CUiWindow* top = ui->GetActiveWindow();
    self->CMainMenu::OnLButtonDown(buttons, x, y);
    CUiWindow* now = ui->GetActiveWindow();
    auto vtableOf = [](CUiWindow* w) { return w ? reinterpret_cast<std::uintptr_t>(w->vtable_) : 0u; };
    static const std::uintptr_t kOpened[6] = {0x0074225C, 0x00741388, 0, 0x00741FAC, 0x00740C3C, 0x0074214C};
    bool ok;
    if (region == 2)
        ok = true;
    else if (region >= 0 && region < 6)
        ok = now != top && vtableOf(now) == kOpened[region] &&
             (region != 1 || !reinterpret_cast<CSaveLoadWindow*>(now)->isSave_);
    else
        ok = now == top;
    ei::verify::Check("CMainMenu::OnLButtonDown", true, ok, "(%d,%d) region %d top %p->%p", x, y, region, top, now);
}
EI_VERIFY(0x00632D70, MainMenuLButtonDownVerify, 0, s_unused);

// The message box's Create (ui_layouts.cpp), ours only: one surface, the shade and frame sprites
// and the buttons' (2 with flags 1 and 2, else 1), regions (2; 1 with flag 1 alone; else an empty
// one and the cancel button), the button regions enabled (two on one row), and with flag 4 the
// edit field as a child.
static void __fastcall MessageBoxBuildVerify(CMessageBox* self, void*) {
    int surfaces = self->surfaces_.size;
    int sprites = self->sceneObjects_.size;
    int regions = self->regions_.size;
    int children = self->children_.size;
    self->CMessageBox::Build();
    std::uint32_t flags = self->flags_;
    bool two = (flags & 2) && (flags & 1);
    int buttons = two ? 2 : 1;
    bool ok = self->surfaces_.size == surfaces + 1 && self->sceneObjects_.size == sprites + 2 + buttons &&
              self->regions_.size == regions + 2 - ((flags & 3) == 1 ? 1 : 0);
    if (ok) {
        // The button regions (screen coordinates after the layout scaling): enabled rectangles,
        // two of them side by side on one row.
        const CUiRegion* last = self->regions_.data[self->regions_.size - 1];
        ok = last->enabled_ && last->bounds_.bottom > last->bounds_.top && last->bounds_.right > last->bounds_.left;
        if (two) {
            const CUiRegion* first = self->regions_.data[self->regions_.size - 2];
            ok = ok && first->enabled_ && first->bounds_.top == last->bounds_.top &&
                 first->bounds_.right < last->bounds_.left;
        }
    }
    if (flags & 4) ok = ok && self->edit_ && self->children_.size == children + 1;
    ei::verify::Check("CMessageBox::Build", true, ok, "flags %X surfaces %d->%d sprites %d->%d regions %d->%d", flags,
                      surfaces, self->surfaces_.size, sprites, self->sceneObjects_.size, regions,
                      self->regions_.size);
}
EI_VERIFY(0x00634790, MessageBoxBuildVerify, 0, s_unused);

// The difficulty question's Create (ui_layouts.cpp), ours only: one surface, six sprites (the
// two check boxes, the hard one checked by its texture shift +0xCC, the shade, the frame, OK and
// cancel), four regions (OK and cancel side by side on one row, the two check boxes one above the
// other), easy_ 0. Reached by ui_main_menu (the main menu's region 0).
static void __fastcall DifficultyBoxBuildVerify(CDifficultyBox* self, void*) {
    int surfaces = self->surfaces_.size;
    int sprites = self->sceneObjects_.size;
    int regions = self->regions_.size;
    self->CDifficultyBox::Build();
    bool ok = self->surfaces_.size == surfaces + 1 && self->sceneObjects_.size == sprites + 6 &&
              self->regions_.size == regions + 4 && self->easy_ == 0;
    if (ok) {
        auto shift = [&](int i) {
            std::uint32_t v;
            std::memcpy(&v, reinterpret_cast<const std::uint8_t*>(self->sceneObjects_.data[sprites + i]) + 0xCC, 4);
            return v;
        };
        ok = shift(0) != 0xBDB80000u && shift(1) == 0xBDB80000u;
        const CUiRegion* const* r = self->regions_.data + regions;
        ok = ok && r[0]->enabled_ && r[1]->enabled_ && r[0]->bounds_.top == r[1]->bounds_.top &&
             r[0]->bounds_.right < r[1]->bounds_.left && r[2]->bounds_.left == r[3]->bounds_.left &&
             r[2]->bounds_.bottom < r[3]->bounds_.top && r[3]->bounds_.bottom < r[0]->bounds_.top;
    }
    ei::verify::Check("CDifficultyBox::Build", true, ok, "surfaces %d->%d sprites %d->%d regions %d->%d easy %d",
                      surfaces, self->surfaces_.size, sprites, self->sceneObjects_.size, regions,
                      self->regions_.size, self->easy_);
}
EI_VERIFY(0x006337B0, DifficultyBoxBuildVerify, 0, s_unused);

// The titles' Create (ui_layouts.cpp), ours only: two surfaces added and kept in +0x164 / +0x168,
// the script's lines appended to the list (the current line its first new node; the sentinel when
// nothing was read), every new line's speed finite, +0x1CC and +0x1F5 cleared and the display's
// +0x14 zero. Reached by ui_main_menu (the main menu's region 4).
static void __fastcall CreditsBuildVerify(CCreditsScreen* self, void*) {
    using Node = stlp::list<TitleLine>::Node;
    auto countLines = [&]() {
        int n = 0;
        for (Node* node = self->lines_.First(); node != self->lines_.End() && n < 100000; node = node->next) ++n;
        return n;
    };
    int surfaces = self->window.surfaces_.size;
    int before = countLines();
    self->CCreditsScreen::Build();
    int after = countLines();
    const std::uint8_t* display = reinterpret_cast<std::uint8_t*(__cdecl*)()>(0x00500960)();
    std::uint32_t display14;
    std::memcpy(&display14, display + 0x14, 4);
    std::uint32_t nextLineBits;
    std::memcpy(&nextLineBits, &self->nextLineAt_, 4);
    bool ok = self->window.surfaces_.size == surfaces + 2 &&
              self->textSurface_ == self->window.surfaces_.data[surfaces] &&
              self->headerSurface_ == self->window.surfaces_.data[surfaces + 1] && nextLineBits == 0 &&
              !self->finished_ && display14 == 0;
    if (after == before) {
        ok = ok && self->currentLine_ == self->lines_.End();
    } else {
        // The first of the new nodes (the list was empty before in every opening seen).
        Node* node = self->lines_.First();
        for (int i = 0; i < before; ++i) node = node->next;
        ok = ok && (before != 0 || self->currentLine_ == node);
        for (; node != self->lines_.End(); node = node->next) {
            std::uint32_t bits;
            std::memcpy(&bits, &node->data.speed, 4);
            ok = ok && (bits & 0x7F800000u) != 0x7F800000u;   // finite
        }
    }
    ei::verify::Check("CCreditsScreen::Build", true, ok, "surfaces %d->%d lines %d->%d", surfaces,
                      self->window.surfaces_.size, before, after);
}
EI_VERIFY(0x0062B7D0, CreditsBuildVerify, 0, s_unused);

static void __fastcall EscMessageVerify(CEscMenu* self, void*, int result) {
    CInterface3D* ui = self->window.interface_;
    bool closes = result == 0x100001 || result == 0x100002 || result == 0x110001;
    self->CEscMenu::OnMessageResult(result);
    bool ok = !closes || (ui->pendingFlag_ && ui->pendingValue_ == 0);
    if (result == 0x10001) ok = *reinterpret_cast<const int*>(0x007AFE20) == 1;
    ei::verify::Check("CEscMenu::OnMessageResult", true, ok, "result %X", result);
}
EI_VERIFY(0x0062ACC0, EscMessageVerify, 0, s_unused);

static bool CreditsClosed(CCreditsScreen* self) {
    CInterface3D* ui = self->window.interface_;
    if (self->fromGame_) return ui->pendingFlag_ && ui->pendingValue_ == 0xCCCCCCCC;
    return *reinterpret_cast<const int*>(0x007AFE20) == 1;
}

static void __fastcall CreditsButtonVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CCreditsScreen>(input);
    self->OnLButtonDown(flags, x, y);
    ei::verify::Check("CCreditsScreen::OnLButtonDown", true, CreditsClosed(self), "from game %d", self->fromGame_);
}
EI_VERIFY(0x0062BFE0, CreditsButtonVerify, 0, s_unused);

static void __fastcall CreditsKeyVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CCreditsScreen>(input);
    self->OnKeyDown(key, repeat, flags);
    bool closes = key == VK_RETURN || key == VK_ESCAPE || key == VK_SPACE;
    ei::verify::Check("CCreditsScreen::OnKeyDown", true, !closes || CreditsClosed(self), "key %X", key);
}
EI_VERIFY(0x0062C010, CreditsKeyVerify, 0, s_unused);

static void __fastcall ExchangeKeyVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CExchangeScreen>(input);
    self->OnKeyDown(key, repeat, flags);
    bool ours = key == VK_BACK || key == VK_RETURN || key == VK_ESCAPE;
    ei::verify::Check("CExchangeScreen::OnKeyDown", true, !ours || UiHandled(self), "key %X", key);
}
EI_VERIFY(0x0062D090, ExchangeKeyVerify, 0, s_unused);

// --- Camp screen and the heroes' spell requirements ------------------------------------------

// 00610020: PUSH EBP; MOV EBP,ESP; SUB ESP,0x10 (6 bytes). Pure: the original into scratch outputs.
static void(__fastcall* s_bestSkill)(CCampScreen*, void*, int, int*, int*);
static void __fastcall BestSkillVerify(CCampScreen* self, void*, int school, int* skill, int* mana) {
    int expectedSkill = 0, expectedMana = 0;
    s_bestSkill(self, nullptr, school, &expectedSkill, &expectedMana);
    self->CCampScreen::BestPartySkill(school, skill, mana);
    ei::verify::Check("CCampScreen::BestPartySkill", true, expectedSkill == *skill && expectedMana == *mana,
                      "school %d: %d/%d, %d/%d", school, expectedSkill, *skill, expectedMana, *mana);
}
EI_VERIFY(0x00610020, BestSkillVerify, 6, s_bestSkill);

// 006188E0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Pure (AL).
static bool(__fastcall* s_canEquip)(CMpHeroScreen*, void*, char*, int);
static bool __fastcall CanEquipVerify(CMpHeroScreen* self, void*, char* item, int mode) {
    bool expected = s_canEquip(self, nullptr, item, mode);
    ei::verify::Check("CMpHeroScreen::CanEquip", expected, self->CMpHeroScreen::CanEquip(item, mode), "type %X",
                      *reinterpret_cast<const int*>(item + 0x10));
    return expected;
}
EI_VERIFY(0x006188E0, CanEquipVerify, 6, s_canEquip);

// Only the strips the page uses are read: the spell and part strips exist only with their
// constructor (otherwise the fields hold leftover bytes), and the equipment panel calls this slot
// (44) with other values when a click finds no item.
static void __fastcall ShowPageTabsVerify(CCampScreen* self, void*, int page, int unused) {
    bool used[3] = {(page >= 1 && page <= 10) || page == 16 || (page >= 13 && page <= 15) || (page >= 19 && page <= 21),
                    page >= 19 && page <= 21, page >= 13 && page <= 15};
    CUiItemStrip* strips[3] = {used[0] ? self->itemStrip_ : nullptr, used[1] ? self->spellStrip_ : nullptr,
                               used[2] ? self->partStrip_ : nullptr};
    int expected[3];
    for (int i = 0; i < 3; ++i) expected[i] = strips[i] ? strips[i]->tab_ : -1;
    if (TabKeys()) {
        auto cycle = [](int tab, int first, int count) {
            return first <= tab && tab < first + count ? (tab + 1 == first + count ? first : tab + 1) : first;
        };
        switch (page) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 16:
            expected[0] = 2; break;
        case 13: expected[0] = 2; expected[2] = 5; break;
        case 14: expected[0] = 0; expected[2] = cycle(expected[2], 0, 4); break;
        case 15: expected[0] = 0; expected[2] = 4; break;
        case 19: expected[0] = 2; expected[1] = 5; break;
        case 20: expected[0] = 1; expected[1] = cycle(expected[1], 0, 3); break;
        case 21: expected[0] = 1; expected[1] = cycle(expected[1], 3, 2); break;
        }
    }
    self->CCampScreen::ShowPageTabs(page, unused);
    bool ok = true;
    for (int i = 0; i < 3; ++i) ok = ok && (!strips[i] || strips[i]->tab_ == expected[i]);
    ei::verify::Check("CCampScreen::ShowPageTabs", true, ok, "page %d", page);
}
EI_VERIFY(0x006110F0, ShowPageTabsVerify, 0, s_unused);
