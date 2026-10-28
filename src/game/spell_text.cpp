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

namespace {

// The game's messages, passed by address.
const char* const kNoSpellConstructor = "No valid spell constructor\n";
const char* const kCantBegin = "Can't begin spell construction with prototype \"%s\"\n";
const char* const kCantEnd = "Can't end spell construction\n";

// The spells and modifiers by name; operator[] files a missing name with a null record.
std::unordered_map<std::string, itemdb::SpellRecord*>& SpellNameMap() { return g_spellDatabase->spells.byCode; }
std::unordered_map<std::string, itemdb::SpellModifierRecord*>& ModifierNameMap() { return g_spellDatabase->modifiers.byCode; }

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
    text::Split(inner, &codes, ';');
    text = str::Left(text, brace);
    str::TrimLeft(text);
    str::TrimRight(text);
}

// The modifier record named `name` (find_or_insert with a null record).
itemdb::SpellModifierRecord* FindModifier(std::string_view name) {
    std::string key(name);
    return ModifierNameMap()[key];
}

} // namespace

CSpell* CSpellManager::CreateSpellFromText(std::string_view source) {
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
        constructor->AddModifier(FindModifier(code));
    }

    if (!constructor->EndConstruct()) return game::FatalErrorAs<CSpell>(kCantEnd);
    CSpell* spell = constructor->container_->spell_.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
