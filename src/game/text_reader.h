#pragma once

// The text manager's line reader: the functions that find a text in the
// resource archives and walk its lines. The manager (CTextManager, item_db_text.h; a global pointer)
// keeps a cursor into the current text (+0x24, +0x28 bytes). Texts are MBCS:
// a character is one byte, or a lead byte (_ismbblead) and a trail byte; its code is
// (signed lead << 8) + signed trail. Lines end with LF (CR also ends the line's content); a line
// whose first word starts with "##" is a comment. All functions are __thiscall on the manager.

#include "item_db_text.h"

// Clears the current text. Returns 1.
BOOL TextClose(CTextManager* self);
// Moves the cursor past the next LF. 0 (cursor left at the end) when there is none.
BOOL TextSkipLine(CTextManager* self);
// Moves the cursor to the start of the next line that is not a "##" comment. 0 at the end.
BOOL TextNextLine(CTextManager* self);
