#pragma once

// The text manager's line reader (004DEB60-004DFFFF): the functions that find a text in the
// resource archives and walk its lines. The manager (CTextManager, item_db_text.h; global pointer
// 007AF788) keeps a cursor (+0x20) into the current text (+0x24, +0x28 bytes). Texts are MBCS:
// a character is one byte, or a lead byte (_ismbblead) and a trail byte; its code is
// (signed lead << 8) + signed trail. Lines end with LF (CR also ends the line's content); a line
// whose first word starts with "##" is a comment. All functions are __thiscall on the manager.

#include "item_db_text.h"
#include "console.h"

// 004DF020. Clears the current text. Returns 1.
BOOL __fastcall TextClose(CTextManager* self);
// 004DF4E0 (__fastcall). The code of the character at `p`.
int __fastcall TextCharCode(const char* p);
// 004DF6E0. Moves the cursor past the next LF. 0 (cursor left at the end) when there is none.
BOOL __fastcall TextSkipLine(CTextManager* self);
// 004DF510. Moves the cursor to the start of the next line that is not a "##" comment. 0 at the end.
BOOL __fastcall TextNextLine(CTextManager* self);
