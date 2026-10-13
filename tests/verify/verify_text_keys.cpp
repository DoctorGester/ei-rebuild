// Checks for the text manager's leftovers in text_sources.cpp and the script loader's sub-call
// reset in script_blocks.cpp (A-S11). Main thread.
//  - Side by side: the key listing 004DEC40 / 004DEFA0 (the original into a scratch array, ours
//    into the caller's; the names compared by content; 004DEC40 in the vanilla launch only). The
//    interface asks for "pers *" when it builds the person texts (00607720) and 00635250; no
//    scenario is known to reach them. The text manager's Create 004DED20 (the client world's
//    set-up, every start: `newgame`, `world_load`): it only sets the C runtime's code page from the
//    locale, so running it twice is the same as once.
//  - Ours only: the text lookup 004DEBC0 (GetFile counts the file's users; its callers in
//    GetItemTexts are rewritten, so only a side-by-side run of that original reaches it) and the
//    script loader's sub-call reset 004D0AF0 (the original LoadFunction: a Rebirth launch's script
//    loads).
#include "verify.h"

#include "../../src/game/item_db_text.h"
#include "../../src/game/script.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>

// script_blocks.cpp (no header declares the shim).
void __fastcall ScriptSubCallClear(ScriptSubCall* call);

namespace {
void* s_unused;

bool SameNames(const mfc::CStringArray& a, const mfc::CStringArray& b) {
    if (a.GetSize() != b.GetSize()) return false;
    for (int i = 0; i < a.GetSize(); ++i) {
        if (std::strcmp(a[i], b[i]) != 0) return false;
    }
    return true;
}

void* __fastcall FindVerify(TextSourceList* list, void*, const char* key, std::uint32_t* size) {
    void* result = TextSourcesFind(list, nullptr, key, size);
    ei::verify::Check("TextSourcesFind (004DEBC0)", true, true, "%s: %p", key, result);
    return result;
}

// 004DEC40: PUSH -1 (2), PUSH 0071B148 (5).
void(__fastcall* s_listKeys)(TextSourceList*, void*, mfc::CStringArray*, const char*);
void __fastcall ListKeysVerify(TextSourceList* list, void*, mfc::CStringArray* out, const char* pattern) {
    mfc::CStringArray expected;
    expected.Construct();
    s_listKeys(list, nullptr, &expected, pattern);
    TextSourcesListKeys(list, nullptr, out, pattern);
    ei::verify::Check("TextSourcesListKeys (004DEC40)", true, SameNames(expected, *out), "%s: %d / %d names",
                      pattern ? pattern : "(null)", expected.GetSize(), out->GetSize());
    expected.Destruct();
}

// 004DEFA0: MOV EAX,[ESP+8] (4), MOV EDX,[ESP+4] (4).
void(__fastcall* s_managerListKeys)(CTextManager*, void*, mfc::CStringArray*, const char*);
void __fastcall ManagerListKeysVerify(CTextManager* self, void*, mfc::CStringArray* out, const char* pattern) {
    mfc::CStringArray expected;
    expected.Construct();
    s_managerListKeys(self, nullptr, &expected, pattern);
    self->ListKeys(out, pattern);
    ei::verify::Check("CTextManager::ListKeys (004DEFA0)", true, SameNames(expected, *out), "%s: %d / %d names",
                      pattern ? pattern : "(null)", expected.GetSize(), out->GetSize());
    expected.Destruct();
}

// 004DED20: MOV EAX,[0079BE94] (5).
int(__fastcall* s_create)(CTextManager*);
int __fastcall CreateVerify(CTextManager* self) {
    int expected = s_create(self);
    int result = self->Create();
    ei::verify::Check("CTextManager::Create (004DED20)", expected, result, "");
    return result;
}

void __fastcall SubCallClearVerify(ScriptSubCall* call) {
    ScriptSubCallClear(call);
    ei::verify::Check("ScriptSubCall clear (004D0AF0)", true,
                      call->type == 0 && call->argIndex == 0 && call->cmd->id == 0, "");
}

} // namespace

EI_VERIFY(0x004DEBC0, FindVerify, 0, s_unused);
EI_VERIFY(0x004DEC40, ListKeysVerify, 7, s_listKeys);
EI_VERIFY(0x004DEFA0, ManagerListKeysVerify, 8, s_managerListKeys);
EI_VERIFY(0x004DED20, CreateVerify, 5, s_create);
EI_VERIFY(0x004D0AF0, SubCallClearVerify, 0, s_unused);
