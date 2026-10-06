// Checks for the multiplayer hero screen's draw, update and close and the trade panel's experience
// figures (ui_hero_screen.cpp). Scenario ui_hero_screen (the hero list and a new hero; the
// statistics page, mode 11, and so DrawExperience and the trade panel's figures, are not reached).
#include "verify.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/party.h"
#include "../../src/game/world.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace {
void* s_unused;
} // namespace

// --- 00625530 / 00625560: pure, side by side ------------------------------------------------------
// Both start with MOV EAX,[ECX+0x150] (6 bytes).

static int(__fastcall* s_experience)(CTradePanel*);
static int __fastcall TradeExperienceVerify(CTradePanel* self) {
    int expected = s_experience(self);
    int actual = self->Experience();
    ei::verify::Check("CTradePanel::Experience", expected, actual, "record %d", self->recordIndex_);
    return expected;
}
EI_VERIFY(0x00625530, TradeExperienceVerify, 6, s_experience);

static int(__fastcall* s_purchaseCost)(CTradePanel*);
static int __fastcall TradePurchaseCostVerify(CTradePanel* self) {
    int expected = s_purchaseCost(self);
    int actual = self->PurchaseCost();
    ei::verify::Check("CTradePanel::PurchaseCost", expected, actual, "record %d", self->recordIndex_);
    return expected;
}
EI_VERIFY(0x00625560, TradePurchaseCostVerify, 6, s_purchaseCost);

// --- 006159E0: Draw (every frame of modes 9 and 10); ours only ---------------------------------------
// It draws and describes, so it runs once: returns 1 and leaves the mode and the selection alone.

static BOOL __fastcall HeroScreenDrawVerify(CMpHeroScreen* self, void*, int pass) {
    int mode = self->mode_;
    int hero = self->heroIndex_;
    BOOL result = self->Draw(pass);
    bool ok = result == 1 && self->mode_ == mode && self->heroIndex_ == hero;
    ei::verify::Check("CMpHeroScreen::Draw", true, ok, "mode %d -> %d result %d", mode, self->mode_, result);
    return result;
}
EI_VERIFY(0x006159E0, HeroScreenDrawVerify, 0, s_unused);

// --- 00617820: Update (every frame); ours only ---------------------------------------------------------
// The preview's turn is recomputed from the frame time (the same within a frame); on the new hero's
// page the create button's tint must follow the attributes and the name.

static void __fastcall HeroScreenUpdateVerify(CMpHeroScreen* self, void*) {
    int rotating = self->rotating_;
    float turn = self->turn_;
    double expectedTurn = turn;
    if (rotating == 1 || rotating == 2) {
        double step = WindowFrameTimeOriginal(&self->window);
        step += step;
        double period = *reinterpret_cast<const volatile float*>(0x007C7F10);
        expectedTurn = std::fmod(rotating == 1 ? step + turn : turn - step, period);
    }
    self->Update();
    ei::verify::CheckNear("CMpHeroScreen::Update turn", static_cast<float>(expectedTurn), self->turn_, "rotating %d",
                          rotating);
    if (self->mode_ == 10) {
        const auto* stats = reinterpret_cast<const UnitStats*>(reinterpret_cast<PartyUnitRecord*>(self->Hero())->stats);
        double points = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
        const char* name = static_cast<CUiEditField*>(self->nameEdit_)->text_;
        bool ready = points == *reinterpret_cast<const volatile float*>(0x00741C98) && name && name[0];
        auto* button = reinterpret_cast<CUiMesh*>(self->window.sceneObjects_.data[9]);
        ei::verify::Check("CMpHeroScreen::Update create button", ready ? 1.0f : 0.5f, button->tint_[0], "points %g",
                          points);
    }
}
EI_VERIFY(0x00617820, HeroScreenUpdateVerify, 0, s_unused);

// --- 00615850: Close; ours only ------------------------------------------------------------------------

static void __fastcall HeroScreenCloseVerify(CMpHeroScreen* self, void*) {
    self->Close();
    ei::verify::Check("CMpHeroScreen::Close", true, self->previewModel_ == nullptr, "preview %p",
                      static_cast<void*>(self->previewModel_));
}
EI_VERIFY(0x00615850, HeroScreenCloseVerify, 0, s_unused);

// --- 00615800 / 00634500 / 00634560 / 00633260: the hero screen's destruction; ours only ---------------
// Leaving the hero screen (Esc in ui_hero_screen) deletes it. The entry destructor leaves nothing to
// check; the caches' walk must visit exactly `count` nodes in key order before releasing; after the
// destructor body the caches are empty and the window's arrays freed.

static void __fastcall VoiceEntryDestroyVerify(MpVoiceEntry* self) { self->Destroy(); }
EI_VERIFY(0x00615800, VoiceEntryDestroyVerify, 0, s_unused);

static void __fastcall ReleaseCachedObjectsVerify(ItemObjectCache* cache) {
    std::uint32_t visited = 0;
    for (ItemObjectCache::Node* node = cache->header->left; node != cache->header && visited <= cache->count;
         node = node->Next())
        ++visited;
    ei::verify::Check("ReleaseCachedObjects", cache->count, visited, "walk");
    ReleaseCachedObjects(cache);
}
EI_VERIFY(0x00634500, ReleaseCachedObjectsVerify, 0, s_unused);
EI_VERIFY(0x00634560, ReleaseCachedObjectsVerify, 0, s_unused);

static void __fastcall HeroScreenDestroyVerify(CMpHeroScreen* self) {
    self->Destroy();
    auto* screen = reinterpret_cast<CItemScreen*>(self);
    ei::verify::Check("CMpHeroScreen::Destroy", true,
                      screen->models_.count == 0 && screen->textures_.count == 0 && !self->window.sceneObjects_.data,
                      "models %u textures %u", screen->models_.count, screen->textures_.count);
}
EI_VERIFY(0x00633260, HeroScreenDestroyVerify, 0, s_unused);

// --- 006167C0 / 00616560 / 00616910: the lists drawn and a hero shown; ours only ----------------------
// They draw text and send the hero's records, so they run once: the selection frame (scene object 2)
// is shown exactly when the selection is among the rows drawn; ShowHero keeps a valid index (else
// -1) and resets the turn. ui_hero_screen (the list, the new hero's voice rows and keys).

namespace {
bool FrameShown(CMpHeroScreen* self) {
    return reinterpret_cast<CUiMesh*>(self->window.sceneObjects_.data[2])->visible_ != 0;
}
int MpHeroTotal() {
    const RecordTable& heroes = MpHeroes();
    return static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize;
}
} // namespace

static void __fastcall HeroListDrawVerify(CMpHeroScreen* self) {
    self->DrawHeroList();
    int top = self->heroTop_, selected = self->heroIndex_;
    bool expected = selected >= top && selected < top + 10 && selected < MpHeroTotal() && top >= 0;
    ei::verify::Check("CMpHeroScreen::DrawHeroList", expected, FrameShown(self), "top %d selected %d", top, selected);
}
EI_VERIFY(0x006167C0, HeroListDrawVerify, 0, s_unused);

static void __fastcall VoicesDrawVerify(CMpHeroScreen* self) {
    self->DrawVoices();
    MpVoiceVector& voices = self->Voices();
    int count = static_cast<int>(voices.end - voices.begin);
    int top = self->voiceTop_, selected = self->voiceIndex_;
    bool expected = selected >= top && selected < top + 11 && selected < count && top >= 0;
    ei::verify::Check("CMpHeroScreen::DrawVoices", expected, FrameShown(self), "top %d selected %d", top, selected);
}
EI_VERIFY(0x00616560, VoicesDrawVerify, 0, s_unused);

static void __fastcall ShowHeroVerify(CMpHeroScreen* self, void*, int index) {
    self->ShowHero(index);
    int expected = index >= 0 && index < MpHeroTotal() ? index : -1;
    ei::verify::Check("CMpHeroScreen::ShowHero", true, self->heroIndex_ == expected && self->turn_ == 0.0f,
                      "index %d -> %d", index, self->heroIndex_);
}
EI_VERIFY(0x00616910, ShowHeroVerify, 0, s_unused);

// 00616E20, ours only (it hands records to the client world, draws a random number and restarts
// the figure's animation): the local player has a record, and the preview is the client unit with
// that record's id (the original's own lookup 00578980). Reached by ShowHero (ui_hero_screen).
static void __fastcall MakePreviewVerify(CMpHeroScreen* self, void*, int flag) {
    self->MakePreview(flag);
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    bool ok = player->members_.size > 0;
    void* unit = ok ? GameUnitByIdOriginal(reinterpret_cast<void*>(0x007AFEA5), nullptr, player->members_.data->unknown_01C)
                    : nullptr;
    ei::verify::Check("CMpHeroScreen::MakePreview", true, ok && unit == self->previewModel_, "members %d, preview %p, unit %p",
                      player->members_.size, static_cast<void*>(self->previewModel_), unit);
}
EI_VERIFY(0x00616E20, MakePreviewVerify, 0, s_unused);

// 00616AC0 (slot 52), ours only (it rebuilds the party): one record of the prototype (text 5) with
// the attributes and height the mode asks for (25 / 0.7 on the hero list, else the old record's),
// text_ the prototype, each figure strip's selection its index in that strip's list (or -1), the
// voice list of the strip that lists it from row 0, the equipment fields the record's first
// entries. ui_hero_screen (a new hero: the first default figure; the figure rows).
static void __fastcall ChooseFigureVerify(CMpHeroScreen* self, void*, const char* prototype) {
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    float kept[4] = {25.0f, 25.0f, 25.0f, 0.7f};
    if (player->members_.size > 0 && self->mode_ != 9) {
        const auto* stats = reinterpret_cast<const UnitStats*>(player->members_.data->stats);
        kept[0] = stats->stat10;
        kept[1] = stats->stat18;
        kept[2] = stats->stat20;
        kept[3] = player->members_.data->height;
    }
    std::string name = prototype;   // the caller's string may be text_ itself
    self->ChooseFigure(prototype);
    auto indexIn = [&](const char* const* begin, const char* const* end) {
        int i = 0;
        for (const char* const* e = begin; e != end; ++e, ++i)
            if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(*e), reinterpret_cast<const unsigned char*>(name.c_str())) == 0)
                return i;
        return -1;
    };
    int first = indexIn(reinterpret_cast<const char* const*>(self->defaults_), reinterpret_cast<const char* const*>(self->defaultsEnd_));
    int second = indexIn(self->names_.begin, self->names_.end);
    PartyUnitRecord* hero = player->members_.data;
    const auto* stats = reinterpret_cast<const UnitStats*>(hero->stats);
    bool ok = player->members_.size == 1 && name == static_cast<const char*>(hero->texts[5]) && name == self->text_ &&
              self->figureStrips_[0]->selected_ == first && self->figureStrips_[1]->selected_ == second &&
              self->voices_ == reinterpret_cast<char**>(&self->voiceLists_[second >= 0 ? 1 : 0]) && self->voiceIndex_ == 0 &&
              self->voiceTop_ == 0 && stats->stat10 == kept[0] && stats->stat10Base == kept[0] && stats->stat18 == kept[1] &&
              stats->stat20Base == kept[2] && hero->height == kept[3] &&
              self->weapon_ == (hero->weaponCount ? reinterpret_cast<char*>(hero->weapons[0]) : nullptr) &&
              self->spellContainer_ == (hero->spellCount ? reinterpret_cast<char*>(hero->spells[0]) : nullptr);
    ei::verify::Check("CMpHeroScreen::ChooseFigure", true, ok, "%s: members %d, strips %d / %d (expected %d / %d), mode %d",
                      name.c_str(), player->members_.size, self->figureStrips_[0]->selected_, self->figureStrips_[1]->selected_,
                      first, second, self->mode_);
}
EI_VERIFY(0x00616AC0, ChooseFigureVerify, 0, s_unused);
