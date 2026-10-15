// Differential checks for the item texts (item_db_text.cpp). The text functions change the text
// manager's cursor (+0x20..+0x28) and write CStrings: ours runs first on the real outputs (the old
// texts kept in our memory), its final cursor state is recorded and the state restored, then the
// original runs on scratch strings made from the kept texts (freed again; SV2h1: ours allocates
// first); strings, state and results are compared and ours' state put back.
//
// Side effect of verifying: every lookup runs twice, so CMappedResource::GetFile raises the
// reference count of a found text file twice (the text manager never closes them either way).
#include "verify.h"

#include "kept_strings.h"

#include "../../src/game/item_db_text.h"

#include <cstring>
#include <string>

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
// with that CString: the original then gets the same place in its scratch copy.
static BOOL(__fastcall* s_getString)(CTextManager*, void*, const char*, mfc::CString*);
static BOOL __fastcall GetStringVerify(CTextManager* self, void*, const char* name, mfc::CString* out) {
    CursorState before = Save(self);
    const std::string kept = ei::verify::KeepText(*out);
    const std::string nameText = name ? name : "";
    const char* old = *out;
    const bool aliased = name && name >= old && name <= old + kept.size();
    const std::size_t offset = aliased ? static_cast<std::size_t>(name - old) : 0;
    BOOL actual = self->GetString(name, out);
    CursorState mine = Save(self);
    Restore(self, before);
    mfc::CString scratch(kept.c_str());
    BOOL expected = s_getString(self, nullptr, aliased ? static_cast<const char*>(scratch) + offset : name, &scratch);
    bool same = expected == actual && SameText(scratch, *out) && SameState(mine, Save(self));
    Restore(self, mine);
    ei::verify::Check("CTextManager::GetString", true, same, "%s: %d '%.60s' vs %d '%.60s'", nameText.c_str(), expected,
                      static_cast<const char*>(scratch), actual, static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x004E0020, GetStringVerify, 6, s_getString);

// 004E0150: PUSH -1; PUSH 0x71B3BB (7 bytes).
static BOOL(__fastcall* s_getItemTexts)(CTextManager*, void*, CItem*, mfc::CString*, mfc::CString*, mfc::CString*);
static BOOL __fastcall GetItemTextsVerify(CTextManager* self, void*, CItem* item, mfc::CString* name,
                                          mfc::CString* description, mfc::CString* kind) {
    CursorState before = Save(self);
    // Some callers pass the same CString twice (0063CEE0 and 0063E820 use one string for the
    // description and the kind, so the kind text overwrites the description): the scratch
    // outputs must alias exactly where the real ones do.
    mfc::CString* outputs[3] = {name, description, kind};
    int alias[3];
    std::string kept[3];
    for (int i = 0; i < 3; ++i) {
        alias[i] = i;
        for (int j = 0; j < i; ++j)
            if (outputs[j] == outputs[i]) {
                alias[i] = j;
                break;
            }
        kept[i] = ei::verify::KeepText(*outputs[i]);
    }
    BOOL actual = self->GetItemTexts(item, name, description, kind);
    CursorState mine = Save(self);
    Restore(self, before);
    mfc::CString scratch[3];
    for (int i = 0; i < 3; ++i)
        if (alias[i] == i) scratch[i] = kept[i].c_str();
    mfc::CString& scratchName = scratch[alias[0]];
    mfc::CString& scratchDescription = scratch[alias[1]];
    mfc::CString& scratchKind = scratch[alias[2]];
    BOOL expected = s_getItemTexts(self, nullptr, item, &scratchName, &scratchDescription, &scratchKind);
    bool sameName = SameText(scratchName, *name), sameDescription = SameText(scratchDescription, *description);
    bool sameKind = SameText(scratchKind, *kind), sameState = SameState(mine, Save(self));
    Restore(self, mine);
    bool same = expected == actual && sameName && sameDescription && sameKind && sameState;
    ei::verify::Check("CTextManager::GetItemTexts", true, same,
                      "kind %X id %08X: result %d/%d name %d description %d kind %d state %d; description '%.60s' vs '%.60s'",
                      item->type_, item->prototypeId_, expected, actual, sameName, sameDescription, sameKind, sameState,
                      static_cast<const char*>(scratchDescription), static_cast<const char*>(*description));
    return actual;
}
EI_VERIFY(0x004E0150, GetItemTextsVerify, 7, s_getItemTexts);

// 004E13E0: PUSH -1; PUSH 0x71B3E8 (7 bytes).
static BOOL(__fastcall* s_formatItemName)(CTextManager*, void*, CItem*, mfc::CString*);
static BOOL __fastcall FormatItemNameVerify(CTextManager* self, void*, CItem* item, mfc::CString* out) {
    CursorState before = Save(self);
    const std::string kept = ei::verify::KeepText(*out);
    BOOL actual = self->FormatItemName(item, out);
    CursorState mine = Save(self);
    Restore(self, before);
    mfc::CString scratch(kept.c_str());
    BOOL expected = s_formatItemName(self, nullptr, item, &scratch);
    bool same = expected == actual && SameText(scratch, *out) && SameState(mine, Save(self));
    Restore(self, mine);
    ei::verify::Check("CTextManager::FormatItemName", true, same, "kind %X id %08X count %u: '%.60s' vs '%.60s'",
                      item->type_, item->prototypeId_, item->count_, static_cast<const char*>(scratch),
                      static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x004E13E0, FormatItemNameVerify, 7, s_formatItemName);

// 004E1530 and 004E1540 only write *out: the original into a scratch slot, ours into *out.
// 004E1530: MOV EAX,[ECX]; MOV ECX,[EAX]; MOV EAX,[ESP+4] (8 bytes).
static void(__fastcall* s_listBegin)(TextSourceList*, void*, CTextManager::SourceNode**);
static void __fastcall ListBeginVerify(TextSourceList* self, void*, CTextManager::SourceNode** out) {
    CTextManager::SourceNode* expected = nullptr;
    s_listBegin(self, nullptr, &expected);
    TextSourcesBegin(self, nullptr, out);
    ei::verify::Check("TextSourcesBegin", expected, *out, "list %p", self->head);
}
EI_VERIFY(0x004E1530, ListBeginVerify, 8, s_listBegin);
// A-W12: byte-identical copies in the world range, the same way.
#define EI_BEGIN_COPY(addr)                                                                          \
    static void(__fastcall* s_listBegin_##addr)(TextSourceList*, void*, CTextManager::SourceNode**);   \
    static void __fastcall ListBeginVerify_##addr(TextSourceList* self, void*, CTextManager::SourceNode** out) { \
        CTextManager::SourceNode* expected = nullptr;                                                 \
        s_listBegin_##addr(self, nullptr, &expected);                                                 \
        TextSourcesBegin(self, nullptr, out);                                                         \
        ei::verify::Check("TextSourcesBegin@" #addr, expected, *out, "list %p", self->head);          \
    }                                                                                                 \
    EI_VERIFY(0x##addr, ListBeginVerify_##addr, 8, s_listBegin_##addr);
EI_BEGIN_COPY(00560030)
EI_BEGIN_COPY(00560040)
EI_BEGIN_COPY(0056DF90)
EI_BEGIN_COPY(0056DFE0)

// 004E1540: MOV EAX,[ESP+4]; MOV ECX,[ECX] (6 bytes).
static void(__fastcall* s_iteratorCopy)(TextSourceIterator*, void*, CTextManager::SourceNode**);
static CTextManager::SourceNode** __fastcall IteratorCopyVerify(TextSourceIterator* self, void*, CTextManager::SourceNode** out) {
    CTextManager::SourceNode* expected = nullptr;
    s_iteratorCopy(self, nullptr, &expected);
    CTextManager::SourceNode** result = self->CopyTo(out);
    ei::verify::Check("TextSourceIterator::CopyTo", expected, *out, "node %p", self->node);
    return result;
}
EI_VERIFY(0x004E1540, IteratorCopyVerify, 6, s_iteratorCopy);

// The interface's byte-identical copies (A-I15), side by side the same way.
#define EI_ITERATOR_COPY(addr)                                                                       \
    static void(__fastcall* s_iteratorCopy_##addr)(TextSourceIterator*, void*, CTextManager::SourceNode**); \
    static CTextManager::SourceNode** __fastcall IteratorCopyVerify_##addr(TextSourceIterator* self, void*, \
                                                     CTextManager::SourceNode** out) {               \
        CTextManager::SourceNode* expected = nullptr;                                                \
        s_iteratorCopy_##addr(self, nullptr, &expected);                                             \
        CTextManager::SourceNode** result = self->CopyTo(out);                                       \
        ei::verify::Check("TextSourceIterator::CopyTo@" #addr, expected, *out, "node %p", self->node); \
        return result;                                                                               \
    }
EI_ITERATOR_COPY(005E2470) EI_ITERATOR_COPY(005F97D0) EI_ITERATOR_COPY(00609A30) EI_ITERATOR_COPY(00609A70)
EI_ITERATOR_COPY(0062C080)
EI_ITERATOR_COPY(00539210)
EI_ITERATOR_COPY(00560050) EI_ITERATOR_COPY(0056DEC0) EI_ITERATOR_COPY(0056DFA0) EI_ITERATOR_COPY(0056DFF0)
EI_VERIFY(0x005E2470, IteratorCopyVerify_005E2470, 6, s_iteratorCopy_005E2470);
EI_VERIFY(0x00539210, IteratorCopyVerify_00539210, 6, s_iteratorCopy_00539210);   // A-U11
EI_VERIFY(0x00560050, IteratorCopyVerify_00560050, 6, s_iteratorCopy_00560050);   // A-W12
EI_VERIFY(0x0056DEC0, IteratorCopyVerify_0056DEC0, 6, s_iteratorCopy_0056DEC0);
EI_VERIFY(0x0056DFA0, IteratorCopyVerify_0056DFA0, 6, s_iteratorCopy_0056DFA0);
EI_VERIFY(0x0056DFF0, IteratorCopyVerify_0056DFF0, 6, s_iteratorCopy_0056DFF0);
EI_VERIFY(0x005F97D0, IteratorCopyVerify_005F97D0, 6, s_iteratorCopy_005F97D0);
EI_VERIFY(0x00609A30, IteratorCopyVerify_00609A30, 6, s_iteratorCopy_00609A30);
EI_VERIFY(0x00609A70, IteratorCopyVerify_00609A70, 6, s_iteratorCopy_00609A70);
EI_VERIFY(0x0062C080, IteratorCopyVerify_0062C080, 6, s_iteratorCopy_0062C080);

// 004E1550 and 004E1590 change the list and the node allocator: only ours runs, then the links
// are checked.
namespace {
void* s_unusedList;
}

static TextSourceList::Node** __fastcall ListEraseVerify(TextSourceList* self, void*, TextSourceList::Node** out,
                                                         TextSourceList::Node* pos) {
    TextSourceList::Node* next = pos->next;
    TextSourceList::Node* prev = pos->prev;
    TextSourceList::Node** result = TextSourcesErase(self, nullptr, out, pos);
    bool ok = result == out && *out == next && prev->next == next && next->prev == prev &&
              *reinterpret_cast<TextSourceList::Node**>(0x0079B22C) == pos;
    ei::verify::Check("TextSourcesErase", true, ok, "list %p node %p", self->head, pos);
    return result;
}
EI_VERIFY(0x004E1550, ListEraseVerify, 0, s_unusedList);

static TextSourceList::Node** __fastcall ListInsertVerify(TextSourceList* self, void*, TextSourceList::Node** out,
                                                          TextSourceList::Node* pos, const TextSource* value) {
    TextSourceList::Node* prev = pos->prev;
    TextSourceList::Node** result = TextSourcesInsert(self, nullptr, out, pos, value);
    TextSourceList::Node* node = *out;
    bool ok = result == out && node && node->next == pos && node->prev == prev && prev->next == node &&
              pos->prev == node;
    ei::verify::Check("TextSourcesInsert", true, ok, "list %p before %p", self->head, pos);
    return result;
}
EI_VERIFY(0x004E1590, ListInsertVerify, 0, s_unusedList);
