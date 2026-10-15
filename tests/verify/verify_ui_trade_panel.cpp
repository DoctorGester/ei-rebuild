// Checks for the camp's trade panel (ui_trade_panel.cpp).
//
// BuildLists (00625880) runs side by side: the original builds the lists, they are copied (texts,
// prices, levels, the learnable flag), ours clears and rebuilds them, and both must agree. Running
// it twice leaves the same state (the statistics are reset and refreshed from the record both
// times; the lists are rebuilt from scratch).
// DrawLists (006261A0) runs side by side on the surface's pixels and the price sprites: the
// original draws, its pixels and the sprites' quads and texture coordinates are recorded and the
// old pixels put back, ours draws (it drops the original's sprites first) and both must agree. On a
// pixel difference the original's draw is settled first (pixel_settle.h, outside disturbance).
#include "verify.h"
#include "pixel_settle.h"

#include "../../src/game/party.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"
#include "../../src/hook/log.h"
#include "../../src/lib/gameapi.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---- Surface pixels (as verify_ui_item_info.cpp) -------------------------------------------------

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

// ---- Sprites ----------------------------------------------------------------------------------

// The quads and texture coordinates of every scene object from `first` on (interface sprites).
std::vector<std::uint8_t> SpriteBytes(CUiWindow* window, int first) {
    std::vector<std::uint8_t> out;
    for (int i = first; i < window->sceneObjects_.size; ++i) {
        auto* mesh = reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[i]);
        int counts[2] = {mesh ? mesh->quadCount_ : -1, mesh ? mesh->uvCount_ : -1};
        out.insert(out.end(), reinterpret_cast<std::uint8_t*>(counts), reinterpret_cast<std::uint8_t*>(counts + 2));
        if (!mesh) continue;
        for (int q = 0; q < mesh->quadCount_; ++q) {
            auto* quad = reinterpret_cast<const std::uint8_t*>(&mesh->quads_[q]);
            out.insert(out.end(), quad, quad + 0x20);   // four x then four y
        }
        auto* uv = reinterpret_cast<const std::uint8_t*>(mesh->uvs_);
        if (uv) out.insert(out.end(), uv, uv + mesh->uvCount_ * 8);
    }
    return out;
}

// ---- Rows ---------------------------------------------------------------------------------------

struct RowCopy {
    std::string name, key, nextKey;
    int price = 0;
    int level = 0;
    bool learnable = false;
};

std::vector<RowCopy> CopyRows(const TradeRows& rows, bool withFlag) {
    std::vector<RowCopy> out;
    for (const TradeRow* row = rows.begin; row != rows.end; ++row) {
        RowCopy c;
        c.name = row->name;
        c.key = row->key;
        c.nextKey = row->nextKey;
        c.price = row->price;
        c.level = row->level;
        c.learnable = withFlag && row->learnable;   // not written on the other lists
        out.push_back(c);
    }
    return out;
}

// -1 when equal, else the first different row (or the shorter count).
int FirstDifferentRow(const std::vector<RowCopy>& a, const std::vector<RowCopy>& b) {
    std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        const RowCopy& x = a[i];
        const RowCopy& y = b[i];
        if (x.name != y.name || x.key != y.key || x.nextKey != y.nextKey || x.price != y.price ||
            x.level != y.level || x.learnable != y.learnable)
            return static_cast<int>(i);
    }
    return a.size() == b.size() ? -1 : static_cast<int>(n);
}

struct PanelCopy {
    std::vector<RowCopy> lists[4];
    int experience;
    int perkTop;
    int learnableTop;
};

PanelCopy CopyPanel(const CTradePanel* panel) {
    PanelCopy c;
    c.lists[0] = CopyRows(panel->learnable_, true);
    c.lists[1] = CopyRows(panel->perks_, false);
    c.lists[2] = CopyRows(panel->schools_, false);
    c.lists[3] = CopyRows(panel->science_, false);
    c.experience = panel->experience_;
    c.perkTop = panel->perkTop_;
    c.learnableTop = panel->learnableTop_;
    return c;
}

} // namespace

// 00625880 / 006261A0: PUSH -1; PUSH imm32 (7).
static void(__fastcall* s_buildLists)(CTradePanel*);
static void __fastcall TradeBuildListsVerify(CTradePanel* self) {
    s_buildLists(self);
    PanelCopy expected = CopyPanel(self);
    self->BuildLists();
    PanelCopy actual = CopyPanel(self);
    static const char* const kNames[4] = {"learnable", "perks", "schools", "science"};
    for (int i = 0; i < 4; ++i) {
        int row = FirstDifferentRow(expected.lists[i], actual.lists[i]);
        ei::verify::Check("CTradePanel::BuildLists", -1, row, "%s: row %d differs (counts %d/%d)", kNames[i], row,
                          static_cast<int>(expected.lists[i].size()), static_cast<int>(actual.lists[i].size()));
    }
    int e[3] = {expected.experience, expected.perkTop, expected.learnableTop};
    int a[3] = {actual.experience, actual.perkTop, actual.learnableTop};
    ei::verify::Check("CTradePanel::BuildLists", e[0], a[0], "experience %d/%d, tops %d/%d %d/%d", e[0], a[0], e[1],
                      a[1], e[2], a[2]);
    ei::verify::Check("CTradePanel::BuildLists", e[1] == a[1] && e[2] == a[2], true, "tops %d/%d %d/%d", e[1], a[1],
                      e[2], a[2]);
}
EI_VERIFY(0x00625880, TradeBuildListsVerify, 7, s_buildLists);

static void(__fastcall* s_drawLists)(CTradePanel*);
static void __fastcall TradeDrawListsVerify(CTradePanel* self) {
    ei::verify::PixelComparisonScope scope;
    void* surface = self->surfaces_.size > 0 ? self->surfaces_.data[0] : nullptr;
    Pixels before;
    bool pixels = AccessPixels(surface, before, false);
    s_drawLists(self);
    int expectedCount = self->sceneObjects_.size;
    std::vector<std::uint8_t> expectedSprites = SpriteBytes(self, 4);
    Pixels expected;
    if (pixels) pixels = AccessPixels(surface, expected, false) && AccessPixels(surface, before, true);
    self->DrawLists();
    int row = -1;
    bool unstable = false;
    if (pixels) {
        Pixels actual;
        if (AccessPixels(surface, actual, false)) row = FirstDifferentRow(expected, actual);
        if (row != -1) {
            // Outside disturbance (ticket X11, as X5 / X8): redraw the original from the saved pixels
            // until two redraws agree, then ours from the saved pixels against that settled draw. Both
            // drop the sprites past the panel's four before drawing, so the redraws do not pile them up.
            auto redraw = [&](Pixels* out) {
                return AccessPixels(surface, before, true) && (s_drawLists(self), true) &&
                       AccessPixels(surface, *out, false);
            };
            ei::verify::SettleOutcome settle = ei::verify::SettleOriginal(
                expected, redraw, [](const Pixels& a, const Pixels& b) { return FirstDifferentRow(a, b) == -1 ? 0 : 1; });
            Pixels again;
            bool ok = AccessPixels(surface, before, true) && (self->DrawLists(), true) && AccessPixels(surface, again, false);
            row = !ok ? -2 : settle.settled ? FirstDifferentRow(expected, again) : -1;
            unstable = ok && !settle.settled;
            ei::Log("verify CTradePanel::DrawLists: pixels differed; the original's redraws %s after %d tries; rerun row %d",
                    settle.settled ? "settled" : "did not settle", settle.tries, row);
        }
    }
    std::vector<std::uint8_t> actualSprites = SpriteBytes(self, 4);
    bool same = row == -1 && expectedCount == self->sceneObjects_.size && expectedSprites == actualSprites;
    ei::verify::Check("CTradePanel::DrawLists", true, same,
                      "pixel row %d (compared: %d%s), scene objects %d/%d, sprite bytes %d/%d%s", row,
                      pixels && !unstable, unstable ? ", original unstable" : "", expectedCount,
                      self->sceneObjects_.size, static_cast<int>(expectedSprites.size()),
                      static_cast<int>(actualSprites.size()), expectedSprites == actualSprites ? "" : " differ");
}
EI_VERIFY(0x006261A0, TradeDrawListsVerify, 7, s_drawLists);

// Build (00624F10, the panel's layout, once per camp / hero screen) runs ours only: it adds a
// surface, regions, sprites and titles. Checked afterwards: the rectangle, one more surface, 44
// more regions (the first the held perks' up arrow, the last the bottom held row), four more scene
// objects, three more titles, one statistics record per party member (+0x150 the last one's
// index) and +0x19C / +0x1A0. Reached when the camp opens (ui_camp, ui_describe) and from the
// hero screen's Create (ui_hero_screen).
static void* s_tradeBuild;
static void __fastcall TradeBuildVerify(CTradePanel* self) {
    int surfaces = self->surfaces_.size;
    int regions = self->regions_.size;
    int objects = self->sceneObjects_.size;
    auto titleCount = [self] { return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(self) + 0x1AC); };
    int titles = titleCount();
    self->Build();
    const char* name = "CTradePanel::Build";
    using DataFn = std::uint8_t*(__thiscall*)(void*);
    int party = *reinterpret_cast<int*>(reinterpret_cast<DataFn>(0x00578BA0)(reinterpret_cast<void*>(0x007AFEA5)) + 0x34);
    RECT expectedRect = {200, 100, 600, 500};
    ei::verify::Check(name, 0, std::memcmp(&expectedRect, &self->rect_, sizeof(RECT)), "rect (%d,%d,%d,%d)",
                      self->rect_.left, self->rect_.top, self->rect_.right, self->rect_.bottom);
    ei::verify::Check(name, surfaces + 1, self->surfaces_.size, "surfaces");
    ei::verify::Check(name, regions + 44, self->regions_.size, "regions");
    ei::verify::Check(name, objects + 4, self->sceneObjects_.size, "scene objects");
    ei::verify::Check(name, titles + 3, titleCount(), "titles");
    if (self->regions_.size == regions + 44) {
        RECT first = self->regions_.data[regions]->bounds_;
        RECT last = self->regions_.data[regions + 43]->bounds_;
        RECT expectedFirst = {360, 320, 380, 330};
        RECT expectedLast = {215, 465, 385, 480};
        ei::verify::Check(name, 0, std::memcmp(&first, &expectedFirst, sizeof(RECT)), "first region (%d,%d,%d,%d)",
                          first.left, first.top, first.right, first.bottom);
        ei::verify::Check(name, 0, std::memcmp(&last, &expectedLast, sizeof(RECT)), "last region (%d,%d,%d,%d)",
                          last.left, last.top, last.right, last.bottom);
    }
    int records = static_cast<int>(self->records_.end - self->records_.begin);
    ei::verify::Check(name, party, records, "records");
    if (party > 0) ei::verify::Check(name, party - 1, self->recordIndex_, "record index");
    ei::verify::Check(name, -1, self->heldRegion_, "+0x19C");
    ei::verify::Check(name, 0.0f, self->holdTime_, "+0x1A0");
}
EI_VERIFY(0x00624F10, TradeBuildVerify, 0, s_tradeBuild);

namespace {
void* s_unused;
} // namespace

// 00626F10, side by side: the original fills the record first, its bytes are kept and the record
// put back, then ours; the bytes it writes compared (all but +0x2B and +0x39..+0x3B). Reached by
// every trade panel Build (ui_camp, ui_describe, ui_hero_screen).
static void(__fastcall* s_snapshotRecord)(CTradePanel*);
static void __fastcall TradeSnapshotVerify(CTradePanel* self) {
    if (self->recordIndex_ < 0) {
        self->SnapshotRecord();
        return;
    }
    TradeRecord& record = self->records_.begin[self->recordIndex_];
    TradeRecord before = record;
    s_snapshotRecord(self);
    TradeRecord expected = record;
    record = before;
    self->SnapshotRecord();
    expected.padAfterPerks = record.padAfterPerks;
    std::memcpy(expected.padAfterBought, record.padAfterBought, sizeof(expected.padAfterBought));
    ei::verify::Check("CTradePanel::SnapshotRecord", 0, std::memcmp(&expected, &record, sizeof(record)), "record %d",
                      self->recordIndex_);
}
EI_VERIFY(0x00626F10, TradeSnapshotVerify, 7, s_snapshotRecord);

// 00626FA0, side by side: the hero's statistics (0x704 bytes) and the record are copied, the
// original restores, its result is kept and the copies put back, then ours; both compared dword by
// dword (a dword that differs must be within a few ULPs as a float: the slots' healed hit points).
// Reached by Settle with keep 0 on a bought record: ui_trade_restore (a school bought on the camp's
// page 1, then Esc).
static void(__fastcall* s_restoreRecord)(CTradePanel*);
static void __fastcall TradeRestoreVerify(CTradePanel* self) {
    if (self->recordIndex_ < 0) {
        s_restoreRecord(self);
        return;
    }
    constexpr int kStatsSize = sizeof(PartyUnitRecord::stats);
    std::uint8_t* stats = reinterpret_cast<CParty*>(LocalPlayerData())->members_.data[self->recordIndex_].stats;
    TradeRecord& record = self->records_.begin[self->recordIndex_];
    std::vector<std::uint8_t> before(stats, stats + kStatsSize);
    TradeRecord recordBefore = record;
    s_restoreRecord(self);
    std::vector<std::uint8_t> expected(stats, stats + kStatsSize);
    TradeRecord recordExpected = record;
    std::memcpy(stats, before.data(), kStatsSize);
    record = recordBefore;
    self->RestoreRecord();
    ei::verify::Check("CTradePanel::RestoreRecord", 0, std::memcmp(&recordExpected, &record, sizeof(record)), "record %d",
                      self->recordIndex_);
    for (int at = 0; at < kStatsSize; at += 4) {
        if (std::memcmp(&expected[at], stats + at, 4) == 0) continue;
        float a, b;
        std::memcpy(&a, &expected[at], 4);
        std::memcpy(&b, stats + at, 4);
        ei::verify::CheckNear("CTradePanel::RestoreRecord", a, b, "stats +0x%X", at);
    }
}
EI_VERIFY(0x00626FA0, TradeRestoreVerify, 10, s_restoreRecord);

// 00625430, ours only: one record per party member, recordIndex_ at the last. Not in a scenario
// (called by the hero screen's 006186F0 / 00617A50 and the lobby).
static void __fastcall TradeResetRecordsVerify(CTradePanel* self) {
    self->ResetRecords();
    int count = static_cast<int>(self->records_.end - self->records_.begin);
    int members = reinterpret_cast<CParty*>(LocalPlayerData())->members_.size;
    ei::verify::Check("CTradePanel::ResetRecords", true, count == members && (count == 0 || self->recordIndex_ == count - 1),
                      "records %d, members %d, index %d", count, members, self->recordIndex_);
}
EI_VERIFY(0x00625430, TradeResetRecordsVerify, 0, s_unused);

// 006255A0, ours only (it rewrites the hero's statistics): with `all` every record ends not bought
// and recordIndex_ is unchanged; otherwise the current record ends not bought. The hero screen's
// take-back and mode changes (ui_hero_screen), the camp's Close.
static void __fastcall TradeSettleVerify(CTradePanel* self, void*, int keep, int all) {
    int index = self->recordIndex_;
    self->Settle(keep, all);
    bool ok = true;
    if (static_cast<std::uint8_t>(all)) {
        ok = self->recordIndex_ == index;
        for (TradeRecord* r = self->records_.begin; r != self->records_.end; ++r) ok = ok && !r->bought;
    } else {
        ok = !self->records_.begin[index].bought;
    }
    ei::verify::Check("CTradePanel::Settle", true, ok, "keep %d all %d index %d", keep & 0xFF, all & 0xFF, index);
}
EI_VERIFY(0x006255A0, TradeSettleVerify, 0, s_unused);

// 00625860, ours only: the record index and both scroll positions as asked. The camp's hero arrows
// (ui_camp).
static void __fastcall TradeSelectHeroVerify(CTradePanel* self, void*, int index) {
    self->SelectHero(index);
    ei::verify::Check("CTradePanel::SelectHero", true, self->recordIndex_ == index,
                      "index %d -> %d", index, self->recordIndex_);
}
EI_VERIFY(0x00625860, TradeSelectHeroVerify, 0, s_unused);

// --- 00613CF0 / 0062A7C0: a row's destructors; ours only (the strings are gone afterwards, so only
// the deleting destructor's result is checked). ui_camp (page 1's lists rebuilt), ui_trade_restore.

static void __fastcall RowDestroyVerify(TradeRow* self, void*) {
    self->Destroy();
    ei::verify::Check("TradeRow::Destroy", true, true, "");
}
EI_VERIFY(0x00613CF0, RowDestroyVerify, 0, s_unused);

static TradeRow* __fastcall RowDeleteVerify(TradeRow* self, void*, int flags) {
    TradeRow* result = self->DeleteThis(flags);
    ei::verify::Check("TradeRow::DeleteThis", true, result == self, "flags %d", flags);
    return result;
}
EI_VERIFY(0x0062A7C0, RowDeleteVerify, 0, s_unused);

// --- 0062A310: a row's copy constructor (S16a: the trade lists' push_back). Ours first, then the
// original into a scratch row (it allocates: the strings' references), compared field by field, the
// scratch row destroyed. 0062A310: PUSH -1; PUSH handler (7 bytes).
namespace {
TradeRow*(__fastcall* s_rowCopy)(TradeRow*, void*, const TradeRow*);
} // namespace
static TradeRow* __fastcall RowCopyVerify(TradeRow* self, void*, const TradeRow* from) {
    TradeRow* result = self->CopyConstruct(from);
    alignas(TradeRow) unsigned char scratch[sizeof(TradeRow)] = {};
    TradeRow* twin = reinterpret_cast<TradeRow*>(scratch);
    TradeRow* expected = s_rowCopy(twin, nullptr, from);
    const bool ok = result == self && expected == twin && twin->name == self->name && twin->key == self->key &&
                    twin->nextKey == self->nextKey && twin->price == self->price && twin->level == self->level &&
                    twin->learnable == self->learnable;
    twin->Destroy();
    ei::verify::Check("TradeRow::CopyConstruct", true, ok, "price %d level %d", self->price, self->level);
    return result;
}
EI_VERIFY(0x0062A310, RowCopyVerify, 7, s_rowCopy);
