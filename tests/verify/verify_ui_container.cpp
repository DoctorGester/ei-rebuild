// Checks for the containers' input handlers (ui_container.cpp).
//
// The dispatchers call other windows' handlers, so only ours runs (the original would deliver
// every event twice). The check follows the dispatch through the handled flags: before the call
// every extra input side's and child's flag is set to a marker; the dispatcher clears the flag of
// each one it calls, so afterwards the marker is left exactly on those it skipped. In order, the
// sides and then the children it should call (enabled, and for the mouse events holding the
// point, before or after the call) must have lost the marker up to the first one whose handler
// set its flag, and every one after that, and every child it should skip, must still have it; the
// container's own flag must say whether one did. The markers left are put back afterwards. When a
// handler changed the lists, nothing is compared.
#include "verify.h"

#include "../../src/game/ui.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint8_t kMarker = 0x5A;

std::uint8_t& FlagByte(bool& flag) { return reinterpret_cast<std::uint8_t&>(flag); }

struct Member {
    bool* flag;
    std::uint8_t saved;
    bool qualified;    // should be called, judged before the call
};

class DispatchCheck {
public:
    DispatchCheck(UiContainerInput* input, bool atPoint, int x, int y)
        : input_(input), container_(input->Container()), atPoint_(atPoint), x_(x), y_(y) {
        for (UiInputSide** s = container_->inputsBegin_; s != container_->inputsEnd_; ++s) {
            sides_.push_back(*s);
            Mark(&(*s)->handled_, true);
        }
        for (int i = 0; i < container_->children_.size_; ++i) {
            CUiWindow* child = container_->children_.data_[i];
            children_.push_back(child);
            Mark(&child->inputHandled_, Qualifies(child));
        }
    }

    // Compares and puts the markers back. Returns false when the lists changed (nothing compared).
    bool Finish(bool* ok, int* stoppedAt) {
        bool same = Same();
        *ok = true;
        *stoppedAt = -1;
        if (same) {
            bool stopped = false;
            int index = 0;
            for (std::size_t i = 0; i < members_.size(); ++i, ++index) {
                std::uint8_t now = FlagByte(*members_[i].flag);
                bool qualifiedNow = i < sides_.size() || Qualifies(children_[i - sides_.size()]);
                bool mayCall = members_[i].qualified || qualifiedNow;
                bool maySkip = !members_[i].qualified || !qualifiedNow;
                if (stopped || !mayCall) {
                    if (now != kMarker) *ok = false;
                    continue;
                }
                if (now == kMarker) {
                    if (!maySkip) *ok = false;
                    continue;
                }
                if (now) {
                    stopped = true;
                    *stoppedAt = index;
                }
            }
            if (input_->handled_ != stopped) *ok = false;
        }
        Restore();
        return same;
    }

private:
    bool Qualifies(CUiWindow* child) const { return child->enabled_ && (!atPoint_ || child->ContainsPoint(x_, y_)); }
    void Mark(bool* flag, bool qualified) {
        members_.push_back(Member{flag, FlagByte(*flag), qualified});
        FlagByte(*flag) = kMarker;
    }
    bool Same() const {
        if (static_cast<std::size_t>(container_->inputsEnd_ - container_->inputsBegin_) != sides_.size()) return false;
        if (static_cast<std::size_t>(container_->children_.size_) != children_.size()) return false;
        for (std::size_t i = 0; i < sides_.size(); ++i)
            if (container_->inputsBegin_[i] != sides_[i]) return false;
        for (std::size_t i = 0; i < children_.size(); ++i)
            if (container_->children_.data_[i] != children_[i]) return false;
        return true;
    }
    // Every flag still marked gets its value back, for the members still in the lists.
    void Restore() {
        for (std::size_t i = 0; i < members_.size(); ++i) {
            bool present = false;
            if (i < sides_.size()) {
                for (UiInputSide** s = container_->inputsBegin_; s != container_->inputsEnd_; ++s)
                    if (*s == sides_[i]) present = true;
            } else {
                CUiWindow* child = children_[i - sides_.size()];
                for (int k = 0; k < container_->children_.size_; ++k)
                    if (container_->children_.data_[k] == child) present = true;
            }
            if (present && FlagByte(*members_[i].flag) == kMarker) FlagByte(*members_[i].flag) = members_[i].saved;
        }
    }

    UiContainerInput* input_;
    CUiContainer* container_;
    bool atPoint_;
    int x_, y_;
    std::vector<UiInputSide*> sides_;
    std::vector<CUiWindow*> children_;
    std::vector<Member> members_;
};

void Report(const char* name, DispatchCheck& check, int a, int b, int c) {
    bool ok = false;
    int stoppedAt = -1;
    if (!check.Finish(&ok, &stoppedAt)) return;
    ei::verify::Check(name, true, ok, "(%d, %d, %d) stopped at %d", a, b, c, stoppedAt);
}

using Handler = void (UiContainerInput::*)(int, int, int);

template <Handler H, bool AtPoint>
void Run(const char* name, UiContainerInput* input, int a, int b, int c) {
    DispatchCheck check(input, AtPoint, b, c);
    (input->*H)(a, b, c);
    Report(name, check, a, b, c);
}

void* s_unused;

} // namespace

// 00601110 / 006011F0: the left button on the escape menu (ui_screens), the main menu's options,
// load and difficulty windows (ui_main_menu), the camp's pages (ui_camp); many screens' own
// handlers call these first.
static void __fastcall LeftDownVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnLeftDown, true>("UiContainerInput::OnLeftDown", self, a, b, c);
}
EI_VERIFY(0x00601110, LeftDownVerify, 0, s_unused);

static void __fastcall LeftUpVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnLeftUp, true>("UiContainerInput::OnLeftUp", self, a, b, c);
}
EI_VERIFY(0x006011F0, LeftUpVerify, 0, s_unused);

// 006012D0: every game scenario (the battle screen's mouse move 00639D60 calls it every frame).
static void __fastcall MouseMoveVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnMouseMove, true>("UiContainerInput::OnMouseMove", self, a, b, c);
}
EI_VERIFY(0x006012D0, MouseMoveVerify, 0, s_unused);

// 006013B0 / 00601490: right clicks on the battle screen (world_client, ui_container_input) and the
// main menu (ui_container_input).
static void __fastcall RightDownVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnRightDown, true>("UiContainerInput::OnRightDown", self, a, b, c);
}
EI_VERIFY(0x006013B0, RightDownVerify, 0, s_unused);

static void __fastcall RightUpVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnRightUp, true>("UiContainerInput::OnRightUp", self, a, b, c);
}
EI_VERIFY(0x00601490, RightUpVerify, 0, s_unused);

// 00601570 / 00601650: double clicks on the main menu (ui_container_input's dblclick / rdblclick).
static void __fastcall LeftDoubleVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnLeftDouble, true>("UiContainerInput::OnLeftDouble", self, a, b, c);
}
EI_VERIFY(0x00601570, LeftDoubleVerify, 0, s_unused);

static void __fastcall RightDoubleVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnRightDouble, true>("UiContainerInput::OnRightDouble", self, a, b, c);
}
EI_VERIFY(0x00601650, RightDoubleVerify, 0, s_unused);

// 00601820: keys on the escape menu's save / load and options windows (their key handlers call it
// first), ui_screens' and ui_container_input's keys on the escape menu.
static void __fastcall KeyDownVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnKeyDown, false>("UiContainerInput::OnKeyDown", self, a, b, c);
}
EI_VERIFY(0x00601820, KeyDownVerify, 0, s_unused);

// 006018E0: letter keys (their WM_CHAR) on the battle screen, the main menu and the escape menu
// (ui_container_input).
static void __fastcall CharVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnChar, false>("UiContainerInput::OnChar", self, a, b, c);
}
EI_VERIFY(0x006018E0, CharVerify, 0, s_unused);

// 006019A0: every key released on the main menu, the escape menu or the hero screen (ui_screens).
static void __fastcall KeyUpVerify(UiContainerInput* self, void*, int a, int b, int c) {
    Run<&UiContainerInput::OnKeyUp, false>("UiContainerInput::OnKeyUp", self, a, b, c);
}
EI_VERIFY(0x006019A0, KeyUpVerify, 0, s_unused);

// 00601730: the wheel over the main menu or the escape menu (ui_container_input). Also the result 1.
static int __fastcall WheelVerify(UiContainerInput* self, void*, int a, int b, int c, int d) {
    DispatchCheck check(self, true, c, d);
    int result = self->OnWheel(a, b, c, d);
    bool ok = false;
    int stoppedAt = -1;
    if (check.Finish(&ok, &stoppedAt))
        ei::verify::Check("UiContainerInput::OnWheel", true, ok && result == 1, "(%d, %d, %d, %d) stopped at %d, result %d",
                          a, b, c, d, stoppedAt, result);
    return result;
}
EI_VERIFY(0x00601730, WheelVerify, 0, s_unused);

// 00601A60: the main menu losing the top to its options or load window (ui_main_menu). Checked:
// the lists unchanged (it only calls every side's and child's slot 1).
static void __fastcall DeactivateVerify(UiContainerInput* self, void*) {
    CUiContainer* c = self->Container();
    UiInputSide** sides = c->inputsBegin_;
    UiInputSide** sidesEnd = c->inputsEnd_;
    CUiWindow** children = c->children_.data_;
    int count = c->children_.size_;
    self->OnDeactivate();
    bool ok = c->inputsBegin_ == sides && c->inputsEnd_ == sidesEnd && c->children_.data_ == children &&
              c->children_.size_ == count;
    ei::verify::Check("UiContainerInput::OnDeactivate", true, ok, "children %d -> %d", count, c->children_.size_);
}
EI_VERIFY(0x00601A60, DeactivateVerify, 0, s_unused);

// --- Construction, destruction, extra input sides (ours only) ------------------------------------

static void* s_unusedLifetime;

// 00600B20: the base constructed, then an empty container with its two vtables.
static CUiContainer* __fastcall ContainerConstructVerify(CUiContainer* self, void*, CInterface3D* ui) {
    CUiContainer* result = self->CUiContainer::Construct(ui);
    bool ok = result == self && self->interface_ == ui && !self->children_.data_ && self->children_.size_ == 0 &&
              self->children_.capacity_ == 0 && self->children_.growBy_ == 0 && !self->inputsBegin_ &&
              !self->inputsEnd_ && !self->inputsCapacity_ && self->vtable_ == reinterpret_cast<void**>(0x00741008) &&
              self->inputVtable_ == reinterpret_cast<void**>(0x00740FD4);
    ei::verify::Check("CUiContainer::Construct", true, ok, "window %p", self);
    return result;
}
EI_VERIFY(0x00600B20, ContainerConstructVerify, 0, s_unusedLifetime);

// 00600BA0: the children array is empty and has no storage afterwards (the window base's
// destructor body leaves +0x148 alone; the object's memory is freed only after the body).
static void __fastcall ContainerDestroyVerify(CUiContainer* self) {
    self->CUiContainer::DestroyContainer();
    bool ok = !self->children_.data_ && self->children_.size_ == 0 && self->children_.capacity_ == 0;
    ei::verify::Check("CUiContainer::DestroyContainer", true, ok, "size %d", self->children_.size_);
}
EI_VERIFY(0x00600BA0, ContainerDestroyVerify, 0, s_unusedLifetime);

// 00600CC0: the side appended (the earlier ones kept in order); the capacity grows to size +
// max(size, 1) when full. Null changes nothing.
static void __fastcall ContainerAddInputVerify(CUiContainer* self, void*, UiInputSide* input) {
    std::vector<UiInputSide*> before(self->inputsBegin_, self->inputsEnd_);
    std::size_t capacity = static_cast<std::size_t>(self->inputsCapacity_ - self->inputsBegin_);
    self->CUiContainer::AddInput(input);
    std::size_t size = before.size();
    bool ok;
    if (!input) {
        ok = static_cast<std::size_t>(self->inputsEnd_ - self->inputsBegin_) == size;
    } else {
        std::size_t expected = size < capacity ? capacity : size + (size > 1 ? size : 1);
        ok = static_cast<std::size_t>(self->inputsEnd_ - self->inputsBegin_) == size + 1 &&
             static_cast<std::size_t>(self->inputsCapacity_ - self->inputsBegin_) == expected &&
             std::equal(before.begin(), before.end(), self->inputsBegin_) && self->inputsBegin_[size] == input;
    }
    ei::verify::Check("CUiContainer::AddInput", true, ok, "input %p size %u capacity %u", input,
                      static_cast<unsigned>(size), static_cast<unsigned>(capacity));
}
EI_VERIFY(0x00600CC0, ContainerAddInputVerify, 0, s_unusedLifetime);
