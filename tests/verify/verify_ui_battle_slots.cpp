// Checks for the battle screen's main-vtable slots (src/game/ui_battle_slots.cpp).
//
// SaveState runs side by side: the save stream's Write (slot 2) is pointed at a recorder while the
// original and ours run, the two byte sequences are compared and the original's bytes are then
// written to the real stream. The other slots save, open help or change the camera, so only ours
// runs and the wrapper checks the state it leaves. Scenarios: every new game reaches PollShown and
// SaveState (the "savea" autosave) and OnUnitJoined (the hero joins); the zone scenarios close the
// screen (Close); OnItemsPicked, OnUnitLeft, OnMessageResult and OnGameOver are not known to be
// reached by a scenario.
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;
void* s_saveState;

// --- The save stream's recorder ------------------------------------------------------------------

constexpr int kStreamSlots = 8;

struct Recording {
    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> chunks;
};
Recording* s_recording;

void __fastcall RecordWrite(void*, void*, const void* buffer, std::uint32_t size) {
    auto* p = static_cast<const unsigned char*>(buffer);
    s_recording->bytes.insert(s_recording->bytes.end(), p, p + size);
    s_recording->chunks.push_back(size);
}

// Runs `fn` with the save stream's writes going into `into`.
template <typename Fn>
void Recorded(Recording* into, Fn fn) {
    void** real = *reinterpret_cast<void***>(GameSaveStream());
    void* fake[kStreamSlots];
    std::memcpy(fake, real, sizeof(fake));
    fake[2] = reinterpret_cast<void*>(&RecordWrite);
    s_recording = into;
    *reinterpret_cast<void***>(GameSaveStream()) = fake;
    fn();
    *reinterpret_cast<void***>(GameSaveStream()) = real;
    s_recording = nullptr;
}

int FirstDifference(const Recording& a, const Recording& b) {
    std::size_t n = a.bytes.size() < b.bytes.size() ? a.bytes.size() : b.bytes.size();
    for (std::size_t i = 0; i < n; ++i)
        if (a.bytes[i] != b.bytes[i]) return static_cast<int>(i);
    return a.bytes.size() == b.bytes.size() ? -1 : static_cast<int>(n);
}

bool InParty(CBattleScreen* screen, const CUnit* unit) {
    UiDwordArray& party = *reinterpret_cast<UiDwordArray*>(&screen->party_);
    for (int i = 0; i < party.size; ++i)
        if (party.data[i] == reinterpret_cast<std::uintptr_t>(unit)) return true;
    return false;
}
} // namespace

static void __fastcall SaveStateVerify(CBattleScreen* self, void*) {
    Recording expected, actual;
    Recorded(&expected, [&] { reinterpret_cast<void(__fastcall*)(CBattleScreen*, void*)>(s_saveState)(self, nullptr); });
    Recorded(&actual, [&] { self->SaveState(); });
    bool same = expected.bytes == actual.bytes && expected.chunks == actual.chunks;
    ei::verify::Check("CBattleScreen::SaveState", true, same, "%u vs %u bytes, first difference at %d",
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      FirstDifference(expected, actual));
    std::size_t pos = 0;
    for (std::uint32_t size : expected.chunks) {
        GameSaveStream()->Write(expected.bytes.data() + pos, size);
        pos += size;
    }
}
// 00635080: PUSH -1; PUSH 0x72D958 (2 + 5 bytes).
EI_VERIFY(0x00635080, SaveStateVerify, 7, s_saveState);

static void __fastcall CloseVerify(CBattleScreen* self, void*) {
    self->Close();
    bool held = reinterpret_cast<const std::uint8_t*>(GameCursor())[0x90] != 0 && GameCursor()->enabled_;
    bool ok = (camera::TheCamera()->flags_ & CCamera::kFree) != 0 && *reinterpret_cast<const int*>(0x0078FF34) == 55 &&
              self->party_.size == 0 && !held;
    ei::verify::Check("CBattleScreen::Close", true, ok, "camera flags %X, party %d", camera::TheCamera()->flags_,
                      self->party_.size);
}
EI_VERIFY(0x00635E60, CloseVerify, 0, s_unused);

static bool __fastcall PollShownVerify(CBattleScreen* self, void*) {
    bool result = self->PollShown();
    ei::verify::Check("CBattleScreen::PollShown", true, result && !self->hasHero_, "result %d", result);
    return result;
}
EI_VERIFY(0x006360F0, PollShownVerify, 0, s_unused);

static void __fastcall ItemsPickedVerify(CBattleScreen* self, void*, mfc::CTypedPtrList<CItem>* items, int money,
                                         int crippled) {
    int count = items->count;
    self->OnItemsPicked(items, money, crippled);
    // RefreshSelection drops the pending action; the list is not changed.
    bool ok = items->count == count && self->pendingAction_ == 0 && self->pendingSlot_ == -1;
    ei::verify::Check("CBattleScreen::OnItemsPicked", true, ok, "%d items, money %d", count, money);
}
EI_VERIFY(0x00636150, ItemsPickedVerify, 0, s_unused);

static void __fastcall UnitLeftVerify(CBattleScreen* self, void*, CUnit* unit) {
    UiDwordArray& party = *reinterpret_cast<UiDwordArray*>(&self->party_);
    std::vector<std::uintptr_t> before(party.data, party.data + party.size);
    bool wasIn = InParty(self, unit);
    bool client = IsMultiplayerClient() != 0;
    self->OnUnitLeft(unit);
    if (client) return;
    // The first entry of the unit is removed, the rest kept in order.
    std::vector<std::uintptr_t> expected;
    bool removed = false;
    for (std::uintptr_t u : before) {
        if (!removed && u == reinterpret_cast<std::uintptr_t>(unit)) {
            removed = true;
            continue;
        }
        expected.push_back(u);
    }
    std::vector<std::uintptr_t> after(party.data, party.data + party.size);
    ei::verify::Check("CBattleScreen::OnUnitLeft", true, expected == after, "in party %d, %d -> %d", wasIn,
                      static_cast<int>(before.size()), static_cast<int>(after.size()));
}
EI_VERIFY(0x006364E0, UnitLeftVerify, 0, s_unused);

static void __fastcall UnitJoinedVerify(CBattleScreen* self, void*, CUnit* unit) {
    UiDwordArray& party = *reinterpret_cast<UiDwordArray*>(&self->party_);
    std::vector<std::uintptr_t> before(party.data, party.data + party.size);
    bool client = IsMultiplayerClient() != 0;
    self->OnUnitJoined(unit);
    if (client) return;
    // One more entry: the unit, the others in their old order.
    std::vector<std::uintptr_t> others;
    int count = 0;
    for (int i = 0; i < party.size; ++i) {
        if (party.data[i] == reinterpret_cast<std::uintptr_t>(unit) && count++ == 0) continue;
        others.push_back(party.data[i]);
    }
    bool inHeroes = false;
    for (int i = 0; i < self->heroes_.size; ++i) inHeroes |= static_cast<int>(self->heroes_.data[i]) == unit->id_;
    bool ok = party.size == static_cast<int>(before.size()) + 1 && count >= 1 && others == before && !inHeroes;
    ei::verify::Check("CBattleScreen::OnUnitJoined", true, ok, "unit %d, party %d -> %d", unit->id_,
                      static_cast<int>(before.size()), party.size);
}
EI_VERIFY(0x006361B0, UnitJoinedVerify, 0, s_unused);

static void __fastcall MessageResultVerify(CBattleScreen* self, void*, int result) {
    CInterface3D* ui = self->interface_;
    self->OnMessageResult(result);
    bool ok = true;
    if (result == 0x20001) {
        CUiWindow* top = ui->GetActiveWindow();
        ok = top != nullptr && top->vtable_ == reinterpret_cast<void**>(0x00741388);
    } else if (result == 0x20002 || result == 0x90001 || result == 0x100001) {
        ok = ui->closeAll_;
    }
    ei::verify::Check("CBattleScreen::OnMessageResult", true, ok, "result %X", result);
}
EI_VERIFY(0x00635EB0, MessageResultVerify, 0, s_unused);

static void __fastcall GameOverVerify(CBattleScreen* self, void*, char won) {
    self->OnGameOver(won);
    auto* top = static_cast<CMessageBox*>(self->interface_->GetActiveWindow());
    bool ok = top != nullptr && top->result_ == (won ? 0xFFFF0000u : 0x20000u);
    ei::verify::Check("CBattleScreen::OnGameOver", true, ok, "won %d", won);
}
EI_VERIFY(0x006365E0, GameOverVerify, 0, s_unused);
