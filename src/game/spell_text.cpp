// Spells from text (00689030): "healing{e2;e2}" as the scripts' CastSpellUnit / CastSpellPoint and
// the wands' prototypes (CItemConstructor::Build) write them.
#include "spell.h"

#include "console.h"
#include "ei/hook.h"
#include "item.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_engine.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// The game's messages, passed by address as the original does.
const char* const kNoSpellConstructor = reinterpret_cast<const char*>(0x00795990);
const char* const kCantBegin = reinterpret_cast<const char*>(0x0079595C);   // ... "%s"
const char* const kCantEnd = reinterpret_cast<const char*>(0x0079593C);

// MFC CString members not in lib/mfc.h, called on existing strings.
struct TextOps : mfc::CString {
    int Find(char c) const;                     // 00700E91 Find(TCHAR)
    void TrimLeft();                            // 006FE216 (white space)
    void TrimRight();                           // 006FE11F (white space)
    void TrimRight(char c);                     // 006FE0DD
};
TextOps& Ops(mfc::CString& s) { return static_cast<TextOps&>(s); }

// 004DBE40: splits `text` at `separator` into `out` (__fastcall, separator on the stack).

// The spell and modifier name maps (STLport hash_map<string, char*>), thiscall on the map.
// 004ABD00: operator[](key) (inserts a null record for a missing name); returns the value.
// The spells and modifiers by name (004ABD00 operator[], 004ACC10 find_or_insert of {name, null}).
stlp::NameMap& SpellNameMap() { return *reinterpret_cast<stlp::NameMap*>(0x007C851C); }
stlp::NameMap& ModifierNameMap() { return *reinterpret_cast<stlp::NameMap*>(0x007C853C); }

// The constructor's entries and CSpell::Clone, called through their game addresses.

int Length(const mfc::CString& s) { return console::Length(s); }

} // namespace

int TextOps::Find(char c) const { return reinterpret_cast<const mfc::CString*>(this)->Find(c); }
void TextOps::TrimLeft() { reinterpret_cast<mfc::CString*>(this)->TrimLeft(); }
void TextOps::TrimRight() { reinterpret_cast<mfc::CString*>(this)->TrimRight(); }
void TextOps::TrimRight(char a0) { reinterpret_cast<mfc::CString*>(this)->TrimRight(a0); }

namespace {

// Splits "name{a;b}" into the trimmed name (in `text`) and the modifier names (in `codes`).
void ParseSpellText(mfc::CString& text, console::CStringArray& codes) {
    int brace = Ops(text).Find('{');
    if (brace == -1) {
        Ops(text).TrimLeft();
        Ops(text).TrimRight();
        return;
    }
    mfc::CString inner = Ops(text).Right(Length(text) - brace - 1);
    Ops(inner).TrimLeft();
    Ops(inner).TrimRight();
    Ops(inner).TrimRight('}');
    Ops(inner).TrimRight();
    text::Split(inner, &codes, ';');
    text = text.Left(brace);
    Ops(text).TrimLeft();
    Ops(text).TrimRight();
}

// The modifier record named `name` (find_or_insert with a null record).
char* FindModifier(const char* name) {
    stlp::string key(name);
    return stlp::NamedSlot<char>(ModifierNameMap(), key);
}

} // namespace

CSpell* CSpellManager::CreateSpellFromText(const char* source) {
    mfc::CString text(source);
    console::CStringArray codes;
    ParseSpellText(text, codes);

    CSpellConstructor* constructor = static_cast<CManagerContext*>(context_)->SpellConstructor();
    if (!constructor) return reinterpret_cast<CSpell*>(game::FatalError(kNoSpellConstructor));
    constructor->CallSetManager(this);

    char* prototype;
    {
        stlp::string key(static_cast<const char*>(text));
        prototype = stlp::NamedSlot<char>(SpellNameMap(), key);
    }
    if (!constructor->CallBeginConstruct(prototype))
        return reinterpret_cast<CSpell*>(game::FatalError(kCantBegin, static_cast<const char*>(text)));

    for (int i = 0; i < codes.GetSize(); ++i) {
        const mfc::CString& code = codes[i];
        if (Length(code) == 0) continue;
        constructor->CallAddModifier(FindModifier(code));
    }

    if (!constructor->EndConstruct()) return reinterpret_cast<CSpell*>(game::FatalError(kCantEnd));
    CSpell* spell = constructor->container_->Spell()->Clone(this);
    constructor->CallSetManager(nullptr);
    return spell;
}
EI_REPLACE_METHOD(0x00689030, CSpellManager::CreateSpellFromText);
