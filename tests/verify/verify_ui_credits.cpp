// Checks for the titles window's frame, close and line destructor (ui_credits.cpp). All ours only:
// the frame steps the movie and the script, the close frees the lines.
// - Draw returns 1; the current line stays or moves on by one node; once finished it stays so.
// - OnClose: the line list empty, the display colour back to the saved one, the screen flag set.
// - TitleLine::Destroy: each CString's reference count (the MFC header before the text: refs,
//   length, capacity) drops by one, or stays for the shared empty string (-1); a string with one
//   reference is freed and not read again; the two names sharing a buffer are not checked.
// Reached by ui_main_menu (region 4 opens the titles, a click closes them).
#include "verify.h"

#include "../../src/game/originals_render.h"
#include "../../src/game/ui_screens.h"

#include <cstdint>

namespace {
void* s_unused;

int Refs(const char* text) { return reinterpret_cast<const int*>(text)[-3]; }
} // namespace

// CCreditsScreen::Draw (0062BD80).
static BOOL __fastcall CreditsDrawVerify(CCreditsScreen* self, void*, int pass) {
    auto* line = self->currentLine_;
    auto* next = line == self->lines_.End() ? line : line->next;
    bool finished = self->finished_;
    BOOL result = self->CCreditsScreen::Draw(pass);
    ei::verify::Check("CCreditsScreen::Draw", true,
                      result == 1 && (self->currentLine_ == line || self->currentLine_ == next) &&
                          (!finished || self->finished_),
                      "result %d finished %d -> %d", result, finished ? 1 : 0, self->finished_ ? 1 : 0);
    return result;
}
EI_VERIFY(0x0062BD80, CreditsDrawVerify, 0, s_unused);

// CCreditsScreen::OnClose (0062BD00).
static void __fastcall CreditsCloseVerify(CCreditsScreen* self, void*) {
    self->CCreditsScreen::OnClose();
    std::uint32_t colour = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(DisplayObject()) + 0x14);
    ei::verify::Check("CCreditsScreen::OnClose", true,
                      self->lines_.First() == self->lines_.End() &&
                          colour == static_cast<std::uint32_t>(self->savedDisplay_) && InterfaceHasScreen(),
                      "lines empty %d colour %08X / %08X", self->lines_.First() == self->lines_.End() ? 1 : 0, colour,
                      static_cast<std::uint32_t>(self->savedDisplay_));
}
EI_VERIFY(0x0062BD00, CreditsCloseVerify, 0, s_unused);

// TitleLine::Destroy (0062BCB0).
static void __fastcall TitleLineDestroyVerify(TitleLine* self, void*) {
    const char* style = self->style;
    const char* text = self->text;
    int styleRefs = Refs(style);
    int textRefs = Refs(text);
    self->TitleLine::Destroy();
    if (style == text) return;
    if (styleRefs != 1)
        ei::verify::Check("TitleLine::Destroy", styleRefs < 0 ? styleRefs : styleRefs - 1, Refs(style), "style refs");
    if (textRefs != 1)
        ei::verify::Check("TitleLine::Destroy", textRefs < 0 ? textRefs : textRefs - 1, Refs(text), "text refs");
}
EI_VERIFY(0x0062BCB0, TitleLineDestroyVerify, 0, s_unused);
