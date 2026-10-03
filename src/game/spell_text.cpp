// Spells from text (00689030): "healing{e2;e2}" as the scripts' CastSpellUnit / CastSpellPoint and
// the wands' prototypes (CItemConstructor::Build) write them.
#include "spell.h"

#include "console.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

constexpr int kContextSlotSpellConstructor = 17;   // +0x44

// The game's messages, passed by address as the original does.
const char* const kNoSpellConstructor = reinterpret_cast<const char*>(0x00795990);
const char* const kCantBegin = reinterpret_cast<const char*>(0x0079595C);   // ... "%s"
const char* const kCantEnd = reinterpret_cast<const char*>(0x0079593C);

// MFC CString members not in lib/mfc.h, called on existing strings.
struct TextOps : mfc::CString {
    int Find(char c) const;                     // 00700E91 Find(TCHAR)
    mfc::CString Left(int count) const;         // 006FDAE9
    mfc::CString Right(int count) const;        // 006FDA6D
    void TrimRight();                           // 006FE216 (white space)
    void TrimLeft();                            // 006FE11F (white space)
    void TrimRight(char c);                     // 006FE0DD
};
TextOps& Ops(mfc::CString& s) { return static_cast<TextOps&>(s); }

// 004DBE40: splits `text` at `separator` into `out` (__fastcall, separator on the stack).
__declspec(naked) void __fastcall Split(const char*, console::CStringArray*, char) EI_ORIGINAL(0x004DBE40)

// The spell and modifier name maps (STLport hash_map<string, char*>), thiscall on the map.
// 004ABD00: operator[](key) (inserts a null record for a missing name); returns the value.
__declspec(naked) char** __fastcall SpellMapAt(void*, void*, const stlp::string*) EI_ORIGINAL(0x004ABD00)
// 004ACC10: find_or_insert(pair); returns the element.
struct NameRecordPair {
    stlp::string name;
    char* record;
};
__declspec(naked) NameRecordPair* __fastcall ModifierMapFindOrInsert(void*, void*, const NameRecordPair*)
    EI_ORIGINAL(0x004ACC10)
void* const kSpellNameMap = reinterpret_cast<void*>(0x007C851C);
void* const kModifierNameMap = reinterpret_cast<void*>(0x007C853C);

// The constructor's entries and CSpell::Clone, called through their game addresses.
__declspec(naked) CSpell* __fastcall SpellClone(CSpell*, void*, CSpellManager*) EI_ORIGINAL(0x0068B410)

int Length(const mfc::CString& s) { return console::Length(s); }

} // namespace

__declspec(naked) int TextOps::Find(char) const EI_ORIGINAL(0x00700E91)
__declspec(naked) mfc::CString TextOps::Left(int) const EI_ORIGINAL(0x006FDAE9)
__declspec(naked) mfc::CString TextOps::Right(int) const EI_ORIGINAL(0x006FDA6D)
__declspec(naked) void TextOps::TrimRight() EI_ORIGINAL(0x006FE216)
__declspec(naked) void TextOps::TrimLeft() EI_ORIGINAL(0x006FE11F)
__declspec(naked) void TextOps::TrimRight(char) EI_ORIGINAL(0x006FE0DD)

namespace {

// Splits "name{a;b}" into the trimmed name (in `text`) and the modifier names (in `codes`).
void ParseSpellText(mfc::CString& text, console::CStringArray& codes) {
    int brace = Ops(text).Find('{');
    if (brace == -1) {
        Ops(text).TrimRight();
        Ops(text).TrimLeft();
        return;
    }
    mfc::CString inner = Ops(text).Right(Length(text) - brace - 1);
    Ops(inner).TrimRight();
    Ops(inner).TrimLeft();
    Ops(inner).TrimRight('}');
    Ops(inner).TrimLeft();
    Split(inner, &codes, ';');
    text = Ops(text).Left(brace);
    Ops(text).TrimRight();
    Ops(text).TrimLeft();
}

// The modifier record named `name` (find_or_insert with a null record).
char* FindModifier(const char* name) {
    stlp::string key(name);
    NameRecordPair entry{key, nullptr};
    char* record = ModifierMapFindOrInsert(kModifierNameMap, nullptr, &entry)->record;
    return record;
}

} // namespace

CSpell* CSpellManager::CreateSpellFromText(const char* source) {
    mfc::CString text(source);
    console::CStringArray codes;
    ParseSpellText(text, codes);

    auto* constructor = ei::VCall<CSpellConstructor*>(context_, kContextSlotSpellConstructor);
    if (!constructor) return reinterpret_cast<CSpell*>(game::FatalError(kNoSpellConstructor));
    constructor->CallSetManager(this);

    char* prototype;
    {
        stlp::string key(static_cast<const char*>(text));
        prototype = *SpellMapAt(kSpellNameMap, nullptr, &key);
    }
    if (!constructor->CallBeginConstruct(prototype))
        return reinterpret_cast<CSpell*>(game::FatalError(kCantBegin, static_cast<const char*>(text)));

    for (int i = 0; i < codes.GetSize(); ++i) {
        const mfc::CString& code = codes[i];
        if (Length(code) == 0) continue;
        constructor->CallAddModifier(FindModifier(code));
    }

    if (!constructor->CallEndConstruct()) return reinterpret_cast<CSpell*>(game::FatalError(kCantEnd));
    CSpell* spell = SpellClone(constructor->container_->Spell(), nullptr, this);
    constructor->CallSetManager(nullptr);
    return spell;
}
EI_REPLACE_METHOD(0x00689030, CSpellManager::CreateSpellFromText);
