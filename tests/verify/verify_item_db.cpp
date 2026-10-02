// Differential checks for the item texts (item_db_text.cpp). The text functions change the text
// manager's cursor (+0x20..+0x28) and write CStrings: the original runs first on scratch copies
// of the outputs, its final cursor state is recorded and the state restored, then ours runs on
// the real outputs; strings, state and results are compared.
//
// Side effect of verifying: every lookup runs twice, so CMappedResource::GetFile raises the
// reference count of a found text file twice (the text manager never closes them either way).
#include "verify.h"

#include "../../src/game/item_db_text.h"

#include <cstring>

namespace {

struct CursorState {
    const char* cursor;
    const char* text;
    std::uint32_t length;
};

CursorState Save(const CTextManager* self) { return {self->cursor_, self->text_, self->length_}; }

void Restore(CTextManager* self, const CursorState& state) {
    self->cursor_ = state.cursor;
    self->text_ = state.text;
    self->length_ = state.length;
}

bool SameState(const CursorState& a, const CursorState& b) {
    return a.cursor == b.cursor && a.text == b.text && a.length == b.length;
}

bool SameText(const mfc::CString& a, const mfc::CString& b) {
    return std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
}

} // namespace

// 004E0000: MOV EAX,[ESP+4]; MOV EDX,[ECX+0x24] (7 bytes).
static BOOL(__fastcall* s_isPastEnd)(CTextManager*, void*, const char*);
static BOOL __fastcall IsPastEndVerify(CTextManager* self, void*, const char* p) {
    BOOL expected = s_isPastEnd(self, nullptr, p);
    ei::verify::Check("CTextManager::IsPastEnd", expected, self->IsPastEnd(p), "p %p text %p length %u", p,
                      self->text_, self->length_);
    return expected;
}
EI_VERIFY(0x004E0000, IsPastEndVerify, 7, s_isPastEnd);

// 004E0020: MOV EAX,FS:[0] (6 bytes). Callers may pass a CString's own text as `name` together
// with that CString; the scratch copy shares the buffer, so the original sees the same aliasing.
static BOOL(__fastcall* s_getString)(CTextManager*, void*, const char*, mfc::CString*);
static BOOL __fastcall GetStringVerify(CTextManager* self, void*, const char* name, mfc::CString* out) {
    CursorState before = Save(self);
    mfc::CString scratch(*out);
    BOOL expected = s_getString(self, nullptr, name, &scratch);
    CursorState after = Save(self);
    Restore(self, before);
    BOOL actual = self->GetString(name, out);
    bool same = expected == actual && SameText(scratch, *out) && SameState(after, Save(self));
    ei::verify::Check("CTextManager::GetString", true, same, "%s: %d '%.60s' vs %d '%.60s'", name, expected,
                      static_cast<const char*>(scratch), actual, static_cast<const char*>(*out));
    return expected;
}
EI_VERIFY(0x004E0020, GetStringVerify, 6, s_getString);

// 004E0150: PUSH -1; PUSH 0x71B3BB (7 bytes).
static BOOL(__fastcall* s_getItemTexts)(CTextManager*, void*, CItem*, mfc::CString*, mfc::CString*, mfc::CString*);
static BOOL __fastcall GetItemTextsVerify(CTextManager* self, void*, CItem* item, mfc::CString* name,
                                          mfc::CString* description, mfc::CString* kind) {
    CursorState before = Save(self);
    mfc::CString scratchName(*name), scratchDescription(*description), scratchKind(*kind);
    BOOL expected = s_getItemTexts(self, nullptr, item, &scratchName, &scratchDescription, &scratchKind);
    CursorState after = Save(self);
    Restore(self, before);
    BOOL actual = self->GetItemTexts(item, name, description, kind);
    bool same = expected == actual && SameText(scratchName, *name) && SameText(scratchDescription, *description) &&
                SameText(scratchKind, *kind) && SameState(after, Save(self));
    ei::verify::Check("CTextManager::GetItemTexts", true, same, "kind %X id %08X: '%.40s'/'%.40s' vs '%.40s'/'%.40s'",
                      item->type_, item->unknown_18, static_cast<const char*>(scratchName),
                      static_cast<const char*>(scratchKind), static_cast<const char*>(*name),
                      static_cast<const char*>(*kind));
    return expected;
}
EI_VERIFY(0x004E0150, GetItemTextsVerify, 7, s_getItemTexts);

// 004E13E0: PUSH -1; PUSH 0x71B3E8 (7 bytes).
static BOOL(__fastcall* s_formatItemName)(CTextManager*, void*, CItem*, mfc::CString*);
static BOOL __fastcall FormatItemNameVerify(CTextManager* self, void*, CItem* item, mfc::CString* out) {
    CursorState before = Save(self);
    mfc::CString scratch(*out);
    BOOL expected = s_formatItemName(self, nullptr, item, &scratch);
    CursorState after = Save(self);
    Restore(self, before);
    BOOL actual = self->FormatItemName(item, out);
    bool same = expected == actual && SameText(scratch, *out) && SameState(after, Save(self));
    ei::verify::Check("CTextManager::FormatItemName", true, same, "kind %X id %08X count %u: '%.60s' vs '%.60s'",
                      item->type_, item->unknown_18, item->unknown_48, static_cast<const char*>(scratch),
                      static_cast<const char*>(*out));
    return expected;
}
EI_VERIFY(0x004E13E0, FormatItemNameVerify, 7, s_formatItemName);

// 004E1530 and 004E1540 only write *out: the original into a scratch slot, ours into *out.
// 004E1530: MOV EAX,[ECX]; MOV ECX,[EAX]; MOV EAX,[ESP+4] (8 bytes).
static void(__fastcall* s_listBegin)(TextSourceList*, void*, CTextManager::SourceNode**);
static void __fastcall ListBeginVerify(TextSourceList* self, void*, CTextManager::SourceNode** out) {
    CTextManager::SourceNode* expected = nullptr;
    s_listBegin(self, nullptr, &expected);
    self->Begin(out);
    ei::verify::Check("TextSourceList::Begin", expected, *out, "list %p", self->sentinel);
}
EI_VERIFY(0x004E1530, ListBeginVerify, 8, s_listBegin);

// 004E1540: MOV EAX,[ESP+4]; MOV ECX,[ECX] (6 bytes).
static void(__fastcall* s_iteratorCopy)(TextSourceIterator*, void*, CTextManager::SourceNode**);
static void __fastcall IteratorCopyVerify(TextSourceIterator* self, void*, CTextManager::SourceNode** out) {
    CTextManager::SourceNode* expected = nullptr;
    s_iteratorCopy(self, nullptr, &expected);
    self->CopyTo(out);
    ei::verify::Check("TextSourceIterator::CopyTo", expected, *out, "node %p", self->node);
}
EI_VERIFY(0x004E1540, IteratorCopyVerify, 6, s_iteratorCopy);

// 004E1550 and 004E1590 change the list and the node allocator: only ours runs, then the links
// are checked.
namespace {
void* s_unusedList;
}

static TextSourceList::Node** __fastcall ListEraseVerify(TextSourceList* self, void*, TextSourceList::Node** out,
                                                         TextSourceList::Node* pos) {
    TextSourceList::Node* next = pos->next;
    TextSourceList::Node* prev = pos->prev;
    TextSourceList::Node** result = self->Erase(out, pos);
    bool ok = result == out && *out == next && prev->next == next && next->prev == prev &&
              *reinterpret_cast<TextSourceList::Node**>(0x0079B22C) == pos;
    ei::verify::Check("TextSourceList::Erase", true, ok, "list %p node %p", self->sentinel, pos);
    return result;
}
EI_VERIFY(0x004E1550, ListEraseVerify, 0, s_unusedList);

static TextSourceList::Node** __fastcall ListInsertVerify(TextSourceList* self, void*, TextSourceList::Node** out,
                                                          TextSourceList::Node* pos, const void* value) {
    TextSourceList::Node* prev = pos->prev;
    TextSourceList::Node** result = self->Insert(out, pos, value);
    TextSourceList::Node* node = *out;
    bool ok = result == out && node && node->next == pos && node->prev == prev && prev->next == node &&
              pos->prev == node;
    ei::verify::Check("TextSourceList::Insert", true, ok, "list %p before %p", self->sentinel, pos);
    return result;
}
EI_VERIFY(0x004E1590, ListInsertVerify, 0, s_unusedList);
