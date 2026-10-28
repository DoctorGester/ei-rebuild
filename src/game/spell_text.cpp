// Spells from text: "healing{e2;e2}" as the scripts' CastSpellUnit / CastSpellPoint and
// the wands' prototypes (CItemConstructor::Build) write them.
#include "spell.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "lib/crt.h"
#include "text_split.h"
#include <string>
#include "lib/str.h"
#include "game_data.h"

namespace {

// The game's messages, passed by address.
const char* const kNoSpellConstructor = "No valid spell constructor\n";
const char* const kCantBegin = "Can't begin spell construction with prototype \"%s\"\n";
const char* const kCantEnd = "Can't end spell construction\n";

// The spells and modifiers by name; operator[] files a missing name with a null record.
itemdb::SpellTable::CodeIndex& SpellNameMap() { return itemdb::TheSpellDatabase().spells.byCode; }
itemdb::SpellModifierTable::CodeIndex& ModifierNameMap() { return itemdb::TheSpellDatabase().modifiers.byCode; }

// The constructor's entries and CSpell::Clone, called through their game addresses.

} // namespace

namespace {

// Splits "name{a;b}" into the trimmed name (in `text`) and the modifier names (in `codes`).
void ParseSpellText(std::string& text, std::vector<std::string>& codes) {
    int brace = str::Find(text, '{');
    if (brace == -1) {
        str::TrimLeft(text);
        str::TrimRight(text);
        return;
    }
    std::string inner = str::Right(text, static_cast<int>(text.size()) - brace - 1);
    str::TrimLeft(inner);
    str::TrimRight(inner);
    str::TrimRight(inner, '}');
    str::TrimRight(inner);
    text::Split(inner.c_str(), &codes, ';');
    text = str::Left(text, brace);
    str::TrimLeft(text);
    str::TrimRight(text);
}

// The modifier record named `name` (find_or_insert with a null record).
itemdb::SpellModifierRecord* FindModifier(const char* name) {
    std::string key(name);
    return ModifierNameMap()[key];
}

} // namespace

CSpell* CSpellManager::CreateSpellFromText(const char* source) {
    std::string text(source);
    std::vector<std::string> codes;
    ParseSpellText(text, codes);

    CSpellConstructor* constructor = context_->SpellConstructor();
    if (!constructor) return game::FatalErrorAs<CSpell>(kNoSpellConstructor);
    constructor->SetManager(this);

    itemdb::SpellRecord* prototype;
    {
        std::string key(text);
        prototype = SpellNameMap()[key];
    }
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<CSpell>(kCantBegin, text.c_str());

    for (const std::string& code : codes) {
        if (code.empty()) continue;
        constructor->AddModifier(FindModifier(code.c_str()));
    }

    if (!constructor->EndConstruct()) return game::FatalErrorAs<CSpell>(kCantEnd);
    CSpell* spell = constructor->container_->Spell()->Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
