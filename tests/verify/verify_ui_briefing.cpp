// Checks for the briefing / base screen's main-vtable slots (src/game/ui_briefing.cpp).
//
// OnUnitLeft only changes the leader and the party array: side by side (original, record, restore,
// ours, compare). The rest close, open windows, leave the game or write the save: ours only, with
// the state they leave checked. Scenarios: zone_base_bz1g and ui_base_dialogue reach the screen;
// SaveState runs with the base's autosave; OnBriefing when a base script sends "briefing"; Close,
// OnMessageResult, OnGameOver and ShowChat are not known to be reached.
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
void* s_unused;
void* s_unitLeft;

constexpr int kPartySnapshot = 16;

struct PartyState {
    CUnit* leader;
    int size;
    std::uintptr_t data[kPartySnapshot];

    static PartyState Take(const CBriefingScreen* screen) {
        PartyState s{};
        s.leader = screen->leader_;
        s.size = screen->party_.size_;
        for (int i = 0; i < s.size && i < kPartySnapshot; ++i) s.data[i] = screen->party_.data_[i];
        return s;
    }
    void Put(CBriefingScreen* screen) const {
        screen->leader_ = leader;
        screen->party_.size_ = size;
        for (int i = 0; i < size && i < kPartySnapshot; ++i) screen->party_.data_[i] = data[i];
    }
};

float MapEdgeFade() { return *reinterpret_cast<const float*>(0x0079BD34); }
CStream* SaveGameStream() { return reinterpret_cast<CStream*>(0x007AFDF8); }
void** MessageBoxVtable() { return reinterpret_cast<void**>(0x00742348); }
void** SaveLoadVtable() { return reinterpret_cast<void**>(0x00741388); }
} // namespace

static void __fastcall CloseVerify(CBriefingScreen* self, void*) {
    self->Close();
    std::uint32_t flags = camera::TheCamera()->flags_;
    float fade = MapEdgeFade();
    bool ok = std::memcmp(&fade, &self->savedEdgeFade_, 4) == 0 &&
              (flags & (CCamera::kFree | CCamera::kBounded | CCamera::kAlternate)) == CCamera::kFree;
    ei::verify::Check("CBriefingScreen::Close", true, ok, "camera flags %X", flags);
}
EI_VERIFY(0x00602BB0, CloseVerify, 0, s_unused);

static void __fastcall MessageResultVerify(CBriefingScreen* self, void*, std::uint32_t result) {
    CInterface3D* ui = self->interface_;
    self->OnMessageResult(result);
    bool ok = true;
    if (result == 0x20001) {
        CUiWindow* top = ui->GetActiveWindow();
        ok = top && top->vtable_ == SaveLoadVtable();
    }
    ei::verify::Check("CBriefingScreen::OnMessageResult", true, ok, "result %X: the load window on top", result);
}
EI_VERIFY(0x00606F40, MessageResultVerify, 0, s_unused);

static void __fastcall UnitLeftVerify(CBriefingScreen* self, void*, CUnit* unit) {
    if (self->party_.size_ > kPartySnapshot) {
        self->OnUnitLeft(unit);
        return;
    }
    PartyState start = PartyState::Take(self);
    reinterpret_cast<void(__fastcall*)(CBriefingScreen*, void*, CUnit*)>(s_unitLeft)(self, nullptr, unit);
    PartyState expected = PartyState::Take(self);
    start.Put(self);
    self->OnUnitLeft(unit);
    ei::verify::Check("CBriefingScreen::OnUnitLeft", expected, PartyState::Take(self), "unit %d, party %d -> %d",
                      unit->id_, start.size, expected.size);
}
// 00603010: PUSH EBX; PUSH ESI; MOV ESI,ECX; MOV ECX,[ESP+0xC] (1 + 1 + 2 + 4 bytes).
EI_VERIFY(0x00603010, UnitLeftVerify, 8, s_unitLeft);

static void __fastcall GameOverVerify(CBriefingScreen* self, void*, int unused) {
    CInterface3D* ui = self->interface_;
    self->OnGameOver(unused);
    CUiWindow* top = ui->GetActiveWindow();
    ei::verify::Check("CBriefingScreen::OnGameOver", true, top && top->vtable_ == MessageBoxVtable(),
                      "the message box on top");
}
EI_VERIFY(0x00608C70, GameOverVerify, 0, s_unused);

static void __fastcall BriefingVerify(CBriefingScreen* self, void*, const char* name) {
    self->OnBriefing(name);
    ei::verify::Check("CBriefingScreen::OnBriefing", true, self->state_ >= 0 && self->state_ <= 4, "state %d",
                      self->state_);
}
EI_VERIFY(0x006030C0, BriefingVerify, 0, s_unused);

static void __fastcall ShowChatVerify(CBriefingScreen* self, void*, char show) {
    self->ShowChat(show);
    ei::verify::Check("CBriefingScreen::ShowChat", true, true, "");
}
EI_VERIFY(0x00603090, ShowChatVerify, 0, s_unused);

static void __fastcall SaveStateVerify(CBriefingScreen* self, void*) {
    CStream* stream = SaveGameStream();
    std::uint32_t before = stream->Position();
    self->SaveState();
    std::uint32_t after = stream->Position();
    ei::verify::Check("CBriefingScreen::SaveState", before + 4, after, "position %u -> %u", before, after);
}
EI_VERIFY(0x00602490, SaveStateVerify, 0, s_unused);

// --- Input and the topic list (src/game/ui_briefing_input.cpp) ---------------------------------
// Ours only (they open topics, start dialogues, walk the leader): the dialogue state stays a valid
// one; OpenTopics leaves either state 0 without topics or state 1 with "goodbye" last, the list at
// its top and nothing hovered; DrawTopics (GDI text) changes no field. Scenario ui_base_dialogue
// (a click on Smith, a topic, phrases); the double click is not in a scenario.

static void __fastcall LeftDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBriefingScreen>(input);
    int before = self->state_;
    self->OnLeftDown(buttons, x, y);
    ei::verify::Check("CBriefingScreen::OnLeftDown", true, self->state_ >= 0 && self->state_ <= 4,
                      "state %d -> %d", before, self->state_);
}
EI_VERIFY(0x006030F0, LeftDownVerify, 0, s_unused);

static void __fastcall LeftDoubleVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBriefingScreen>(input);
    int before = self->state_;
    self->OnLeftDouble(buttons, x, y);
    ei::verify::Check("CBriefingScreen::OnLeftDouble", true, self->state_ >= 0 && self->state_ <= 4,
                      "state %d -> %d", before, self->state_);
}
EI_VERIFY(0x00603360, LeftDoubleVerify, 0, s_unused);

static void __fastcall OpenTopicsVerify(CBriefingScreen* self, void*, CEngineObject* person) {
    self->OpenTopics(person);
    bool ok;
    int count = self->topics_.size_;
    if (count <= 0) {
        ok = self->state_ == 0;
    } else {
        ok = self->state_ == 1 && self->topicTop_ == 0 && self->hoveredTopic_ == -1 &&
             std::strcmp(self->topics_[count - 1], "goodbye") == 0;
    }
    ei::verify::Check("CBriefingScreen::OpenTopics", true, ok, "%d topics, state %d", count, self->state_);
}
EI_VERIFY(0x00608B60, OpenTopicsVerify, 0, s_unused);

static void __fastcall DrawTopicsVerify(CBriefingScreen* self, void*) {
    int top = self->topicTop_;
    int hovered = self->hoveredTopic_;
    int count = self->topics_.size_;
    self->DrawTopics();
    bool ok = top == self->topicTop_ && hovered == self->hoveredTopic_ && count == self->topics_.size_;
    ei::verify::Check("CBriefingScreen::DrawTopics", true, ok, "top %d, hovered %d", top, hovered);
}
EI_VERIFY(0x006071E0, DrawTopicsVerify, 0, s_unused);
