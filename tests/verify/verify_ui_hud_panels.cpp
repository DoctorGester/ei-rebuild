// Checks for the HUD panels' state displays (ui_hud_panels.cpp). Scenario: ui_hud (the unit panel
// shows the hero every frame; the top panel's page buttons and sling; the movement panel's inner
// button), and every scenario with a battle screen.
//
// The unit panel's ShowHealth / ShowParts and the top panel's ShowPage / Layout run side by side:
// the fields they write are spoiled (set to values they never write, so a rewrite that writes
// nothing shows even when a frame repeats the last one's values), the original runs, its result
// is recorded, the fields are spoiled again, ours runs and the results are compared. Their text
// draws and the log's text run twice (the same text at the same place). ToggleFlag gives orders:
// ours only, the units' flags checked.
#include "verify.h"

#include "../../src/game/core_transform.h"
#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/world.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

CUiMesh* Sprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }

// The fields a bar display writes on sprite `index`: visibility, colour row, the quad's right
// edge (and the two values the original copies onto themselves) and four texture coordinates.
struct BarState {
    int visible;
    float vOffset;
    float quad[4];   // x[2], x[3], y[2], y[3]
    float uv[4];
};

BarState ReadBar(CUiMesh* m) {
    BarState s{};
    s.visible = m->visible_;
    s.vOffset = m->vOffset_;
    if (m->quads_) {
        s.quad[0] = m->quads_[0].x[2];
        s.quad[1] = m->quads_[0].x[3];
        s.quad[2] = m->quads_[0].y[2];
        s.quad[3] = m->quads_[0].y[3];
    }
    if (m->uvs_ && m->uvCount_ >= 4) std::memcpy(s.uv, m->uvs_ + 4, sizeof(s.uv));
    return s;
}

void WriteBar(CUiMesh* m, const BarState& s) {
    m->visible_ = s.visible;
    m->vOffset_ = s.vOffset;
    if (m->quads_) {
        m->quads_[0].x[2] = s.quad[0];
        m->quads_[0].x[3] = s.quad[1];
        m->quads_[0].y[2] = s.quad[2];
        m->quads_[0].y[3] = s.quad[3];
    }
    if (m->uvs_ && m->uvCount_ >= 4) std::memcpy(m->uvs_ + 4, s.uv, sizeof(s.uv));
}

// A recognisable spoiled state: what a bar display may write is set to values it never writes
// (the two values the original copies onto themselves are left alone).
constexpr int kSpoiledVisible = 7;
constexpr float kSpoiled = -9.0f;

BarState Spoiled(const BarState& s) {
    BarState x = s;
    x.visible = kSpoiledVisible;
    x.vOffset = kSpoiled;
    x.quad[0] = x.quad[1] = kSpoiled;
    for (float& v : x.uv) v = kSpoiled;
    return x;
}

// `after` with every field still spoiled (not written) put back to its value in `before`.
BarState Merged(const BarState& after, const BarState& before) {
    BarState x = after;
    if (x.visible == kSpoiledVisible) x.visible = before.visible;
    if (x.vOffset == kSpoiled) x.vOffset = before.vOffset;
    for (int k = 0; k < 2; ++k)
        if (x.quad[k] == kSpoiled) x.quad[k] = before.quad[k];
    for (int k = 0; k < 4; ++k)
        if (x.uv[k] == kSpoiled) x.uv[k] = before.uv[k];
    return x;
}

std::vector<BarState> ReadBars(CUiWindow* w, int first, int count) {
    std::vector<BarState> v;
    for (int i = 0; i < count; ++i) v.push_back(ReadBar(Sprite(w, first + i)));
    return v;
}

void SpoilBars(CUiWindow* w, int first, const std::vector<BarState>& before) {
    for (std::size_t i = 0; i < before.size(); ++i) WriteBar(Sprite(w, first + static_cast<int>(i)), Spoiled(before[i]));
}

void MergeBars(CUiWindow* w, int first, const std::vector<BarState>& before) {
    for (std::size_t i = 0; i < before.size(); ++i) {
        CUiMesh* m = Sprite(w, first + static_cast<int>(i));
        WriteBar(m, Merged(ReadBar(m), before[i]));
    }
}

// The first sprite whose state differs (floats within a few ULPs), or -1.
int FirstDifferentBar(const std::vector<BarState>& a, const std::vector<BarState>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const BarState& x = a[i];
        const BarState& y = b[i];
        bool same = x.visible == y.visible && ei::verify::NearlyEqual(x.vOffset, y.vOffset, 4, 1e-6f);
        for (int k = 0; k < 4 && same; ++k)
            same = ei::verify::NearlyEqual(x.quad[k], y.quad[k], 4, 1e-6f) && ei::verify::NearlyEqual(x.uv[k], y.uv[k], 4, 1e-6f);
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

// Both versions run from the same spoiled bars (so one that writes nothing shows even when a
// frame repeats the last one's values); what neither wrote is put back afterwards.
template <typename Original, typename Ours>
int CompareBarRuns(CHudUnitPanel* self, int first, int count, Original original, Ours ours, std::vector<BarState>* expected,
                   std::vector<BarState>* actual) {
    std::vector<BarState> before = ReadBars(self, first, count);
    SpoilBars(self, first, before);
    original();
    *expected = ReadBars(self, first, count);
    SpoilBars(self, first, before);
    ours();
    *actual = ReadBars(self, first, count);
    MergeBars(self, first, before);
    return FirstDifferentBar(*expected, *actual);
}

// 006477E0: MOV EAX,FS:[0] (6 bytes). Sprites 10..13.
void(__fastcall* s_showHealth)(CHudUnitPanel*, void*, CUnit*);
void __fastcall ShowHealthVerify(CHudUnitPanel* self, void*, CUnit* unit) {
    std::vector<BarState> expected, actual;
    int i = CompareBarRuns(
        self, 10, 4, [&] { s_showHealth(self, nullptr, unit); }, [&] { self->ShowHealth(unit); }, &expected, &actual);
    ei::verify::Check("CHudUnitPanel::ShowHealth", -1, i, "hp %d/%d mana %u/%u: sprite %d visible %d/%d edge %g/%g u %g/%g",
                      unit->packedVitals.hitPoints, unit->packedVitals.maxHitPoints, unit->packedVitals.mana, unit->packedVitals.maxMana, i < 0 ? -1 : 10 + i,
                      i < 0 ? 0 : expected[i].visible, i < 0 ? 0 : actual[i].visible, i < 0 ? 0.0 : expected[i].quad[0],
                      i < 0 ? 0.0 : actual[i].quad[0], i < 0 ? 0.0 : expected[i].uv[0], i < 0 ? 0.0 : actual[i].uv[0]);
}

// 00647B70: PUSH -1; PUSH imm32 (7 bytes). Sprites 14..25.
void(__fastcall* s_showParts)(CHudUnitPanel*, void*, CUnit*);
void __fastcall ShowPartsVerify(CHudUnitPanel* self, void*, CUnit* unit) {
    std::vector<BarState> expected, actual;
    int i = CompareBarRuns(
        self, 14, 12, [&] { s_showParts(self, nullptr, unit); }, [&] { self->ShowParts(unit); }, &expected, &actual);
    const UnitBodyPart* part = i >= 0 ? &unit->bodyParts[i / 2] : nullptr;
    ei::verify::Check("CHudUnitPanel::ShowParts", -1, i, "sprite %d (part state %d, %d/%u): visible %d/%d edge %g/%g row %g/%g",
                      i < 0 ? -1 : 14 + i, part ? part->state : 0, part ? part->durability : 0,
                      part ? part->maxDurability : 0u, i < 0 ? 0 : expected[i].visible, i < 0 ? 0 : actual[i].visible,
                      i < 0 ? 0.0 : expected[i].quad[0], i < 0 ? 0.0 : actual[i].quad[0],
                      i < 0 ? 0.0 : expected[i].vOffset, i < 0 ? 0.0 : actual[i].vOffset);
}

// 00646B90 (A-H25): PUSH -1; PUSH imm32 (7 bytes). The effects list: the "more" mark (sprite 9),
// the six icons (sprites 26..31: visibility and frame) and the kept pictures. Side by side like
// the bars: the sprite fields are spoiled, the original runs, its result is recorded, the kept
// pictures are put back and the fields spoiled again, ours runs; then the fields neither wrote
// get their old values back. The texts are drawn and the pictures copied twice (the same ones at
// the same places). Reached when the shown unit has an effect on it.
struct EffectsState {
    int more;
    int kept[6];
    int visible[6];
    float u[6];
    float v[6];
};
const int kSpoiledInt = -77;
const float kSpoiledFloat = -77.0f;

EffectsState TakeEffects(CHudUnitPanel* self) {
    EffectsState state;
    state.more = Sprite(self, 9)->visible_;
    for (int i = 0; i < 6; ++i) {
        state.kept[i] = self->effectIcons_[i];
        state.visible[i] = Sprite(self, 26 + i)->visible_;
        state.u[i] = Sprite(self, 26 + i)->uOffset_;
        state.v[i] = Sprite(self, 26 + i)->vOffset_;
    }
    return state;
}

void SpoilEffects(CHudUnitPanel* self, const EffectsState& before) {
    Sprite(self, 9)->visible_ = kSpoiledInt;
    for (int i = 0; i < 6; ++i) {
        self->effectIcons_[i] = before.kept[i];
        Sprite(self, 26 + i)->visible_ = kSpoiledInt;
        Sprite(self, 26 + i)->uOffset_ = kSpoiledFloat;
        Sprite(self, 26 + i)->vOffset_ = kSpoiledFloat;
    }
}

void(__fastcall* s_showEffects)(CHudUnitPanel*, void*, CUnit*);
void __fastcall ShowEffectsVerify(CHudUnitPanel* self, void*, CUnit* unit) {
    EffectsState before = TakeEffects(self);
    SpoilEffects(self, before);
    s_showEffects(self, nullptr, unit);
    EffectsState expected = TakeEffects(self);
    SpoilEffects(self, before);
    self->ShowEffects(unit);
    EffectsState actual = TakeEffects(self);
    bool same = std::memcmp(&expected, &actual, sizeof(expected)) == 0;
    // The fields left alone get their values back.
    if (actual.more == kSpoiledInt) Sprite(self, 9)->visible_ = before.more;
    for (int i = 0; i < 6; ++i) {
        CUiMesh* icon = Sprite(self, 26 + i);
        if (actual.visible[i] == kSpoiledInt) icon->visible_ = before.visible[i];
        if (actual.u[i] == kSpoiledFloat) icon->uOffset_ = before.u[i];
        if (actual.v[i] == kSpoiledFloat) icon->vOffset_ = before.v[i];
    }
    int first = -1;
    for (int i = 5; i >= 0; --i)
        if (expected.kept[i] != actual.kept[i] || expected.visible[i] != actual.visible[i] ||
            expected.u[i] != actual.u[i] || expected.v[i] != actual.v[i])
            first = i;
    ei::verify::Check("CHudUnitPanel::ShowEffects", true, same,
                      "%d effects, more %d/%d, icon %d: kept %d/%d visible %d/%d u %g/%g v %g/%g", unit->effectCount,
                      expected.more, actual.more, first, first < 0 ? 0 : expected.kept[first],
                      first < 0 ? 0 : actual.kept[first], first < 0 ? 0 : expected.visible[first],
                      first < 0 ? 0 : actual.visible[first], first < 0 ? 0.0 : expected.u[first],
                      first < 0 ? 0.0 : actual.u[first], first < 0 ? 0.0 : expected.v[first],
                      first < 0 ? 0.0 : actual.v[first]);
}

// 00649810 (stolen 0): ours only. Afterwards every selected party unit has the panel's flag (0 or
// 1), and the button shows it.
void __fastcall ToggleFlagVerify(CHudMovePanel* self, void*) {
    self->ToggleFlag();
    bool ok = self->flag_ == 0 || self->flag_ == 1;
    for (int i = 0; i < self->owner_->partyCount_; ++i) {
        CUnit* unit = self->owner_->party_[i];
        if ((unit->flags_ & 0x10) && unit->unknown_438 != self->flag_) ok = false;
    }
    float frame = Sprite(self, 5)->uOffset_;
    ok = ok && frame == (self->flag_ == 0 ? 0.3359375f : 0.0f);
    ei::verify::Check("CHudMovePanel::ToggleFlag", true, ok, "flag %d frame %g", self->flag_, frame);
}

// The top panel's page state: the two buttons' tints and the message log's position.
struct PageState {
    float tints[2][3];
    int maxPosition;
    int position;
    bool moved;
};

CUiScrollText* Log(CHudTopPanel* p) { return static_cast<CUiScrollText*>(p->owner_->children_.data[9]); }

PageState ReadPage(CHudTopPanel* p) {
    PageState s{};
    for (int i = 0; i < 2; ++i) std::memcpy(s.tints[i], Sprite(p, 3 + i)->tint_, sizeof(s.tints[i]));
    CUiScrollText* log = Log(p);
    s.maxPosition = log->maxPosition_;
    s.position = log->position_;
    s.moved = log->moved_;
    return s;
}

// 0064A320: SUB ESP,0x18; MOV EAX,0xC (8 bytes). The tints are spoiled before ours runs; the
// log's text is set again by ours.
void(__fastcall* s_showPage)(CHudTopPanel*, void*);
void __fastcall ShowPageVerify(CHudTopPanel* self, void*) {
    PageState before = ReadPage(self);
    s_showPage(self, nullptr);
    PageState expected = ReadPage(self);
    for (int i = 0; i < 2; ++i)
        for (float& t : Sprite(self, 3 + i)->tint_) t = -9.0f;
    Log(self)->moved_ = before.moved;
    self->ShowPage();
    PageState actual = ReadPage(self);
    bool same = std::memcmp(expected.tints, actual.tints, sizeof(expected.tints)) == 0 &&
                expected.maxPosition == actual.maxPosition && expected.position == actual.position &&
                expected.moved == actual.moved;
    ei::verify::Check("CHudTopPanel::ShowPage", true, same, "page %d: tints %g,%g/%g,%g position %d/%d of %d/%d moved %d/%d",
                      self->page_, expected.tints[0][0], expected.tints[1][0], actual.tints[0][0], actual.tints[1][0],
                      expected.position, actual.position, expected.maxPosition, actual.maxPosition, expected.moved,
                      actual.moved);
}

// The top panel's layout: the placed sprites' positions, the log's rectangle, the regions' offsets.
struct LayoutState {
    Vector3 positions[5];
    RECT logRect;
    std::vector<int> offsets;
};
constexpr int kPlaced[5] = {0, 1, 3, 4, 5};

CTransformObject* Placed(CHudTopPanel* p, int i) {
    return static_cast<CTransformObject*>(p->sceneObjects_.data[kPlaced[i]]);
}

LayoutState ReadLayout(CHudTopPanel* p) {
    LayoutState s{};
    for (int i = 0; i < 5; ++i) s.positions[i] = Placed(p, i)->localPosition_;
    s.logRect = Log(p)->rect_;
    for (int i = 1; i < p->regions_.size; ++i) {
        s.offsets.push_back(p->regions_.data[i]->offsetX_);
        s.offsets.push_back(p->regions_.data[i]->offsetY_);
    }
    return s;
}

// 00649E20: SUB ESP,0x10; PUSH ESI; MOV ESI,ECX (6 bytes). Positions and offsets are spoiled
// before ours runs (the log's rectangle is set by both).
void(__fastcall* s_layout)(CHudTopPanel*, void*);
void __fastcall LayoutVerify(CHudTopPanel* self, void*) {
    s_layout(self, nullptr);
    LayoutState expected = ReadLayout(self);
    for (int i = 0; i < 5; ++i) Placed(self, i)->localPosition_ = Vector3{-9.0f, -9.0f, -9.0f};
    for (int i = 1; i < self->regions_.size; ++i) self->regions_.data[i]->offsetX_ = self->regions_.data[i]->offsetY_ = 12345;
    self->Layout();
    LayoutState actual = ReadLayout(self);
    int bad = -1;
    for (int i = 0; i < 5 && bad < 0; ++i) {
        const float* e = &expected.positions[i].x;
        const float* a = &actual.positions[i].x;
        for (int k = 0; k < 3; ++k)
            if (!ei::verify::NearlyEqual(e[k], a[k], 4, 1e-6f)) bad = i;
    }
    bool same = bad < 0 && std::memcmp(&expected.logRect, &actual.logRect, sizeof(RECT)) == 0 &&
                expected.offsets == actual.offsets;
    ei::verify::Check("CHudTopPanel::Layout", true, same, "offset %d: sprite %d, log rect %ld/%ld", self->offset_,
                      bad < 0 ? -1 : kPlaced[bad], expected.logRect.top, actual.logRect.top);
}
} // namespace

// 0064A7A0 (stolen 0; it makes and releases item copies): ours only. Page 1's text has one line
// for the money when above 0 and one per kind of the player's items (slot 14), and the log shows
// it when page 1 is up.
static void __fastcall ShowItemsVerify(CHudTopPanel* self) {
    WorldPlayer* player = reinterpret_cast<LocalViewData*>(WorldClient()->LocalView())->player;
    std::vector<CItem*> kinds;
    for (auto* node = player->items.head; node; node = node->next) {
        auto* item = static_cast<CItem*>(node->data);
        bool seen = false;
        for (CItem* kind : kinds) seen = seen || kind->IsSameAs(item);
        if (!seen) kinds.push_back(item);
    }
    int money = static_cast<int>(reinterpret_cast<CParty*>(LocalPlayerData())->unknown_50);
    self->ShowItems();
    const char* text = self->pageTexts_[1];
    int lines = 0;
    for (const char* c = text; *c; ++c) lines += *c == 10;
    int expected = static_cast<int>(kinds.size()) + (money > 0 ? 1 : 0);
    ei::verify::Check("CHudTopPanel::ShowItems", expected, lines, "%d kinds, money %d", static_cast<int>(kinds.size()), money);
}

EI_VERIFY(0x006477E0, ShowHealthVerify, 6, s_showHealth);
EI_VERIFY(0x00647B70, ShowPartsVerify, 7, s_showParts);
EI_VERIFY(0x00646B90, ShowEffectsVerify, 7, s_showEffects);
EI_VERIFY(0x00649810, ToggleFlagVerify, 0, s_unused);
EI_VERIFY(0x0064A320, ShowPageVerify, 8, s_showPage);
EI_VERIFY(0x00649E20, LayoutVerify, 6, s_layout);
EI_VERIFY(0x0064A7A0, ShowItemsVerify, 0, s_unused);

// 006481D0 (A-H25; the keys that pick a body part in a game): it only tints four buttons. The
// original, its tints kept and the buttons' tints overwritten, then ours; compared.
// 006481D0: SUB ESP,0x18; XOR EDX,EDX (5 bytes).
static void(__fastcall* s_showBodyPart)(CHudUnitPanel*);
static void __fastcall ShowBodyPartVerify(CHudUnitPanel* self) {
    s_showBodyPart(self);
    float expected[4][3];
    for (int i = 0; i < 4; ++i) {
        CUiMesh* button = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[3 + i]);
        std::memcpy(expected[i], button->tint_, sizeof(expected[i]));
        button->tint_[0] = button->tint_[1] = button->tint_[2] = -1.0f;
    }
    self->ShowBodyPart();
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        CUiMesh* button = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[3 + i]);
        ok = ok && std::memcmp(expected[i], button->tint_, sizeof(expected[i])) == 0;
    }
    ei::verify::Check("CHudUnitPanel::ShowBodyPart", true, ok, "part %d", self->bodyPart_);
}
EI_VERIFY(0x006481D0, ShowBodyPartVerify, 5, s_showBodyPart);
