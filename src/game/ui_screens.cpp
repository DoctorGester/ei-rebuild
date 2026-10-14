// Interface screens of 00610000-00630000: the camp's item strips, the multiplayer hero screen, the
// escape menu, the credits and the exchange window. See ui_screens.h and docs/notes/ui.md.
#include "ui_screens.h"

#include "core_maths.h"
#include "item.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "originals_units.h"

namespace {
// The game's own strings (the sound system is given the game's pointers, as the original does).
const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* const kSoundCampScroll = GameString(0x00793CB8);   // "buttons\camp\scroll.wav"
const char* const kSoundCampMode = GameString(0x00793CA0);     // "buttons\camp\mode.wav"
const char* const kSoundMenuStone = GameString(0x00793FCC);    // "buttons\menu\stone.wav"
const char* const kSoundMenuOk = GameString(0x00793FF0);       // "buttons\menu\ok.wav"
const char* const kSoundMessageOk = GameString(0x00793A88);    // "buttons\messbox\ok.wav"

// Item fields read by the strips.
int ItemType(const char* item) { return *reinterpret_cast<const int*>(item + 0x10); }
int ItemSubKind(const char* item) { return *reinterpret_cast<const int*>(item + 0x50); }
std::uint32_t ItemRecord(const char* item) { return *reinterpret_cast<const std::uint32_t*>(item + 0x54); }
float ItemWeight(const char* item) { return *reinterpret_cast<const float*>(item + 0x34); }

constexpr int kTypeWeapon = 0x3004;
constexpr int kTypeArmor = 0x3006;
constexpr int kTypeQuick = 0x3007;
constexpr int kTypeSpellContainer = 0x3008;

// The game's _ftol (006E853C): truncation of the value in ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// Not rewritten (forwarders). Thiscall methods are declared __fastcall with the unused EDX.
__declspec(naked) int __fastcall PlaySoundAt(CInterface3D*, void*, const char*, int) EI_ORIGINAL(0x005F7E60)
// 005F7C80 / 005F7C60: the chat line (when it is not open: 005E3930 / 005E36C0).
// 005FC730: a region's tooltip (text "tip_<id>").
__declspec(naked) void __fastcall SetRegionTip(CUiRegion*, void*, int, int, int) EI_ORIGINAL(0x005FC730)
// 0065B090: a scroll bar's position; returns whether it moved since the last call.
BOOL TakeScrollPos(void* bar, void*, int* position) { return ScrollbarTakePositionOriginal(bar, nullptr, position); }
// The hero screen's equipment panels.

// Unit statistics (UnitStats, not rewritten here; called through their addresses).
__declspec(naked) double __fastcall StatsPeriod(void*) EI_ORIGINAL(0x00523CD0)
__declspec(naked) char* __fastcall SpellPrototype(void*) EI_ORIGINAL(0x0068BA20)

void* MultiplayerClient() { return *reinterpret_cast<void**>(0x0079B5D8); }
void* const kGame = reinterpret_cast<void*>(0x007AFE20);
// 0079BE84: the camp's tab keys are active.
bool TabKeysEnabled() { return *reinterpret_cast<const int*>(0x0079BE84) != 0; }

// Virtual slot `slot` of an object with its vtable at +0.
template <typename Fn>
Fn Virtual(const void* object, int slot) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(object))[slot]);
}

// A scroll bar's virtual slot 25 (+0x64): set the position.
void SetScrollPos(void* bar, int position) {
    Virtual<void(__thiscall*)(void*, int)>(bar, 25)(bar, position);
}
} // namespace

// --- Shared ---------------------------------------------------------------------------------

__declspec(naked) void UiInput::BaseLButtonDown(int, int, int) EI_ORIGINAL(0x00601110)
__declspec(naked) void UiInput::BaseLButtonUp(int, int, int) EI_ORIGINAL(0x006011F0)
__declspec(naked) void UiInput::BaseMouseMove(int, int, int) EI_ORIGINAL(0x006012D0)
__declspec(naked) void UiInput::BaseKeyDown(int, int, int) EI_ORIGINAL(0x00601820)
__declspec(naked) void UiInput::BaseKeyUp(int, int, int) EI_ORIGINAL(0x006019A0)
__declspec(naked) void UiInput::BaseChar(int, int, int) EI_ORIGINAL(0x006018E0)
__declspec(naked) int UiInput::BaseWheel(int, int, int, int) EI_ORIGINAL(0x00601730)

__declspec(naked) void __fastcall UiItemsSetSize(UiItemArray*, void*, int, int) EI_ORIGINAL(0x0062A390)

// --- Item predicates -------------------------------------------------------------------------

namespace {
// The item as loot (type 0x3007), or null.
const CItemLoot* AsLoot(const char* item) {
    auto* loot = reinterpret_cast<const CItemLoot*>(item);
    return loot->type_ == kTypeQuick ? loot : nullptr;
}
const unsigned char* NoMaterial() { return reinterpret_cast<const unsigned char*>(0x007846F0); }   // "none"
} // namespace

bool UiItemKinds::IsSpell(const char* item) {
    const CItemLoot* loot = AsLoot(item);
    return loot && loot->kind_ == 2 && loot->recordType_ == 3;
}
EI_REPLACE_METHOD(0x005EE050, UiItemKinds::IsSpell);

bool UiItemKinds::IsModifier(const char* item) {
    const CItemLoot* loot = AsLoot(item);
    return loot && loot->kind_ == 2 && loot->recordType_ == 4;
}
EI_REPLACE_METHOD(0x005EE080, UiItemKinds::IsModifier);

BOOL UiItemKinds::IsSpellContainer(const char* item) {
    return reinterpret_cast<const CEngineObject*>(item)->type_ == kTypeSpellContainer;
}
EI_REPLACE_METHOD(0x005EE0B0, UiItemKinds::IsSpellContainer);

bool UiItemKinds::IsPart(const char* item) {
    const CItemLoot* loot = AsLoot(item);
    return loot && loot->kind_ == 2 && loot->recordType_ <= 2;
}
EI_REPLACE_METHOD(0x005EE0D0, UiItemKinds::IsPart);

bool UiItemKinds::IsKindOne(const char* item) {
    const CItemLoot* loot = AsLoot(item);
    return loot && loot->kind_ == 1;
}
EI_REPLACE_METHOD(0x005EE110, UiItemKinds::IsKindOne);

bool UiItemKinds::IsBuildable(const char* item) {
    auto* object = reinterpret_cast<CItem*>(const_cast<char*>(item));
    auto* prototype = reinterpret_cast<const ItemPrototypeRecord*>(object->GetPrototype());
    if (prototype && !prototype->buildable_) return false;
    int type = object->type_;
    if (type == kTypeWeapon || type == 0x3005) return true;
    if (type != kTypeArmor) return false;
    prototype = reinterpret_cast<const ItemPrototypeRecord*>(object->GetPrototype());
    return prototype &&
           gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(prototype->materialClass_), NoMaterial()) != 0;
}
EI_REPLACE_METHOD(0x005EE130, UiItemKinds::IsBuildable);


namespace uiorig {
void PlayUiSound(CInterface3D* ui, const char* name, int flags) { PlaySoundAt(ui, nullptr, name, flags); }
void LeaveGame() { FlowCloseSessionsOriginal(kGame); }
} // namespace uiorig

// --- CUiItemStrip -----------------------------------------------------------------------------

namespace {
UiItemKinds* KindsOf(CUiWindow* owner) { return reinterpret_cast<UiItemKinds*>(owner); }
} // namespace

void CUiItemStrip::SetTab(int tab) {
    float& position = position_[tab_];
    position = static_cast<float>(GameFtol(static_cast<double>(pending_) + position));
    tab_ = tab;
    for (int i = 0; i < 6; ++i) {
        auto* object = reinterpret_cast<char*>(window.sceneObjects_.data[i + 2]);
        float shade = i == tab ? 1.0f : 0.5f;
        auto* color = reinterpret_cast<float*>(object + 0x224);
        color[0] = shade;
        color[1] = shade;
        color[2] = shade;
    }
    pending_ = 0.0f;
    velocity_ = 0.0f;
    scrollTimer_ = 0.0f;
    scrollDirection_ = 0;
}
EI_REPLACE_METHOD(0x00620960, CUiItemStrip::SetTab);
__declspec(naked) void CUiItemStrip::CallSetTab(int) EI_ORIGINAL(0x00620960)

void CUiItemStrip::SetFilter(int filter) {
    for (float& position : position_) position = 0.0f;
    pending_ = 0.0f;
    filter_ = filter;
    velocity_ = 0.0f;
    // Each filter has its own button texture column (+0xCC of the tab objects) and tooltips.
    int tipBase;
    std::uint32_t column;
    switch (filter) {
    case 0: tipBase = 0x526A; column = 0x3F280000; break;   // 0.65625
    case 1: tipBase = 0x52CE; column = 0x3EA80000; break;   // 0.328125
    case 2: tipBase = 0x5206; column = 0; break;
    default: return;
    }
    for (int i = 2; i < 8; ++i) {
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(window.sceneObjects_.data[i]) + 0xCC) = column;
        SetRegionTip(window.regions_.data[i], nullptr, tipBase + i, 1, 0);
    }
}
EI_REPLACE_METHOD(0x00621420, CUiItemStrip::SetFilter);

void CUiItemStrip::SelectTab(int tab) {
    if (TabKeysEnabled()) SetTab(tab);
}
EI_REPLACE_METHOD(0x00621510, CUiItemStrip::SelectTab);

void CUiItemStrip::CycleTab(int first, int count) {
    if (!TabKeysEnabled()) return;
    int next = first;
    if (first <= tab_ && tab_ < first + count) {
        next = tab_ + 1;
        if (next == first + count) next = first;
    }
    SetTab(next);
}
EI_REPLACE_METHOD(0x00621530, CUiItemStrip::CycleTab);

int CUiItemStrip::ItemTab(const char* item) {
    UiItemKinds* kinds = KindsOf(owner_);
    if (kinds->IsPart(item) || kinds->IsKindOne(item)) return 0;
    if (kinds->IsSpell(item) || kinds->IsModifier(item)) return 1;
    int type = ItemType(item);
    if (type == kTypeWeapon || type == 0x3005 || type == kTypeArmor || type == kTypeSpellContainer) return 2;
    if (type == kTypeQuick) return 3;
    return (type != 0x3009) + 4;
}
EI_REPLACE_METHOD(0x006218F0, CUiItemStrip::ItemTab);

int CUiItemStrip::SpellTab(const char* item) {
    UiItemKinds* kinds = KindsOf(owner_);
    if (kinds->IsSpell(item)) {
        // A record out of range is read through null, as in the original.
        const char* spell = itemdb::Spells().At(ItemRecord(item), itemdb::kSpellSize);
        switch (Field<int>(spell, 0x10)) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 5;
        }
    }
    if (kinds->IsModifier(item)) {
        const char* modifier = itemdb::SpellModifiers().At(ItemRecord(item), itemdb::kSpellModifierSize);
        return (Field<int>(modifier, 0xC) > 5) + 3;
    }
    return 5;
}
EI_REPLACE_METHOD(0x006219A0, CUiItemStrip::SpellTab);

int CUiItemStrip::ArmorTab(const char* item) {
    UiItemKinds* kinds = KindsOf(owner_);
    if (kinds->IsPart(item)) {
        switch (ItemSubKind(item)) {
        case 0: return 0;
        case 1: {
            const char* armor = itemdb::Armors().At(ItemRecord(item), itemdb::kArmorSize);
            return Field<std::uint32_t>(armor, 0xC) > 2 ? 1 : 2;
        }
        case 2: return 3;
        default: return 5;
        }
    }
    if (ItemType(item) == kTypeArmor) return 3;
    return kinds->IsKindOne(item) ? 4 : 5;
}
EI_REPLACE_METHOD(0x00621A80, CUiItemStrip::ArmorTab);

void CUiItemStrip::RebuildList() {
    UiItemsSetSize(&list_, nullptr, 0, -1);
    if (filter_ == 0 && tab_ == 5) {
        for (int i = 0; i < source_->size; ++i) {
            int index = list_.size;
            char* item = source_->data[i];
            UiItemsSetSize(&list_, nullptr, index + 1, -1);
            list_.data[index] = item;
        }
    } else {
        for (int i = 0; i < source_->size; ++i) {
            char* item = source_->data[i];
            if (TabOf(item) == tab_) {
                int index = list_.size;
                UiItemsSetSize(&list_, nullptr, index + 1, -1);
                list_.data[index] = item;
            }
        }
    }
    count_ = list_.size;
}
EI_REPLACE_METHOD(0x006212F0, CUiItemStrip::RebuildList);

int CUiItemStrip::TabOf(const char* item) {
    switch (filter_) {
    case 0: return ItemTab(item);
    case 1: return SpellTab(item);
    case 2: return ArmorTab(item);
    default: return 0;
    }
}

void CUiItemStrip::ScrollToItem(char* item) {
    if (!item) return;
    auto* wanted = reinterpret_cast<CItem*>(item);
    float found = -1.0f;   // the list index of the last match (FILD of the size, stored as float)
    UiItemsSetSize(&list_, nullptr, 0, -1);
    // The original inlines the array's SetSize (0062A390) in the second loop.
    auto append = [&](char* entry) {
        if (wanted->IsSameAsSlot(reinterpret_cast<CItem*>(entry))) found = static_cast<float>(list_.size);
        int index = list_.size;
        UiItemsSetSize(&list_, nullptr, index + 1, -1);
        list_.data[index] = entry;
    };
    if (filter_ == 0 && tab_ == 5) {
        for (int i = 0; i < source_->size; ++i) append(source_->data[i]);
    } else {
        int tab = TabOf(item);
        if (TabKeysEnabled()) CallSetTab(tab);
        for (int i = 0; i < source_->size; ++i)
            if (TabOf(source_->data[i]) == tab_) append(source_->data[i]);
    }
    if (!(found < 0.0f)) {
        float& position = position_[tab_];
        double lowest = static_cast<double>(found) - 8.0 + 2.0;   // kept on the FPU
        if (lowest > position) {
            position = static_cast<float>(lowest);
        } else if (position > found) {
            position = found;
        }
    }
    pending_ = 0.0f;
    velocity_ = 0.0f;
}
EI_REPLACE_METHOD(0x00621570, CUiItemStrip::ScrollToItem);

char* CUiItemStrip::CallItemAt(int x, int y, int* cursor) {
    return Virtual<char*(__thiscall*)(CUiItemStrip*, int, int, int*)>(this, 25)(this, x, y, cursor);
}

void CUiItemStrip::CallRebuildList() { Virtual<void(__thiscall*)(CUiItemStrip*)>(this, 26)(this); }

char* CUiItemStrip::ItemAt(int x, int y, int* cursor) {
    if (!window.enabled_) return nullptr;
    if (window.RegionAt(x, y) != 8) return nullptr;
    switch (actionMode_) {
    case 1: *cursor = 0x16; break;
    case 2: *cursor = 0x18; break;
    case 3: *cursor = 0x17; break;
    }
    POINT point{x, y};
    window.interface_->CallScalePointFromScreen(&point);
    // A hundred pixels per slot from x 50, plus the tab's scroll position.
    constexpr float kSlotsPerPixel = 0.01f;
    int index = GameFtol(static_cast<double>(point.x - 50) * kSlotsPerPixel + position_[tab_]);
    CallRebuildList();
    if (index < 0 || index >= list_.size) return nullptr;
    return list_.data[index];
}
EI_REPLACE_METHOD(0x00621F30, CUiItemStrip::ItemAt);

void CUiItemStrip::SettleScroll() {
    // Releasing an arrow completes the step under way: the pending scroll keeps its fraction of a
    // slot and gains a whole slot when that fraction is under half.
    float pending = pending_;
    if (!(pending >= 0.0f)) {
        float fraction = static_cast<float>(pending - static_cast<double>(GameFtol(pending)));
        pending_ = fraction;
        if (fraction > -0.5f) pending_ = fraction - 1.0f;
    } else if (pending > 0.0f) {
        float fraction = static_cast<float>(pending - static_cast<double>(GameFtol(pending)));
        pending_ = fraction;
        if (!(fraction >= 0.5f)) pending_ = fraction + 1.0f;
    }
    scrollDirection_ = 0;
    scrollTimer_ = 0.0f;
    UiCapture(window.interface_) = nullptr;
}

void CUiItemStrip::OnLButtonDown(int, int x, int y) {
    CInterface3D* ui = window.interface_;
    int region = window.RegionAt(x, y);
    if (region == 0 || region == 1) {
        if (!UiShiftDown(ui)) {
            uiorig::PlayUiSound(ui, kSoundCampScroll, 0);
            UiCapture(ui) = CaptureValue(InputOf(this));
            scrollDirection_ = region == 0 ? 1 : 2;
        } else {
            // Shift jumps to the start or (clamped later) the end.
            *reinterpret_cast<std::uint32_t*>(&position_[tab_]) = region == 0 ? 0 : 0x49742400;   // 1e6
            *reinterpret_cast<std::uint32_t*>(&pending_) = 0;
            *reinterpret_cast<std::uint32_t*>(&velocity_) = 0;
        }
    }
    if (tabsEnabled_ && region >= 2 && region < 8) {
        uiorig::PlayUiSound(ui, kSoundCampMode, 0);
        SetTab(region - 2);
    }
    if (region >= 0) UiHandled(this) = true;
}

void CUiItemStrip::OnLButtonUp(int, int, int) {
    if (scrollDirection_ != 0) SettleScroll();
}

void CUiItemStrip::OnMouseMove(int, int x, int y) {
    int region = window.RegionAt(x, y);
    // Leaving the held arrow stops scrolling.
    if ((scrollDirection_ == 1 && region != 0) || (scrollDirection_ == 2 && region != 1)) SettleScroll();
}

BOOL CUiItemStrip::OnWheel(int, short delta, int x, int y) {
    if (window.RegionAt(x, y) == 8) {
        pending_ = static_cast<float>(static_cast<double>(delta / 120) + pending_);
        uiorig::PlayUiSound(window.interface_, kSoundCampScroll, 0);
    }
    return 1;
}

void CUiItemStrip::OnLButtonDownForward(int flags, int x, int y) {
    OnLButtonDown(flags, x, y);
    // The original passes its own first argument's stack slot as the cursor output.
    char* item = CallItemAt(x, y, &flags);
    if (item) Virtual<void(__thiscall*)(CUiWindow*, char*, int)>(owner_, 42)(owner_, item, actionMode_);
}

// Entry points on the input sub-object.
void __fastcall ItemStripLButtonDown(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CUiItemStrip>(input)->OnLButtonDown(flags, x, y);
}
void __fastcall ItemStripLButtonUp(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CUiItemStrip>(input)->OnLButtonUp(flags, x, y);
}
void __fastcall ItemStripMouseMove(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CUiItemStrip>(input)->OnMouseMove(flags, x, y);
}
BOOL __fastcall ItemStripWheel(void* input, void*, int keys, int delta, int x, int y) {
    return WindowFromInput<CUiItemStrip>(input)->OnWheel(keys, static_cast<short>(delta), x, y);
}
void __fastcall ItemStripLButtonDownForward(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CUiItemStrip>(input)->OnLButtonDownForward(flags, x, y);
}
EI_REPLACE(0x00620E10, ItemStripLButtonDown);
EI_REPLACE(0x00621010, ItemStripLButtonUp);
EI_REPLACE(0x00620F10, ItemStripMouseMove);
EI_REPLACE(0x006210F0, ItemStripWheel);
EI_REPLACE(0x006212A0, ItemStripLButtonDownForward);

// --- Heroes' skills ---------------------------------------------------------------------------

int HeroSchoolSkill(void* stats, int school) {
    return x87::fistp(static_cast<float>(UnitStatsSpellSchoolSkill(static_cast<UnitStats*>(stats), nullptr, school)));
}

int HeroManaUnits(void* stats) {
    // The maximum is stored as a float (FSTP) before the division; the quotient is not rounded.
    float maximum = StoreFloat(static_cast<float>(UnitStatsManaCurve(static_cast<UnitStats*>(stats))));
    return GameFtol(maximum / StatsPeriod(stats));
}

bool HeroCanUseSpell(void* stats, char* spell) {
    bool ok = true;
    int required = *reinterpret_cast<int*>(spell + 0x50);
    const char* prototype = SpellPrototype(spell);
    if (HeroSchoolSkill(stats, *reinterpret_cast<const int*>(prototype + 0x14)) < required) ok = false;
    float cost = *reinterpret_cast<float*>(spell + 0x40);
    int units = HeroManaUnits(stats);
    if (units < GameFtol(cost)) ok = false;
    return ok;
}

// --- CCampScreen ------------------------------------------------------------------------------

void CCampScreen::BestPartySkill(int school, int* skill, int* mana) {
    int bestSkill = 0;
    int bestMana = 0;
    for (int i = 0; i < *reinterpret_cast<int*>(LocalPlayerData() + 0x34); ++i) {
        char* heroes = *reinterpret_cast<char**>(LocalPlayerData() + 0x30);
        void* stats = heroes + i * 0x784 + 0x20;
        int value = HeroSchoolSkill(stats, school);
        if (!(bestSkill > value)) bestSkill = value;
        int units = HeroManaUnits(stats);
        if (!(bestMana > units)) bestMana = units;
    }
    *skill = bestSkill;
    *mana = bestMana;
}
EI_REPLACE_METHOD(0x00610020, CCampScreen::BestPartySkill);

void CCampScreen::ShowPageTabs(int page, int) {
    switch (page) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 16:
        itemStrip_->SelectTab(2);
        break;
    case 13:
        itemStrip_->SelectTab(2);
        partStrip_->SelectTab(5);
        break;
    case 14:
        itemStrip_->SelectTab(0);
        partStrip_->CycleTab(0, 4);
        break;
    case 15:
        itemStrip_->SelectTab(0);
        partStrip_->SelectTab(4);
        break;
    case 19:
        itemStrip_->SelectTab(2);
        spellStrip_->SelectTab(5);
        break;
    case 20:
        itemStrip_->SelectTab(1);
        spellStrip_->CycleTab(0, 3);
        break;
    case 21:
        itemStrip_->SelectTab(1);
        spellStrip_->CycleTab(3, 2);
        break;
    }
}
EI_REPLACE_METHOD(0x006110F0, CCampScreen::ShowPageTabs);

// --- CMpHeroScreen ----------------------------------------------------------------------------

void CMpHeroScreen::CallShowHero(int index) { HeroScreenShowHeroOriginal(this, nullptr, index); }
void CMpHeroScreen::CallDrawVoices() { HeroScreenDrawVoicesOriginal(this); }
void CMpHeroScreen::CallDrawHeroList() { HeroScreenDrawListOriginal(this); }

char* CMpHeroScreen::Hero() { return Virtual<char*(__thiscall*)(CMpHeroScreen*)>(this, 51)(this); }
void CMpHeroScreen::Refresh() { Virtual<void(__thiscall*)(CMpHeroScreen*)>(this, 47)(this); }

void CMpHeroScreen::ClampHeroSelection() {
    if (heroIndex_ < 0) heroIndex_ = 0;
    const RecordTable& heroes = MpHeroes();
    auto count = static_cast<std::uint32_t>(static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize);
    if (static_cast<std::uint32_t>(heroIndex_) >= count) heroIndex_ = static_cast<int>(count) - 1;
    int selected = heroIndex_;
    // Ten rows are visible.
    if (heroTop_ + 10 <= selected) heroTop_ = selected - 9;
    if (heroTop_ > selected) heroTop_ = selected;
    SetScrollPos(heroScroll_, heroTop_);
    TakeScrollPos(heroScroll_, nullptr, &heroTop_);
    CallShowHero(heroIndex_);
}
EI_REPLACE_METHOD(0x00618FF0, CMpHeroScreen::ClampHeroSelection);

void CMpHeroScreen::ClampVoiceSelection() {
    if (voiceIndex_ < 0) voiceIndex_ = 0;
    int count = static_cast<int>(voices_[1] - voices_[0]) >> 3;
    if (static_cast<std::uint32_t>(voiceIndex_) >= static_cast<std::uint32_t>(count)) voiceIndex_ = count - 1;
    int selected = voiceIndex_;
    // Eleven rows are visible.
    if (voiceTop_ + 11 <= selected) voiceTop_ = selected - 10;
    if (voiceTop_ > selected) voiceTop_ = selected;
    SetScrollPos(voiceScroll_, voiceTop_);
    TakeScrollPos(voiceScroll_, nullptr, &voiceTop_);
    // Each voice is {file, name}; the hero keeps the file name at +0x18.
    const auto& file = *reinterpret_cast<const mfc::CString*>(voices_[0] + voiceIndex_ * 8);
    *reinterpret_cast<mfc::CString*>(Hero() + 0x18) = file;
    CallDrawVoices();
}
EI_REPLACE_METHOD(0x00619090, CMpHeroScreen::ClampVoiceSelection);

double CMpHeroScreen::EquipmentWeight() {
    char* hero = Hero();
    double weight = 0.0;
    if (weapon_) weight = ItemWeight(weapon_);
    if (armor_) weight += ItemWeight(armor_);
    if (spellContainer_) weight += ItemWeight(spellContainer_);
    int count = *reinterpret_cast<int*>(hero + 0x754);
    char** quick = *reinterpret_cast<char***>(hero + 0x750);
    for (int i = 0; i < count; ++i)
        if (quick[i]) weight += ItemWeight(quick[i]);
    return weight;
}
EI_REPLACE_METHOD(0x006193C0, CMpHeroScreen::EquipmentWeight);

void CMpHeroScreen::OnItemChosen(char* item, int mode) {
    Hero();   // called for nothing, as in the original
    if (mode == 0) {
        bool equipped = true;
        switch (ItemType(item)) {
        case kTypeWeapon:
            weapon_ = item;
            ShowWeapon(weaponPanel_, nullptr, item);
            break;
        case kTypeArmor:
            armor_ = item;
            ShowArmour(weaponPanel_, nullptr, item);
            break;
        case kTypeSpellContainer:
            spellContainer_ = item;
            ShowContainer(spellPanel_, nullptr, item);
            break;
        default:
            equipped = false;
            break;
        }
        if (equipped) uiorig::PlayUiSound(window.interface_, kSoundMessageOk, 0);
    }
    Refresh();
}
EI_REPLACE_METHOD(0x00618980, CMpHeroScreen::OnItemChosen);

bool CMpHeroScreen::CanEquip(char* item, int) {
    if (ItemType(item) != kTypeSpellContainer) return true;
    char* spell = *reinterpret_cast<char**>(item + 0x44);
    if (!spell) return true;
    char* hero = Hero();
    return HeroCanUseSpell(hero + 0x20, spell);
}
EI_REPLACE_METHOD(0x006188E0, CMpHeroScreen::CanEquip);

char* CMpHeroScreen::GetHero() { return *reinterpret_cast<char**>(LocalPlayerData() + 0x30); }
EI_REPLACE_METHOD(0x00616900, CMpHeroScreen::GetHero);

void CMpHeroScreen::OnLButtonUp(int flags, int x, int y) {
    InputOf(this)->BaseLButtonUp(flags, x, y);
    // Releasing a rotate arrow.
    void*& capture = UiCapture(window.interface_);
    if (capture == CaptureValue(InputOf(this))) capture = nullptr;
    rotating_ = 0;
}

void __fastcall MpHeroLButtonUp(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CMpHeroScreen>(input)->OnLButtonUp(flags, x, y);
}
EI_REPLACE(0x006177D0, MpHeroLButtonUp);

// --- CEscMenu ---------------------------------------------------------------------------------

void CEscMenu::OnMouseMove(int flags, int x, int y) {
    CUiModel* model = window.models_.data[0];
    int region = window.RegionAt(x, y);
    // Hovering a stone plays its animation (the first two only in single player).
    switch (region) {
    case 0:
        if (hovered_ != 0 && !IsMultiplayerClient()) {
            uiorig::PlayUiSound(window.interface_, kSoundMenuStone, 0);
            model->Play(2);
        }
        hovered_ = 0;
        break;
    case 1:
        if (hovered_ != 1 && !IsMultiplayerClient()) {
            uiorig::PlayUiSound(window.interface_, kSoundMenuStone, 0);
            model->Play(3);
        }
        hovered_ = 1;
        break;
    case 2:
    case 3:
    case 4: {
        static const int kAnimation[5] = {0, 0, 5, 6, 4};
        if (hovered_ != region) {
            uiorig::PlayUiSound(window.interface_, kSoundMenuStone, 0);
            model->Play(kAnimation[region]);
        }
        hovered_ = region;
        break;
    }
    default:
        if (hovered_ != -1) model->Stop();
        hovered_ = -1;
        break;
    }
    InputOf(this)->BaseMouseMove(flags, x, y);
}

void CEscMenu::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        uiorig::PlayUiSound(window.interface_, kSoundMenuOk, 0);
        window.interface_->SetPendingValue(0);
        UiHandled(this) = true;
    }
}

void CEscMenu::OnMessageResult(int result) {
    // 0x10001: the quit box's yes; 0x100001/0x100002/0x110001 close the menu.
    if (static_cast<std::uint32_t>(result) > 0x100002) {
        if (result == 0x110001) window.interface_->SetPendingValue(0);
    } else if (static_cast<std::uint32_t>(result) >= 0x100001) {
        window.interface_->SetPendingValue(0);
    } else if (result == 0x10001) {
        uiorig::LeaveGame();
    }
}
EI_REPLACE_METHOD(0x0062ACC0, CEscMenu::OnMessageResult);

void __fastcall EscMenuMouseMove(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CEscMenu>(input)->OnMouseMove(flags, x, y);
}
void __fastcall EscMenuKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CEscMenu>(input)->OnKeyDown(key, repeat, flags);
}
EI_REPLACE(0x0062AD10, EscMenuMouseMove);
EI_REPLACE(0x0062B300, EscMenuKeyDown);

// --- CCreditsScreen ---------------------------------------------------------------------------

void CCreditsScreen::Close() {
    if (fromGame_) window.interface_->SetPendingValue(0xCCCCCCCC);
    else uiorig::LeaveGame();
}

void CCreditsScreen::OnLButtonDown(int, int, int) { Close(); }

void CCreditsScreen::OnKeyDown(int key, int, int) {
    if (key == VK_RETURN || key == VK_ESCAPE || key == VK_SPACE) Close();
}

void __fastcall CreditsLButtonDown(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CCreditsScreen>(input)->OnLButtonDown(flags, x, y);
}
void __fastcall CreditsKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CCreditsScreen>(input)->OnKeyDown(key, repeat, flags);
}
EI_REPLACE(0x0062BFE0, CreditsLButtonDown);
EI_REPLACE(0x0062C010, CreditsKeyDown);

// --- CExchangeScreen --------------------------------------------------------------------------

void CExchangeScreen::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (UiHandled(this)) return;
    switch (key) {
    case VK_BACK:
        ChatKeyBackspaceOriginal(interface_);
        UiHandled(this) = true;
        break;
    case VK_RETURN:
        ChatKeyEnterOriginal(interface_);
        UiHandled(this) = true;
        break;
    case VK_ESCAPE:
        ClientDropPartyOriginal(MultiplayerClient());
        UiHandled(this) = true;
        break;
    }
}

void __fastcall ExchangeKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CExchangeScreen>(input)->OnKeyDown(key, repeat, flags);
}
EI_REPLACE(0x0062D090, ExchangeKeyDown);
