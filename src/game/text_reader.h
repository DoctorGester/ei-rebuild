#pragma once

// The text manager's line reader (004DEB60-004DFFFF): the functions that find a text in the
// resource archives and walk its lines. The manager (CTextManager, item_db_text.h; global pointer
// 007AF788) keeps a cursor (+0x20) into the current text (+0x24, +0x28 bytes). Texts are MBCS:
// a character is one byte, or a lead byte (_ismbblead) and a trail byte; its code is
// (signed lead << 8) + signed trail. Lines end with LF (CR also ends the line's content); a line
// whose first word starts with "##" is a comment. All functions are __thiscall on the manager.

#include "item_db_text.h"
#include "console.h"

// 004DEFC0. Makes the text `key` current (the first archive whose GetFile knows it), cursor at its
// start. True when found and not empty.
BOOL __fastcall TextOpen(CTextManager* self, void* edx, const char* key);
// 004DF020. Clears the current text. Returns 1.
BOOL __fastcall TextClose(CTextManager* self);
// 004DF4E0 (__fastcall). The code of the character at `p`.
int __fastcall TextCharCode(const char* p);
// 004DF6E0. Moves the cursor past the next LF. 0 (cursor left at the end) when there is none.
BOOL __fastcall TextSkipLine(CTextManager* self);
// 004DF510. Moves the cursor to the start of the next line that is not a "##" comment. 0 at the end.
BOOL __fastcall TextNextLine(CTextManager* self);
// 004DF8D0. The rest of the current line (up to CR or LF) into `out`; with `advance` the cursor
// then moves to the next line that is not a comment. 0 when the cursor is at the end.
BOOL __fastcall TextReadLine(CTextManager* self, void* edx, mfc::CString* out, BOOL advance);
// 004DFB80. The text up to the next '#' into `out`; with `advance` the cursor moves there.
BOOL __fastcall TextReadToHash(CTextManager* self, void* edx, mfc::CString* out, BOOL advance);
// 004DFCB0. The current line's words (separated by spaces and tabs; a word still open when the
// text ends is dropped) into `words`; with `advance` the cursor moves to the next line that is not
// a comment. 0 when the cursor is at the end.
BOOL __fastcall TextSplit(CTextManager* self, void* edx, console::CStringArray* words, BOOL advance);
// 004DF030. Moves the cursor to the line that starts with `key`: null finds the next "#..." header
// (not a "##" comment); one word compares the line's first word; several words (split at spaces)
// compare the line's words, lower-cased, with them. 1 when found, else 0 (cursor at the end).
BOOL __fastcall TextFindLine(CTextManager* self, void* edx, const char* key);
