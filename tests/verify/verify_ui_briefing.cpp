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
#include <string>

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
// OnUnitJoined: ours only (the client world's control, the camera, maybe a dialogue start): the
// unit is in the party once more than before (when the local player has records). Reached when
// the party is created on the base (zone_base_bz1g, ui_base_dialogue).
static void __fastcall UnitJoinedVerify(CBriefingScreen* self, void*, CUnit* unit) {
    auto count = [&] {
        int n = 0;
        for (int i = 0; i < self->party_.size_; ++i) n += reinterpret_cast<CUnit*>(self->party_.data_[i]) == unit;
        return n;
    };
    int before = count();
    int size = self->party_.size_;
    self->OnUnitJoined(unit);
    int after = count();
    bool ok = self->party_.size_ == size ? after == before : (self->party_.size_ >= size + 1 && after == before + 1);
    ei::verify::Check("CBriefingScreen::OnUnitJoined", true, ok, "unit %d: party %d -> %d", unit->id_, size,
                      self->party_.size_);
}
EI_VERIFY(0x00602C90, UnitJoinedVerify, 0, s_unused);

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

// LoadPersonTexts (00607720): ours only (map insertions); the tree's keys stay in strictly rising
// order and the node count does not go down. Reached by the screen's Create (zone_base_bz1g).
namespace {
int CountInOrder(const PersonTextNode* node, const PersonTextNode** last, bool* ordered) {
    if (!node) return 0;
    int n = CountInOrder(node->left, last, ordered);
    if (*last && !((*last)->key < node->key)) *ordered = false;
    *last = node;
    return n + 1 + CountInOrder(node->right, last, ordered);
}
} // namespace

static void __fastcall PersonTextsVerify(CBriefingScreen* self, void*) {
    const PersonTextNode* last = nullptr;
    bool ordered = true;
    int before = CountInOrder(self->personTexts_.header->parent, &last, &ordered);
    self->LoadPersonTexts();
    last = nullptr;
    ordered = true;
    int after = CountInOrder(self->personTexts_.header->parent, &last, &ordered);
    ei::verify::Check("CBriefingScreen::LoadPersonTexts", true, ordered && after >= before, "%d -> %d texts", before,
                      after);
}
EI_VERIFY(0x00607720, PersonTextsVerify, 0, s_unused);

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

// Mouse move: ours only; in state 1 the hovered topic is -1 or a shown row's topic.
static void __fastcall MouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBriefingScreen>(input);
    self->OnMouseMove(buttons, x, y);
    bool ok = true;
    if (self->state_ == 1 && !self->chatActive_) {
        int h = self->hoveredTopic_;
        ok = h == -1 || (h >= self->topicTop_ && h < self->topicTop_ + 5 && h < self->topics_.size);
    }
    ei::verify::Check("CBriefingScreen::OnMouseMove", true, ok, "hovered %d, top %d", self->hoveredTopic_,
                      self->topicTop_);
}
EI_VERIFY(0x00603D00, MouseMoveVerify, 0, s_unused);

// Keys (ui_briefing_keys.cpp): ours only; Enter, Esc and Space are always used (unless the chat
// window had the input), and Enter / Esc leave the phrases behind (state not 3). ui_base_dialogue
// ends with Esc.
static void __fastcall KeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CBriefingScreen>(input);
    bool chat = self->chatActive_ != 0;
    int before = self->state_;
    self->OnKeyDown(key, repeat, flags);
    bool ok = true;
    if (!chat && (key == VK_RETURN || key == VK_ESCAPE || key == VK_SPACE)) ok = self->inputHandled_;
    if (!chat && before == 3 && (key == VK_ESCAPE || (key == VK_RETURN && !self->chat_))) ok = ok && self->state_ != 3;
    ei::verify::Check("CBriefingScreen::OnKeyDown", true, ok, "key %X, state %d -> %d", key, before, self->state_);
}
EI_VERIFY(0x006034A0, KeyDownVerify, 0, s_unused);

// --- Topics (ui_briefing_topics.cpp) -----------------------------------------------------------
// Side by side: FindTopics only rebuilds topics_ (the original's list is recorded, then ours is
// compared with it); TopicTitle writes a fresh string (both into raw storage). Reached by the
// mouse moving over Smith and by the topic list's drawing (ui_base_dialogue).

namespace {
void* s_findTopics;
void* s_topicTitle;

std::string TopicsText(const CBriefingScreen* self) {
    std::string text;
    for (int i = 0; i < self->topics_.size; ++i) {
        text += static_cast<const char*>(self->topics_[i]);
        text += '|';
    }
    return text;
}
} // namespace

static void __fastcall FindTopicsVerify(CBriefingScreen* self, void*, int personId) {
    reinterpret_cast<void(__fastcall*)(CBriefingScreen*, void*, int)>(s_findTopics)(self, nullptr, personId);
    std::string expected = TopicsText(self);
    self->FindTopics(personId);
    std::string actual = TopicsText(self);
    ei::verify::Check("CBriefingScreen::FindTopics", true, expected == actual, "person %d: %.80s / %.80s", personId,
                      expected.c_str(), actual.c_str());
}
// 00607360: PUSH -0x1; PUSH 0x0072B000 (2 + 5 bytes).
EI_VERIFY(0x00607360, FindTopicsVerify, 7, s_findTopics);

static mfc::CString* __fastcall TopicTitleVerify(CBriefingScreen* self, void*, mfc::CString* out, const char* topic) {
    alignas(4) std::uint8_t raw[4];
    auto* expected = reinterpret_cast<mfc::CString* (__fastcall*)(CBriefingScreen*, void*, mfc::CString*, const char*)>(
        s_topicTitle)(self, nullptr, reinterpret_cast<mfc::CString*>(raw), topic);
    mfc::CString* actual = self->TopicTitle(out, topic);
    bool ok = std::strcmp(*expected, *actual) == 0;
    ei::verify::Check("CBriefingScreen::TopicTitle", true, ok, "%s: %.60s / %.60s", topic,
                      static_cast<const char*>(*expected), static_cast<const char*>(*actual));
    expected->~CString();
    return actual;
}
// 006078C0: PUSH -0x1; PUSH 0x0072B087 (2 + 5 bytes).
EI_VERIFY(0x006078C0, TopicTitleVerify, 7, s_topicTitle);

static void __fastcall OpenTopicsVerify(CBriefingScreen* self, void*, CEngineObject* person) {
    self->OpenTopics(person);
    bool ok;
    int count = self->topics_.size;
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
    int count = self->topics_.size;
    self->DrawTopics();
    bool ok = top == self->topicTop_ && hovered == self->hoveredTopic_ && count == self->topics_.size;
    ei::verify::Check("CBriefingScreen::DrawTopics", true, ok, "top %d, hovered %d", top, hovered);
}
EI_VERIFY(0x006071E0, DrawTopicsVerify, 0, s_unused);
