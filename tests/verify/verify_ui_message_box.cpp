// Checks for the message box's and the difficulty question's input handlers
// (src/game/ui_message_box.cpp). The answers play sounds, so those paths run only ours and the
// wrapper checks the answer left pending; the difficulty question's check boxes (regions 2 / 3 and
// clicks outside) run side by side. Scenario: ui_main_menu (the difficulty question's boxes, its
// keys and cancel button; the quit box's cancel button), ui_screens / ui_container_input (Esc on
// the quit box).
#include "verify.h"

#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;
void* s_difficultyClick;

using MouseHandler = void(__fastcall*)(void*, void*, int, int, int);

constexpr std::uint32_t kExitResult = 0xFFFF0000u;

struct Pending {
    std::uint32_t value;
    std::uint32_t flag;
};

Pending TakePending(const CInterface3D* ui) { return {ui->pendingValue_, ui->pendingFlag_ ? 1u : 0u}; }

bool Changed(const Pending& a, const Pending& b) { return a.value != b.value || a.flag != b.flag; }

// The answer `answer` is pending (or nothing changed when `answer` is 0).
void CheckAnswer(const char* name, const Pending& before, const CInterface3D* ui, std::uint32_t answer, int detail) {
    Pending expected = answer != 0 ? Pending{answer, 1u} : before;
    ei::verify::Check(name, expected, TakePending(ui), "answer %X (%d)", answer, detail);
}

// The edit field's text was copied into the box's buffer.
void CheckEditCopied(const char* name, const CMessageBox* box) {
    bool same = std::strcmp(box->editText_, box->edit_->text_) == 0;
    ei::verify::Check(name, true, same, "edit text");
}
} // namespace

static void __fastcall MessageClickVerify(void* input, void*, int buttons, int x, int y) {
    auto* box = WindowFromInput<CMessageBox>(input);
    CInterface3D* ui = box->interface_;
    int region = box->RegionAt(x, y);
    bool exits = region == 0 && box->result_ == kExitResult;
    Pending before = TakePending(ui);
    box->OnLButtonDown(buttons, x, y);
    if (exits) return;   // the exit movie, then the game quits
    std::uint32_t answer = region >= 0 ? box->result_ | static_cast<std::uint32_t>(region + 1) : 0;
    CheckAnswer("CMessageBox::OnLButtonDown", before, ui, answer, region);
    if (region == 0 && (box->flags_ & 4)) CheckEditCopied("CMessageBox::OnLButtonDown", box);
}
EI_VERIFY(0x00634D20, MessageClickVerify, 0, s_unused);

static void __fastcall MessageKeyVerify(void* input, void*, int key, int repeat, int flags) {
    auto* box = WindowFromInput<CMessageBox>(input);
    CInterface3D* ui = box->interface_;
    if (key == VK_RETURN && box->result_ == kExitResult) {
        box->OnKeyDown(key, repeat, flags);   // the exit movie, then the game quits
        return;
    }
    bool answers = key == VK_RETURN || key == VK_ESCAPE;
    Pending before = TakePending(ui);
    box->OnKeyDown(key, repeat, flags);
    // A child (the edit field) may have used the key first; then nothing is answered.
    bool answered = Changed(before, TakePending(ui));
    if (answers) {
        if (answered) {
            CheckAnswer("CMessageBox::OnKeyDown", before, ui, box->result_ | (key == VK_RETURN ? 1u : 2u), key);
            if (key == VK_RETURN && (box->flags_ & 4)) CheckEditCopied("CMessageBox::OnKeyDown", box);
        }
        ei::verify::Check("CMessageBox::OnKeyDown", true, box->inputHandled_, "key %X", key);
    } else {
        ei::verify::Check("CMessageBox::OnKeyDown", false, answered, "key %X", key);
    }
}
EI_VERIFY(0x00634E60, MessageKeyVerify, 0, s_unused);

namespace {
// The fields the difficulty question's check boxes change.
struct ChoiceState {
    int easy;
    std::uint32_t easyOffset;
    std::uint32_t hardOffset;
    Pending pending;
    int difficulty;

    static ChoiceState Take(const CDifficultyBox* box) {
        ChoiceState s{};
        s.easy = box->easy_;
        std::memcpy(&s.easyOffset, &box->EasyBox()->textureOffsetU_, 4);
        std::memcpy(&s.hardOffset, &box->HardBox()->textureOffsetU_, 4);
        s.pending = TakePending(box->interface_);
        s.difficulty = GameDifficulty();
        return s;
    }
    void Put(CDifficultyBox* box) const {
        box->easy_ = easy;
        std::memcpy(&box->EasyBox()->textureOffsetU_, &easyOffset, 4);
        std::memcpy(&box->HardBox()->textureOffsetU_, &hardOffset, 4);
        box->interface_->pendingValue_ = pending.value;
        box->interface_->pendingFlag_ = pending.flag != 0;
        GameDifficulty() = difficulty;
    }
};
} // namespace

static void __fastcall DifficultyClickVerify(void* input, void*, int buttons, int x, int y) {
    auto* box = WindowFromInput<CDifficultyBox>(input);
    int region = box->RegionAt(x, y);
    if (region == 0 || region == 1) {
        // The answers play a sound: ours only.
        Pending before = TakePending(box->interface_);
        int easy = box->easy_;
        box->OnLButtonDown(buttons, x, y);
        CheckAnswer("CDifficultyBox::OnLButtonDown", before, box->interface_,
                    box->result_ | static_cast<std::uint32_t>(region + 1), region);
        if (region == 0) ei::verify::Check("CDifficultyBox::OnLButtonDown", easy, GameDifficulty(), "difficulty");
        return;
    }
    // The check boxes (and clicks elsewhere): side by side.
    ChoiceState start = ChoiceState::Take(box);
    reinterpret_cast<MouseHandler>(s_difficultyClick)(input, nullptr, buttons, x, y);
    ChoiceState expected = ChoiceState::Take(box);
    start.Put(box);
    box->OnLButtonDown(buttons, x, y);
    ei::verify::Check("CDifficultyBox::OnLButtonDown", expected, ChoiceState::Take(box), "region %d", region);
}
EI_VERIFY(0x00633DD0, DifficultyClickVerify, 5, s_difficultyClick);

static void __fastcall DifficultyKeyVerify(void* input, void*, int key, int repeat, int flags) {
    auto* box = WindowFromInput<CDifficultyBox>(input);
    Pending before = TakePending(box->interface_);
    int easy = box->easy_;
    box->OnKeyDown(key, repeat, flags);
    bool answered = Changed(before, TakePending(box->interface_));
    if (key == VK_RETURN || key == VK_ESCAPE) {
        if (answered) {
            CheckAnswer("CDifficultyBox::OnKeyDown", before, box->interface_,
                        box->result_ | (key == VK_RETURN ? 1u : 2u), key);
            if (key == VK_RETURN) ei::verify::Check("CDifficultyBox::OnKeyDown", easy, GameDifficulty(), "difficulty");
        }
        ei::verify::Check("CDifficultyBox::OnKeyDown", true, box->inputHandled_, "key %X", key);
    } else {
        ei::verify::Check("CDifficultyBox::OnKeyDown", false, answered, "key %X", key);
    }
}
EI_VERIFY(0x00633EC0, DifficultyKeyVerify, 0, s_unused);
