// Spells from text (00689030): "healing{e2;e2}" as the scripts' CastSpellUnit / CastSpellPoint and
// the wands' prototypes (CItemConstructor::Build) write them.
#include "spell.h"

#include "console.h"
#include "ei/hook.h"
#include "item.h"
#include "item_db.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "game_data.h"

namespace {

// The game's messages, passed by address as the original does.
const char* const kNoSpellConstructor = "No valid spell constructor\n";
const char* const kCantBegin = "Can't begin spell construction with prototype \"%s\"\n";
const char* const kCantEnd = "Can't end spell construction\n";

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
itemdb::SpellTable::CodeIndex& SpellNameMap() { return itemdb::TheSpellDatabase().spells.byCode; }
itemdb::SpellModifierTable::CodeIndex& ModifierNameMap() { return itemdb::TheSpellDatabase().modifiers.byCode; }

// The constructor's entries and CSpell::Clone, called through their game addresses.

} // namespace

int TextOps::Find(char c) const { return mfc::CString::Find(c); }
void TextOps::TrimLeft() { mfc::CString::TrimLeft(); }
void TextOps::TrimRight() { mfc::CString::TrimRight(); }
void TextOps::TrimRight(char a0) { mfc::CString::TrimRight(a0); }

namespace {

// Splits "name{a;b}" into the trimmed name (in `text`) and the modifier names (in `codes`).
void ParseSpellText(mfc::CString& text, console::CStringArray& codes) {
    int brace = Ops(text).Find('{');
    if (brace == -1) {
        Ops(text).TrimLeft();
        Ops(text).TrimRight();
        return;
    }
    mfc::CString inner = Ops(text).Right(text.GetLength() - brace - 1);
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
itemdb::SpellModifierRecord* FindModifier(const char* name) {
    stlp::string key(name);
    return stlp::NamedSlot<itemdb::SpellModifierRecord>(ModifierNameMap(), key);
}

} // namespace

CSpell* CSpellManager::CreateSpellFromText(const char* source) {
    mfc::CString text(source);
    console::CStringArray codes;
    ParseSpellText(text, codes);

    CSpellConstructor* constructor = context_->SpellConstructor();
    if (!constructor) return game::FatalErrorAs<CSpell>(kNoSpellConstructor);
    constructor->SetManager(this);

    itemdb::SpellRecord* prototype;
    {
        stlp::string key(text.GetString());
        prototype = stlp::NamedSlot<itemdb::SpellRecord>(SpellNameMap(), key);
    }
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<CSpell>(kCantBegin, text.GetString());

    for (int i = 0; i < codes.GetSize(); ++i) {
        const mfc::CString& code = codes[i];
        if (code.GetLength() == 0) continue;
        constructor->AddModifier(FindModifier(code));
    }

    if (!constructor->EndConstruct()) return game::FatalErrorAs<CSpell>(kCantEnd);
    CSpell* spell = constructor->container_->Spell()->Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
EI_REWRITTEN(0x00689030, CSpellManager::CreateSpellFromText);
