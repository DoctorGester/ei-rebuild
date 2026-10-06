// Checks for the save/load and options windows' key handlers and the save list's selection
// helpers (src/game/ui_menu_keys.cpp), and the scrolling text's tokeniser (ui_scroll_text.cpp).
//
// The key handlers and ShowSelection play sounds, open message boxes, save or load, so ours run
// alone and the wrappers check the state they must leave (selection, name and caret, pending
// close value, handled flag). IsReservedSave is pure and runs side by side. Tokenize runs the
// original first on a stand-in window (the interface pointer and a copy of the token array) and
// ours on the real one; the tokens each appended are compared, then the stand-in's are freed.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
void* s_unused;

template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - 0x94);
}

bool PendingIs(CUiWindow* w, std::uint32_t value) {
    return w->interface_->pendingFlag_ && w->interface_->pendingValue_ == value;
}

const char* Chars(char* const& field) { return field; }
int Length(char* const& field) { return reinterpret_cast<const int*>(field)[-2]; }
} // namespace

// --- Save/load window ----------------------------------------------------------------------------

static bool(__fastcall* s_isReserved)(CSaveLoadWindow*, void*, int);
static bool __fastcall IsReservedSaveVerify(CSaveLoadWindow* self, void*, int index) {
    bool expected = s_isReserved(self, nullptr, index);
    bool actual = self->CSaveLoadWindow::IsReservedSave(index);
    ei::verify::Check("CSaveLoadWindow::IsReservedSave", expected, actual, "index %d folder %s", index,
                      Chars(self->entriesBegin_[index].folder_));
    return expected;
}
EI_VERIFY(0x00657820, IsReservedSaveVerify, 5, s_isReserved);

static void __fastcall ShowSelectionVerify(CSaveLoadWindow* self, void*) {
    int count = self->EntryCount();
    int expected = self->selected_ < 0 ? 0 : self->selected_;
    if (!(static_cast<unsigned>(expected) < static_cast<unsigned>(count))) expected = count - 1;
    int before = self->selected_;
    self->CSaveLoadWindow::ShowSelection();
    bool ok = self->selected_ == expected;
    if (ok && expected >= 0) ok = !self->nameTyped_ && std::strcmp(Chars(self->name_), Chars(self->entriesBegin_[expected].title_)) == 0;
    ei::verify::Check("CSaveLoadWindow::ShowSelection", true, ok, "selected %d->%d (expected %d, %d entries)", before,
                      self->selected_, expected, count);
}
EI_VERIFY(0x00655430, ShowSelectionVerify, 0, s_unused);

static void __fastcall SaveLoadKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* w = FromInput<CSaveLoadWindow>(input);
    bool sliding = w->sliding_;
    int selected = w->selected_;
    int count = w->EntryCount();
    int cursor = w->cursor_;
    std::string name = Chars(w->name_);
    w->CSaveLoadWindow::OnKeyDown(key, repeat, flags);
    bool unchanged = w->selected_ == selected && w->cursor_ == cursor && name == Chars(w->name_);
    int step = key == 0x21 ? -16 : key == 0x22 ? 16 : key == 0x26 ? -1 : key == 0x28 ? 1 : 0;
    bool ok = true;
    if (sliding) {
        ok = unchanged;
    } else if (key == 0x1B) {
        ok = PendingIs(w, 0x100001) && w->inputHandled_;
    } else if (step != 0) {
        int expected = selected + step < 0 ? 0 : selected + step;
        if (!(static_cast<unsigned>(expected) < static_cast<unsigned>(count))) expected = count - 1;
        ok = w->inputHandled_ && w->selected_ == expected;
    } else if (key == 0x0D) {
        ok = w->inputHandled_;
    } else if (w->isSave_ && (key == 8 || key == 0x2E || (key >= 0x23 && key <= 0x27))) {
        // Edited (the caret within the name; Home / End at its ends), or the base handler took it.
        int length = Length(w->name_);
        bool edited = w->inputHandled_ && w->cursor_ >= 0 && w->cursor_ <= length;
        if (key == 0x24) edited = edited && w->cursor_ == 0;
        if (key == 0x23) edited = edited && w->cursor_ == length;
        ok = edited || unchanged;
    }
    ei::verify::Check("CSaveLoadWindow::OnKeyDown", true, ok, "key %X sliding %d selected %d->%d cursor %d->%d", key,
                      sliding, selected, w->selected_, cursor, w->cursor_);
}
EI_VERIFY(0x006548A0, SaveLoadKeyDownVerify, 0, s_unused);

// 00655050 (typing a save name; no scenario types one): ours only. A load window, Backspace / Tab /
// Enter / Esc and Ctrl leave the name alone; otherwise the name grew by the character at the caret
// (the caret one further) or stayed as it was (too long or too wide), unless it was restarted empty.
static void __fastcall SaveLoadCharVerify(void* input, void*, int key, int repeat, int flags) {
    auto* w = FromInput<CSaveLoadWindow>(input);
    int cursor = w->cursor_;
    std::string name = Chars(w->name_);
    bool fixed = !w->isSave_ || key == 8 || key == 9 || key == 0x0D || key == 0x1B;
    bool ctrl = w->interface_->ctrlDown_;
    w->CSaveLoadWindow::OnChar(key, repeat, flags);
    std::string now = Chars(w->name_);
    bool ok;
    if (fixed) {
        ok = now == name && w->cursor_ == cursor;
    } else {
        bool grew = w->cursor_ >= 1 && static_cast<std::size_t>(w->cursor_) <= now.size() &&
                    now[w->cursor_ - 1] == static_cast<char>(key) && now.size() <= 60;
        bool kept = now == name && w->cursor_ == cursor;
        bool restarted = now.empty() && w->cursor_ == 0;
        ok = grew || kept || restarted || (ctrl && (kept || restarted));
    }
    ei::verify::Check("CSaveLoadWindow::OnChar", true, ok, "key %X cursor %d->%d length %d->%d", key, cursor,
                      w->cursor_, static_cast<int>(name.size()), static_cast<int>(now.size()));
}
EI_VERIFY(0x00655050, SaveLoadCharVerify, 0, s_unused);

// --- Options window --------------------------------------------------------------------------------

static void __fastcall OptionsKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* w = FromInput<COptionsWindow>(input);
    bool sliding = w->sliding_;
    bool waiting = w->waitingForKey_;
    int row = w->selectedRow_;
    // The row Up / Down should land on.
    int target = row;
    if (key == 0x26 || key == 0x28) {
        int dir = key == 0x26 ? -1 : 1;
        for (int i = row + dir; i >= 0 && i < 14; i += dir) {
            if (w->rows_[i].binding_ || w->rows_[i].spin_) {
                target = i;
                break;
            }
        }
    }
    bool enterBinding = key == 0x0D && w->rows_[row].binding_ != nullptr;
    w->COptionsWindow::OnKeyDown(key, repeat, flags);
    bool ok = true;
    if (sliding)
        ok = w->waitingForKey_ == waiting && w->selectedRow_ == row;
    else if (waiting)
        ok = w->inputHandled_ && ((flags & 0x1FF) != 1 || !w->waitingForKey_);
    else if (key == 0x1B)
        ok = w->inputHandled_ && PendingIs(w, 0x110001);
    else if (key == 0x26 || key == 0x28)
        ok = w->inputHandled_ && w->selectedRow_ == target;
    else if (enterBinding)
        ok = w->inputHandled_ && w->waitingForKey_;
    ei::verify::Check("COptionsWindow::OnKeyDown", true, ok, "key %X flags %X sliding %d waiting %d row %d->%d", key,
                      flags, sliding, waiting, row, w->selectedRow_);
}
EI_VERIFY(0x00650E40, OptionsKeyDownVerify, 0, s_unused);

// --- Scrolling text ----------------------------------------------------------------------------------

namespace {
bool SameToken(const UiTextToken* a, const UiTextToken* b) {
    if (!a || !b) return a == b;
    std::uint32_t va, vb;
    std::memcpy(&va, a, 4);
    std::memcpy(&vb, b, 4);
    if (va != vb || a->type_ != b->type_) return false;
    if (a->type_ == 1) {
        auto* fa = static_cast<const UiFormatToken*>(a);
        auto* fb = static_cast<const UiFormatToken*>(b);
        return fa->newline_ == fb->newline_ && fa->font_ == fb->font_ && fa->colour_ == fb->colour_;
    }
    auto* wa = static_cast<const UiWordToken*>(a);
    auto* wb = static_cast<const UiWordToken*>(b);
    return wa->width_ == wb->width_ && std::strcmp(wa->text_, wb->text_) == 0;
}

void FreeToken(UiTextToken* token) {
    if (!token) return;
    if (token->type_ == 2) reinterpret_cast<mfc::CString*>(&static_cast<UiWordToken*>(token)->text_)->~CString();
    gamecrt::operator_delete(token);
}
} // namespace

static void(__fastcall* s_tokenize)(void*, void*, const mfc::CString*);
static void __fastcall TokenizeVerify(CUiScrollText* self, void*, const mfc::CString* text) {
    // The stand-in: the interface (word widths) and a copy of the token array (its count decides
    // a font change's line break).
    alignas(8) static std::uint8_t standIn[sizeof(CUiScrollText)];
    std::memset(standIn, 0, sizeof(standIn));
    auto* scratch = reinterpret_cast<CUiScrollText*>(standIn);
    scratch->interface_ = self->interface_;
    UiTokenArray& own = self->tokens_;
    UiTokenArray& copy = scratch->tokens_;
    copy = own;
    if (own.data_) {
        int capacity = own.maxSize_ > 0 ? own.maxSize_ : 1;
        copy.data_ = static_cast<UiTextToken**>(gamecrt::operator_new(capacity * sizeof(UiTextToken*)));
        std::memcpy(copy.data_, own.data_, own.size_ * sizeof(UiTextToken*));
    }
    int before = own.size_;
    s_tokenize(scratch, nullptr, text);
    self->CUiScrollText::Tokenize(text);
    bool ok = copy.size_ == own.size_;
    int bad = -1;
    for (int i = before; ok && i < own.size_; ++i) {
        if (!SameToken(copy.data_[i], own.data_[i])) {
            ok = false;
            bad = i;
        }
    }
    ei::verify::Check("CUiScrollText::Tokenize", true, ok, "tokens %d: %d vs %d, first difference %d, text \"%.60s\"",
                      before, copy.size_, own.size_, bad, static_cast<const char*>(*text));
    for (int i = before; i < copy.size_; ++i) FreeToken(copy.data_[i]);
    if (copy.data_) gamecrt::operator_delete(copy.data_);
}
EI_VERIFY(0x0065D4B0, TokenizeVerify, 7, s_tokenize);
